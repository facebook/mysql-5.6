/*****************************************************************************

Copyright (C) 2016 Facebook, Inc. All Rights Reserved.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; version 2 of the License.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA

*****************************************************************************
Created 10/05/2016 Anirban Rahut
****************************************************************************/

#ifndef SQL_SHARDED_LOCKS_INCLUDED
#define SQL_SHARDED_LOCKS_INCLUDED

#include <cstdint>
#include <cstddef>
#include "my_pthread.h"

#ifdef SHARDED_LOCKING
class THD;
#include <vector>
// assumption is that mtx and mtx_array
// will be named as LOCK_foo and LOCK_foo_sharded
#define SHARDED(arg) arg, arg##_sharded

void mutex_assert_owner_all_shards(mysql_mutex_t *mtx,
  std::vector<mysql_mutex_t> *mtx_array);

void mutex_assert_not_owner_all_shards(mysql_mutex_t *mtx,
  std::vector<mysql_mutex_t> *mtx_array);

void mutex_assert_owner_shard(mysql_mutex_t *mtx,
  std::vector<mysql_mutex_t> *mtx_array, const THD *thd);

void mutex_assert_not_owner_shard(mysql_mutex_t *mtx,
  std::vector<mysql_mutex_t> *mtx_array, const THD *thd);

void mutex_lock_all_shards(mysql_mutex_t *mtx,
  std::vector<mysql_mutex_t> *mtx_array);

void mutex_unlock_all_shards(mysql_mutex_t *mtx,
  std::vector<mysql_mutex_t> *mtx_array);

void mutex_lock_shard(mysql_mutex_t *mtx,
  std::vector<mysql_mutex_t> *mtx_array, const THD *thd);

void mutex_unlock_shard(mysql_mutex_t *mtx,
  std::vector<mysql_mutex_t> *mtx_array, const THD *thd);
#else
#define SHARDED(arg) arg
#define mutex_assert_owner_all_shards mysql_mutex_assert_owner
#define mutex_assert_not_owner_all_shards mysql_mutex_assert_not_owner
#define mutex_assert_owner_shard(arg1, arg2) mysql_mutex_assert_owner(arg1)
#define mutex_assert_not_owner_shard(arg1, arg2) \
  mysql_mutex_assert_not_owner(arg1)
#define mutex_lock_all_shards mysql_mutex_lock
#define mutex_unlock_all_shards mysql_mutex_unlock
#define mutex_lock_shard(arg1, arg2) mysql_mutex_lock(arg1)
#define mutex_unlock_shard(arg1, arg2) mysql_mutex_unlock(arg1)
#endif

#endif
