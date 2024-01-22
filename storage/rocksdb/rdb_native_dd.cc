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

/* MySQL header files */
#include "sql/dd/types/table.h"  // dd::Table

/* MyRocks header files */
#include "sql/plugin_table.h"
#include "storage/rocksdb/ha_rocksdb_proto.h"
#include "storage/rocksdb/rdb_datadic.h"

namespace myrocks {
std::unordered_set<dd::Object_id> native_dd::s_dd_table_ids = {};

bool native_dd::is_dd_table_id(dd::Object_id id) {
  return (native_dd::s_dd_table_ids.find(id) !=
          native_dd::s_dd_table_ids.end());
}

int native_dd::reject_if_dd_table(const dd::Table *table_def,
                                  bool is_dd_system_thread) {
  // during DDSE change, s_dd_table_ids may contain old dd table ids, thus
  // allow drop/rename if current SE isn't target DDSE and current thread is
  // dd bootstrap system thread
  if (table_def != nullptr && is_dd_table_id(table_def->se_private_id()) &&
      !(is_dd_system_thread &&
        default_dd_system_storage_engine != DEFAULT_DD_ROCKSDB)) {
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
}

bool rocksdb_dict_get_server_version(uint *version) {
  return rdb_get_dict_manager()
      ->get_dict_manager_selector_non_const(false /*is_tmp_table*/)
      ->get_server_version(version);
}

bool rocksdb_dict_set_server_version() {
  return rdb_get_dict_manager()
      ->get_dict_manager_selector_non_const(false /*is_tmp_table*/)
      ->set_server_version();
}

bool rocksdb_is_supported_system_table(const char *db_name,
                                       const char *tbl_name, bool) {
  return strcmp(db_name, "mysql") == 0 &&
         (strcmp(tbl_name, "db") == 0 || strcmp(tbl_name, "user") == 0);
}

bool rocksdb_ddse_dict_init(
    [[maybe_unused]] dict_init_mode_t dict_init_mode, uint,
    [[maybe_unused]] List<const dd::Object_table> *tables,
    [[maybe_unused]] List<const Plugin_tablespace> *tablespaces) {
  assert(tables);
  assert(tables->is_empty());
  assert(tablespaces);
  assert(tablespaces->is_empty());

  assert(dict_init_mode == DICT_INIT_CREATE_FILES ||
         dict_init_mode == DICT_INIT_CHECK_FILES);

  return false;
}

bool rocksdb_is_dict_readonly() { return false; }

void rocksdb_dict_cache_reset_tables_and_tablespaces() {
  rdb_get_ddl_manager()->reset_map();
}

bool rocksdb_dict_recover(dict_recovery_mode_t dict_recovery_mode, uint) {
  switch (dict_recovery_mode) {
    case DICT_RECOVERY_INITIALIZE_SERVER:
    case DICT_RECOVERY_INITIALIZE_TABLESPACES:
    case DICT_RECOVERY_RESTART_SERVER:
      return false;
  }
  MY_ASSERT_UNREACHABLE();
  return true;
}

void rocksdb_dict_cache_reset(const char *, const char *) {
  // TODO(laurynas): in theory, should remove this entry from the DDL manager?
  // But we can reload only the whole thing, like
  // rocksdb_dict_cache_reset_tables_and_tablespaces does.
}

}  // namespace myrocks
