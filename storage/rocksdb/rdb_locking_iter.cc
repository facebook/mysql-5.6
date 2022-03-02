
#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation  // gcc: Class implementation
#endif

#define MYSQL_SERVER 1

/* This C++ file's header file */
#include "./rdb_locking_iter.h"

namespace myrocks {

rocksdb::Iterator *GetLockingIterator(
    rocksdb::Transaction *trx, const rocksdb::ReadOptions &read_options,
    rocksdb::ColumnFamilyHandle *column_family,
    const std::shared_ptr<Rdb_key_def> &kd, ulonglong *counter) {
  return new LockingIterator(trx, column_family, kd, read_options, counter);
}

/*
  @brief
    Seek to the first key K that is equal or greater than target,
    locking the range [target; K].
*/

void LockingIterator::Seek(const rocksdb::Slice &target) {
  m_have_locked_until = false;
  m_iter = m_txn->GetIterator(m_read_opts, m_cfh);
  m_iter->Seek(target);
  ScanForward(target, false);
}

void LockingIterator::SeekForPrev(const rocksdb::Slice &target) {
  m_have_locked_until = false;
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
  @brief
    Lock range from target to end_key.

  @detail
    In forward-ordered scan, target < end_key. In backward-ordered scan, it's
    other way around.

    We might have already locked a subset of this range, a subrange that
    starts from target and extends to some point between target and end_key.
*/
void LockingIterator::lock_up_to(bool scan_forward,
                                 const rocksdb::Slice &target,
                                 const rocksdb::Slice &end_key) {
  const int inv = scan_forward ? 1 : -1;
  auto cmp = m_cfh->GetComparator();
  bool endp_arg = m_kd->m_is_reverse_cf;

  if (m_have_locked_until &&
      cmp->Compare(end_key, rocksdb::Slice(m_locked_until)) * inv <= 0) {
    // We've already locked this range. The following has happened:
    // - m_iter->key() returned $KEY
    // - other transaction(s) have inserted row $ROW before the $KEY.
    // - we got a range lock on [range_start, $KEY]
    // - we've read $ROW and returned.
    // Now, we're looking to lock [$ROW, $KEY] but we don't need to,
    // we already have a lock on this range.
  } else {
    if (scan_forward) {
      m_status = m_txn->GetRangeLock(m_cfh, rocksdb::Endpoint(target, endp_arg),
                                     rocksdb::Endpoint(end_key, endp_arg));
    } else {
      m_status =
          m_txn->GetRangeLock(m_cfh, rocksdb::Endpoint(end_key, endp_arg),
                              rocksdb::Endpoint(target, endp_arg));
    }

    if (!m_status.ok()) return;

    // Save the bound where we locked until:
    m_have_locked_until = true;
    m_locked_until.assign(end_key.data(), end_key.size());
    if (m_lock_count) (*m_lock_count)++;
  }
}

/*
  Lock the range from target till the iterator end point that we are scaning
  towards. If there's no iterator bound, use index start (or end, depending
  on the scan direction)
*/
void LockingIterator::lock_till_iterator_end(bool scan_forward,
                                             const rocksdb::Slice &target) {
  rocksdb::Slice end;
  uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE];
  uint size;
  if (scan_forward) {
    if (m_read_opts.iterate_upper_bound)
      end = *m_read_opts.iterate_upper_bound;
    else {
      if (m_kd->m_is_reverse_cf)
        m_kd->get_infimum_key(buf, &size);
      else
        m_kd->get_supremum_key(buf, &size);

      DBUG_ASSERT(size == Rdb_key_def::INDEX_NUMBER_SIZE);
      end = rocksdb::Slice((const char *)buf, size);
    }
  } else {
    if (m_read_opts.iterate_lower_bound)
      end = *m_read_opts.iterate_lower_bound;
    else {
      if (m_kd->m_is_reverse_cf)
        m_kd->get_supremum_key(buf, &size);
      else
        m_kd->get_infimum_key(buf, &size);

      DBUG_ASSERT(size == Rdb_key_def::INDEX_NUMBER_SIZE);
      end = rocksdb::Slice((const char *)buf, size);
    }
  }
  // This will set m_status accordingly
  lock_up_to(scan_forward, target, end);
}

/*
  Lock the range between [target, (current m_iter position)] and position
  the iterator on the first record in it.

  @param call_next false means current iterator position is achieved by
                         calling Seek(target).
                    true means one also needs to call Next()
*/
void LockingIterator::Scan(bool scan_forward, const rocksdb::Slice &target,
                           bool call_next) {
  if (!m_iter->Valid()) {
    m_status = m_iter->status();
    m_valid = false;
    if (m_status.ok()) {
      // m_iter has reached EOF
      lock_till_iterator_end(scan_forward, target);
    }
    return;
  }

  while (1) {
    DEBUG_SYNC(my_core::thd_get_current_thd(), "rocksdb.locking_iter_scan");

    if (my_core::thd_killed(current_thd)) {
      m_status = rocksdb::Status::Aborted();
      m_valid = false;
      return;
    }

    const int inv = scan_forward ? 1 : -1;
    auto cmp = m_cfh->GetComparator();

    auto end_key = m_iter->key();
    std::string end_key_copy = end_key.ToString();

    lock_up_to(scan_forward, target, end_key);
    if (!m_status.ok()) {
      // Failed to get a lock (most likely lock wait timeout)
      m_valid = false;
      return;
    }

    // Ok, now we have a lock which is inhibiting modifications in the range
    // Somebody might have done external modifications, though:
    //  - removed the key we've found
    //  - added a key before that key.

    // First, refresh the iterator:
    delete m_iter;
    m_iter = m_txn->GetIterator(m_read_opts, m_cfh);

    // Then, try seeking to the same row
    if (scan_forward)
      m_iter->Seek(target);
    else
      m_iter->SeekForPrev(target);

    if (call_next && m_iter->Valid() && !cmp->Compare(m_iter->key(), target)) {
      if (scan_forward)
        m_iter->Next();
      else
        m_iter->Prev();
    }

    if (m_iter->Valid()) {
      if (cmp->Compare(m_iter->key(), rocksdb::Slice(end_key_copy)) * inv <=
          0) {
        // Ok, the found key is within the locked range.
        m_status = rocksdb::Status::OK();
        m_valid = true;
        break;
      } else {
        // We've got a key but it is outside the range we've locked.
        // Re-try the lock-and-read step.
        continue;
      }
    } else {
      m_valid = false;
      m_status = m_iter->status();
      if (m_status.ok()) {
        // m_iter has reached EOF
        lock_till_iterator_end(scan_forward, target);
      }
      break;
    }
  }
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

}  // namespace myrocks
