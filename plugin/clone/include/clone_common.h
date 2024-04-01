/*
  Copyright (C) 2022-2023 Laurynas Biveinis

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

#ifndef CLONE_COMMON_H
#define CLONE_COMMON_H

#include "clone.h"
#include "sql/handler.h"

namespace myclone {

class Ha_clone_common_cbk : public Ha_clone_cbk {
 public:
  [[nodiscard]] int precopy(THD *thd, uint task_id) override;

 protected:
  /** Perform the cross-engine synchronization for logs: execute a
    performance_schema.log_status query, and call set_log_stop for each storage
    engine with its part of STORAGE_ENGINES column JSON object from that query.
    Also assign synchronization coordinates get from log_status table to the
    parameter synchronization_coordinates.
    @param[in]	synchronization_coordinates	synchronization coordinates
    @return error code */
  [[nodiscard]] int synchronize_logs(Key_Values &synchronization_coordinates);

 private:
  /** Get json object from the json wrapper after doing json validations
    @param[in]	object_name the object name, used for debug printing
    @param[in]	json_wrapper json wrapper to be converted to be json object
    @return converted json object */
  [[nodiscard]] const Json_object *get_json_object(std::string_view object_name,
                                                   Json_wrapper &json_wrapper);

  /** Extract key and values from local_repl_info.
      local_repl_info has 3 pieces of information: gtid, binlog_file, offset.
      We will also get gtid corresponding to the binlog_file and offset and
    record that gtid as well. This is to sanity check that this gtid we get from
    binlog_file and offset is same as what we have in local_repl_info.
    @param[in]	local_repl_info the "local" column we get from log_status table
    @param[in]	synchronization_coordinates Key_Values of gtid(from log_status
    table), binlog_file, binlog_offset and gtid(from binlog_file and
    binlog_offset)
    @return error code */
  [[nodiscard]] int populate_synchronization_coordinates(
      const Json_object &local_repl_info,
      Key_Values &synchronization_coordinates);
};

}  // namespace myclone

#endif  // CLONE_COMMON_H
