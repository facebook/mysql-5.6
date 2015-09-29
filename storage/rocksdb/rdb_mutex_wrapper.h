#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>

#include "rocksdb/status.h"
#include "rocksdb/utilities/transaction_db_mutex.h"

class Wrapped_mysql_mutex: public rocksdb::TransactionDBMutex {
  Wrapped_mysql_mutex(const Wrapped_mysql_mutex& p) = delete;
  Wrapped_mysql_mutex& operator = (const Wrapped_mysql_mutex& p)=delete;
 public:
  Wrapped_mysql_mutex();
  virtual ~Wrapped_mysql_mutex();

  // Attempt to acquire lock.  Return OK on success, or other Status on failure.
  // If returned status is OK, TransactionDB will eventually call UnLock().
  virtual rocksdb::Status Lock() override;

  // Attempt to acquire lock.  If timeout is non-negative, operation should be
  // failed after this many microseconds.
  // Returns OK on success,
  //         TimedOut if timed out,
  //         or other Status on failure.
  // If returned status is OK, TransactionDB will eventually call UnLock().
  virtual rocksdb::Status TryLockFor(int64_t timeout_time) override;

  // Unlock Mutex that was successfully locked by Lock() or TryLockUntil()
  virtual void UnLock() override;

 private:
  mysql_mutex_t mutex_;
  friend class Wrapped_mysql_cond;

#ifndef STANDALONE_UNITTEST
  void SetUnlockAction(THD *thd_arg, PSI_stage_info *old_stage_arg);
  THD *thd;
  PSI_stage_info old_stage;
#endif
};


class Wrapped_mysql_cond: public rocksdb::TransactionDBCondVar {
 public:
  Wrapped_mysql_cond();
  virtual ~Wrapped_mysql_cond();

  // Block current thread until condition variable is notified by a call to
  // Notify() or NotifyAll().  Wait() will be called with mutex locked.
  // Returns OK if notified.
  // Returns non-OK if TransactionDB should stop waiting and fail the operation.
  // May return OK spuriously even if not notified.
  virtual rocksdb::Status
  Wait(std::shared_ptr<rocksdb::TransactionDBMutex> mutex) override;

  // Block current thread until condition variable is notifiesd by a call to
  // Notify() or NotifyAll(), or if the timeout is reached.
  // If timeout is non-negative, operation should be failed after this many
  // microseconds.
  // If implementing a custom version of this class, the implementation may
  // choose to ignore the timeout.
  //
  // Returns OK if notified.
  // Returns TimedOut if timeout is reached.
  // Returns other status if TransactionDB should otherwis stop waiting and
  //  fail the operation.
  // May return OK spuriously even if not notified.
  virtual rocksdb::Status
  WaitFor(std::shared_ptr<rocksdb::TransactionDBMutex> mutex,
          int64_t timeout_time) override;

  // If any threads are waiting on *this, unblock at least one of the
  // waiting threads.
  virtual void Notify() override;

  // Unblocks all threads waiting on *this.
  virtual void NotifyAll() override;

 private:
  mysql_cond_t cond_;
};


class Wrapped_mysql_mutex_factory : public rocksdb::TransactionDBMutexFactory {
 public:
  virtual std::shared_ptr<rocksdb::TransactionDBMutex>
  AllocateMutex() override {
    return
      std::shared_ptr<rocksdb::TransactionDBMutex>(new Wrapped_mysql_mutex);
  }

  virtual std::shared_ptr<rocksdb::TransactionDBCondVar>
  AllocateCondVar() override {
    return
      std::shared_ptr<rocksdb::TransactionDBCondVar>(new Wrapped_mysql_cond);
  }

  virtual ~Wrapped_mysql_mutex_factory() {}
};

