/* Copyright (c) 2018, Oracle and/or its affiliates. All rights reserved.

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

#ifndef MYSQL_SERVICE_RPC_PLUGIN_INCLUDED
#define MYSQL_SERVICE_RPC_PLUGIN_INCLUDED

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "my_inttypes.h"

// These structs are defined for communication between rpc plugin and myrocks
enum class myrocks_value_type {
  BOOL,
  UNSIGNED_INT,
  SIGNED_INT,
  DOUBLE,
  STRING,
};

struct myrocks_column_cond_value {
  myrocks_value_type type;
  uint32_t length = 0;  // valid for string
  union {
    bool boolVal;
    uint64_t i64Val;
    double doubleVal;
    const char *stringVal;
  };
};

struct myrocks_where_item {
  std::string column;
  enum class where_op {
    _EQ,
    _LT,
    _GT,
    _LE,
    _GE,
  } op;
  myrocks_column_cond_value value;
};

struct myrocks_order_by_item {
  std::string column;
  enum class order_by_op { _ASC, _DESC } op;
};

struct myrocks_column_value {
  myrocks_value_type type;
  bool isNull = false;
  uint32_t length = 0;  // valid for string and binary
  union {
    bool boolVal;
    uint64_t i64Val;
    int64_t signed_i64Val;
    double doubleVal;
    uchar *stringVal;
  };
};

constexpr int MAX_VALUES_PER_RPC_WHERE_ITEM = 30;
struct myrocks_where_in_item {
  std::string column;
  std::array<myrocks_column_cond_value, MAX_VALUES_PER_RPC_WHERE_ITEM> values;
  // If the number of values is above the limit, then these values are stored in
  // the following more_values.
  myrocks_column_cond_value *more_values;
  uint32_t num_values;
};

// This rpc buffer is allocated per each rpc thread
constexpr int MAX_COLUMNS_PER_RPC_BUFFER = 200;
using myrocks_columns =
    std::array<myrocks_column_value, MAX_COLUMNS_PER_RPC_BUFFER>;

/*
   myrocks calls this callback function whenever a row is ready.
   rpc_buffer             : points to protocol-specific (eg. thrift) rpc buffer
                            where myrocks_columns are converted to
                            protocol-specific format and stored.
   myrocks_columns_values : points to where myrocks_column_value typed values
                            are stored.
   num_columns            : stores the number of columns to be fetched from
                            myrocks_column_values.
*/
using myrocks_bypass_rpc_send_row_fn =
    void (*)(void *rpc_buffer, myrocks_columns *myrocks_columns_values,
             uint64_t num_columns);

// This is allocated and populated by rpc plugin
struct myrocks_select_from_rpc {
  std::string db_name;
  std::string table_name;
  std::vector<std::string> columns;
  std::vector<myrocks_where_item> where;
  std::vector<myrocks_where_in_item> where_in;
  uint64_t limit;
  uint64_t limit_offset;
  std::string force_index;
  std::vector<myrocks_order_by_item> order_by;
  void *rpc_buffer;
  myrocks_bypass_rpc_send_row_fn send_row;
  uint64_t hlc_lower_bound_ts;  // 0 if hlc lower bound is not given
};

struct bypass_rpc_exception {
  uint32_t errnum = 0;
  std::string sqlstate;
  std::string message;
};

extern "C" struct rpc_plugin_service_st {
  bypass_rpc_exception (*bypass_select)(const myrocks_select_from_rpc *param);
} * rpc_plugin_service;

#ifdef MYSQL_DYNAMIC_PLUGIN
#define bypass_select(a) rpc_plugin_service->bypass_select((a))
#else
/**
  Run bypass select query

  @param param       struct containing the details of the given query
                     if param is nullptr, bypass engine does nothing but
                     destroying its THD, preparing to uninstall rpc plugin.
  @return bypass_rpc_exception containing exception info if exception happens
*/
bypass_rpc_exception bypass_select(const myrocks_select_from_rpc *param);
#endif /* MYSQL_DYNAMIC_PLUGIN */

#endif /* MYSQL_SERVICE_RPC_PLUGIN_INCLUDED */
