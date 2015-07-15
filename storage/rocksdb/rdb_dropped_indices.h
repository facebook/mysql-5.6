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

typedef std::unordered_set<uint32> Dropped_Index_Set;

class Dropped_indices_manager
{
  Dropped_Index_Set di_set;
  Dict_manager* dict;
  mutable mysql_mutex_t dim_mutex;

public:
  Dropped_indices_manager()
    : dict(nullptr) {}
  void init(Table_ddl_manager* ddl_manager, Dict_manager *dict_);
  void cleanup();

  // check if we have anything to filter
  bool empty() const;

  // check if index is in the map
  bool has_index(uint32 index) const;

  // get entire list of indices to check if they're finished
  Dropped_Index_Set get_indices() const;

  // add new indices to the drop index manager
  void add_indices(RDBSE_KEYDEF** key_descr, uint32 n_keys,
                   rocksdb::WriteBatch *batch);

  // remove indices once all their data is gone
  void remove_indices(const std::unordered_set<uint32> & indices);

private:
  bool initialized() const { return dict != nullptr; }

  // internal thread-unsafe function for populating map
  void set_insert_table(RDBSE_KEYDEF** key_descr, uint32 n_keys,
                        const char* log_action);
  void set_insert(uint32 index_id,
                  const char* log_action);
};
