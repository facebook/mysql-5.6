/*
   Copyright (c) 2012,2013 Monty Program Ab

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

/* C++ system header files */
#include <string>
#include <vector>

/* MyRocks header files */
#include "rocksdb/table.h"

namespace myrocks {

enum rdb_io_error_type_enum {
  RDB_IO_ERROR_TX_COMMIT,
  RDB_IO_ERROR_DICT_COMMIT,
  RDB_IO_ERROR_BG_THREAD
};
using RDB_IO_ERROR_TYPES = enum rdb_io_error_type_enum;

void rdb_handle_io_error(rocksdb::Status status, RDB_IO_ERROR_TYPES type);

void rdb_get_cf_options(
  const std::string &cf_name,
  rocksdb::ColumnFamilyOptions *opts) __attribute__((__nonnull__));

int rdb_normalize_tablename(
  const char *tablename,
  StringBuffer<256> *strbuf)
  __attribute__((__nonnull__, __warn_unused_result__));

int rdb_split_normalized_tablename(const char *fullname,
                                   StringBuffer<256> *dbbuf,
                                   StringBuffer<256> *tablebuf,
                                   StringBuffer<256> *partitionbuf)
  __attribute__((__nonnull__, __warn_unused_result__));

std::vector<std::string> rdb_get_table_names(void);

/* Check if given table has a primary key */
bool rdb_table_has_hidden_pk(const my_core::TABLE* table_arg)
  __attribute__((__nonnull__, __warn_unused_result__));

int rdb_get_table_perf_counters(
  const char *tablename,
  RDB_SHARE_PERF_COUNTERS *counters) __attribute__((__nonnull__(2)));

void rdb_request_save_stats();

/*
  Access to singleton objects.
*/

rocksdb::DB* rdb_get_rocksdb_db();

class Rdb_cf_manager;
Rdb_cf_manager& rdb_get_cf_manager();

rocksdb::BlockBasedTableOptions& rdb_get_table_options();

class Rdb_dict_manager;
Rdb_dict_manager* rdb_get_dict_manager(void)
  __attribute__((__warn_unused_result__));

class Rdb_ddl_manager;
Rdb_ddl_manager* rdb_get_ddl_manager(void)
  __attribute__((__warn_unused_result__));

class Rdb_binlog_manager;
Rdb_binlog_manager* rdb_get_binlog_manager(void)
  __attribute__((__warn_unused_result__));

}  // namespace myrocks
