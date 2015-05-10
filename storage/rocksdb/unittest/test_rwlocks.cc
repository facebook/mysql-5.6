/*
  TODO: MP AB Copyrights
*/
#include <my_sys.h>
#include <string.h>
#include "../rdb_locks.h"

#include "thr_template.cc"

/*****************************************************************************
 *
 * This is a basic test for Read/Write Row Locks.
 *
 *****************************************************************************/


/////////////////////////////////////////////////////////////////////////////
// Utility data structures
/////////////////////////////////////////////////////////////////////////////
/* This will hold one lock table that we're testing */
LockTable *lock_table;

const int N_ACCTS= 100;
int bank_accounts[N_ACCTS];
int total_money;

bool prevent_deadlocks= true;

int n_deadlocks= 0;

int timeout_sec;

int compare_int_keys(const uchar *s, size_t slen,
                     const uchar *t, size_t tlen)
{
  DBUG_ASSERT(slen==sizeof(int));
  DBUG_ASSERT(tlen==sizeof(int));
  int sval;
  int tval;
  memcpy(&sval, s, sizeof(int));
  memcpy(&tval, t, sizeof(int));
  if (sval < tval)
    return -1;
  else if (sval > tval)
    return 1;
  else
    return 0;
}

/* Not really a hash function */
ulong int_hashfunc(const char *key, size_t key_len)
{
  DBUG_ASSERT(key_len == sizeof(int));
  int keyval;
  memcpy(&keyval, key, sizeof(int));
  return keyval;
}

/////////////////////////////////////////////////////////////////////////////
// Real tests
/////////////////////////////////////////////////////////////////////////////

LF_PINS *thread1_pins;

/*
  Really basic tests with one thread.
*/
void basic_test()
{
  fprintf(stderr, "Basic test (in one thread) starting\n");
  thread1_pins= lock_table->get_pins();
  LF_PINS *pins= thread1_pins;

  /* Two write locks */
  Row_lock *lock1;
  Row_lock *lock2;

  int val1;

  fprintf(stderr, "  Test recursive acquisition of a read lock\n");
  val1=1;

  lock1= lock_table->get_lock(pins, (uchar*)&val1, sizeof(int), timeout_sec,
                              false);
  lock2= lock_table->get_lock(pins, (uchar*)&val1, sizeof(int), timeout_sec,
                              false);
  DBUG_ASSERT(lock1 && lock2);
  lock_table->release_lock(pins, lock1);
  lock_table->release_lock(pins, lock2);

  fprintf(stderr, "  Test recursive acquisition of a write lock\n");
  val1=2;

  lock1= lock_table->get_lock(pins, (uchar*)&val1, sizeof(int), timeout_sec,
                              true);
  lock2= lock_table->get_lock(pins, (uchar*)&val1, sizeof(int), timeout_sec,
                              true);
  DBUG_ASSERT(lock1 && lock2);
  lock_table->release_lock(pins, lock1);
  lock_table->release_lock(pins, lock2);

  fprintf(stderr, "  Acquire a read lock, then a write lock\n");
  val1=2;

  lock1= lock_table->get_lock(pins, (uchar*)&val1, sizeof(int), timeout_sec,
                              false);
  lock2= lock_table->get_lock(pins, (uchar*)&val1, sizeof(int), timeout_sec,
                              true);
  DBUG_ASSERT(lock1 && lock2);
  lock_table->release_lock(pins, lock1);
  lock_table->release_lock(pins, lock2);

  fprintf(stderr, "  Acquire a write lock, then a read lock\n");
  val1=2;

  lock1= lock_table->get_lock(pins, (uchar*)&val1, sizeof(int), timeout_sec,
                              true);
  lock2= lock_table->get_lock(pins, (uchar*)&val1, sizeof(int), timeout_sec,
                              false);
  DBUG_ASSERT(lock1 && lock2);
  lock_table->release_lock(pins, lock1);
  lock_table->release_lock(pins, lock2);

  fprintf(stderr, "  Test what happens when one gets MAX_READ_LOCKS locks\n");
  const int MAX_READ_LOCKS= 10;

  /* Use different LF_PINS* to pretend that we are from different threads */
  LF_PINS *extra_pins[MAX_READ_LOCKS];
  Row_lock *thread_locks[MAX_READ_LOCKS];
  int i;
  for (i= 0; i < MAX_READ_LOCKS; i++)
    extra_pins[i]= lock_table->get_pins();

  for (int count=0; count<2; count++)
  {
    fprintf(stderr, "  .. get MAX_READ_LOCKS read locks\n");
    /* Get max possible number of read locks */
    for (i= 0; i < MAX_READ_LOCKS; i++)
    {
      thread_locks[i]=
        lock_table->get_lock(extra_pins[i], (uchar*)&val1, sizeof(int),
                             timeout_sec, false);
      DBUG_ASSERT(thread_locks[i]);
    }

    fprintf(stderr, "  .. try getting another read lock\n");
    lock1= lock_table->get_lock(pins, (uchar*)&val1, sizeof(int), timeout_sec,
                                false);
    DBUG_ASSERT(!lock1);
    fprintf(stderr, "  .. now, release one lock and try getting it again\n");
    lock_table->release_lock(extra_pins[0], thread_locks[0]);
    lock1= lock_table->get_lock(pins, (uchar*)&val1, sizeof(int), timeout_sec,
                                false);
    DBUG_ASSERT(lock1);

    for (i= 1; i < MAX_READ_LOCKS; i++)
      lock_table->release_lock(extra_pins[i], thread_locks[i]);

    lock_table->release_lock(pins, lock1);
  }

  for (i= 0; i < MAX_READ_LOCKS; i++)
    lock_table->put_pins(extra_pins[i]);

  fprintf(stderr, "Basic test finished\n");
}

/*
  A two-threads test
*/
typedef void (*callback_func_t)();

callback_func_t thread2_work= NULL;
mysql_mutex_t thread2_has_work_mutex;
mysql_cond_t  thread2_has_work_cond;

bool thread2_exit= false;
bool thread2_done= false;
mysql_mutex_t thread2_done_mutex;
mysql_cond_t  thread2_done_cond;

void do_in_thread2(callback_func_t func);
pthread_handler_t thread2_func(void *arg);

// Secondary thread locals
Row_lock *otherlock;
LF_PINS *thread2_pins;

// Second thread funcs
void init_second_thread()
{
  fprintf(stderr, "Initializing second thread\n");
  thread2_pins= lock_table->get_pins();
}


void get_a_read_lock()
{
  const int val1= 1;
  otherlock= lock_table->get_lock(thread2_pins, (uchar*)&val1, sizeof(int), 10,
                                  false);
  DBUG_ASSERT(otherlock);
}

void release_a_read_lock()
{
  lock_table->release_lock(thread2_pins, otherlock);
}

void get_a_write_lock()
{
  const int val1= 1;
  otherlock= lock_table->get_lock(thread2_pins, (uchar*)&val1, sizeof(int), 10,
                                  true);
  DBUG_ASSERT(otherlock);
}

void release_a_write_lock()
{
  lock_table->release_lock(thread2_pins, otherlock);
}

void do_nothing()
{
}

/*
  Test number two: test read-write locks using two threads
*/
void test2()
{
  int res;
  pthread_attr_t thr_attr;
  pthread_t thread2;
  LF_PINS *pins= thread1_pins;

  fprintf(stderr, "Test#2 (two threads, slow) starting\n");
  mysql_mutex_init(0, &thread2_has_work_mutex, 0);
  mysql_cond_init (0, &thread2_has_work_cond, 0);
  mysql_mutex_init(0, &thread2_done_mutex, 0);
  mysql_cond_init (0, &thread2_done_cond, 0);

  pthread_attr_init(&thr_attr);
  pthread_attr_setdetachstate(&thr_attr,PTHREAD_CREATE_DETACHED);
  res= pthread_create(&thread2, &thr_attr, thread2_func, NULL /*arg*/);
  DBUG_ASSERT(!res);

  fprintf(stderr, "  Test that two threads can acquire a read-lock\n");
  const int val1= 1;
  Row_lock *lock1;
  Row_lock *lock2;
  lock1= lock_table->get_lock(pins, (uchar*)&val1, sizeof(int), timeout_sec,
                              false);

  do_in_thread2(init_second_thread);
  do_in_thread2(get_a_read_lock);
  do_in_thread2(release_a_read_lock);
  lock_table->release_lock(pins, lock1);

  fprintf(stderr, "  Test that a write-lock can not be acquired by two threads\n");
  do_in_thread2(get_a_write_lock);
  lock1= lock_table->get_lock(pins, (uchar*)&val1, sizeof(int), 3 /* seconds timeout */,
                              true);
  DBUG_ASSERT(lock1==NULL);
  fprintf(stderr, "  Test that a write lock prevents acquisition of a read lock\n");
  lock1= lock_table->get_lock(pins, (uchar*)&val1, sizeof(int), 3 /* seconds timeout */,
                              false);
  DBUG_ASSERT(lock1==NULL);
  do_in_thread2(release_a_write_lock);

  fprintf(stderr, "  Take read locks by both threads, then try to get a write lock also\n");
  do_in_thread2(get_a_read_lock);
  lock1= lock_table->get_lock(pins, (uchar*)&val1, sizeof(int), 3 /* seconds timeout */,
                              false);
  DBUG_ASSERT(lock1);
  lock2= lock_table->get_lock(pins, (uchar*)&val1, sizeof(int), 3 /* seconds timeout */,
                              true);
  DBUG_ASSERT(lock2==NULL);
  do_in_thread2(release_a_read_lock);
  lock_table->release_lock(pins, lock1);

  /* Finish */
  thread2_exit= true;
  do_in_thread2(do_nothing);
  pthread_attr_destroy(&thr_attr);
  fprintf(stderr, "Test#2 finished\n");
}

void do_in_thread2(callback_func_t func)
{
  // Tell thread2 it has work to do
  mysql_mutex_lock(&thread2_has_work_mutex);
  thread2_work= func;
  thread2_done= false;
  mysql_cond_signal(&thread2_has_work_cond);
  mysql_mutex_unlock(&thread2_has_work_mutex);

  // Wait for work to be finished
  mysql_mutex_lock(&thread2_done_mutex);
  while (!thread2_done)
  {
    int res;
    do {
      res= mysql_cond_wait(&thread2_done_cond, &thread2_done_mutex);
    } while (res == EINTR);
    DBUG_ASSERT(!res);
  }
  thread2_work= NULL; // ??
  mysql_mutex_unlock(&thread2_done_mutex);
}

pthread_handler_t thread2_func(void *arg)
{
  while (1)
  {
    mysql_mutex_lock(&thread2_has_work_mutex);
    while (!thread2_work)
    {
      int res;
      do {
        res= mysql_cond_wait(&thread2_has_work_cond, &thread2_has_work_mutex);
      } while (res == EINTR);
      DBUG_ASSERT(!res);
    }
    mysql_mutex_unlock(&thread2_has_work_mutex);

    thread2_work();

    // Report that work is finished
    mysql_mutex_lock(&thread2_done_mutex);
    thread2_work= NULL;
    thread2_done= true;
    mysql_cond_signal(&thread2_done_cond);
    mysql_mutex_unlock(&thread2_done_mutex);

    if (thread2_exit)
      break;
  }
  return 0;
}

void init_shared_data()
{
  total_money= 0;
  for (int i=0; i < N_ACCTS;i++)
  {
    bank_accounts[i]= 1000;
    total_money += bank_accounts[i];
  }
}

void check_shared_data(const char *name)
{
  int money_after= 0;
  for (int i=0; i < N_ACCTS; i++)
    money_after += bank_accounts[i];
  if (money_after == total_money)
    fprintf(stderr, "# validation %s ok\n", name);
  else
    fprintf(stderr, "# validation %s failed: expected %d found %d\n", name,
            total_money, money_after);
}

void do_tests()
{
  fprintf(stderr, "# lf_hash based lock table tests\n");

  /* Global initialization */
  lock_table= new LockTable;
  lock_table->init(compare_int_keys, int_hashfunc);

  init_shared_data();

  basic_test();

  test2();

  prevent_deadlocks= true;
  timeout_sec= 10*1000;

  //locktable_test1(NULL);
#if 0
  test_concurrently("locktable_test1", locktable_test1, 2 /*THREADS*/, 10 /*CYCLES*/);
  check_shared_data("1");

  prevent_deadlocks= false;
  timeout_sec= 2;
  test_concurrently("locktable_test1", locktable_test1, 2 /*THREADS*/, 10 /*CYCLES*/);
  check_shared_data("2");
  fprintf(stderr, "# n_deadlocks=%d\n", n_deadlocks);
#endif
  lock_table->cleanup();

  fprintf(stderr, "# tests end\n");
}
