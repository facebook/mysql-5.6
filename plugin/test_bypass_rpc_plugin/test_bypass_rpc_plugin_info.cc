/* Copyright (c) 2015, 2020, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#include <cinttypes>
#include <regex>
#include <string>

#include <fcntl.h>
#include <mysql/plugin.h>
#include <mysql/service_rpc_plugin.h>
#include <sql/sql_class.h>
#include <sql/sql_lex.h>
#include <sql/sql_thd_internal_api.h>
#include "m_string.h"
#include "my_inttypes.h"
#include "my_io.h"
#include "my_sys.h"
#include "sql/debug_sync.h"
#include "sql/mysqld_thd_manager.h"
#include "sql/srv_session.h"

#define RPC_MAX_QUERY_LENGTH 1000
#define MAX_STRING_LENGTH 1000

struct test_thread_context {
  my_thread_handle thread;
  void *p;
  bool thread_finished;
  void (*test_function)(void *);
};

static const char *log_filename_sql = "test_bypass_rpc_plugin_sql.result";
static const char *log_filename_rpc = "test_bypass_rpc_plugin_rpc.result";
static const char *input_filename = "test_bypass_rpc_plugin_input.txt";
static const char *input_hlc_filename = "test_bypass_rpc_plugin_hlc.txt";
FILE *outfile_rpc, *outfile_sql;

static void *test_sql_threaded_wrapper(void *param) {
  struct test_thread_context *context = (struct test_thread_context *)param;
  if (srv_session_init_thread(context->p))
    fprintf(outfile_rpc, "srv_session_init_thread failed.");

  context->test_function(context->p);
  srv_session_deinit_thread();

  context->thread_finished = true;
  return nullptr;
}

static std::vector<std::string> splitString(const std::string &s) {
  std::vector<std::string> v;

  std::stringstream ss(s);
  while (ss.good()) {
    std::string substr;
    getline(ss, substr, ',');
    v.push_back(std::move(substr));
  }
  return v;
}

static void fillFields(myrocks_select_from_rpc &param, std::string &fields) {
  if (fields == "*") {
    // select all, just empty columns field
    return;
  }
  param.columns = splitString(fields);
}

static void fillTable(myrocks_select_from_rpc &param, std::string &table) {
  param.table_name = table;
}

static void fillDbname(myrocks_select_from_rpc &param, std::string &dbname) {
  param.db_name = dbname;
}

static myrocks_where_item::where_op convertToWhereOp(std::string &op) {
  if (op == "=") return myrocks_where_item::where_op::EQ;
  if (op == "<") return myrocks_where_item::where_op::LT;
  if (op == ">") return myrocks_where_item::where_op::GT;
  if (op == "<=") return myrocks_where_item::where_op::LE;
  if (op == ">=") return myrocks_where_item::where_op::GE;
  return myrocks_where_item::where_op::EQ;
}

static void send_row(void * /* unused */, myrocks_columns *values,
                     uint64_t num_columns) {
  for (uint64_t i = 0; i < num_columns; ++i) {
    auto &column = values->at(i);
    if (!column.isNull) {
      switch (column.type) {
        case myrocks_value_type::BOOL: {
          fprintf(outfile_rpc, "%d ", column.boolVal);
          break;
        }
        case myrocks_value_type::UNSIGNED_INT: {
          fprintf(outfile_rpc, "%" PRIu64 " ", column.i64Val);
          break;
        }
        case myrocks_value_type::SIGNED_INT: {
          fprintf(outfile_rpc, "%" PRId64 " ", column.signed_i64Val);
          break;
        }
        case myrocks_value_type::DOUBLE: {
          fprintf(outfile_rpc, "%f ", column.doubleVal);
          break;
        }
        case myrocks_value_type::STRING: {
          std::string str(reinterpret_cast<char *>(column.stringVal),
                          column.length);
          fprintf(outfile_rpc, "%s ", str.c_str());
          break;
        }
        default: {
          // we don't throw exception here because mysql will continue
          // calling send_row() function until processing the query ends
          fprintf(outfile_rpc, "null ");
        }
      }
    }
  }
  fprintf(outfile_rpc, "\n");
}

static int sql_start_result_metadata(void *, uint, uint, const CHARSET_INFO *) {
  DBUG_TRACE;
  return false;
}

static int sql_field_metadata(void *, struct st_send_field *,
                              const CHARSET_INFO *) {
  DBUG_TRACE;
  return false;
}

static int sql_end_result_metadata(void *, uint, uint) {
  DBUG_TRACE;
  return false;
}

static int sql_start_row(void *) {
  DBUG_TRACE;
  return false;
}

static int sql_end_row(void *) {
  DBUG_TRACE;
  fprintf(outfile_sql, "\n");
  return false;
}

static void sql_abort_row(void *) { DBUG_TRACE; }

static ulong sql_get_client_capabilities(void *) {
  DBUG_TRACE;
  return 0;
}

static int sql_get_null(void *) {
  DBUG_TRACE;
  return false;
}

static int sql_get_integer(void *, longlong) {
  DBUG_TRACE;
  return false;
}

static int sql_get_longlong(void *, longlong, uint) {
  DBUG_TRACE;
  return false;
}

static int sql_get_decimal(void *, const decimal_t *) {
  DBUG_TRACE;
  return false;
}

static int sql_get_double(void *, double, uint32) {
  DBUG_TRACE;
  return false;
}

static int sql_get_date(void *, const MYSQL_TIME *) {
  DBUG_TRACE;
  return false;
}

static int sql_get_time(void *, const MYSQL_TIME *, uint) {
  DBUG_TRACE;
  return false;
}

static int sql_get_datetime(void *, const MYSQL_TIME *, uint) {
  DBUG_TRACE;
  return false;
}

static int sql_get_string(void *, const char *const value, size_t length,
                          const CHARSET_INFO *const) {
  DBUG_TRACE;
  char tmp[MAX_STRING_LENGTH];
  auto len = std::min((int)length, MAX_STRING_LENGTH-1);
  strncpy(tmp, value, len);
  tmp[len] = '\0';
  fprintf(outfile_sql, "%s ", tmp);
  return false;
}

static void sql_handle_error(void *, uint sql_errno, const char *const err_msg,
                             const char *const) {
  DBUG_TRACE;
  fprintf(outfile_sql, "ERROR %u: %s\n", sql_errno, err_msg);
}

const struct st_command_service_cbs sql_cbs = {
    sql_start_result_metadata,
    sql_field_metadata,
    sql_end_result_metadata,
    sql_start_row,
    sql_end_row,
    sql_abort_row,
    sql_get_client_capabilities,
    sql_get_null,
    sql_get_integer,
    sql_get_longlong,
    sql_get_decimal,
    sql_get_double,
    sql_get_date,
    sql_get_time,
    sql_get_datetime,
    sql_get_string,
    nullptr,  // sql_handle_ok,
    sql_handle_error,
    nullptr,  // sql_shutdown,
    nullptr,
};

static void test_in_spawned_thread(void *p, void (*test_function)(void *)) {
  my_thread_attr_t attr; /* Thread attributes */
  my_thread_attr_init(&attr);
  (void)my_thread_attr_setdetachstate(&attr, MY_THREAD_CREATE_JOINABLE);

  struct test_thread_context *context = (struct test_thread_context *)my_malloc(
      PSI_INSTRUMENT_ME, sizeof(struct test_thread_context), MYF(0));
  context->p = p;
  context->thread_finished = false;
  context->test_function = test_function;

  /* now create the thread and call test_session within the thread. */
  if (my_thread_create(&context->thread, &attr, test_sql_threaded_wrapper,
                       context) != 0)
    fprintf(outfile_rpc, "Could not create test session thread");
  struct st_plugin_int *plugin = (struct st_plugin_int *)p;
  plugin->data = (void *)context;
}

static void fill_table_and_columns(myrocks_select_from_rpc &param,
                                   std::string &query) {
  std::regex r(
      R"(SELECT \/\*\+ bypass \*\/ ((,?\S+){1,}) FROM ([^. ]+)\.([^. ]+))");
  std::smatch m;
  if (regex_search(query, m, r)) {
    std::string columns(m[1]), dbname(m[3]), table(m[4]);
    fillFields(param, columns);
    fillTable(param, table);
    fillDbname(param, dbname);
    query = m.suffix();
    return;
  }
  query.clear();  // indicating that the query is not supported
}

static void fill_force_index(myrocks_select_from_rpc &param,
                             std::string &query) {
  std::regex r(R"(FORCE INDEX \((\S+)\))");
  std::smatch m;
  if (regex_search(query, m, r)) {
    std::string index(m[1]);
    param.force_index = std::move(index);
    query = m.suffix();
  }
}

static void fill_order_by(myrocks_select_from_rpc &param, std::string &query) {
  std::regex r(R"((\S+) (DESC|ASC))");
  std::smatch m;
  while (regex_search(query, m, r)) {
    std::string column(m[1]), order(m[2]);
    myrocks_order_by_item oitem;
    oitem.column = std::move(column);
    if (order == "DESC") {
      oitem.op = myrocks_order_by_item::order_by_op::DESC;
    } else {
      oitem.op = myrocks_order_by_item::order_by_op::ASC;
    }
    param.order_by.push_back(std::move(oitem));
    query = m.suffix();
  }
}

static void fill_where_in(myrocks_select_from_rpc &param, std::string &query) {
  std::regex r(R"((\S+) IN \(((,?\d+){1,})\))");
  std::smatch m;
  while (regex_search(query, m, r)) {
    std::string name(m[1]), input_values(m[2]);
    auto idx = 0;
    auto values = splitString(input_values);
    myrocks_where_in_item witem;
    witem.column = std::move(name);
    witem.num_values = values.size();
    witem.more_values = nullptr;
    if (witem.num_values > MAX_VALUES_PER_RPC_WHERE_ITEM) {
      witem.more_values = new myrocks_column_cond_value[values.size()];
      for (const auto &value : values) {
        myrocks_column_cond_value cond_val;
        cond_val.type = myrocks_value_type::UNSIGNED_INT;
        cond_val.i64Val = stol(value);
        witem.more_values[idx++] = std::move(cond_val);
      }
    } else {
      for (const auto &value : values) {
        myrocks_column_cond_value cond_val;
        cond_val.type = myrocks_value_type::UNSIGNED_INT;
        cond_val.i64Val = stol(value);
        witem.values[idx++] = std::move(cond_val);
      }
    }
    param.where_in.push_back(std::move(witem));
    query = m.suffix();
  }
}

static void fill_where_string(myrocks_select_from_rpc &param,
                              std::string &query,
                              std::vector<std::string> &string_vec) {
  std::regex r(R"(([a-zA-Z0-9_]{1,})(>|=|<|>=|<=)\"(\S+)\")");
  std::smatch m;
  while (regex_search(query, m, r)) {
    std::string name(m[1]), op(m[2]), val(m[3]);
    myrocks_column_cond_value cond_val;
    cond_val.type = myrocks_value_type::STRING;

    string_vec.push_back(std::move(val));
    cond_val.stringVal = string_vec.back().c_str();
    cond_val.length = string_vec.back().size();
    myrocks_where_item witem = {.column = std::move(name),
                                .op = convertToWhereOp(op),
                                .value = std::move(cond_val)};
    param.where.push_back(std::move(witem));
    query = m.suffix();
  }
}

static void fill_where(myrocks_select_from_rpc &param, std::string &query) {
  std::regex r(R"(([a-zA-Z0-9_]{1,})(>|=|<|>=|<=)(\d+))");
  std::smatch m;
  while (regex_search(query, m, r)) {
    std::string name(m[1]), op(m[2]), val(m[3]);
    myrocks_column_cond_value cond_val;
    cond_val.type = myrocks_value_type::UNSIGNED_INT;
    cond_val.i64Val = stol(val);
    myrocks_where_item witem = {.column = std::move(name),
                                .op = convertToWhereOp(op),
                                .value = std::move(cond_val)};
    param.where.push_back(std::move(witem));
    query = m.suffix();
  }
}

static void fill_limit(myrocks_select_from_rpc &param, std::string &query) {
  std::regex r1(R"(LIMIT (\d+),(\d+))"), r2(R"(LIMIT (\d+))");
  std::smatch m;
  if (regex_search(query, m, r1)) {
    std::string offset(m[1]), limit(m[2]);
    param.limit_offset = stol(offset);
    param.limit = stol(limit);
    query = m.suffix();
  } else if (regex_search(query, m, r2)) {
    std::string limit(m[1]);
    param.limit_offset = 0;
    param.limit = stol(limit);
    query = m.suffix();
  } else {
    // bypass engine interprets this value as having no limit
    param.limit = std::numeric_limits<uint64_t>::max();
    param.limit_offset = 0;
  }
}

static void test_sql() {
  void *plugin_ctx = nullptr;
  MYSQL_SESSION st_session = srv_session_open(NULL, plugin_ctx);
  if (!st_session) {
    fprintf(outfile_sql, "error in opening srv_session\n");
    return;
  }

  FILE *infile = my_fopen(input_filename, O_RDONLY, MYF(0));
  if (infile == nullptr) {
    fprintf(outfile_sql, "error in opening input file: %s\n", input_filename);
    srv_session_close(st_session);
    return;
  }
  char line_buffer[RPC_MAX_QUERY_LENGTH];
  while (fgets(line_buffer, RPC_MAX_QUERY_LENGTH, infile)) {
    std::string query_str(line_buffer);
    fprintf(outfile_sql, "%s", query_str.c_str());
    COM_DATA cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.com_query.query = query_str.c_str();
    cmd.com_query.length = query_str.size();
    bool fail = command_service_run_command(
        st_session, COM_QUERY, &cmd, &my_charset_utf8mb3_general_ci, &sql_cbs,
        CS_TEXT_REPRESENTATION, nullptr);
    if (fail) {
      fprintf(outfile_sql, "error in running cmd: %d\n", fail);
      srv_session_close(st_session);
      return;
    }
  }
  my_fclose(infile, MYF(0));
  srv_session_close(st_session);
}

static void test_rpc(void *) {
  FILE *infile = my_fopen(input_filename, O_RDONLY, MYF(0));
  FILE *hlc_file = my_fopen(input_hlc_filename, O_RDONLY, MYF(0));
  uint64_t hlc_lower_bound_ts = 0;

  if (infile == nullptr) {
    fprintf(outfile_rpc, "error in opening input file: %s\n", input_filename);
    return;
  }

  // read hlc input value from the input file if it exists
  if (hlc_file != nullptr) {
    auto c = fscanf(hlc_file, "%" PRIu64, &hlc_lower_bound_ts);
    my_fclose(hlc_file, MYF(0));
    if (c == 0) {
      fprintf(outfile_rpc, "error in reading hlc value\n");
      return;
    }
  }

  char line_buffer[RPC_MAX_QUERY_LENGTH];
  while (fgets(line_buffer, RPC_MAX_QUERY_LENGTH, infile)) {
    std::string query_str(line_buffer);
    std::vector<std::string> string_vec;
    fprintf(outfile_rpc, "%s", query_str.c_str());

    myrocks_select_from_rpc param;
    param.send_row = send_row;
    param.hlc_lower_bound_ts = hlc_lower_bound_ts;

    fill_table_and_columns(param, query_str);
    if (query_str.empty()) continue;
    fill_force_index(param, query_str);
    fill_where_in(param, query_str);
    fill_where_string(param, query_str, string_vec);
    fill_where(param, query_str);
    fill_order_by(param, query_str);
    fill_limit(param, query_str);

    const auto &exception = bypass_select(&param);
    if (exception.errnum) {
      fprintf(outfile_rpc, "ERROR %d: %s\n", exception.errnum,
              exception.message.c_str());
    }

    // if allocated, more_values needs to be deallocated
    for (auto &witem : param.where_in) {
      if (witem.more_values) {
        delete[] witem.more_values;
      }
    }
  }
  my_fclose(infile, MYF(0));
  bypass_select(nullptr);  // inform that rpc plugin will be uninstalled
}

static int test_bypass_rpc_plugin_init(void *p) {
  DBUG_TRACE;
  unlink(log_filename_sql);
  unlink(log_filename_rpc);
  outfile_sql = my_fopen(log_filename_sql, O_CREAT | O_RDWR, MYF(0));
  outfile_rpc = my_fopen(log_filename_rpc, O_CREAT | O_RDWR, MYF(0));
  test_sql();
  test_in_spawned_thread(p, test_rpc);
  return 0;
}

static int test_bypass_rpc_plugin_deinit(void *p) {
  DBUG_TRACE;
  struct st_plugin_int *plugin = (struct st_plugin_int *)p;
  struct test_thread_context *context =
      (struct test_thread_context *)plugin->data;
  my_thread_join(&context->thread, nullptr);
  my_free(context);
  my_fclose(outfile_rpc, MYF(0));
  my_fclose(outfile_sql, MYF(0));
  return 0;
}

/* Mandatory structure describing the properties of the plugin. */
static struct st_mysql_daemon test_bypass_rpc_plugin = {
    MYSQL_DAEMON_INTERFACE_VERSION};

mysql_declare_plugin(test_daemon){
    MYSQL_DAEMON_PLUGIN,
    &test_bypass_rpc_plugin,
    "test_bypass_rpc_plugin_info",
    "MySQL Eng",
    "Test bypass rpc plugin info",
    PLUGIN_LICENSE_GPL,
    test_bypass_rpc_plugin_init,   /* Plugin Init */
    nullptr,                       /* Plugin Check uninstall */
    test_bypass_rpc_plugin_deinit, /* Plugin Deinit */
    0x0100 /* 1.0 */,
    nullptr, /* status variables */
    nullptr, /* system variables */
    nullptr, /* config options */
    0,       /* flags */
} mysql_declare_plugin_end;
