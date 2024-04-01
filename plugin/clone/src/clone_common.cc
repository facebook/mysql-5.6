/*
  Copyright (C) 2022, 2023, Laurynas Biveinis

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

#include "plugin/clone/include/clone_common.h"

#include <filesystem>
#include <string>
#include <utility>
#include "sql/binlog.h"

#include "lex_string.h"
#include "my_dbug.h"
#include "my_inttypes.h"
#include "plugin/clone/include/clone_hton.h"
#include "plugin/clone/include/clone_status.h"
#include "sql-common/json_dom.h"
#include "sql/handler.h"
#include "sql/sql_plugin_ref.h"
#include "storage/perfschema/table_log_status.h"

namespace myclone {

// Call precopy for the participating storage engines that have the
// precopy method.
int Ha_clone_common_cbk::precopy(THD *thd, uint task_id) {
  const auto &all_locators = get_all_locators();

  // Must be called by InnoDB the clone-driving engine
  assert(all_locators[0].m_hton->db_type == DB_TYPE_INNODB);
  assert(get_loc_index() == 0);
  auto *const saved_cbk_hton = get_hton();

  uint index = 0;
  for (const auto &loc : all_locators) {
    auto *const precopy_fn = loc.m_hton->clone_interface.clone_precopy;
    if (precopy_fn) {
      set_loc_index(index);
      const auto res =
          precopy_fn(loc.m_hton, thd, loc.m_loc, loc.m_loc_len, task_id, this);
      if (res != 0) {
        set_loc_index(0);
        set_hton(saved_cbk_hton);
        return res;
      }
    }
    ++index;
  }
  set_loc_index(0);
  set_hton(saved_cbk_hton);

  return 0;
}

const Json_object *Ha_clone_common_cbk::get_json_object(
    std::string_view object_name, Json_wrapper &json_wrapper) {
  assert(json_wrapper.is_dom());
  const auto *const json_dom = json_wrapper.get_dom();
  // get_dom above returns only a const pointer, correctly. Json_wrapper only
  // takes a non-const pointer. Let's trust it will not modify the passed value.
  const Json_wrapper json_for_str{const_cast<Json_dom *>(json_dom), true};
  String json_str_buf;
  // This may fail and return true, in which case there's nothing reasonable to
  // do, so try to print what's in the buffer anyway.
  const auto json_format_failed [[maybe_unused]] =
      json_for_str.to_string(&json_str_buf, true, __PRETTY_FUNCTION__,
                             JsonDocumentDefaultDepthHandler);
  assert(!json_format_failed);
  // Size reverse-engineered from ER_CLONE_SERVER_TRACE used by log_error. No
  // way to track it automatically, but very unlikely it would silently shrink.
  char msg_buf[512];
  snprintf(msg_buf, sizeof(msg_buf), "%s: %.*s", object_name.data(),
           static_cast<int>(json_str_buf.length()), json_str_buf.ptr());
  log_error(nullptr, false, 0, msg_buf);

  assert(json_dom->json_type() == enum_json_type::J_OBJECT);
  return static_cast<const Json_object *>(json_dom);
}

int Ha_clone_common_cbk::populate_synchronization_coordinates(
    const Json_object &local_repl_info,
    Key_Values &synchronization_coordinates) {
  // get synchronization coordinates from log status
  static const std::string gtid_executed_key_str = "gtid_executed";
  const auto &gtid_executed_json = local_repl_info.get(gtid_executed_key_str);
  assert(gtid_executed_json->json_type() == enum_json_type::J_STRING);
  const auto &gtid_executed_str_json =
      static_cast<const Json_string &>(*gtid_executed_json);
  const auto &gtid_executed_str = gtid_executed_str_json.value();
  synchronization_coordinates = {{"gtid_from_log_status", gtid_executed_str}};

  static const std::string binary_log_file_key_str = "binary_log_file";
  const auto &binary_log_file_json_ptr =
      local_repl_info.get(binary_log_file_key_str);
  if (binary_log_file_json_ptr == nullptr) {
    // if there is no binlog file, don't populate
    return 0;
  }
  const auto &binary_log_file_json = *binary_log_file_json_ptr;
  assert(binary_log_file_json.json_type() == enum_json_type::J_STRING);
  const auto &binary_log_file_str_json =
      static_cast<const Json_string &>(binary_log_file_json);
  const auto &binary_log_file_str = binary_log_file_str_json.value();
  synchronization_coordinates.push_back(
      {binary_log_file_key_str, binary_log_file_str});

  static const std::string binary_log_position_key_str = "binary_log_position";
  const auto &binary_log_position_json_ptr =
      local_repl_info.get(binary_log_position_key_str);
  if (binary_log_position_json_ptr == nullptr) {
    // if there is no binlog offset, don't populate
    return 0;
  }
  const auto &binary_log_position_json = *binary_log_position_json_ptr;
  assert(binary_log_position_json.json_type() == enum_json_type::J_INT);
  const auto &binary_log_position_int_json =
      static_cast<const Json_int &>(binary_log_position_json);
  const auto &binary_log_position_int = binary_log_position_int_json.value();
  synchronization_coordinates.push_back(
      {binary_log_position_key_str, std::to_string(binary_log_position_int)});

  // get gtid from binlog file/pos
  static const std::string gtid_from_binlog_file_offset_str =
      "gtid_from_binlog_file_offset";
  Sid_map sid_map(NULL);
  Gtid_set gtid_executed(&sid_map);
  char full_file_name[FN_REFLEN];
  mysql_bin_log.make_log_name(full_file_name, binary_log_file_str.c_str());
  // if the binlog has been purged, don't populate
  std::filesystem::path file_path(full_file_name);
  if (!std::filesystem::exists(file_path)) {
    return 0;
  }
  char info_mesg[512];
  snprintf(info_mesg, sizeof(info_mesg),
           "Reading gtid from binlog %s offset %s", full_file_name,
           std::to_string(binary_log_position_int).c_str());
  log_error(nullptr, false, 0, info_mesg);
  MYSQL_BIN_LOG::enum_read_gtids_from_binlog_status ret =
      mysql_bin_log.read_gtids_from_binlog(full_file_name, &gtid_executed, NULL,
                                           NULL, &sid_map, false, false,
                                           binary_log_position_int);
  if (ret == MYSQL_BIN_LOG::ERROR || ret == MYSQL_BIN_LOG::TRUNCATED) {
    return ER_BINLOG_FILE_OPEN_FAILED;
  } else {
    char *gtid_from_binlog_file_offset;
    gtid_executed.to_string(&gtid_from_binlog_file_offset);
    synchronization_coordinates.push_back(
        {gtid_from_binlog_file_offset_str,
         std::string(gtid_from_binlog_file_offset)});
    my_free(gtid_from_binlog_file_offset);
  }

  return 0;
}

int Ha_clone_common_cbk::synchronize_logs(
    Key_Values &synchronization_coordinates) {
  const auto &all_locators = get_all_locators();

  std::unique_ptr<table_log_status> table{static_cast<table_log_status *>(
      table_log_status::create(&table_log_status::m_share))};

  auto err = table->rnd_init(true);
  assert(err == 0);

  err = table->rnd_next();
  if (err != 0) return err;

  auto &log_status_row = table->get_row();
  const Json_object *storage_engines =
      get_json_object("w_storage_engines", log_status_row.w_storage_engines);
  const Json_object *local_repl_info =
      get_json_object("w_local", log_status_row.w_local);
  if (local_repl_info == nullptr || storage_engines == nullptr) {
    return ER_KEY_NOT_FOUND;
  }
  err = populate_synchronization_coordinates(*local_repl_info,
                                             synchronization_coordinates);
  if (err != 0) {
    return err;
  }

  DEBUG_SYNC_C("after_clone_se_sync");

  for (const auto &json_se_pos : *storage_engines) {
    const auto &se_name = json_se_pos.first;
    const LEX_CSTRING lex_c_se_name{.str = se_name.c_str(),
                                    .length = se_name.length()};
    auto *const plugin_ref = ha_resolve_by_name_raw(nullptr, lex_c_se_name);

    auto *const hton = plugin_data<handlerton *>(plugin_ref);

    if (hton->clone_interface.clone_set_log_stop) {
      // O(n^2) but n == 2
      const auto loc_itr = std::find_if(
          all_locators.begin(), all_locators.cend(),
          [hton](const Locator &loc) { return loc.m_hton == hton; });
      assert(loc_itr != all_locators.cend());

      (hton->clone_interface.clone_set_log_stop)(
          loc_itr->m_loc, loc_itr->m_loc_len, *json_se_pos.second);
    }

    plugin_unlock(nullptr, plugin_ref);
  }

  log_status_row.cleanup();

  return 0;
}
}  // namespace myclone
