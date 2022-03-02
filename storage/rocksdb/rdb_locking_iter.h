
/* MySQL header files */
#include "./rdb_threads.h" /* for thd_get_current_thd */
#include "sql/debug_sync.h"
#include "sql/handler.h" /* handler */

/* MyRocks header files */
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
//    lock_iter->Valid()==true && lock_iter->key() == 'bcd';
//
//   After the above, the returned record 'bcd' is locked by transaction trx.
//   Also, the range between ['abc'..'bcd'] is empty and locked by trx.
//
//    lock_iter->Next();
//    lock_iter->Valid()==true && lock_iter->key() == 'efg'
//
//   Now, the range ['bcd'.. 'efg'] (bounds inclusive) is also locked, and there
//   are no records between 'bcd'  and 'efg'.
//
class LockingIterator : public rocksdb::Iterator {
  rocksdb::Transaction *m_txn;
  rocksdb::ColumnFamilyHandle *m_cfh;
  const std::shared_ptr<Rdb_key_def> &m_kd;

  rocksdb::ReadOptions m_read_opts;
  rocksdb::Iterator *m_iter;
  rocksdb::Status m_status;

  // note: an iterator that has reached EOF has status()==OK && m_valid==false
  bool m_valid;

  ulonglong *m_lock_count;

  // If true, m_locked_until has a valid key value.
  bool m_have_locked_until;

  // The key value until we've locked the range. That is, we have a range lock
  // on [current_position ... m_locked_until].
  // This is used to avoid making extra GetRangeLock() calls.
  std::string m_locked_until;

 public:
  LockingIterator(rocksdb::Transaction *txn, rocksdb::ColumnFamilyHandle *cfh,
                  const std::shared_ptr<Rdb_key_def> &kd,
                  const rocksdb::ReadOptions &opts,
                  ulonglong *lock_count = nullptr)
      : m_txn(txn),
        m_cfh(cfh),
        m_kd(kd),
        m_read_opts(opts),
        m_iter(nullptr),
        m_status(rocksdb::Status::InvalidArgument()),
        m_valid(false),
        m_lock_count(lock_count),
        m_have_locked_until(false) {}

  ~LockingIterator() { delete m_iter; }

  virtual bool Valid() const override { return m_valid; }

  // Note: MyRocks doesn't ever call these:
  virtual void SeekToFirst() override;
  virtual void SeekToLast() override;

  virtual void Seek(const rocksdb::Slice &target) override;

  // Position at the last key in the source that at or before target.
  // The iterator is Valid() after this call iff the source contains
  // an entry that comes at or before target.
  virtual void SeekForPrev(const rocksdb::Slice &target) override;

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

  virtual rocksdb::Status status() const override { return m_status; }

 private:
  void lock_up_to(bool scan_forward, const rocksdb::Slice &target,
                  const rocksdb::Slice &end_key);
  void lock_till_iterator_end(bool scan_forward, const rocksdb::Slice &target);
  void Scan(bool scan_forward, const rocksdb::Slice &target, bool call_next);

  inline void ScanForward(const rocksdb::Slice &target, bool call_next) {
    Scan(true, target, call_next);
  }

  inline void ScanBackward(const rocksdb::Slice &target, bool call_next) {
    Scan(false, target, call_next);
  }
};

rocksdb::Iterator *GetLockingIterator(
    rocksdb::Transaction *trx, const rocksdb::ReadOptions &read_options,
    rocksdb::ColumnFamilyHandle *column_family,
    const std::shared_ptr<Rdb_key_def> &kd, ulonglong *counter);

}  // namespace myrocks
