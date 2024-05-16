/*
  Copyright (C) 2022, 2023, 2024 Meta Platforms, Inc.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#ifndef RDB_LOCKING_ITER_H_
#define RDB_LOCKING_ITER_H_

// C++ header files
#include <memory>
#include <string>

// RocksDB header files
#include "rocksdb/iterator.h"

// MySQL header files

// MyRocks header files
#include "./ha_rocksdb.h"
#include "./rdb_datadic.h"

namespace myrocks {

//
// LockingIterator is an iterator that locks the rows before returning, as well
// as scanned gaps between the rows.
//
//  Example:
//    lock_iter= trx->GetLockingIterator();
//    lock_iter->Seek('abc');
//    lock_iter->Valid() && lock_iter->key() == 'bcd';
//
//   After the above, the returned record 'bcd' is locked by transaction trx.
//   Also, the range between ['abc'..'bcd'] is empty and locked by trx.
//
//    lock_iter->Next();
//    lock_iter->Valid() && lock_iter->key() == 'efg'
//
//   Now, the range ['bcd'.. 'efg'] (bounds inclusive) is also locked, and there
//   are no records between 'bcd'  and 'efg'.
//
class [[nodiscard]] LockingIterator : public rocksdb::Iterator {
  rocksdb::Transaction &m_txn;
  rocksdb::ColumnFamilyHandle &m_cfh;
  const Rdb_key_def &m_kd;

  rocksdb::ReadOptions m_read_opts;
  std::unique_ptr<rocksdb::Iterator> m_iter;

  // Status for either m_iter or m_txn operation
  rocksdb::Status m_status;

  // note: an iterator that has reached EOF has status()==OK && m_valid==false
  bool m_valid;

  ulonglong *m_lock_count;

  // The key value until we've locked the range. That is, we have a range lock
  // on [current_position ... m_locked_until].
  // This is used to avoid making extra GetRangeLock() calls.
  std::string m_locked_until;

 public:
  LockingIterator(rocksdb::Transaction &txn, rocksdb::ColumnFamilyHandle &cfh,
                  const Rdb_key_def &kd, const rocksdb::ReadOptions &opts,
                  ulonglong *lock_count = nullptr)
      : m_txn(txn),
        m_cfh(cfh),
        m_kd(kd),
        m_read_opts(opts),
        m_status(rocksdb::Status::InvalidArgument()),
        m_valid(false),
        m_lock_count(lock_count) {}

  bool Valid() const override { return m_valid; }

  // Note: MyRocks doesn't ever call these:
  void SeekToFirst() override;
  void SeekToLast() override;

  void Seek(const rocksdb::Slice &target) override;

  // Position at the last key in the source that at or before target.
  // The iterator is Valid() after this call iff the source contains
  // an entry that comes at or before target.
  void SeekForPrev(const rocksdb::Slice &target) override;

  void Next() override;
  void Prev() override;

  rocksdb::Slice key() const override {
    assert(Valid());
    return m_iter->key();
  }

  rocksdb::Slice value() const override {
    assert(Valid());
    return m_iter->value();
  }

  rocksdb::Status status() const override { return m_status; }

 private:
  void lock_up_to(bool scan_forward, const rocksdb::Slice &target,
                  const rocksdb::Slice &end_key);
  void lock_till_iterator_end(bool scan_forward, const rocksdb::Slice &target);
  void Scan(bool scan_forward, const rocksdb::Slice &target, bool skip_next);

  inline void ScanForward(const rocksdb::Slice &target, bool skip_next) {
    Scan(true, target, skip_next);
  }

  inline void ScanBackward(const rocksdb::Slice &target, bool skip_next) {
    Scan(false, target, skip_next);
  }
  LockingIterator(const LockingIterator &) = delete;
  LockingIterator &operator=(const LockingIterator &) = delete;
  LockingIterator(LockingIterator &&) = delete;
  LockingIterator &operator=(LockingIterator &&) = delete;
};

[[nodiscard]] inline std::unique_ptr<rocksdb::Iterator> GetLockingIterator(
    rocksdb::Transaction &trx, const rocksdb::ReadOptions &read_options,
    rocksdb::ColumnFamilyHandle &column_family, const Rdb_key_def &kd,
    ulonglong *counter) {
  return std::make_unique<LockingIterator>(trx, column_family, kd, read_options,
                                           counter);
}

}  // namespace myrocks

#endif  // RDB_LOCKING_ITER_H_
