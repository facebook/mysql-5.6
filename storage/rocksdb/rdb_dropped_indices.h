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

#include <unordered_map>
#include <unordered_set>

#include "rocksdb/db.h"

class Dict_manager;
class Table_ddl_manager;
class RDBSE_KEYDEF;

typedef std::unordered_map<uint32, RDBSE_KEYDEF*> Dropped_Index_Map;

class Dropped_indices_manager
{
  Dropped_Index_Map di_map;
  Table_ddl_manager* ddl_manager;
  mutable mysql_mutex_t dim_mutex;

public:
  Dropped_indices_manager()
    : ddl_manager(nullptr) {}
  void init(Table_ddl_manager* ddl_manager_, Dict_manager *dict);
  void cleanup();

  // check if we have anything to filter
  bool empty() const;

  // check if index is in the map
  bool has_index(uint32 index) const;

  // get entire list of indices to check if they're finished
  Dropped_Index_Map get_indices() const;

  // add new indices to the drop index manager
  void add_indices(RDBSE_KEYDEF** key_descr, uint32 n_keys,
                   rocksdb::WriteBatch *batch);

  // remove indices once all their data is gone
  void remove_indices(const std::unordered_set<uint32> & indices,
                      Dict_manager *dict);

private:
  bool initialized() const { return ddl_manager != nullptr; }

  // internal thread-unsafe function for populating map
  void map_insert(RDBSE_KEYDEF** key_descr, uint32 n_keys,
                  const char* log_action);
};
