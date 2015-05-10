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

#ifdef USE_PRAGMA_INTERFACE
#pragma interface			/* gcc class implementation */
#endif

#include "my_sys.h"
#include "lf.h"

class Row_lock_impl;


/*
  A Row Lock Handle.

  @brief
    A handle is either a read lock handle, or a write lock handle.

  @detail
    Internally, there is one Row_lock_impl object for a given key value.
    The object stores info about all read/write locks that were obtained
    for this key value.

    When the user calls LockTable::release_lock(), we need to know whether
    this he is releasing a read lock, or a write lock (he may have both at the
    same time).

    In order to achieve that, Row_lock_impl has a read Row_lock and a write
    Row_lock. LockTable can check the type of lock, and then
    Row_lock::get_impl() uses offsetof() pointer arithmetic to get the
    underlying Row_lock.
*/

class Row_lock
{
public:
  char is_write_handle;

  Row_lock_impl *get_impl(bool *is_write_handle);
};


/*
  @brief
  A table of row locks. It is backed by a lock-free hash.

  @detail
  There are two types of locks:
  - Read lock (compatible with other read locks)
  - Write lock (not compatible with either read or write locks).

  Locks are recursive: the same thread can hold both read and write lock on the
  same row. It can also acquire multiple read or write locks. Each get_lock()
  call must have a matching release_lock() call.
*/

class LockTable
{
  LF_HASH lf_hash;

public:
  void init(lf_key_comparison_func_t key_cmp_func,
            lf_hashfunc_t hashfunc);

  void cleanup();
  /* Before using the LockTable, each thread should get its own "pins". */
  LF_PINS* get_pins() { return lf_hash_get_pins(&lf_hash); }
  void put_pins(LF_PINS *pins) { return lf_hash_put_pins(pins); }

  Row_lock* get_lock(LF_PINS *pins, const uchar* key, size_t keylen,
                     int timeout_sec, bool is_write_lock);

  void release_lock(LF_PINS *pins, Row_lock *own_lock);

private:
  bool do_locking_action(LF_PINS *pins, Row_lock_impl *found_lock,
                         int timeout_sec, bool is_write_lock);
};


