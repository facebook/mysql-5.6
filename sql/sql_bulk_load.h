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

#pragma once

#include "sql/sql_cmd.h" /* Sql_cmd */
#include "sql/sql_lex.h"

/**
  Child classes deal with SQL statements:
  * BULK LOAD START
  * BULK LOAD COMMIT
  * BULK LOAD ROLLBACK
*/

class Sql_cmd_bulk_load : public Sql_cmd {
 public:
  Sql_cmd_bulk_load(const LEX_STRING &bulk_load_session_id)
      : m_bulk_load_session_id(bulk_load_session_id) {}
  ~Sql_cmd_bulk_load() override = default;

 protected:
  const LEX_STRING m_bulk_load_session_id;
  bool is_valid_id(const char *id, size_t length);
};

class Sql_cmd_bulk_load_commit final : public Sql_cmd_bulk_load {
 public:
  Sql_cmd_bulk_load_commit(const LEX_STRING &bulk_load_session_id)
      : Sql_cmd_bulk_load(bulk_load_session_id) {}
  enum_sql_command sql_command_code() const override {
    return SQLCOM_BULK_LOAD_COMMIT;
  }
  bool execute(THD *thd) override;
};

class Sql_cmd_bulk_load_rollback final : public Sql_cmd_bulk_load {
 public:
  Sql_cmd_bulk_load_rollback(const LEX_STRING &bulk_load_session_id)
      : Sql_cmd_bulk_load(bulk_load_session_id) {}
  enum_sql_command sql_command_code() const override {
    return SQLCOM_BULK_LOAD_ROLLBACK;
  }
  bool execute(THD *thd) override;
};

class Sql_cmd_bulk_load_start final : public Sql_cmd_bulk_load {
 public:
  Sql_cmd_bulk_load_start(const LEX_STRING &bulk_load_session_id,
                          Mem_root_array<Table_ident *> *table_list)
      : Sql_cmd_bulk_load(bulk_load_session_id), m_table_list(table_list) {}

  enum_sql_command sql_command_code() const override {
    return SQLCOM_BULK_LOAD_START;
  }

  bool execute(THD *thd) override;

  Mem_root_array<Table_ident *> *m_table_list;
};
