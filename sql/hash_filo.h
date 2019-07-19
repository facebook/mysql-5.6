/* Copyright (c) 2000, 2011, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */


/*
** A class for static sized hash tables where old entries are deleted in
** first-in-last-out to usage.
*/

#ifndef  HASH_FILO_H
#define  HASH_FILO_H

#include "hash.h"        /* my_hash_get_key, my_hash_free_key, HASH */
#include "mysqld.h"      /* key_hash_filo_lock */

struct hash_filo_element
{
private:
  hash_filo_element *next_used,*prev_used;
public:
  hash_filo_element() {}
  hash_filo_element *next()
  { return next_used; }
  hash_filo_element *prev()
  { return prev_used; }

  template<typename T, bool fixed_size> friend class hash_filo_impl;
};

/* base class with mutex support - used by hash_filo_impl */
class hash_mutex_base {
public:
  typedef mysql_mutex_t lock_type;
  lock_type lock;

protected:
  void init_lock() {
    mysql_mutex_init(key_hash_filo_lock, &lock, MY_MUTEX_INIT_FAST);
  }

  void destroy_lock() {
    mysql_mutex_destroy(&lock);
  }

  void acquire_read_lock() {
    mysql_mutex_lock(&lock);
  }

  void release_read_lock() {
    mysql_mutex_unlock(&lock);
  }

  void acquire_write_lock() {
    mysql_mutex_lock(&lock);
  }

  void release_write_lock() {
    mysql_mutex_unlock(&lock);
  }

  void assert_read_lock() {
    mysql_mutex_assert_owner(&lock);
  }

  void assert_write_lock() {
    mysql_mutex_assert_owner(&lock);
  }
};

/* base class with rwlock support - used by hash_filo_impl */
class hash_rwlock_base {
public:
  typedef mysql_rwlock_t lock_type;
  lock_type lock;

protected:
#ifndef DBUG_OFF
  hash_rwlock_base(): write_locked(false), read_lock_cnt(0) {}
#endif

  void init_lock() {
    /* NOTE: This is registered globally with other server rwlocks */
    mysql_rwlock_init(key_rwlock_hash_filo, &lock);
  }

  void destroy_lock() {
    mysql_rwlock_destroy(&lock);
  }

  void assert_read_lock() {
    DBUG_ASSERT(is_write_locked() || is_read_locked());
  }

  void assert_write_lock() {
    DBUG_ASSERT(is_write_locked());
  }

public:
  void acquire_write_lock() {
    mysql_rwlock_wrlock(&lock);
    DBUG_ASSERT(!write_locked);
    DBUG_ASSERT(read_lock_cnt == 0);
#ifndef DBUG_OFF
    write_locked = true;
    write_tid = pthread_self();
#endif
  }

  void acquire_read_lock() {
    mysql_rwlock_rdlock(&lock);
#ifndef DBUG_OFF
    read_lock_cnt++;
#endif
  }

  void release_read_lock() {
#ifndef DBUG_OFF
    read_lock_cnt--;
#endif
    mysql_rwlock_unlock(&lock);
  }

  void release_write_lock() {
    DBUG_ASSERT(write_locked);
    DBUG_ASSERT(read_lock_cnt == 0);
    DBUG_ASSERT(write_tid == pthread_self());
#ifndef DBUG_OFF
    write_locked = false;
    write_tid = 0;
#endif
    mysql_rwlock_unlock(&lock);
  }

#ifndef DBUG_OFF
  bool is_write_locked() {
    return write_locked;
  }

  bool is_read_locked() {
    return read_lock_cnt > 0;
  }
#endif

private:
#ifndef DBUG_OFF
  std::atomic<bool> write_locked;
  std::atomic<pthread_t> write_tid;
  std::atomic<int> read_lock_cnt;
#endif
};

template<typename hash_lock_base, bool fixed_size>
class hash_filo_impl : public hash_lock_base
{
private:
  const uint key_offset, key_length;
  const my_hash_get_key get_key;
  /** Size of this hash table. */
  uint m_size;
  my_hash_free_key free_element;
  bool init;
  CHARSET_INFO *hash_charset;

  hash_filo_element *first_link,*last_link;

public:
  HASH cache;

  hash_filo_impl(uint size, uint key_offset_arg , uint key_length_arg,
                 my_hash_get_key get_key_arg, my_hash_free_key free_element_arg,
                 CHARSET_INFO *hash_charset_arg)
    : key_offset(key_offset_arg), key_length(key_length_arg),
    get_key(get_key_arg), m_size(size),
    free_element(free_element_arg),init(0),
    hash_charset(hash_charset_arg),
    first_link(NULL),
    last_link(NULL)
  {
    memset(&cache, 0, sizeof(cache));
    hash_lock_base::init_lock();
  }

  ~hash_filo_impl()
  {
    if (init)
    {
      if (cache.array.buffer)	/* Avoid problems with thread library */
	(void) my_hash_free(&cache);
      hash_lock_base::destroy_lock();
    }
  }
  void clear(bool locked=0)
  {
    if (!init)
      init=1;
    if (!locked)
      hash_lock_base::acquire_write_lock();
    hash_lock_base::assert_write_lock();
    first_link= NULL;
    last_link= NULL;
    (void) my_hash_free(&cache);
    (void) my_hash_init(&cache, hash_charset, m_size, key_offset,
                        key_length, get_key, free_element,0);
    if (!locked)
      hash_lock_base::release_write_lock();
  }

  hash_filo_element *first()
  {
    hash_lock_base::assert_read_lock();
    return first_link;
  }

  hash_filo_element *last()
  {
    hash_lock_base::assert_read_lock();
    return last_link;
  }

  hash_filo_element *search(uchar* key, size_t length)
  {
    if (fixed_size) {
      /* Fixed size case we need write lock to update LRU */
      hash_lock_base::assert_write_lock();
    } else {
      /* Unbounded size case we don't update LRU and just take read lock */
      hash_lock_base::assert_read_lock();
    }

    hash_filo_element *entry=(hash_filo_element*)
      my_hash_search(&cache,(uchar*) key,length);

    // If size is unbound, no need to update LRU
    if (!fixed_size) {
      return entry;
    }

    if (entry)
    {
      // Found; link it first
      DBUG_ASSERT(first_link != NULL);
      DBUG_ASSERT(last_link != NULL);
      if (entry != first_link)
      {						// Relink used-chain
	if (entry == last_link)
        {
	  last_link= last_link->prev_used;
          /*
            The list must have at least 2 elements,
            otherwise entry would be equal to first_link.
          */
          DBUG_ASSERT(last_link != NULL);
          last_link->next_used= NULL;
        }
	else
	{
          DBUG_ASSERT(entry->next_used != NULL);
          DBUG_ASSERT(entry->prev_used != NULL);
	  entry->next_used->prev_used = entry->prev_used;
	  entry->prev_used->next_used = entry->next_used;
	}
        entry->prev_used= NULL;
        entry->next_used= first_link;

        first_link->prev_used= entry;
        first_link=entry;
      }
    }
    return entry;
  }

  my_bool add(hash_filo_element *entry)
  {
    hash_lock_base::assert_write_lock();

    if (!m_size) return 1;
    if (fixed_size && cache.records == m_size)
    {
      hash_filo_element *tmp=last_link;
      last_link= last_link->prev_used;
      if (last_link != NULL)
      {
        last_link->next_used= NULL;
      }
      else
      {
        /* Pathological case, m_size == 1 */
        first_link= NULL;
      }
      my_hash_delete(&cache,(uchar*) tmp);
    }
    if (my_hash_insert(&cache,(uchar*) entry))
    {
      if (free_element)
	(*free_element)(entry);		// This should never happen
      return 1;
    }
    entry->prev_used= NULL;
    entry->next_used= first_link;
    if (first_link != NULL)
      first_link->prev_used= entry;
    else
      last_link= entry;
    first_link= entry;

    return 0;
  }

  uint size()
  { return m_size; }

  void resize(uint new_size)
  {
    hash_lock_base::acquire_write_lock();
    m_size= new_size;
    clear(true);
    hash_lock_base::release_write_lock();
  }
};

/* Existing class for all the other use cases */
typedef hash_filo_impl<hash_mutex_base, /* fixed_size = */ true> hash_filo;

/*
  Though I don't use fixed_size = false here, for now I'm keeping the option
  for the purpose of doing experiments
 */
typedef hash_filo_impl<hash_rwlock_base, /* fixed_size = */ true> hash_filo_rw;

#endif
