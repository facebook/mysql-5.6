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

#include "sql/sql_bulk_load.h"
#include "dd/types/abstract_table.h"
#include "sql/auth/auth_acls.h"
#include "sql_base.h"

bool Sql_cmd_bulk_load::is_valid_id(const char *id, size_t length) {
  if (length == 0 || length > 64) return false;
  if (std::strncmp(id, "sys", 3) == 0) return false;
  for (size_t i = 0; i < length; i++) {
    if (std::isspace(id[i])) return false;
  }
  return true;
}

bool Sql_cmd_bulk_load_start::execute(THD *thd) {
  LEX *const lex = thd->lex;
  if (!is_valid_id(m_bulk_load_session_id.str, m_bulk_load_session_id.length)) {
    my_error(ER_DA_BULK_LOAD, MYF(0),
             "Valid ID should not include whitespace, length > 0 and < 64, not "
             "start with 'sys'");
    return true;
  }
  Table_ref *const all_tables = lex->query_tables;
  if (check_table_access(thd, INSERT_ACL, all_tables, true, UINT_MAX, false))
    return true;
  if (open_and_lock_tables(thd, all_tables, 0)) {
    my_error(ER_DA_BULK_LOAD, MYF(0), thd->get_stmt_da()->message_text());
    return true;
  }
  if (ha_bulk_load_action(thd, BULK_LOAD_START, m_bulk_load_session_id.str,
                          all_tables)) {
    my_error(ER_DA_BULK_LOAD, MYF(0), "Bulk load fails to start");
    return true;
  }
  my_ok(thd);
  return false;
}

bool Sql_cmd_bulk_load_commit::execute(THD *thd) {
  if (!is_valid_id(m_bulk_load_session_id.str, m_bulk_load_session_id.length)) {
    my_error(ER_DA_BULK_LOAD, MYF(0),
             "Valid ID should not include whitespace, length > 0 and < 64, not "
             "start with 'sys'");
    return true;
  }
  // TODO: compact and ingest, update session status
  my_ok(thd);
  return false;
}

bool Sql_cmd_bulk_load_rollback::execute(THD *thd) {
  if (!is_valid_id(m_bulk_load_session_id.str, m_bulk_load_session_id.length)) {
    my_error(ER_DA_BULK_LOAD, MYF(0),
             "Valid ID should not include whitespace, length > 0 and < 64, not "
             "start with 'sys'");
    return true;
  }
  if (ha_bulk_load_action(thd, BULK_LOAD_ROLLBACK,
                          m_bulk_load_session_id.str)) {
    my_error(ER_DA_BULK_LOAD, MYF(0), "Fail to drop bulk load");
    return true;
  }
  my_ok(thd);
  return false;
}
