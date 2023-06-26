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

#include <string>
#include <utility>

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

// Perform the cross-engine synchronization: execute a
// performance_schema.log_status query, and call set_log_stop for each storage
// engine with its part of STORAGE_ENGINES column JSON object from that query.
// If InnoDB is present, this is called between SST COPY and REDO COPY clone
// stages. When MyRocks is the sole storage engine, it should be called after
// creating the final checkpoint.
int Ha_clone_common_cbk::synchronize_engines() {
  const auto &all_locators = get_all_locators();

  std::unique_ptr<table_log_status> table{static_cast<table_log_status *>(
      table_log_status::create(&table_log_status::m_share))};

  auto err = table->rnd_init(true);
  assert(err == 0);

  err = table->rnd_next();
  if (err != 0) return err;

  auto &log_status_row = table->get_row();
  const auto &se_positions = log_status_row.w_storage_engines;
  assert(se_positions.is_dom());

  const auto *const json_dom = se_positions.get_dom();
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
  snprintf(msg_buf, sizeof(msg_buf), "engine positions: %.*s",
           static_cast<int>(json_str_buf.length()), json_str_buf.ptr());
  log_error(nullptr, false, 0, msg_buf);

  assert(json_dom->json_type() == enum_json_type::J_OBJECT);

  DEBUG_SYNC_C("after_clone_se_sync");

  const auto *const json_obj = static_cast<const Json_object *>(json_dom);
  for (const auto &json_se_pos : *json_obj) {
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
