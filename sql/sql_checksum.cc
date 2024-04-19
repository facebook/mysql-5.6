/* Copyright (c) 2024 Meta Platforms, Inc. All rights reserved.

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

/* Checksum one or more tables, optionally filtering on some columns */

#include "sql/sql_checksum.h"

#include "auth/auth_acls.h"
#include "mysql/components/services/log_builtins.h"
#include "sql/debug_sync.h"
#include "sql/log.h"
#include "sql/mysqld.h"
#include "sql/mysqld_thd_manager.h"
#include "sql/parser_yystype.h"
#include "sql/protocol_classic.h"
#include "sql/sql_base.h"
#include "sql/sql_class.h"
#include "sql/sql_error.h"
#include "sql/sql_table.h"
#include "sql/transaction.h"

/**
  Main command entry point for CHECKSUM TABLE statement.

  @return true on error, false otherwise.
*/
bool Sql_cmd_checksum_tables::execute(THD *thd) {
  DBUG_TRACE;
  LEX *const lex = thd->lex;
  Table_ref *const tables = lex->query_tables;
  if (check_table_access(thd, SELECT_ACL, tables, false, UINT_MAX, false))
    return true;

  return mysql_checksum_table(thd, tables, m_item_list, &lex->check_opt);
}
