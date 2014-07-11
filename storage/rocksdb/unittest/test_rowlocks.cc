/*
  TODO: MP AB Copyrights
*/
#include <my_sys.h>
#include <string.h>
#include "../rdb_locks.h"

#include "thr_template.cc"


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


/* This is one thread */
pthread_handler_t locktable_test1(void *arg)
{
  LF_PINS *pins;
  pins= lf_hash_get_pins(&lock_table->lf_hash);

  /* In a loop, get a couple of locks */
  int loop;
  DBUG_ASSERT(RAND_MAX > N_ACCTS);

  for (loop=0; loop < 200*1000; loop++)
  {
    int val1, val2;
    val1= rand() % N_ACCTS;
    do {
      val2= rand() % N_ACCTS;
    } while (val2 == val1);

    if (prevent_deadlocks && val2 > val1)
    {
      int tmp=val2;
      val2= val1;
      val1= tmp;
    }

    int transfer=150;

    Row_lock *lock1;
    Row_lock *lock2;

    lock1= lock_table->get_lock(pins, (uchar*)&val1, sizeof(int), timeout_sec);
    DBUG_ASSERT(lock1);
    lock2= lock_table->get_lock(pins, (uchar*)&val2, sizeof(int), timeout_sec);

    if (!prevent_deadlocks && !lock2)
    {
      //Couldn't get lock2, must be a deadlock
      lock_table->release_lock(pins, lock1);

      mysql_mutex_lock(&mutex);
      n_deadlocks++;
      mysql_mutex_unlock(&mutex);
      continue;
    }

    DBUG_ASSERT(lock2);
    bank_accounts[val1] -= transfer;
    bank_accounts[val2] += transfer;

    lock_table->release_lock(pins, lock1);
    lock_table->release_lock(pins, lock2);
  }

  lf_hash_put_pins(pins);

  // Test harness needs us to signal that we're done:
  mysql_mutex_lock(&mutex);
  if (!--running_threads) mysql_cond_signal(&cond);
  mysql_mutex_unlock(&mutex);

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
  prevent_deadlocks= true;
  timeout_sec= 10*1000;

  locktable_test1(NULL);

  test_concurrently("locktable_test1", locktable_test1, 2 /*THREADS*/, 10 /*CYCLES*/);
  check_shared_data("1");


  prevent_deadlocks= false;
  timeout_sec= 2;
  test_concurrently("locktable_test1", locktable_test1, 2 /*THREADS*/, 10 /*CYCLES*/);
  check_shared_data("2");
  fprintf(stderr, "# n_deadlocks=%d\n", n_deadlocks);

  lock_table->cleanup();

  fprintf(stderr, "# tests end\n");
}
