#include <mutex>
#include <thread>
#include <unordered_map>

#include <mysql/service_rpc_plugin.h>
#include "sql/binlog.h"
#include "sql/debug_sync.h" /* DEBUG_SYNC */
#include "sql/mysqld_thd_manager.h"
#include "sql/protocol_rpc.h"
#include "sql/sql_base.h"
#include "sql/sql_lex.h"
#include "sql/strfunc.h"
#include "sql/transaction.h" /* trans_commit_stmt */

namespace {
// return true if input is not valid, otherwise return false
bool check_input(const myrocks_select_from_rpc *param) {
  if (param == nullptr) {
    return true;
  }
  if (param->db_name.empty() || param->table_name.empty() ||
      param->send_row == nullptr) {
    return true;
  }
  return false;
}

class RPC_Query_formatter : public THD::Query_formatter {
 private:
  void append_where_op(String &buf, myrocks_where_item::where_op op) {
    switch (op) {
      case myrocks_where_item::where_op::EQ:
        buf.append("=");
        return;
      case myrocks_where_item::where_op::LT:
        buf.append("<");
        return;
      case myrocks_where_item::where_op::GT:
        buf.append(">");
        return;
      case myrocks_where_item::where_op::LE:
        buf.append("<=");
        return;
      case myrocks_where_item::where_op::GE:
        buf.append(">=");
        return;
      default:
        buf.append("?");
    }
  }

  void append_value(String &buf, const myrocks_column_cond_value &value) {
    switch (value.type) {
      case myrocks_value_type::UNSIGNED_INT:
      case myrocks_value_type::SIGNED_INT: {
        auto val = std::to_string(value.i64Val);
        buf.append(val.c_str());
        return;
      }
      case myrocks_value_type::STRING: {
        String str(value.stringVal, value.length, system_charset_info);
        buf.append(str.c_ptr());
        return;
      }
      default: {
        buf.append("UNKNOWN_TYPE");
        return;
      }
    }
  }

  void append_order_op(String &buf, myrocks_order_by_item::order_by_op op) {
    switch (op) {
      case myrocks_order_by_item::ASC:
        buf.append("ASC");
        return;
      case myrocks_order_by_item::DESC:
        buf.append("DESC");
        return;
      default:
        buf.append("?");
    }
  }

 public:
  RPC_Query_formatter() {
    mysql_mutex_init(key_LOCK_rpc_query, &LOCK_rpc_query, MY_MUTEX_INIT_FAST);
  }

  ~RPC_Query_formatter() override { mysql_mutex_destroy(&LOCK_rpc_query); }

  virtual void format_query(String &buf) override {
    mysql_mutex_lock(&LOCK_rpc_query);
    if (m_param == nullptr) {
      mysql_mutex_unlock(&LOCK_rpc_query);
      return;
    }
    bool is_first = true;  // first item in the vector?
    buf.append("SELECT /* bypass rpc */ ");
    for (auto const &col : m_param->columns) {
      if (is_first) {
        is_first = false;
      } else {
        buf.append(",");
      }
      buf.append(col.c_str());
    }
    buf.append(" FROM ");
    buf.append(m_param->db_name.c_str());
    buf.append(".");
    buf.append(m_param->table_name.c_str());
    if (!m_param->force_index.empty()) {
      buf.append(" FORCE INDEX (");
      buf.append(m_param->force_index.c_str());
      buf.append(")");
    }
    buf.append(" WHERE ");

    is_first = true;
    for (auto const &item : m_param->where) {
      if (is_first) {
        is_first = false;
      } else {
        buf.append(" AND ");
      }
      buf.append(item.column.c_str());
      append_where_op(buf, item.op);
      append_value(buf, item.value);
    }

    for (auto const &item : m_param->where_in) {
      if (is_first) {
        is_first = false;
      } else {
        buf.append(" AND ");
      }
      buf.append(item.column.c_str());
      buf.append(" IN (");
      bool more_values = item.num_values > MAX_VALUES_PER_RPC_WHERE_ITEM;
      bool is_first_in = true;  // first item in IN clause?
      for (uint i = 0; i < item.num_values; ++i) {
        if (is_first_in) {
          is_first_in = false;
        } else {
          buf.append(",");
        }
        auto value = more_values ? item.more_values[i] : item.values[i];
        append_value(buf, value);
      }
      buf.append(")");
    }

    if (!m_param->order_by.empty()) {
      buf.append(" ORDER BY ");
      is_first = true;
      for (auto const &order : m_param->order_by) {
        if (is_first) {
          is_first = false;
        } else {
          buf.append(", ");
        }
        buf.append(order.column.c_str());
        buf.append(" ");
        append_order_op(buf, order.op);
      }
    }
    if (m_param->limit != std::numeric_limits<uint64_t>::max()) {
      buf.append(" LIMIT ");
      auto limit = std::to_string(m_param->limit);
      if (m_param->limit_offset) {
        auto limit_offset = std::to_string(m_param->limit_offset);
        buf.append(limit_offset.c_str());
        buf.append(", ");
        buf.append(limit.c_str());
      } else {
        buf.append(limit.c_str());
      }
    }
    mysql_mutex_unlock(&LOCK_rpc_query);
  }
  void set_rpc_query(const myrocks_select_from_rpc *param) {
    mysql_mutex_lock(&LOCK_rpc_query);
    m_param = param;
    mysql_mutex_unlock(&LOCK_rpc_query);
  }

 private:
  const myrocks_select_from_rpc *m_param{nullptr};
  mysql_mutex_t LOCK_rpc_query;
};

void initialize_thd() {
  if (!current_thd) {
    // first call from this rpc thread
    THD *thd = new THD();
    my_thread_init();
    thd->set_new_thread_id();
    thd->thread_stack = reinterpret_cast<char *>(&thd);
    thd->store_globals();
    thd->set_proc_info("Initialized");
    thd->set_time();

    // this is to ensure that thd->get_protocol()->connection_alive()
    // returns true all the time
    Protocol_RPC *protocol = new Protocol_RPC();
    thd->push_protocol(protocol);
    thd->security_context()->assign_user(STRING_WITH_LEN("rpc_plugin"));
    RPC_Query_formatter *formatter = new RPC_Query_formatter();
    thd->set_query_formatter(formatter);
    Global_THD_manager::get_instance()->add_thd(thd);

    LEX *lex = new LEX();
    thd->lex = lex;
  }
}

// return true if the requested hlc bound is not met, otherwise return false
bool check_hlc_bound(THD *thd, const myrocks_select_from_rpc *param) {
  if (param->hlc_lower_bound_ts == 0 ||
      !thd->variables.enable_block_stale_hlc_read) {
    // no hlc bound from client, or block_stale_hlc_read is not enabled
    return false;
  }
  uint64_t requested_hlc = param->hlc_lower_bound_ts;
  uint64_t applied_hlc =
      mysql_bin_log.get_selected_database_hlc(param->db_name);
  if (requested_hlc > applied_hlc) {
    return true;
  }
  return false;
}

// return true if opening a table fails, otherwise return false
bool rpc_open_table(THD *thd, const myrocks_select_from_rpc *param) {
  lex_start(thd);
  LEX_CSTRING db_name_lex_cstr, table_name_lex_cstr;
  Table_ref *table_list;

  if (lex_string_strmake(thd->mem_root, &db_name_lex_cstr,
                         param->db_name.c_str(), param->db_name.length()) ||
      lex_string_strmake(thd->mem_root, &table_name_lex_cstr,
                         param->table_name.c_str(),
                         param->table_name.length())) {
    goto thd_err;
  }

  if (make_table_list(thd, thd->lex->query_block, db_name_lex_cstr,
                      table_name_lex_cstr)) {
    goto thd_err;
  }

  table_list = thd->lex->query_block->m_table_list.first;
  thd->lex->sql_command = SQLCOM_SELECT;

  if (open_tables_for_query(thd, table_list, 0)) {
    goto thd_err;
  }
  return false;

thd_err:
  thd->lex->unit->cleanup(true);
  lex_end(thd->lex);
  thd->free_items();
  thd->reset_query_attrs();
  return true;
}
}  // namespace

/**
  Run bypass select query
*/
bypass_rpc_exception bypass_select(const myrocks_select_from_rpc *param) {
  if (param == nullptr) {
    // this means rpc plugin wants to destroy THD before killing rpc thread
    if (current_thd) {
      THD *thd = current_thd;
      thd->release_resources();
      Global_THD_manager::get_instance()->remove_thd(thd);
      delete thd;
    }
    bypass_rpc_exception ret;
    return ret;
  }

  initialize_thd();
  if (check_input(param)) {
    bypass_rpc_exception ret;
    ret.errnum = ER_NOT_SUPPORTED_YET;
    ret.sqlstate = "MYF(0)";
    ret.message = "Bypass rpc input is not valid";
    return ret;
  }

  if (wait_for_hlc_timeout_ms != 0 && param->hlc_lower_bound_ts != 0) {
    // bypass rpc doesn't allow nonzero value in wait_for_hlc_timeout_ms,
    // because it will block one of rpc threads
    bypass_rpc_exception ret{
        ER_NOT_SUPPORTED_YET, "MYF(0)",
        "Bypass rpc does not allow nonzero value in wait_for_hlc_timeout_ms"};
    return ret;
  }

  if (check_hlc_bound(current_thd, param)) {
    bypass_rpc_exception ret{
        ER_STALE_HLC_READ, "MYF(0)",
        "Requested HLC timestamp is higher than current engine HLC"};
    return ret;
  }

  if (rpc_open_table(current_thd, param)) {
    bypass_rpc_exception ret{ER_NOT_SUPPORTED_YET, "MYF(0)",
                             "Error in opening a table"};
    return ret;
  }
  current_thd->status_var.com_stat[SQLCOM_SELECT]++;
  current_thd->status_var.questions++;
  myrocks_columns columns;
  THD *thd = current_thd;
  RPC_Query_formatter *formatter =
      dynamic_cast<RPC_Query_formatter *>(thd->get_query_formatter());
  if (formatter) {
    // for populating rpc query in SHOW PROCESSLIST
    formatter->set_rpc_query(param);
  }

  DBUG_EXECUTE_IF("bypass_rpc_processlist_test", {
    const char act[] = "now signal ready_to_run_processlist wait_for continue";
    assert(!debug_sync_set_action(thd, STRING_WITH_LEN(act)));
  });

  if (rocksdb_hton == nullptr ||
      rocksdb_hton->bypass_select_by_key == nullptr) {
    bypass_rpc_exception ret{ER_UNKNOWN_ERROR, "MYF(0)",
                             "Handlerton is not set"};
    return ret;
  }
  const auto &ret = rocksdb_hton->bypass_select_by_key(thd, &columns, *param);

  // clean up before returning back to the rpc plugin
  trans_commit_stmt(thd);  // need to call this because we locked table
  close_thread_tables(thd);
  thd->lex->unit->cleanup(true);
  lex_end(thd->lex);
  thd->free_items();
  thd->reset_query_attrs();
  thd->mdl_context.release_transactional_locks();
  thd->mem_root->ClearForReuse();
  if (formatter) {
    formatter->set_rpc_query(nullptr);
  }
  return ret;
}
