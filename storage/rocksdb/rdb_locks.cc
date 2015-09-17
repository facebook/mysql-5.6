/*
   Copyright (c) 2012,2013 Monty Program Ab

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

#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation        // gcc: Class implementation
#endif

#include "./rdb_locks.h"

#include <mysql/plugin.h>

#include <string.h>

#include "./ha_rocksdb.h"  // TODO(???): this is too much


typedef struct
{
  /*
    How many times the owner has acquired the lock
   (0 means it's an invalid object and there is really no lock
  */
  uint count;

  /* Data identifying the owner thread. */
  void *owner_info;

  bool is_valid() { return count!=0; }
} LOCK_OWNER_INFO;


/*
  A row lock that one gets from LockTable.

  @note
  - the structure is stored in LF_HASH, which will copy a part of
    structure with memcpy(). See LockTable::init().
  - Use of offsetof() to convert between Row_lock_impl and its
    write_handle/read_handle is another the structure is a POD.
*/

class Row_lock_impl
{
  friend class Row_lock;
public:
  /* Read and write handles */
  Row_lock write_handle;
  Row_lock read_handle;

  Row_lock* get_lock_handle(bool is_write_lock)
  {
    return is_write_lock? &write_handle : &read_handle;
  }

  /*
    MAX_READ_LOCKS is an internal parameter, but test_rwlocks.cc also defines
    and uses it.
  */
  enum { MAX_READ_LOCKS=10 };

  char *rowkey; /* The key this lock is for */
  int len; /* length of the rowkey */

  /* TRUE - this row_lock is being deleted */
  bool deleted;

  /*
    How many are waiting for the lock (for whatever reason, some may want
    a read lock and some may want a write lock)
  */
  int waiters;

  /* Write lock, if any */
  LOCK_OWNER_INFO write_lock;

  /*
    Read locks, if any. All the locks are at the beginning of the array, the
    first element with .count==0 marks the end of the valid data.
  */
  LOCK_OWNER_INFO read_locks[MAX_READ_LOCKS];

  inline bool have_write_lock() { return write_lock.count != 0; }
  inline bool have_read_locks() { return read_locks[0].count != 0; }

  bool have_one_read_lock(void *pins)
  {
    return (read_locks[0].owner_info == pins) &&
           (read_locks[1].count==0);
  }

  /*
    One must hold this mutex
     - when marking lock as busy or free
     - when adding/removing himself from waiters
    the mutex is also associated with the condition when waiting for the lock.
  */
  mysql_mutex_t mutex;

  /*
    Use this condition to wait for the "row lock is available for locking"
    condition. That is, those who change the condition so that some locks that
    were not possible before become possible, will do mysql_cond_broadcast()
    so that all waiters can check if they can put their desired locks.
  */
  mysql_cond_t cond;
};


Row_lock_impl* Row_lock::get_impl(bool *is_write_arg)
{
  *is_write_arg= is_write_handle;
  char *ptr;

  if (is_write_handle)
    ptr= ((char*)this) - offsetof(Row_lock_impl, write_handle);
  else
    ptr= ((char*)this) - offsetof(Row_lock_impl, read_handle);

  return (Row_lock_impl*)ptr;
}


static uchar* get_row_lock_hash_key(const uchar *entry, size_t* key_len, my_bool)
{
  Row_lock_impl *rlock= (Row_lock_impl*)entry;
  *key_len= rlock->len;
  return (uchar*) rlock->rowkey;
}

/**
  Row_lock_impl constructor

  It is called from lf_hash and takes a pointer to an LF_SLIST instance.
  Row_lock_impl is located at arg+sizeof(LF_SLIST)
*/
static void rowlock_init(uchar *arg)
{
  Row_lock_impl *rc= (Row_lock_impl*)(arg+LF_HASH_OVERHEAD);
  DBUG_ENTER("rowlock_init");

  memset(rc, 0, sizeof(*rc));

  mysql_mutex_init(0 /* TODO: register in P_S. */, &rc->mutex, MY_MUTEX_INIT_FAST);
  mysql_cond_init(0, &rc->cond, 0);

  rc->waiters= 0;
  DBUG_VOID_RETURN;
}


/**
  Row_lock_impl destructor

  It is called from lf_hash and takes a pointer to an LF_SLIST instance.
  Row_lock_impl is located at arg+sizeof(LF_SLIST)
*/
static void rowlock_destroy(uchar *arg)
{
  Row_lock_impl *rc= (Row_lock_impl*)(arg+LF_HASH_OVERHEAD);
  DBUG_ENTER("rowlock_destroy");

  mysql_mutex_destroy(&rc->mutex);
  mysql_cond_destroy(&rc->cond);

  DBUG_ASSERT(rc->waiters == 0);
  DBUG_VOID_RETURN;
}


/*
  Global initialization. Should be called before any other operation.
*/

void LockTable::init(lf_key_comparison_func_t key_cmp_func,
                     lf_hashfunc_t hashfunc)
{
  lf_hash_init(&lf_hash, sizeof(Row_lock_impl), LF_HASH_UNIQUE, 0 /* key offset */,
               0 /*key_len*/, get_row_lock_hash_key /*get_hash_key*/,
               NULL /*charset*/);

  lf_hash.alloc.constructor= rowlock_init;
  lf_hash.alloc.destructor=  rowlock_destroy;

  lf_hash.key_comparator= key_cmp_func;
  lf_hash.hashfunc=       hashfunc;

  lf_hash.element_size= offsetof(Row_lock_impl, mutex);
}


/* This should be called when shutting down */

void LockTable::cleanup()
{
  /* We should not have any locks at this point */
  DBUG_ASSERT(lf_hash.count == 0);
  lf_hash_destroy(&lf_hash);
}


/*
  Get a lock for given row. The lock is either a read lock or a write lock

  @param pins          Pins for this thread as returned by LockTable::get_pins().
  @param key           Row key
  @param keylen        Length of the row key, in bytes.
  @param timeout_sec   Wait at most this many seconds.
  @param write_lock    TRUE <=> Get a write (exclusive) lock
                       FALSE<=> Get a read (shared) lock

  @detail

  @return
    pointer  Pointer to the obtained lock
    NULL     Failed to acquire the lock (timeout or out-of-memory error).

  @note
    The code is based on wt_thd_will_wait_for() in mysys/waiting_threads.c
*/

Row_lock* LockTable::get_lock(LF_PINS *pins, const uchar* key, size_t keylen,
                              int timeout_sec, bool is_write_lock)
{
  Row_lock_impl *found_lock;
  void *ptr;
  bool inserted= false;

  bool locked= false;

  uchar *key_copy= NULL;

retry:
  while (!(ptr= lf_hash_search(&lf_hash, pins, key, keylen)))
  {
    Row_lock_impl new_lock;
    new_lock.write_handle.is_write_handle= 1;
    new_lock.read_handle.is_write_handle= 0;
    new_lock.deleted= FALSE;
    new_lock.waiters= 0;
    new_lock.write_lock.count= 0;
    memset(&new_lock.write_lock, 0, sizeof(new_lock.write_lock));
    memset(&new_lock.read_locks, 0, sizeof(new_lock.read_locks));

    if (!key_copy && !(key_copy= (uchar*)my_malloc(keylen, MYF(0))))
      return NULL;
    memcpy(key_copy, key, keylen);
    new_lock.rowkey= (char*)key_copy;
    new_lock.len= keylen;

    int res= lf_hash_insert(&lf_hash, pins, &new_lock);

    if (res == -1)
       goto err; /* out of memory */

    inserted= !res;
    if (inserted)
    {
      /*
        key_copy is now used in the entry in hash table.
        We should not free it.
      */
      key_copy= NULL;
    }

    /*
      Two cases: either lf_hash_insert() failed - because another thread
      has just inserted a resource with the same id - and we need to retry.

      Or lf_hash_insert() succeeded, and then we need to repeat
      lf_hash_search() to find a real address of the newly inserted element.

      That is, we don't care what lf_hash_insert() has returned.
      And we need to repeat the loop anyway.
    */
  }

  if (ptr == MY_ERRPTR)
    goto err; /* Out of memory */

  found_lock= (Row_lock_impl*)ptr;
  mysql_mutex_lock(&found_lock->mutex);

  if (found_lock->deleted)
  {
    /* We have found the lock, but it was deleted after that */
    mysql_mutex_unlock(&found_lock->mutex);
    lf_hash_search_unpin(pins);
    goto retry;
  }

  /* We're holding Row_lock_impl's mutex, which prevents anybody from deleting it */
  lf_hash_search_unpin(pins);

  /* This will release the mutex: */
  locked= do_locking_action(pins, found_lock, timeout_sec, is_write_lock);

err:
  if (key_copy)
    my_free(key_copy);

  return locked? found_lock->get_lock_handle(is_write_lock) : NULL;
}


/*
  Given a lock's Row_lock structure, acquire the lock.

  Before actually doing that, check that this is not a "spurious wake up".

  @detail
    The caller guarantees that found_lock is an existing Row_lock_impl, in
    particular, we own found_lock->mutex which prevents others from modifying
    it.

  @return
    true -  Got the lock
    false - Didn't get the lock
*/

bool LockTable::do_locking_action(LF_PINS *pins, Row_lock_impl *found_lock,
                                  int timeout_sec, bool is_write_lock)
{
#ifndef STANDALONE_UNITTEST
  bool enter_cond_done= false;
  PSI_stage_info old_stage;
  THD *thd;
#endif
  bool retval= true;

restart:
  if (is_write_lock)
  {
    if (found_lock->have_write_lock() &&
        found_lock->write_lock.owner_info == pins)
    {
      /* We already have this lock, so just increment the count */
      found_lock->write_lock.count++;
      retval= true;
      goto func_exit;
    }

    /*
      We can get a write lock if
      1. there are no other write locks (we've already handled the case
         when the present write lock is ours), and
      2. there are no other read locks, except maybe our own lock
    */
    if (!found_lock->have_write_lock() &&          // (1)
        (!found_lock->have_read_locks() ||         // (2)
         found_lock->have_one_read_lock(pins)))    // (2)
    {
      found_lock->write_lock.owner_info= pins;
      found_lock->write_lock.count++;
      retval= true;
      goto func_exit;
    }
    else
    {
      goto wait_and_retry;
    }
  }
  else
  {
    /*
      We can get a read lock if
      - there are no write locks (except maybe our lock)
    */
    if (found_lock->have_write_lock() &&
        found_lock->write_lock.owner_info != pins)
    {
      goto wait_and_retry;
    }

    /* Find, or insert our lock */
    int i;
    for (i= 0; i < Row_lock_impl::MAX_READ_LOCKS; i++)
    {
      if (!found_lock->read_locks[i].is_valid() ||
          found_lock->read_locks[i].owner_info == pins)
      {
        break;
      }
    }
    if (i == Row_lock_impl::MAX_READ_LOCKS)
    {
      /* Too many read locks. */
      retval= false;
      goto func_exit;
    }
    found_lock->read_locks[i].owner_info= pins;
    found_lock->read_locks[i].count++;
    retval= true;
    goto func_exit;
  }

wait_and_retry:
  {
    found_lock->waiters++;
    int res= 0;

    struct timespec wait_timeout;
    set_timespec(wait_timeout, timeout_sec);
#ifndef STANDALONE_UNITTEST
    thd= current_thd;
    thd_enter_cond(thd, &found_lock->cond, &found_lock->mutex,
                   &stage_waiting_on_row_lock, &old_stage);
    enter_cond_done= true;
#endif
    bool killed= false;
    do
    {
      res= mysql_cond_timedwait(&found_lock->cond, &found_lock->mutex,
                                &wait_timeout);

      DBUG_EXECUTE_IF("myrocks_simulate_lock_timeout1",
                      {res= ETIMEDOUT;});
#ifndef STANDALONE_UNITTEST
      killed= thd_killed(thd);
#endif
    } while (!killed && res == EINTR);

    if (res || killed)
    {
      if (res != ETIMEDOUT)
        fprintf(stderr, "wait failed: %d\n", res);

      found_lock->waiters--; // we're not waiting anymore

      retval= false;
      goto func_exit;
    }
  }
  /* Ok, wait succeeded */
  found_lock->waiters--; // we're not waiting anymore
  goto restart;

func_exit:
  {
    bool free_lock= (!found_lock->have_read_locks() &&
                     !found_lock->have_write_lock());
    char *rowkey= found_lock->rowkey;

    if (free_lock)
      found_lock->deleted= true;

#ifndef STANDALONE_UNITTEST
    if (enter_cond_done)
      thd_exit_cond(current_thd, &old_stage);
    else
      mysql_mutex_unlock(&found_lock->mutex);
#else
      mysql_mutex_unlock(&found_lock->mutex);
#endif

    if (free_lock)
    {
      int res __attribute__((unused));
      res= lf_hash_delete(&lf_hash, pins, found_lock->rowkey, found_lock->len);
      DBUG_ASSERT(res == 0);
      my_free(rowkey);
    }
  }

  return retval;
}


/*
  @brief Release a previously obtained lock

  @param pins           This thread pins
  @param own_lock       Previously obtained lock
  @param is_write_lock  Whether this was a write lock or a read lock.

  @detail
    Release a lock.
    If nobody is holding/waiting, we will also delete the Row_lock entry.
    If somebody is waiting, we will signal them.

*/

void LockTable::release_lock(LF_PINS *pins, Row_lock *own_lock_handle)
{
  bool is_write_lock;
  Row_lock_impl *own_lock= own_lock_handle->get_impl(&is_write_lock);
  /* Acquire the mutex to prevent anybody from getting into the queue */
  mysql_mutex_lock(&own_lock->mutex);

  if (is_write_lock)
  {
    DBUG_ASSERT(own_lock->write_lock.owner_info == pins &&
                own_lock->write_lock.count > 0);

    if (--own_lock->write_lock.count)
    {
      /*
        We've released the lock once. We've acquired it more than once though,
        so we still keep it.
      */
      mysql_mutex_unlock(&own_lock->mutex);
      return;
    }

    /* Fall through to either signaling the waiters or deleting the lock*/
  }
  else
  {
    /* Releasing a read lock. */
    int i;
    for (i=0; i < Row_lock_impl::MAX_READ_LOCKS; i++)
    {
      if (own_lock->read_locks[i].owner_info == pins)
        break; // this is our lock
    }

    DBUG_ASSERT(i < Row_lock_impl::MAX_READ_LOCKS &&
                own_lock->read_locks[i].count != 0);

    if (--own_lock->read_locks[i].count)
    {
      /* Released our lock, but it was acquired more than once. */
      mysql_mutex_unlock(&own_lock->mutex);
      return;
    }

    /*
      Removing our lock may leave a gap in the array of read locks.
      Move the last lock to remove the gap.
    */
    int j;
    for (j= i+1; j < Row_lock_impl::MAX_READ_LOCKS; j++)
    {
      if (!own_lock->read_locks[j].is_valid())
        break;
    }

    if (j != i+1)
    {
      own_lock->read_locks[i]= own_lock->read_locks[j-1];
      own_lock->read_locks[j-1].count= 0;
    }

    if (own_lock->have_read_locks() || own_lock->have_write_lock())
    {
      // One less read lock. No difference.
      mysql_mutex_unlock(&own_lock->mutex);
      return;
    }

    /* Fall through to either signaling the waiters or deleting the lock*/
  }

  if (own_lock->waiters)
  {
    mysql_cond_broadcast(&own_lock->cond);
    mysql_mutex_unlock(&own_lock->mutex);
    return;
  }

  /* Nobody is waiting */
  if (!own_lock->have_read_locks() && !own_lock->have_write_lock())
  {
    /* this will call mysql_mutex_unlock() */
    char *rowkey= own_lock->rowkey;
    own_lock->deleted= true;
    mysql_mutex_unlock(&own_lock->mutex);
    int res __attribute__((unused));
    res= lf_hash_delete(&lf_hash, pins, own_lock->rowkey, own_lock->len);
    DBUG_ASSERT(res == 0);
    my_free(rowkey);
  }
  else
  {
    /* Nobody is waiting, but we still have a lock */
    mysql_mutex_unlock(&own_lock->mutex);
  }
}

