#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>

#include "my_sys.h"
#include "mysql/plugin.h"

#include "rocksdb/status.h"

#include "ha_rocksdb.h"  /* for thd_enter_cond and thd_exit_cond */
#include "sql_class.h"

#include "rdb_mutex_wrapper.h"

using namespace rocksdb;

static
PSI_stage_info stage_waiting_on_row_lock2= { 0, "Waiting for row lock", 0};

static const int64_t MICROSECS= 1000*1000;
static const int64_t BIG_TIMEOUT= MICROSECS * 60 * 60 * 24 * 7 * 365;

Wrapped_mysql_cond::Wrapped_mysql_cond() {
  mysql_cond_init(0, &cond_, 0);
}

Wrapped_mysql_cond::~Wrapped_mysql_cond() {
  mysql_cond_destroy(&cond_);
}

Status Wrapped_mysql_cond::Wait(std::shared_ptr<TransactionDBMutex> mutex_arg) {
  return WaitFor(mutex_arg, BIG_TIMEOUT);
}


/*
  @brief
    Wait on condition *cond_ptr. The caller must make sure that we own
    *mutex_ptr.  The mutex is released and re-acquired by the wait function.

  @param
     timeout_micros  Timeout in microseconds. Negative value means no timeout.

  @return
    Status::OK()       - Wait successfull
    Status::TimedOut() - Timed out or wait killed (the caller can check
                         thd_killed() to determine which occurred)
*/

Status
Wrapped_mysql_cond::WaitFor(std::shared_ptr<TransactionDBMutex> mutex_arg,
                            int64_t timeout_micros)
{
  auto *mutex_obj= (Wrapped_mysql_mutex*)mutex_arg.get();
  mysql_mutex_t * const mutex_ptr= &mutex_obj->mutex_;

  int res= 0;
  struct timespec wait_timeout;

  if (timeout_micros < 0)
    timeout_micros= BIG_TIMEOUT;
  set_timespec_nsec(wait_timeout, timeout_micros*1000);

#ifndef STANDALONE_UNITTEST
  PSI_stage_info old_stage;
  mysql_mutex_assert_owner(mutex_ptr);

  if (current_thd && mutex_obj->old_stage_info.count(current_thd) == 0)
  {
    thd_enter_cond(current_thd, &cond_, mutex_ptr, &stage_waiting_on_row_lock2,
                   &old_stage);
    /*
      After the mysql_cond_timedwait we need make this call

        thd_exit_cond(thd, &old_stage);

      to inform the SQL layer that KILLable wait has ended. However,
      that will cause mutex to be released. Defer the release until the mutex
      that is unlocked by RocksDB's Pessimistic Transactions system.
    */
    mutex_obj->SetUnlockAction(&old_stage);
  }
#endif
  bool killed= false;

  do
  {
    res= mysql_cond_timedwait(&cond_, mutex_ptr, &wait_timeout);

#ifndef STANDALONE_UNITTEST
    if (current_thd)
      killed= thd_killed(current_thd);
#endif
  } while (!killed && res == EINTR);

  if (res || killed)
    return Status::TimedOut();
  else
    return Status::OK();
}


/*

  @note
  This function may be called while not holding the mutex that is used to wait
  on the condition variable.

  The manual page says ( http://linux.die.net/man/3/pthread_cond_signal):

  The pthread_cond_broadcast() or pthread_cond_signal() functions may be called
  by a thread whether or not it currently owns the mutex that threads calling
  pthread_cond_wait() or pthread_cond_timedwait() have associated with the
  condition variable during their waits; however, IF PREDICTABLE SCHEDULING
  BEHAVIOR IS REQUIRED, THEN THAT MUTEX SHALL BE LOCKED by the thread calling
  pthread_cond_broadcast() or pthread_cond_signal().

  What's "predicate scheduling" and do we need it? The explanation is here:

  https://groups.google.com/forum/?hl=ky#!msg/comp.programming.threads/wEUgPq541v8/ZByyyS8acqMJ
  "The problem (from the realtime side) with condition variables is that
  if you can signal/broadcast without holding the mutex, and any thread
  currently running can acquire an unlocked mutex and check a predicate
  without reference to the condition variable, then you can have an
  indirect priority inversion."

  Another possible consequence is that one can create spurious wake-ups when
  there are multiple threads signaling the condition.

  None of this looks like a problem for our use case.
*/

void Wrapped_mysql_cond::Notify()
{
  mysql_cond_signal(&cond_);
}


/*
  @note
    This is called without holding the mutex that's used for waiting on the
    condition. See ::Notify().
*/
void Wrapped_mysql_cond::NotifyAll()
{
  mysql_cond_broadcast(&cond_);
}


Wrapped_mysql_mutex::Wrapped_mysql_mutex()
{
  mysql_mutex_init(0 /* Don't register in P_S. */, &mutex_,
                   MY_MUTEX_INIT_FAST);
}

Wrapped_mysql_mutex::~Wrapped_mysql_mutex() {
    mysql_mutex_destroy(&mutex_);
}

Status Wrapped_mysql_mutex::Lock() {
  mysql_mutex_lock(&mutex_);
  DBUG_ASSERT(old_stage_info.count(current_thd) == 0);
  return Status::OK();
}

// Attempt to acquire lock.  If timeout is non-negative, operation may be
// failed after this many milliseconds.
// If implementing a custom version of this class, the implementation may
// choose to ignore the timeout.
// Return OK on success, or other Status on failure.
Status Wrapped_mysql_mutex::TryLockFor(int64_t timeout_time) {
  /*
    Note: PThreads API has pthread_mutex_timedlock(), but mysql's
    mysql_mutex_* wrappers do not wrap that function.
  */
  mysql_mutex_lock(&mutex_);
  return Status::OK();
}


#ifndef STANDALONE_UNITTEST
void Wrapped_mysql_mutex::SetUnlockAction(PSI_stage_info *old_stage_arg)
{
  mysql_mutex_assert_owner(&mutex_);
  DBUG_ASSERT(old_stage_info.count(current_thd) == 0);

  old_stage_info[current_thd] =
      std::make_shared<PSI_stage_info>(*old_stage_arg);
}
#endif

// Unlock Mutex that was successfully locked by Lock() or TryLockUntil()
void Wrapped_mysql_mutex::UnLock() {
#ifndef STANDALONE_UNITTEST
  if (old_stage_info.count(current_thd) > 0)
  {
    std::shared_ptr<PSI_stage_info> old_stage = old_stage_info[current_thd];
    old_stage_info.erase(current_thd);
    /* The following will call mysql_mutex_unlock */
    thd_exit_cond(current_thd, old_stage.get());
    return;
  }
#endif
  mysql_mutex_unlock(&mutex_);
}

