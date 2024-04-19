/* Copyright (c) 2024 Meta Platforms, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#pragma once

#include <assert.h>
#include <sys/types.h>

#include "include/lex_string.h"
#include "include/m_ctype.h"
#include "include/my_sqlcommand.h"
#include "sql/current_thd.h"
#include "sql/mysqld.h"
#include "sql/parser_yystype.h"
#include "sql/sql_cmd.h" /* Sql_cmd */
#include "sql/sql_lex.h"
#include "sql/sql_list.h"
#include "sql_string.h"

class Sql_cmd_checksum_tables final : public Sql_cmd {
 public:
  Sql_cmd_checksum_tables() {}

  enum_sql_command sql_command_code() const override { return SQLCOM_CHECKSUM; }

  bool execute(THD *thd) override;

  void set_item_list(mem_root_deque<Item *> *item_list) {
    m_item_list = item_list;
  }

 private:
  mem_root_deque<Item *> *m_item_list{nullptr};
};
