
#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation // gcc: Class implementation
#endif

#define MYSQL_SERVER 1

/* This C++ file's header file */
#include "./rdb_locking_iter.h"

namespace myrocks {

rocksdb::Iterator* GetLockingIterator(
    rocksdb::Transaction *trx,
    const rocksdb::ReadOptions& read_options,
    rocksdb::ColumnFamilyHandle* column_family,
    bool is_rev_cf,
    ulonglong *counter) {
  return new LockingIterator(trx, column_family, is_rev_cf, read_options,
                             counter);
}

/*
  @brief
    Seek to the first key K that is equal or greater than target,
    locking the range [target; K].
*/

void LockingIterator::Seek(const rocksdb::Slice& target) {
  m_iter = m_txn->GetIterator(m_read_opts, m_cfh);
  m_iter->Seek(target);
  ScanForward(target, false);
}

void LockingIterator::SeekForPrev(const rocksdb::Slice& target) {
  m_iter = m_txn->GetIterator(m_read_opts, m_cfh);
  m_iter->SeekForPrev(target);
  ScanBackward(target, false);
}

/*
  @brief
    Move the iterator to the next key, locking the range between the current
    and the next key.

  @detail
    Implementation is similar to Seek(next_key). Since we don't know what the
    next_key is, we reach it by calling { Seek(current_key); Next(); }
*/
void LockingIterator::Next() {
  DEBUG_SYNC(my_core::thd_get_current_thd(), "rocksdb.LockingIterator.Next");
  assert(Valid());
  // Save the current key value. We need it as the left endpoint
  // of the range lock we're going to acquire
  std::string current_key = m_iter->key().ToString();

  m_iter->Next();
  ScanForward(rocksdb::Slice(current_key), true);
}

/*
  @brief
    Move the iterator to the previous key, locking the range between the current
    and the previous key.
*/

void LockingIterator::Prev() {
  assert(Valid());

  std::string current_key = m_iter->key().ToString();
  m_iter->Prev();
  ScanBackward(rocksdb::Slice(current_key), true);
}


/*
  @detail
  Ideally, this function should
   - find the first key $first_key
   - lock the range [-inf; $first_key]
   - return, the iterator is positioned on $first_key

  The problem here is that we cannot have "-infinity" bound.

  Note: we don't have a practical use for this function - MyRocks always
  searches within one index_name.table_name, which means we are only looking
  at the keys with index_number as the prefix.
*/

void LockingIterator::SeekToFirst() {
  DBUG_ASSERT(0);
  m_status = rocksdb::Status::NotSupported("Not implemented");
  m_valid = false;
}

/*
  @detail
    See SeekToFirst.
*/

void LockingIterator::SeekToLast() {
  DBUG_ASSERT(0);
  m_status = rocksdb::Status::NotSupported("Not implemented");
  m_valid = false;
}

} // namespace myrocks

