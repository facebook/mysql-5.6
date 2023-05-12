/*
   Copyright (c) 2023 Meta, Inc

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

/* This C++ file's header file */
#include "rdb_native_dd.h"
#include <cstring>

/* MySQL header files */
#include "sql/dd/types/table.h"  // dd::Table

/* MyRocks header files */
#include "ha_rocksdb.h"
#include "storage/rocksdb/ha_rocksdb_proto.h"
#include "storage/rocksdb/rdb_datadic.h"

namespace myrocks {
std::unordered_set<dd::Object_id> native_dd::s_dd_table_ids = {};

bool native_dd::is_dd_table_id(dd::Object_id id) {
  return (native_dd::s_dd_table_ids.find(id) !=
          native_dd::s_dd_table_ids.end());
}

int native_dd::reject_if_dd_table(const dd::Table *table_def) {
  if (table_def != nullptr && is_dd_table_id(table_def->se_private_id())) {
    my_error(ER_NOT_ALLOWED_COMMAND, MYF(0));
    return HA_ERR_UNSUPPORTED;
  }

  return (0);
}

void native_dd::insert_dd_table_ids(dd::Object_id dd_table_id) {
  s_dd_table_ids.insert(dd_table_id);
}

void native_dd::clear_dd_table_ids() { s_dd_table_ids.clear(); }

void rocksdb_dict_register_dd_table_id(dd::Object_id dd_table_id) {
  native_dd::insert_dd_table_ids(dd_table_id);
};

bool rocksdb_dict_get_server_version(uint *version) {
  return rdb_get_dict_manager()
      ->get_dict_manager_selector_non_const(false /*is_tmp_table*/)
      ->get_server_version(version);
};

bool rocksdb_dict_set_server_version() {
  return rdb_get_dict_manager()
      ->get_dict_manager_selector_non_const(false /*is_tmp_table*/)
      ->set_server_version();
};

bool rocksdb_is_supported_system_table([[maybe_unused]] const char *db_name,
                                       [[maybe_unused]] const char *tbl_name,
                                       bool) {
  DBUG_EXECUTE_IF("ddse_rocksdb", {
    if (strcmp(db_name, "mysql") == 0 &&
        strcmp(tbl_name, "password_history") == 0) {
      return true;
    }
  });
  return false;
}

}  // namespace myrocks
