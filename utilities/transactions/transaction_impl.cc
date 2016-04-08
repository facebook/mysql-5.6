//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#ifndef ROCKSDB_LITE

#include "utilities/transactions/transaction_impl.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "db/column_family.h"
#include "db/db_impl.h"
#include "rocksdb/comparator.h"
#include "rocksdb/db.h"
#include "rocksdb/snapshot.h"
#include "rocksdb/status.h"
#include "rocksdb/utilities/transaction_db.h"
#include "util/string_util.h"
#include "util/sync_point.h"
#include "utilities/transactions/transaction_db_impl.h"
#include "utilities/transactions/transaction_util.h"

namespace rocksdb {

struct WriteOptions;

std::atomic<TransactionID> TransactionImpl::txn_id_counter_(1);

TransactionID TransactionImpl::GenTxnID() {
  return txn_id_counter_.fetch_add(1);
}

TransactionImpl::TransactionImpl(TransactionDB* txn_db,
                                 const WriteOptions& write_options,
                                 const TransactionOptions& txn_options)
    : TransactionBaseImpl(txn_db->GetBaseDB(), write_options),
      txn_db_impl_(nullptr),
      txn_id_(0),
      expiration_time_(0),
      lock_timeout_(0) {
  txn_db_impl_ = dynamic_cast<TransactionDBImpl*>(txn_db);
  assert(txn_db_impl_);

  db_impl_ = dynamic_cast<DBImpl*>(txn_db->GetBaseDB());
  assert(db_impl_);

  Initialize(txn_options);
}

void TransactionImpl::Initialize(const TransactionOptions& txn_options) {
  txn_id_ = GenTxnID();

  two_phase_commit_ = txn_options.enable_two_phase_commit;

  name_ = txn_options.name;

  exec_status_ = STARTED;

  lock_timeout_ = txn_options.lock_timeout * 1000;
  if (lock_timeout_ < 0) {
    // Lock timeout not set, use default
    lock_timeout_ =
        txn_db_impl_->GetTxnDBOptions().transaction_lock_timeout * 1000;
  }

  if (txn_options.expiration >= 0) {
    expiration_time_ = start_time_ + txn_options.expiration * 1000;
  } else {
    expiration_time_ = 0;
  }

  if (txn_options.set_snapshot) {
    SetSnapshot();
  }

  if (expiration_time_ > 0) {
    txn_db_impl_->InsertExpirableTransaction(txn_id_, this);
  }

  if (two_phase_commit_) {
    assert(name_.length() > 0);
    txn_db_impl_->RegisterTransaction(this);
  }
}

TransactionImpl::~TransactionImpl() {
  txn_db_impl_->UnLock(this, &GetTrackedKeys());
  if (expiration_time_ > 0) {
    txn_db_impl_->RemoveExpirableTransaction(txn_id_);
  }
  if (two_phase_commit_) {
    txn_db_impl_->UnregisterTransaction(this);
    // if we have a prep section that was never committed
    // and we are releasing the transaction then we
    // can release that prep section
    if (log_number_ != 0 && exec_status_ != COMMITED) {
      dbimpl_->MarkLogAsHavingPrepSectionFlushed(log_number_);
    }
  }
}

void TransactionImpl::Clear() {
  assert(txn_db_impl_ != nullptr);
  txn_db_impl_->UnLock(this, &GetTrackedKeys());
  TransactionBaseImpl::Clear();
}

void TransactionImpl::Reinitialize(TransactionDB* txn_db,
                                   const WriteOptions& write_options,
                                   const TransactionOptions& txn_options) {
  TransactionBaseImpl::Reinitialize(txn_db->GetBaseDB(), write_options);
  Initialize(txn_options);
}

bool TransactionImpl::IsExpired() const {
  if (expiration_time_ > 0) {
    if (db_->GetEnv()->NowMicros() >= expiration_time_) {
      // Transaction is expired.
      return true;
    }
  }

  return false;
}

Status TransactionImpl::CommitBatch(WriteBatch* batch) {
  TransactionKeyMap keys_to_unlock;

  Status s = LockBatch(batch, &keys_to_unlock);

  if (s.ok()) {
    s = DoCommit(batch);

    txn_db_impl_->UnLock(this, &keys_to_unlock);
  }

  return s;
}

Status TransactionImpl::Prepare() {
  if (!two_phase_commit_) {
    return Status::InvalidArgument(
        "Cannot prepare a single phase transaction.");
  }

  if (IsExpired()) {
    return Status::Expired();
  }

  Status s;

  // must concern ourselves with expiration and/or lock stealing
  if (expiration_time_ > 0) {
    // need to compare/exchange bc locks could be stolen under us here
    ExecutionStatus expected = STARTED;
    bool can_prepare = std::atomic_compare_exchange_strong(
        &exec_status_, &expected, AWAITING_PREPARE);

    if (can_prepare) {
      // transaction can't expire after preparation
      expiration_time_ = 0;

      s = db_impl_->WriteImpl(
        write_options_, GetWriteBatch()->GetWriteBatch(), nullptr, this);

      if (s.ok()) {
        // at this point the WAL should  have synced and we should know
        // which log our prepared section resides in
        assert(log_number_ != 0);
        dbimpl_->MarkLogAsContainingPrepSection(log_number_);
        exec_status_.store(PREPARED);
      }
    } else if (exec_status_ == LOCKS_STOLEN) {
      return Status::Expired();
    } else {
      s = Status::InvalidArgument(
          "Transaction is not in state for preparation.");
    }
  } else if (exec_status_ == STARTED) {
    // expiration and lock stealing is not possible
    exec_status_.store(AWAITING_PREPARE);
      s = db_impl_->WriteImpl(
        write_options_, GetWriteBatch()->GetWriteBatch(), nullptr, this);
    if (s.ok()) {
      assert(log_number_ != 0);
      dbimpl_->MarkLogAsContainingPrepSection(log_number_);
      exec_status_.store(PREPARED);
    }
  } else {
    s = Status::InvalidArgument("Transaction is not in state for preparation.");
  }

  return s;
}

Status TransactionImpl::Commit() {
  Status s = DoCommit(GetWriteBatch()->GetWriteBatch());

  Clear();

  return s;
}

Status TransactionImpl::DoCommit(WriteBatch* batch) {
  Status s;

  if (two_phase_commit_ && exec_status_ != PREPARED) {
    return Status::InvalidArgument(
        "Two phase transaction is not in state for commit.");
  }

  if (expiration_time_ > 0) {
    // two phase transaction cannot expire after preparation
    assert(!two_phase_commit_);

    if (IsExpired()) {
      return Status::Expired();
    }

    // Transaction should only be committed if the thread succeeds
    // changing its execution status to AWAITING_COMMIT. This is because
    // A different transaction may consider this one expired and attempt
    // to steal its locks between the IsExpired() check and the beginning
    // of a commit.
    ExecutionStatus expected = STARTED;
    bool can_commit = std::atomic_compare_exchange_strong(
        &exec_status_, &expected, AWAITING_COMMIT);

    TEST_SYNC_POINT("TransactionTest::ExpirableTransactionDataRace:1");

    if (can_commit) {
      s = db_impl_->WriteImpl(write_options_, batch, nullptr, this);
      if (s.ok()) {
        exec_status_.store(COMMITED);
      }
    } else if (exec_status_ == LOCKS_STOLEN) {
      return Status::Expired();
    } else {
      return Status::InvalidArgument("Transaction is not in state for commit.");
    }
  } else {
    // expiration and lock stealing is not a concern
    exec_status_.store(AWAITING_COMMIT);
    s = db_impl_->WriteImpl(write_options_, batch, nullptr, this);
    if (s.ok()) {
      exec_status_.store(COMMITED);

      if (two_phase_commit_) {
        // FindObsoleteFiles must now look to the memtables
        // to determine what prep logs must be kept around,
        // not the prep section heap.
        dbimpl_->MarkLogAsHavingPrepSectionFlushed(log_number_);
      }
    }
  }

  return s;
}

Status TransactionImpl::Rollback() {
  Status s;

  if (two_phase_commit_) {
    // data has prepared to WAL so we must append
    // a rollback marker
    if (exec_status_ == PREPARED) {
      exec_status_.store(AWAITING_ROLLBACK);
      s = db_impl_->WriteImpl(
        write_options_, GetWriteBatch()->GetWriteBatch(), nullptr, this);
      if (s.ok()) {
        // we do not need to keep our prepared section around
        dbimpl_->MarkLogAsHavingPrepSectionFlushed(log_number_);
        Clear();
        exec_status_.store(ROLLEDBACK);
      }
    } else if (exec_status_ == STARTED) {
      // prepare couldn't have taken place
      Clear();
    } else if (exec_status_ == COMMITED) {
      s = Status::InvalidArgument(
          "This transaction has alrady been committed.");
    } else {
      s = Status::InvalidArgument(
          "Two phase transaction is not in state for rollback.");
    }
  } else {
    Clear();
  }

  return s;
}

Status TransactionImpl::RollbackToSavePoint() {
  if (two_phase_commit_ && exec_status_ != STARTED) {
    return Status::InvalidArgument("Transaction is beyond state for rollback.");
  }

  // Unlock any keys locked since last transaction
  const std::unique_ptr<TransactionKeyMap>& keys =
      GetTrackedKeysSinceSavePoint();

  if (keys) {
    txn_db_impl_->UnLock(this, keys.get());
  }

  return TransactionBaseImpl::RollbackToSavePoint();
}

// Lock all keys in this batch.
// On success, caller should unlock keys_to_unlock
Status TransactionImpl::LockBatch(WriteBatch* batch,
                                  TransactionKeyMap* keys_to_unlock) {
  class Handler : public WriteBatch::Handler {
   public:
    // Sorted map of column_family_id to sorted set of keys.
    // Since LockBatch() always locks keys in sorted order, it cannot deadlock
    // with itself.  We're not using a comparator here since it doesn't matter
    // what the sorting is as long as it's consistent.
    std::map<uint32_t, std::set<std::string>> keys_;

    Handler() {}

    void RecordKey(uint32_t column_family_id, const Slice& key) {
      std::string key_str = key.ToString();

      auto iter = (keys_)[column_family_id].find(key_str);
      if (iter == (keys_)[column_family_id].end()) {
        // key not yet seen, store it.
        (keys_)[column_family_id].insert({std::move(key_str)});
      }
    }

    virtual Status PutCF(uint32_t column_family_id, const Slice& key,
                         const Slice& value) override {
      RecordKey(column_family_id, key);
      return Status::OK();
    }
    virtual Status MergeCF(uint32_t column_family_id, const Slice& key,
                           const Slice& value) override {
      RecordKey(column_family_id, key);
      return Status::OK();
    }
    virtual Status DeleteCF(uint32_t column_family_id,
                            const Slice& key) override {
      RecordKey(column_family_id, key);
      return Status::OK();
    }
  };

  // Iterating on this handler will add all keys in this batch into keys
  Handler handler;
  batch->Iterate(&handler);

  Status s;

  // Attempt to lock all keys
  for (const auto& cf_iter : handler.keys_) {
    uint32_t cfh_id = cf_iter.first;
    auto& cfh_keys = cf_iter.second;

    for (const auto& key_iter : cfh_keys) {
      const std::string& key = key_iter;

      s = txn_db_impl_->TryLock(this, cfh_id, key);
      if (!s.ok()) {
        break;
      }
      TrackKey(keys_to_unlock, cfh_id, std::move(key), kMaxSequenceNumber,
               false);
    }

    if (!s.ok()) {
      break;
    }
  }

  if (!s.ok()) {
    txn_db_impl_->UnLock(this, keys_to_unlock);
  }

  return s;
}

// Attempt to lock this key.
// Returns OK if the key has been successfully locked.  Non-ok, otherwise.
// If check_shapshot is true and this transaction has a snapshot set,
// this key will only be locked if there have been no writes to this key since
// the snapshot time.
Status TransactionImpl::TryLock(ColumnFamilyHandle* column_family,
                                const Slice& key, bool read_only,
                                bool untracked) {
  uint32_t cfh_id = GetColumnFamilyID(column_family);
  std::string key_str = key.ToString();
  bool previously_locked;
  Status s;

  // lock this key if this transactions hasn't already locked it
  SequenceNumber current_seqno = kMaxSequenceNumber;
  SequenceNumber new_seqno = kMaxSequenceNumber;

  const auto& tracked_keys = GetTrackedKeys();
  const auto tracked_keys_cf = tracked_keys.find(cfh_id);
  if (tracked_keys_cf == tracked_keys.end()) {
    previously_locked = false;
  } else {
    auto iter = tracked_keys_cf->second.find(key_str);
    if (iter == tracked_keys_cf->second.end()) {
      previously_locked = false;
    } else {
      previously_locked = true;
      current_seqno = iter->second.seq;
    }
  }

  // lock this key if this transactions hasn't already locked it
  if (!previously_locked) {
    s = txn_db_impl_->TryLock(this, cfh_id, key_str);
  }

  SetSnapshotIfNeeded();

  // Even though we do not care about doing conflict checking for this write,
  // we still need to take a lock to make sure we do not cause a conflict with
  // some other write.  However, we do not need to check if there have been
  // any writes since this transaction's snapshot.
  // TODO(agiardullo): could optimize by supporting shared txn locks in the
  // future
  if (untracked || snapshot_ == nullptr) {
    // Need to remember the earliest sequence number that we know that this
    // key has not been modified after.  This is useful if this same
    // transaction
    // later tries to lock this key again.
    if (current_seqno == kMaxSequenceNumber) {
      // Since we haven't checked a snapshot, we only know this key has not
      // been modified since after we locked it.
      new_seqno = db_->GetLatestSequenceNumber();
    } else {
      new_seqno = current_seqno;
    }
  } else {
    // If a snapshot is set, we need to make sure the key hasn't been modified
    // since the snapshot.  This must be done after we locked the key.
    if (s.ok()) {
      s = ValidateSnapshot(column_family, key, current_seqno, &new_seqno);

      if (!s.ok()) {
        // Failed to validate key
        if (!previously_locked) {
          // Unlock key we just locked
          txn_db_impl_->UnLock(this, cfh_id, key.ToString());
        }
      }
    }
  }

  if (s.ok()) {
    // Let base class know we've conflict checked this key.
    TrackKey(cfh_id, key_str, new_seqno, read_only);
  }

  return s;
}

// Return OK() if this key has not been modified more recently than the
// transaction snapshot_.
Status TransactionImpl::ValidateSnapshot(ColumnFamilyHandle* column_family,
                                         const Slice& key,
                                         SequenceNumber prev_seqno,
                                         SequenceNumber* new_seqno) {
  assert(snapshot_);

  SequenceNumber seq = snapshot_->GetSequenceNumber();
  if (prev_seqno <= seq) {
    // If the key has been previous validated at a sequence number earlier
    // than the curent snapshot's sequence number, we already know it has not
    // been modified.
    return Status::OK();
  }

  *new_seqno = seq;

  assert(dynamic_cast<DBImpl*>(db_) != nullptr);
  auto db_impl = reinterpret_cast<DBImpl*>(db_);

  ColumnFamilyHandle* cfh =
      column_family ? column_family : db_impl->DefaultColumnFamily();

  return TransactionUtil::CheckKeyForConflicts(db_impl, cfh, key.ToString(),
                                               snapshot_->GetSequenceNumber(),
                                               false /* cache_only */);
}

bool TransactionImpl::TryStealingLocks() {
  assert(IsExpired());
  ExecutionStatus expected = STARTED;
  return std::atomic_compare_exchange_strong(&exec_status_, &expected,
                                             LOCKS_STOLEN);
}

void TransactionImpl::UnlockGetForUpdate(ColumnFamilyHandle* column_family,
                                         const Slice& key) {
  txn_db_impl_->UnLock(this, GetColumnFamilyID(column_family), key.ToString());
}

}  // namespace rocksdb

#endif  // ROCKSDB_LITE
