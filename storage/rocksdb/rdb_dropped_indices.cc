/*
   Copyright (c) 2015, Facebook, Inc.

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
#include "sql_class.h"
#include "ha_rocksdb.h"
#include "my_stacktrace.h"

#include "rdb_dropped_indices.h"
#include "rdb_datadic.h"

/* This is here to get PRIu64, PRId64 */
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>

#ifdef HAVE_PSI_INTERFACE
PSI_mutex_key key_mutex_dropped_indices_manager;
#endif

void Dropped_indices_manager::init(Table_ddl_manager* ddl_manager,
                                   Dict_manager *dict_)
{
  mysql_mutex_init(key_mutex_dropped_indices_manager, &dim_mutex,
                   MY_MUTEX_INIT_FAST);
  dict = dict_;

  std::vector<uint32> index_ids;
  dict->get_drop_indexes_ongoing(index_ids);

  /*
   * ddl_manager remembers maximum MyRocks internal index id and assigns
   * and increments new index id when new table/index is created.
   * drop-ongoing index ids do not exist in ddl_manager, but
   * exist in DDL_DROP_INDEX_ONGOING data dictionary. ddl_manager should
   * skip these index ids so that it does not allocate conflicting index ids.
   * (Example: CREATE INDEX 10, 11, 12; DROP INDEX 11, 12; restarting instance.
   * ddl_manager sees the maximum index id is 10. But next allocation
   * has to be 13, if dropping index has not completed)
   */
  for (auto index_id: index_ids)
  {
    set_insert(index_id, "Resume");
    if (index_id >= ddl_manager->get_current_number())
    {
      ddl_manager->set_next_number(index_id+1);
    }
  }

  DBUG_ASSERT(initialized());
}

void Dropped_indices_manager::set_insert_table(RDBSE_KEYDEF** key_descr,
                                               uint32 n_keys,
                                               const char* log_action)
{
  for (uint32 i = 0; i < n_keys; i++) {
    set_insert(key_descr[i]->get_index_number(), log_action);
  }
}

// Not thread safe
void Dropped_indices_manager::set_insert(uint32 index_id,
                                         const char* log_action)
{
  uint32 cf_id= -1;
  if (!dict->get_cf_id(index_id, &cf_id))
  {
    sql_print_error("RocksDB: Failed to get column family info "
                    "from index id %u. MyRocks data dictionary may "
                    "get corrupted.", index_id);
    abort_with_stack_traces();
  }
  di_set.insert(index_id);
  sql_print_information("RocksDB: %s filtering dropped index %d",
                        log_action, index_id);
}

void Dropped_indices_manager::cleanup()
{
  mysql_mutex_destroy(&dim_mutex);
  di_set.clear();
}

bool Dropped_indices_manager::empty() const
{
  bool empty = true;
  if (initialized()) {
    mysql_mutex_lock(&dim_mutex);
    empty = di_set.empty();
    mysql_mutex_unlock(&dim_mutex);
  }
  return empty;
}

// check if a uint32 (encoded as a Slice) is in the map
bool Dropped_indices_manager::has_index(uint32 index) const
{
  bool match = false;
  if (initialized()) {
    mysql_mutex_lock(&dim_mutex);
    if (!di_set.empty()) {
      match = di_set.find(index) != di_set.end();
    }
    mysql_mutex_unlock(&dim_mutex);
  }
  return match;
}

// get entire list of indices to check if they're finished
Dropped_Index_Set Dropped_indices_manager::get_indices() const
{
  DBUG_ASSERT(initialized());
  mysql_mutex_lock(&dim_mutex);
  Dropped_Index_Set indices = di_set;
  mysql_mutex_unlock(&dim_mutex);
  return indices;
}

// add new indices to the map
void Dropped_indices_manager::add_indices(RDBSE_KEYDEF** key_descr,
                                          uint32 n_keys,
                                          rocksdb::WriteBatch *batch)
{
  DBUG_ASSERT(initialized());
  mysql_mutex_lock(&dim_mutex);

  set_insert_table(key_descr, n_keys, "Begin");

  for (uint32 i = 0; i < n_keys; i++)
  {
    dict->start_drop_index_ongoing(batch, key_descr[i]->get_index_number());
  }

  mysql_mutex_unlock(&dim_mutex);
}

// remove indices from the map, and delete from data dictionary
void Dropped_indices_manager::remove_indices(
    const std::unordered_set<uint32> & indices)
{
  DBUG_ASSERT(initialized());
  mysql_mutex_lock(&dim_mutex);
  std::unique_ptr<rocksdb::WriteBatch> wb= dict->begin();
  rocksdb::WriteBatch *batch= wb.get();

  for (auto index : indices) {
    auto it = di_set.find(index);
    if (it != di_set.end()) {
      sql_print_information("RocksDB: Finished filtering dropped index %d",
                            index);
      dict->end_drop_index_ongoing(batch, index);
      dict->delete_index_cf_mapping(batch, index);
      di_set.erase(it);
    }
  }
  dict->commit(batch);

  mysql_mutex_unlock(&dim_mutex);
}
