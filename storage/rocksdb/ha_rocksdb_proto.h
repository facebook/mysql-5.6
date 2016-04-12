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

/* MySQL header files */
#include "./sql_string.h"

/* RocksDB includes */
#include "rocksdb/table.h"

namespace myrocks {

void get_cf_options(
  const std::string &cf_name,
  rocksdb::ColumnFamilyOptions *opts) MY_ATTRIBUTE((__nonnull__));

int rocksdb_normalize_tablename(const char *tablename,
                                StringBuffer<256> *strbuf)
  MY_ATTRIBUTE((__nonnull__, __warn_unused_result__));
int rocksdb_split_normalized_tablename(const char *fullname,
                                       StringBuffer<256> *dbbuf,
                                       StringBuffer<256> *tablebuf,
                                       StringBuffer<256> *partitionbuf)
  MY_ATTRIBUTE((__nonnull__, __warn_unused_result__));
std::vector<std::string> get_share_names(void);

int rdb_get_table_perf_counters(const char *tablename,
                                Rdb_perf_counters *counters)
  MY_ATTRIBUTE((__nonnull__(2)));

void rdb_get_global_perf_counters(Rdb_perf_counters *counters)
  MY_ATTRIBUTE((__nonnull__(1)));

void request_save_stats();

/*
  Access to singleton objects.
*/

rocksdb::DB *rocksdb_get_rdb();

class Rdb_cf_manager;
Rdb_cf_manager& rocksdb_get_cf_manager();

rocksdb::BlockBasedTableOptions& rocksdb_get_table_options();

class Rdb_dict_manager;
Rdb_dict_manager *get_dict_manager(void)
  MY_ATTRIBUTE((__warn_unused_result__));

class Rdb_ddl_manager;
Rdb_ddl_manager *get_ddl_manager(void)
  MY_ATTRIBUTE((__warn_unused_result__));

class Rdb_binlog_manager;
Rdb_binlog_manager *get_binlog_manager(void)
  MY_ATTRIBUTE((__warn_unused_result__));

}  // namespace myrocks
