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

// return true if opening a table fails, otherwise return false
bool rpc_open_table(const myrocks_select_from_rpc *param) {
  if (param == nullptr) {
    return true;
  }

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

  THD *thd = current_thd;
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
  if (rpc_open_table(param)) {
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
