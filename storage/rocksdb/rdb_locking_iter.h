
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
//   Now, the range ['bcd'.. 'efg'] (bounds inclusive) is also locked, and there are no
//   records between 'bcd'  and 'efg'.
//
class LockingIterator : public rocksdb::Iterator {

  rocksdb::Transaction *m_txn;
  rocksdb::ColumnFamilyHandle* m_cfh;
  bool m_is_rev_cf;
  rocksdb::ReadOptions m_read_opts;
  rocksdb::Iterator *m_iter;
  rocksdb::Status m_status;

  // note: an iterator that has reached EOF has status()==OK && m_valid==false
  bool  m_valid;

  ulonglong *m_lock_count;
 public:
  LockingIterator(rocksdb::Transaction *txn,
                  rocksdb::ColumnFamilyHandle *cfh,
                  bool is_rev_cf,
                  const rocksdb::ReadOptions& opts,
                  ulonglong *lock_count=nullptr
                  ) :
    m_txn(txn), m_cfh(cfh), m_is_rev_cf(is_rev_cf), m_read_opts(opts), m_iter(nullptr),
    m_status(rocksdb::Status::InvalidArgument()), m_valid(false),
    m_lock_count(lock_count) {}

  ~LockingIterator() {
    delete m_iter;
  }

  virtual bool Valid() const override { return m_valid; }

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
    return m_iter->key();
  }

  virtual rocksdb::Slice value() const override {
    assert(Valid());
    return m_iter->value();
  }

  virtual rocksdb::Status status() const override {
    return m_status;
  }

 private:
  template <bool forward> void Scan(const rocksdb::Slice& target,
                                    bool call_next) {
    if (!m_iter->Valid()) {
      m_status = m_iter->status();
      m_valid = false;
      return;
    }

    while (1) {
      /*
        note: the underlying iterator checks iterator bounds, so we don't need
        to check them here
      */
      DEBUG_SYNC(my_core::thd_get_current_thd(), "rocksdb.locking_iter_scan");

      if (my_core::thd_killed(current_thd)) {
        m_status = rocksdb::Status::Aborted();
        m_valid  = false; 
        return;
      }

      auto end_key = m_iter->key();
      bool endp_arg= m_is_rev_cf;
      if (forward) {
        m_status = m_txn->GetRangeLock(m_cfh,
                                     rocksdb::Endpoint(target, endp_arg),
                                     rocksdb::Endpoint(end_key, endp_arg));
      } else {
        m_status = m_txn->GetRangeLock(m_cfh,
                                     rocksdb::Endpoint(end_key, endp_arg),
                                     rocksdb::Endpoint(target, endp_arg));
      }

      if (!m_status.ok()) {
        // Failed to get a lock (most likely lock wait timeout)
        m_valid = false;
        return;
      }
      if (m_lock_count)  (*m_lock_count)++;
      std::string end_key_copy= end_key.ToString();

      //Ok, now we have a lock which is inhibiting modifications in the range
      // Somebody might have done external modifications, though:
      //  - removed the key we've found
      //  - added a key before that key.

      // First, refresh the iterator:
      delete m_iter;
      m_iter = m_txn->GetIterator(m_read_opts, m_cfh);

      // Then, try seeking to the same row
      if (forward)
        m_iter->Seek(target);
      else
        m_iter->SeekForPrev(target);

      auto cmp= m_cfh->GetComparator();

      if (call_next && m_iter->Valid() && !cmp->Compare(m_iter->key(), target)) {
        if (forward)
          m_iter->Next();
        else
          m_iter->Prev();
      }

      if (m_iter->Valid()) {
        int inv = forward ? 1 : -1;
        if (cmp->Compare(m_iter->key(), rocksdb::Slice(end_key_copy))*inv <= 0) {
          // Ok, the found key is within the range.
          m_status = rocksdb::Status::OK();
          m_valid= true;
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
        m_valid = false;
        m_status = m_iter->status();
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
