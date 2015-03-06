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

#include "rdb_dropped_indices.h"
#include "rdb_datadic.h"

/* This is here to get PRIu64, PRId64 */
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>

const char* DROP_INDEX_TABLE_NAME = "__drop_index__";

#ifdef HAVE_PSI_INTERFACE
PSI_mutex_key key_mutex_dropped_indices_manager;
#endif

void Dropped_indices_manager::init(Table_ddl_manager* ddl_manager_,
                                   Dict_manager *dict)
{
  mysql_mutex_init(key_mutex_dropped_indices_manager, &dim_mutex,
                   MY_MUTEX_INIT_FAST);
  ddl_manager = ddl_manager_;

  RDBSE_TABLE_DEF* tbl = ddl_manager->find((uchar*)DROP_INDEX_TABLE_NAME,
                                           strlen(DROP_INDEX_TABLE_NAME),
                                           false);

  if (tbl == nullptr) {
    tbl = new RDBSE_TABLE_DEF;
    tbl->n_keys = 0;
    tbl->key_descr = nullptr;
    tbl->dbname_tablename.append(DROP_INDEX_TABLE_NAME,
                                 strlen(DROP_INDEX_TABLE_NAME));

    std::unique_ptr<rocksdb::WriteBatch> wb= dict->begin();
    rocksdb::WriteBatch *batch= wb.get();
    ddl_manager->put_and_write(tbl, batch);
    dict->commit(batch);
    return;
  }

  map_insert(tbl->key_descr, tbl->n_keys, "Resume");

  DBUG_ASSERT(initialized());
}

// add keys to our internal map
// NOT thread safe
void Dropped_indices_manager::map_insert(RDBSE_KEYDEF** key_descr,
                                         uint32 n_keys, const char* log_action)
{
  for (uint32 i = 0; i < n_keys; i++) {
    uint32 index = key_descr[i]->get_index_number();
    DBUG_ASSERT(key_descr[i]->get_cf() != nullptr);
    di_map[index] = new RDBSE_KEYDEF(key_descr[i], i);

    sql_print_information("RocksDB: %s filtering dropped index %d",
                          log_action, index);
  }
}

void Dropped_indices_manager::cleanup()
{
  mysql_mutex_destroy(&dim_mutex);
  for (auto i : di_map) {
    delete i.second;
  }
  di_map.clear();
  ddl_manager = nullptr;
  DBUG_ASSERT(!initialized());
}

bool Dropped_indices_manager::empty() const
{
  bool empty = true;
  if (initialized()) {
    mysql_mutex_lock(&dim_mutex);
    empty = di_map.empty();
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
    if (!di_map.empty()) {
      match = di_map.find(index) != di_map.end();
    }
    mysql_mutex_unlock(&dim_mutex);
  }
  return match;
}

// get entire list of indices to check if they're finished
Dropped_Index_Map Dropped_indices_manager::get_indices() const
{
  DBUG_ASSERT(initialized());
  mysql_mutex_lock(&dim_mutex);
  Dropped_Index_Map indices = di_map;
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

  map_insert(key_descr, n_keys, "Begin");

  RDBSE_TABLE_DEF* old_tbl = ddl_manager->find(
    (uchar*)DROP_INDEX_TABLE_NAME, strlen(DROP_INDEX_TABLE_NAME), false);
  DBUG_ASSERT(old_tbl != nullptr);
  mysql_mutex_lock(&old_tbl->mutex);
  RDBSE_TABLE_DEF* tbl = new RDBSE_TABLE_DEF;
  tbl->n_keys = old_tbl->n_keys + n_keys;
  tbl->key_descr = new RDBSE_KEYDEF*[tbl->n_keys];
  memcpy(tbl->key_descr, old_tbl->key_descr,
         sizeof(RDBSE_KEYDEF*) * old_tbl->n_keys);
  // zero out old pointers so they aren't freed
  memset(old_tbl->key_descr, 0, sizeof(RDBSE_KEYDEF*) * old_tbl->n_keys);
  size_t k = old_tbl->n_keys;

  for (uint32 i = 0; i < n_keys; i++)
  {
    tbl->key_descr[k] = new RDBSE_KEYDEF(key_descr[i], k);
    ++k;
  }

  DBUG_ASSERT(k == tbl->n_keys);
  tbl->dbname_tablename.append(DROP_INDEX_TABLE_NAME,
                               strlen(DROP_INDEX_TABLE_NAME));
  mysql_mutex_unlock(&old_tbl->mutex);
  ddl_manager->put_and_write(tbl, batch);

  mysql_mutex_unlock(&dim_mutex);
}

// remove indices from the map
void Dropped_indices_manager::remove_indices(
    const std::unordered_set<uint32> & indices,
    Dict_manager *dict)
{
  DBUG_ASSERT(initialized());
  mysql_mutex_lock(&dim_mutex);
  std::unique_ptr<rocksdb::WriteBatch> wb= dict->begin();
  rocksdb::WriteBatch *batch= wb.get();

  RDBSE_TABLE_DEF* old_tbl = ddl_manager->find(
    (uchar*)DROP_INDEX_TABLE_NAME, strlen(DROP_INDEX_TABLE_NAME), false);
  DBUG_ASSERT(old_tbl != nullptr);
  mysql_mutex_lock(&old_tbl->mutex);

  // We can't just use math to subtract indices.size() from di_map.size(),
  // since there could be duplicates. So instead we build up a vector and
  // then convert it to an RDBSE_TABLE_DEF afterward.
  std::vector<RDBSE_KEYDEF*> key_descr;
  for (size_t i = 0; i < old_tbl->n_keys; i++) {
    RDBSE_KEYDEF* keydef = old_tbl->key_descr[i];
    if (indices.find(keydef->get_index_number()) == indices.end()) {
      key_descr.push_back(new RDBSE_KEYDEF(keydef, key_descr.size()));
    }
  }

  RDBSE_TABLE_DEF* tbl = new RDBSE_TABLE_DEF;
  tbl->n_keys = key_descr.size();
  tbl->key_descr = new RDBSE_KEYDEF*[tbl->n_keys];
  memcpy(tbl->key_descr, &key_descr[0], sizeof(RDBSE_KEYDEF*) * tbl->n_keys);
  tbl->dbname_tablename.append(DROP_INDEX_TABLE_NAME,
                               strlen(DROP_INDEX_TABLE_NAME));
  mysql_mutex_unlock(&old_tbl->mutex);
  ddl_manager->put_and_write(tbl, batch);

  for (auto index : indices) {
    auto it = di_map.find(index);
    if (it != di_map.end()) {
      sql_print_information("RocksDB: Finished filtering dropped index %d",
                            index);
      delete it->second;
      di_map.erase(it);
    }
  }
  dict->commit(batch);

  mysql_mutex_unlock(&dim_mutex);
}
