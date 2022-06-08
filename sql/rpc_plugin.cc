#include <mutex>
#include <thread>
#include <unordered_map>

#include <mysql/service_rpc_plugin.h>
#include "./storage/rocksdb/nosql_access.h"
#include "sql/binlog.h"
#include "sql/mysqld_thd_manager.h"
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

void initialize_thd() {
  if (!current_thd) {
    // first call from this rpc thread
    THD *thd = new THD();
    my_thread_init();
    thd->set_new_thread_id();
    thd->thread_stack = reinterpret_cast<char *>(&thd);
    thd->store_globals();
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
    bypass_rpc_exception ret;
    ret.errnum = ER_NOT_SUPPORTED_YET;
    ret.sqlstate = "MYF(0)";
    ret.message =
        "Bypass rpc does not allow nonzero value in wait_for_hlc_timeout_ms";
    return ret;
  }

  if (check_hlc_bound(current_thd, param)) {
    bypass_rpc_exception ret;
    ret.errnum = ER_STALE_HLC_READ;
    ret.sqlstate = "MYF(0)";
    ret.message = "Requested HLC timestamp is higher than current engine HLC";
    return ret;
  }

  if (rpc_open_table(current_thd, param)) {
    bypass_rpc_exception ret;
    ret.errnum = ER_NOT_SUPPORTED_YET;
    ret.sqlstate = "MYF(0)";
    ret.message = "error in opening a table";
    return ret;
  }
  current_thd->status_var.com_stat[SQLCOM_SELECT]++;
  current_thd->status_var.questions++;
  myrocks_columns columns;
  THD *thd = current_thd;
  const auto &ret = myrocks::myrocks_select_by_key(thd, &columns, *param);

  // clean up before returning back to the rpc plugin
  trans_commit_stmt(thd);  // need to call this because we locked table
  close_thread_tables(thd);
  thd->lex->unit->cleanup(true);
  lex_end(thd->lex);
  thd->free_items();
  thd->reset_query_attrs();
  thd->mdl_context.release_transactional_locks();
  thd->mem_root->ClearForReuse();

  return ret;
}
