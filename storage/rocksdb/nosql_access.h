/*
   Copyright (c) 2019, Facebook, Inc.

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

/* C++ standard header files */
#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

/* C standard header files */
#include <ctype.h>

/* MySQL header files */
#include <mysql/service_rpc_plugin.h>
#include "./sql_string.h"
#include "sql/protocol.h"
#include "sql/sql_class.h"

#pragma once

enum bypass_type {
  SQL,
  RPC,
};

struct REJECTED_ITEM {
  // Timestamp of rejected query
  my_timeval rejected_bypass_query_timestamp;
  // Rejected query
  std::string rejected_bypass_query;
  // Error message
  std::string error_msg;
  // Type of "nosql" access
  bypass_type unsupported_bypass_type;
};

class Query_block;

namespace myrocks {

bool rocksdb_handle_single_table_select(THD *thd, Query_block *select_lex);

extern std::deque<REJECTED_ITEM> rejected_bypass_queries;
extern std::mutex rejected_bypass_query_lock;

bypass_rpc_exception rocksdb_select_by_key(THD *thd, myrocks_columns *columns,
                                           const myrocks_select_from_rpc &str);

}  // namespace myrocks
