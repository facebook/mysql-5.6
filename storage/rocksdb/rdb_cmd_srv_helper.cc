/*
   Copyright (c) 2024, Facebook, Inc.

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

#include "./rdb_cmd_srv_helper.h"

#include <cinttypes>
#include <string_view>

#include "rdb_utils.h"  // LOG_COMPONENT_TAG for includes below

#include "mysql/components/services/log_builtins.h"
#include "mysqld_error.h"
#include "sql/fb_vector_base.h"

namespace myrocks {

namespace {
constexpr std::string_view INDEX_DATA_TYPE_METADATA = "metadata";
constexpr std::string_view INDEX_DATA_TYPE_QUANTIZER = "quantizer";
constexpr std::string_view INDEX_DATA_TYPE_PRODUCT_QUANTIZER =
    "product_quantizer";

enum class Rdb_vector_index_data_version { NONE, V1 };
constexpr std::string_view METADATA_KEY_VERSION = "version";
constexpr std::string_view METADATA_KEY_NLIST = "nlist";
constexpr std::string_view METADATA_KEY_PQ_M = "pq_m";
constexpr std::string_view METADATA_KEY_PQ_NBITS = "pq_nbits";
const char *CMD_SRV_USER = "admin:sys.database";

// create query statement to read vector index data
std::string create_query(const std::string &table_name, const std::string &id,
                         const std::string_view type) {
  return "SELECT value FROM " + table_name + " WHERE id = '" + id +
         "' and type = '" + std::string(type) + "' order by seqno";
}

}  // namespace

Rdb_cmd_srv_status Rdb_cmd_srv_helper::get_error_status(
    MYSQL_H_wrapper &mysql_wrapper, bool connection_error = false) {
  MYSQL_H mysql_h = mysql_wrapper.mysql;
  unsigned int error_no;
  if (m_command_error_info->sql_errno(mysql_h, &error_no)) {
    LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG,
                    "failed to get error number");
    return Rdb_cmd_srv_status("unknown error, failed to get error number");
  }
  // if it is a connection error, sql_error does not work because the required
  // data structure is not initialized.
  if (connection_error) {
    return Rdb_cmd_srv_status(error_no, "connection error", "");
  }
  std::array<char, MYSQL_ERRMSG_SIZE> error_msg;
  char *error_msg_ptr = error_msg.data();
  std::string error_msg_str;
  if (m_command_error_info->sql_error(mysql_h, &error_msg_ptr)) {
    LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG,
                    "failed to get error message");
  } else {
    error_msg_str = std::string(error_msg.data());
  }
  char *sqlstate = nullptr;
  std::string sqlstate_str;
  if (m_command_error_info->sql_state(mysql_h, &sqlstate)) {
    LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG,
                    "failed to get sql state");
  } else if (sqlstate != nullptr) {
    sqlstate_str = std::string(sqlstate);
  }

  return Rdb_cmd_srv_status(error_no, error_msg_str, sqlstate_str);
}

Rdb_cmd_srv_status Rdb_cmd_srv_helper::connect(MYSQL_H_wrapper &mysql_wrapper) {
  if (mysql_wrapper.init()) {
    LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG,
                    "failed to init command factory");
    return Rdb_cmd_srv_status("failed to init command factory");
  }

  MYSQL_H mysql_h = mysql_wrapper.mysql;
  if (m_command_options->set(mysql_h, MYSQL_COMMAND_USER_NAME, CMD_SRV_USER)) {
    LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG,
                    "failed to set user name");
    return Rdb_cmd_srv_status("failed to set user name");
  }
  if (m_command_factory->connect(mysql_h)) {
    LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG, "failed to connect");
    return get_error_status(mysql_wrapper, true);
  }

  return Rdb_cmd_srv_status();
}

Rdb_cmd_srv_status Rdb_cmd_srv_helper::execute_query(
    const std::string &db_name, const std::string &query,
    MYSQL_H_wrapper &mysql_wrapper, MYSQL_RES_H_wrapper &mysql_res_wrapper) {
  const std::string use_db = "USE " + db_name;
  if (m_command_query->query(mysql_wrapper.mysql, use_db.data(),
                             use_db.length())) {
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "failed to execute query: %s", use_db.c_str());
    return get_error_status(mysql_wrapper);
  }

  if (m_command_query->query(mysql_wrapper.mysql, query.data(),
                             query.length())) {
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "failed to execute query: %s", query.c_str());
    return get_error_status(mysql_wrapper);
  }

  if (mysql_res_wrapper.store_result(mysql_wrapper.mysql)) {
    LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG, "failed to store result");
    return get_error_status(mysql_wrapper);
  }

  return Rdb_cmd_srv_status();
}

Rdb_cmd_srv_status Rdb_cmd_srv_helper::get_row_count(
    MYSQL_H_wrapper &mysql_wrapper, uint64_t &row_count) {
  if (m_command_query->affected_rows(mysql_wrapper.mysql, &row_count)) {
    LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG,
                    "failed to get row count");
    return get_error_status(mysql_wrapper);
  }

  return Rdb_cmd_srv_status();
}

Rdb_cmd_srv_status Rdb_cmd_srv_helper::check_row_count(
    MYSQL_H_wrapper &mysql_wrapper, uint64_t expected_row_count) {
  uint64_t row_count;
  auto status = get_row_count(mysql_wrapper, row_count);
  if (status.error()) {
    return status;
  }
  if (row_count != expected_row_count) {
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "expect one row, but got %" PRIu64, row_count);
    return Rdb_cmd_srv_status("Unexpected number of rows returned.");
  }

  return Rdb_cmd_srv_status();
}

Rdb_cmd_srv_status Rdb_cmd_srv_helper::check_column_types(
    MYSQL_H_wrapper &mysql_wrapper, MYSQL_RES_H_wrapper &mysql_res_wrapper,
    const std::vector<enum_field_types> &field_types) {
  MYSQL_RES_H mysql_res = mysql_res_wrapper.mysql_res;
  unsigned int num_column;
  if (m_command_field_info->num_fields(mysql_res, &num_column)) {
    LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG,
                    "failed to get column count");
    return get_error_status(mysql_wrapper);
  }

  if (num_column != field_types.size()) {
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "expect one column, but got %u", num_column);

    return Rdb_cmd_srv_status("Unexepcted column count");
  }

  MYSQL_FIELD_H *fields_info = nullptr;
  if (m_command_field_info->fetch_fields(mysql_res, &fields_info) ||
      fields_info == nullptr) {
    LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG, "failed to fetch fields");
    return get_error_status(mysql_wrapper);
  }
  MYSQL_FIELD *fields = reinterpret_cast<MYSQL_FIELD *>(fields_info);
  for (unsigned int i = 0; i < num_column; i++) {
    MYSQL_FIELD &field = fields[i];
    if (field.type != field_types[i]) {
      LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                      "expect column %u type %u, but got %u", i, field_types[i],
                      field.type);
      return Rdb_cmd_srv_status("Unexpected column type for column " +
                                std::to_string(i));
    }
  }
  return Rdb_cmd_srv_status();
}

Rdb_cmd_srv_status Rdb_cmd_srv_helper::fetch_row(
    MYSQL_H_wrapper &mysql_wrapper, MYSQL_RES_H_wrapper &mysql_res_wrapper,
    MYSQL_ROW_H *row, ulong **column_lengths) {
  MYSQL_RES_H mysql_res = mysql_res_wrapper.mysql_res;
  if (m_command_query_result->fetch_row(mysql_res, row)) {
    LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG, "failed to fetch row");
    return get_error_status(mysql_wrapper);
  }

  if (m_command_query_result->fetch_lengths(mysql_res, column_lengths)) {
    return get_error_status(mysql_wrapper);
  }

  return Rdb_cmd_srv_status();
}

Rdb_cmd_srv_status Rdb_cmd_srv_helper::get_json_column(
    MYSQL_ROW_H row, ulong *column_lengths, int col, Json_dom_ptr &dom_ptr,
    enum_json_type expected_type) {
  JsonParseDefaultErrorHandler parse_handler(__func__, 0);
  dom_ptr = Json_dom::parse(row[col], column_lengths[col], parse_handler,
                            JsonDocumentDefaultDepthHandler);
  if (!dom_ptr) {
    LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG,
                    "failed to parse metadata");
    return Rdb_cmd_srv_status("failed to parse json column " +
                              std::to_string(col));
  }

  if (dom_ptr->json_type() != expected_type) {
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "expect json type %d, but got %d",
                    static_cast<int>(expected_type),
                    static_cast<int>(dom_ptr->json_type()));
    return Rdb_cmd_srv_status("Unexpected json type for column " +
                              std::to_string(col));
  }

  return Rdb_cmd_srv_status();
}

static bool get_json_int_field(Json_object *dom, const std::string_view &field,
                               longlong &value) {
  Json_dom *field_dom = dom->get(MYSQL_LEX_CSTRING{field.data(), field.size()});
  if (field_dom == nullptr) {
    value = 0;
    return false;
  }
  if (field_dom->json_type() != enum_json_type::J_INT) {
    return true;
  }
  Json_int *number = down_cast<Json_int *>(field_dom);
  value = number->value();
  return false;
}

Rdb_cmd_srv_status Rdb_cmd_srv_helper::read_index_metadata(
    const std::string &db_name, const std::string &table_name,
    const std::string &id, Rdb_vector_index_data &index_data) {
  MYSQL_H_wrapper mysql_wrapper(m_command_factory);
  auto status = connect(mysql_wrapper);
  if (status.error()) {
    return status;
  }

  MYSQL_RES_H_wrapper mysql_res_wrapper(m_command_query_result);
  const auto query = create_query(table_name, id, INDEX_DATA_TYPE_METADATA);
  status = execute_query(db_name, query, mysql_wrapper, mysql_res_wrapper);
  if (status.error()) {
    return status;
  }
  status = check_row_count(mysql_wrapper, 1);
  if (status.error()) {
    return status;
  }
  status = check_column_types(mysql_wrapper, mysql_res_wrapper,
                              std::vector<enum_field_types>{MYSQL_TYPE_JSON});
  if (status.error()) {
    return status;
  }

  MYSQL_ROW_H row = nullptr;
  ulong *column_lengths = nullptr;
  status = fetch_row(mysql_wrapper, mysql_res_wrapper, &row, &column_lengths);
  if (status.error()) {
    return status;
  }
  Json_dom_ptr dom_ptr;
  status = get_json_column(row, column_lengths, 0, dom_ptr,
                           enum_json_type::J_OBJECT);
  if (status.error()) {
    return status;
  }
  Json_object *json_object = down_cast<Json_object *>(dom_ptr.get());
  longlong int_field_val;
  if (get_json_int_field(json_object, METADATA_KEY_VERSION, int_field_val)) {
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "invalid version number");
    return Rdb_cmd_srv_status("failed to read version number from metadata");
  }
  if (int_field_val != (int)Rdb_vector_index_data_version::V1) {
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "invalid version number %lld", int_field_val);
    return Rdb_cmd_srv_status("unsupported version number");
  }
  if (get_json_int_field(json_object, METADATA_KEY_NLIST, int_field_val)) {
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "invalid nlist");
    return Rdb_cmd_srv_status("failed to read nlist from metadata");
  }
  index_data.m_nlist = int_field_val;
  if (get_json_int_field(json_object, METADATA_KEY_PQ_M, int_field_val)) {
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "invalid pq_m");
    return Rdb_cmd_srv_status("failed to read pq_m from metadata");
  }
  index_data.m_pq_m = int_field_val;
  if (get_json_int_field(json_object, METADATA_KEY_PQ_NBITS, int_field_val)) {
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "invalid pq_nbits");
    return Rdb_cmd_srv_status("failed to read pq_nbits from metadata");
  }
  index_data.m_pq_nbits = int_field_val;

  return status;
}

Rdb_cmd_srv_status Rdb_cmd_srv_helper::read_codes(const std::string &db_name,
                                                  const std::string &table_name,
                                                  const std::string &id,
                                                  const std::string_view type,
                                                  std::vector<float> &codes) {
  MYSQL_H_wrapper mysql_wrapper(m_command_factory);
  auto status = connect(mysql_wrapper);
  if (status.error()) {
    return status;
  }

  MYSQL_RES_H_wrapper mysql_res_wrapper(m_command_query_result);
  const auto query = create_query(table_name, id, type);
  status = execute_query(db_name, query, mysql_wrapper, mysql_res_wrapper);
  if (status.error()) {
    return status;
  }
  uint64_t row_count;
  status = get_row_count(mysql_wrapper, row_count);
  if (status.error()) {
    return status;
  }
  status = check_column_types(mysql_wrapper, mysql_res_wrapper,
                              std::vector<enum_field_types>{MYSQL_TYPE_JSON});
  if (status.error()) {
    return status;
  }
  std::vector<float> row_codes;
  for (uint64_t i = 0; i < row_count; i++) {
    MYSQL_ROW_H row = nullptr;
    ulong *column_lengths = nullptr;
    status = fetch_row(mysql_wrapper, mysql_res_wrapper, &row, &column_lengths);
    if (status.error()) {
      return status;
    }
    Json_dom_ptr dom_ptr;
    status = get_json_column(row, column_lengths, 0, dom_ptr,
                             enum_json_type::J_ARRAY);
    if (status.error()) {
      return status;
    }
    Json_wrapper wrapper(std::move(dom_ptr));
    if (parse_fb_vector(wrapper, row_codes)) {
      return Rdb_cmd_srv_status("failed to parser vector codes");
    }
    codes.insert(codes.end(), row_codes.begin(), row_codes.end());
  }

  return status;
}

Rdb_cmd_srv_status Rdb_cmd_srv_helper::load_index_data(
    const std::string &db_name, const std::string &table_name,
    const std::string &id, std::unique_ptr<Rdb_vector_index_data> &index_data) {
  index_data = std::make_unique<Rdb_vector_index_data>();
  auto status = read_index_metadata(db_name, table_name, id, *index_data);
  if (status.error()) {
    return status;
  }
  status = read_codes(db_name, table_name, id, INDEX_DATA_TYPE_QUANTIZER,
                      index_data->m_quantizer_codes);
  if (status.error()) {
    return status;
  }
  if (index_data->m_pq_m > 0) {
    // only read pq codes when needed
    return read_codes(db_name, table_name, id,
                      INDEX_DATA_TYPE_PRODUCT_QUANTIZER,
                      index_data->m_pq_codes);
  }
  return status;
}

}  // namespace myrocks
