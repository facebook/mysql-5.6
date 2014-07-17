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

#include <mysql/plugin.h>

#include "ha_rocksdb.h"  // TODO: this is too much

#include "rdb_locks.h"

static uchar* get_row_lock_hash_key(const uchar *entry, size_t* key_len, my_bool)
{
  Row_lock *rlock= (Row_lock*)entry;
  *key_len= rlock->len;
  return (uchar*) rlock->rowkey;
}

/**
  Row_lock constructor

  It is called from lf_hash and takes a pointer to an LF_SLIST instance.
  Row_lock is located at arg+sizeof(LF_SLIST)
*/
static void rowlock_init(uchar *arg)
{
  Row_lock *rc= (Row_lock*)(arg+LF_HASH_OVERHEAD);
  DBUG_ENTER("rowlock_init");

  memset(rc, 0, sizeof(*rc));

  mysql_mutex_init(0 /* TODO: register in P_S. */, &rc->mutex, MY_MUTEX_INIT_FAST);
  mysql_cond_init(0, &rc->cond, 0);

  rc->waiters= 0;
  DBUG_VOID_RETURN;
}


/**
  Row_lock destructor

  It is called from lf_hash and takes a pointer to an LF_SLIST instance.
  Row_lock is located at arg+sizeof(LF_SLIST)
*/
static void rowlock_destroy(uchar *arg)
{
  Row_lock *rc= (Row_lock*)(arg+LF_HASH_OVERHEAD);
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
  lf_hash_init(&lf_hash, sizeof(Row_lock), LF_HASH_UNIQUE, 0 /* key offset */,
               0 /*key_len*/, get_row_lock_hash_key /*get_hash_key*/,
               NULL /*charset*/);

  lf_hash.alloc.constructor= rowlock_init;
  lf_hash.alloc.destructor=  rowlock_destroy;

  lf_hash.key_comparator= key_cmp_func;
  lf_hash.hashfunc=       hashfunc;

  lf_hash.element_size= offsetof(Row_lock, mutex);
}


/* This should be called when shutting down */

void LockTable::cleanup()
{
  /* We should not have any locks at this point */
  DBUG_ASSERT(lf_hash.count == 0);
  lf_hash_destroy(&lf_hash);
}


/*
  Get a lock for given row

  @param pins          Pins for this thread as returned by LockTable::get_pins().
  @param key           Row key
  @param keylen        Length of the row key, in bytes.
  @param timeout_sec   Wait at most this many seconds.

  @return
    pointer  Pointer to the obtained lock
    NULL     Failed to acquire the lock (timeout or out-of-memory error).


  @note
    The code is based on wt_thd_will_wait_for() in mysys/waiting_threads.c
*/

Row_lock* LockTable::get_lock(LF_PINS *pins, const uchar* key, size_t keylen,
                              int timeout_sec)
{
  Row_lock *found_lock;
  void *ptr;
  bool inserted= false;

  uchar *key_copy= NULL;

retry:
  while (!(ptr= lf_hash_search(&lf_hash, pins, key, keylen)))
  {
    Row_lock new_lock;
    new_lock.deleted= FALSE;
    new_lock.waiters= 0;
    new_lock.busy= 0;

    if (!key_copy && !(key_copy= (uchar*)my_malloc(keylen, MYF(0))))
      return NULL;
    memcpy(key_copy, key, keylen);
    new_lock.rowkey= (char*)key_copy;
    new_lock.len= keylen;

    int res= lf_hash_insert(&lf_hash, pins, &new_lock);

    if (res == -1)
       goto return_null; /* out of memory */

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
    goto return_null; /* Out of memory */

  found_lock= (Row_lock*)ptr;
  mysql_mutex_lock(&found_lock->mutex);

  if (found_lock->deleted)
  {
    /* We have found the lock, but it was deleted after that */
    mysql_mutex_unlock(&found_lock->mutex);
    lf_hash_search_unpin(pins);
    goto retry;
  }

  /* We're holding Row_lock's mutex, which prevents anybody from deleting it */
  lf_hash_search_unpin(pins);

  if (!found_lock->busy)
  {
    /* We got the Row_lock. Do nothing. */
    found_lock->busy= 1;
    found_lock->owner_data= pins;
    mysql_mutex_unlock(&found_lock->mutex);
  }
  else
  {
    if (found_lock->owner_data == pins)
    {
      /* We already own this lock */
      found_lock->busy++;
      mysql_mutex_unlock(&found_lock->mutex);
    }
    else
    {
      /* The found row_lock is not ours. Wait for it. */
      found_lock->waiters++;
      int res= 0;

      struct timespec wait_timeout;
      set_timespec(wait_timeout, timeout_sec);
#ifndef STANDALONE_UNITTEST
      THD *thd= current_thd;
      PSI_stage_info old_stage;
      thd_enter_cond(thd, &found_lock->cond, &found_lock->mutex,
                     &stage_waiting_on_row_lock, &old_stage);
#endif
      while (found_lock->busy)
      {
        res= mysql_cond_timedwait(&found_lock->cond, &found_lock->mutex,
                                  &wait_timeout);
        bool killed= false;
#ifndef STANDALONE_UNITTEST
        killed= thd_killed(thd);
#endif
        if (res == ETIMEDOUT || killed)
        {
          if (found_lock->busy)
          {
            // We own the mutex still
            found_lock->waiters--; // we're not waiting anymore
            mysql_mutex_unlock(&found_lock->mutex);
            goto return_null;
          }
          else
            break;
        }
        if (res!=0)
          fprintf(stderr, "wait failed: %d\n", res);
      }

      /*
        Ok, now we own the mutex again, and the lock is released. Take it.
      */
      DBUG_ASSERT(!found_lock->busy);
      found_lock->busy= 1;
      found_lock->owner_data= pins;
      found_lock->waiters--; // we're not waiting anymore
#ifndef STANDALONE_UNITTEST
      thd_exit_cond(thd, &old_stage);
#else
      mysql_mutex_unlock(&found_lock->mutex);
#endif
    }
  }

  if (key_copy)
    my_free(key_copy);
  return found_lock;

return_null:
  if (key_copy)
    my_free(key_copy);
  return NULL;
}


/*
  Release the previously obtained lock
    @param pins      This thread pins
    @param own_lock  Previously obtained lock
*/

void LockTable::release_lock(LF_PINS *pins, Row_lock *own_lock)
{
  /* Acquire the mutex to prevent anybody from getting into the queue */
  mysql_mutex_lock(&own_lock->mutex);

  DBUG_ASSERT(own_lock->owner_data == pins);

  if (--own_lock->busy)
  {
    /*
      We've released the lock once. We've acquired it more than once though,
      so we still keep it.
    */
    mysql_mutex_unlock(&own_lock->mutex);
    return;
  }

  if (own_lock->waiters)
  {
    /*
      Somebody is waiting for this lock (they can't stop as we're holding the
      mutex). They are now responsible for disposing of the lock.
    */
    mysql_cond_signal(&own_lock->cond);
    mysql_mutex_unlock(&own_lock->mutex);
  }
  else
  {
    /* Nobody's waiting. Release the lock */
    char *rowkey= own_lock->rowkey;
    own_lock->deleted= true;
    mysql_mutex_unlock(&own_lock->mutex);
    int res __attribute__((unused));
    res= lf_hash_delete(&lf_hash, pins, own_lock->rowkey, own_lock->len);
    DBUG_ASSERT(res == 0);
    my_free(rowkey);
  }
}
