
/* MySQL header files */
#include "sql/handler.h"   /* handler */
#include "sql/debug_sync.h"
#include "./rdb_threads.h" /* for thd_get_current_thd */

/* MyRocks header files */
#include "./ha_rocksdb.h"

namespace myrocks {

//////////////////////////////////////////////////////////////////////////////
// Locking iterator
//////////////////////////////////////////////////////////////////////////////

//
// LockingIterator is an iterator that locks the rows before returning, as well
// as scanned gaps between the rows.
//
//  Example:
//    lock_iter= trx->GetLockingIterator();
//    lock_iter->Seek('abc');
//    lock_iter->Valid()==true && lock_iter->key() == 'bcd';
//
//   After the above, the returned record 'bcd' is locked by transaction trx.
//   Also, the range between ['abc'..'bcd'] is empty and locked by trx.
//
//    lock_iter->Next();
//    lock_iter->Valid()==true && lock_iter->key() == 'efg'
//
//   Now, the range ['bcd'.. 'efg'] (bounds incluive) is also locked, and there are no
//   records between 'bcd'  and 'efg'.
//
class LockingIterator : public rocksdb::Iterator {

  rocksdb::Transaction *txn_;
  rocksdb::ColumnFamilyHandle* cfh_;
  bool m_is_rev_cf;
  rocksdb::ReadOptions read_opts_;
  rocksdb::Iterator *iter_;
  rocksdb::Status status_;

  // note: an iterator that has reached EOF has status()==OK && valid_==false
  bool  valid_;

  ulonglong *lock_count_;
 public:
  LockingIterator(rocksdb::Transaction *txn,
                  rocksdb::ColumnFamilyHandle *cfh,
                  bool is_rev_cf,
                  const rocksdb::ReadOptions& opts,
                  ulonglong *lock_count=nullptr
                  ) :
    txn_(txn), cfh_(cfh), m_is_rev_cf(is_rev_cf), read_opts_(opts), iter_(nullptr),
    status_(rocksdb::Status::InvalidArgument()), valid_(false),
    lock_count_(lock_count) {}

  ~LockingIterator() {
    delete iter_;
  }

  virtual bool Valid() const override { return valid_; }

  // Note: MyRocks doesn't ever call these:
  virtual void SeekToFirst() override;
  virtual void SeekToLast() override;

  virtual void Seek(const rocksdb::Slice& target) override;

  // Position at the last key in the source that at or before target.
  // The iterator is Valid() after this call iff the source contains
  // an entry that comes at or before target.
  virtual void SeekForPrev(const rocksdb::Slice& target) override;

  virtual void Next() override;
  virtual void Prev() override;

  virtual rocksdb::Slice key() const override {
    assert(Valid());
    return iter_->key();
  }

  virtual rocksdb::Slice value() const override {
    assert(Valid());
    return iter_->value();
  }

  virtual rocksdb::Status status() const override {
    return status_;
  }

 private:
  template <bool forward> void Scan(const rocksdb::Slice& target,
                                    bool call_next) {
    if (!iter_->Valid()) {
      status_ = iter_->status();
      valid_ = false;
      return;
    }

    while (1) {
      /*
        note: the underlying iterator checks iterator bounds, so we don't need
        to check them here
      */
      DEBUG_SYNC(my_core::thd_get_current_thd(), "rocksdb.locking_iter_scan");
      auto end_key = iter_->key();
      bool endp_arg= m_is_rev_cf;
      if (forward) {
        status_ = txn_->GetRangeLock(cfh_,
                                     rocksdb::Endpoint(target, endp_arg),
                                     rocksdb::Endpoint(end_key, endp_arg));
      } else {
        status_ = txn_->GetRangeLock(cfh_,
                                     rocksdb::Endpoint(end_key, endp_arg),
                                     rocksdb::Endpoint(target, endp_arg));
      }

      if (!status_.ok()) {
        // Failed to get a lock (most likely lock wait timeout)
        valid_ = false;
        return;
      }
      if (lock_count_)  (*lock_count_)++;
      std::string end_key_copy= end_key.ToString();

      //Ok, now we have a lock which is inhibiting modifications in the range
      // Somebody might have done external modifications, though:
      //  - removed the key we've found
      //  - added a key before that key.

      // First, refresh the iterator:
      delete iter_;
      iter_ = txn_->GetIterator(read_opts_, cfh_);

      // Then, try seeking to the same row
      if (forward)
        iter_->Seek(target);
      else
        iter_->SeekForPrev(target);

      auto cmp= cfh_->GetComparator();

      if (call_next && iter_->Valid() && !cmp->Compare(iter_->key(), target)) {
        if (forward)
          iter_->Next();
        else
          iter_->Prev();
      }

      if (iter_->Valid()) {
        int inv = forward ? 1 : -1;
        if (cmp->Compare(iter_->key(), rocksdb::Slice(end_key_copy))*inv <= 0) {
          // Ok, the found key is within the range.
          status_ = rocksdb::Status::OK();
          valid_= true;
          break;
        } else {
          // We've got a key but it is outside the range we've locked.
          // Re-try the lock-and-read step.
          continue;
        }
      } else {
        // There's no row (within the iterator bounds perhaps). Exit now.
        // (we might already have locked a range in this function but there's
        // nothing we can do about it)
        valid_ = false;
        status_ = iter_->status();
        break;
      }
    }
  }

  inline void ScanForward(const rocksdb::Slice& target, bool call_next) {
    Scan<true>(target, call_next);
  }

  inline void ScanBackward(const rocksdb::Slice& target, bool call_next) {
    Scan<false>(target, call_next);
  }
};

rocksdb::Iterator*
GetLockingIterator(rocksdb::Transaction *trx,
                   const rocksdb::ReadOptions& read_options,
                   rocksdb::ColumnFamilyHandle* column_family,
                   bool is_rev_cf,
                   ulonglong *counter);

} // namespace myrocks
