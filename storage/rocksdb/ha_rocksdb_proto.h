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

/* C++ standard header files */
#include <string>
#include <vector>

/* MySQL header files */
#include "./sql_string.h"

/* RocksDB includes */
#include "rocksdb/table.h"
#include "rocksdb/utilities/transaction_db.h"

/* MyRocks header files */
#include "./rdb_global.h"

namespace myrocks {

enum RDB_IO_ERROR_TYPE {
  RDB_IO_ERROR_TX_COMMIT,
  RDB_IO_ERROR_DICT_COMMIT,
  RDB_IO_ERROR_BG_THREAD,
  RDB_IO_ERROR_GENERAL,
  RDB_IO_ERROR_LAST
};

void rdb_handle_io_error(const rocksdb::Status status,
                         const RDB_IO_ERROR_TYPE err_type)
#if defined(__clang__)
    MY_ATTRIBUTE((optnone));
#else
    MY_ATTRIBUTE((optimize("O0")));
#endif

int rdb_normalize_tablename(const std::string &tablename, std::string *str)
    MY_ATTRIBUTE((__nonnull__, __warn_unused_result__));

int rdb_split_normalized_tablename(const std::string &fullname, std::string *db,
                                   std::string *table = nullptr,
                                   std::string *partition = nullptr)
    MY_ATTRIBUTE((__warn_unused_result__));

std::vector<std::string> rdb_get_open_table_names(void);

int rdb_get_table_perf_counters(const char *tablename,
                                Rdb_perf_counters *counters)
    MY_ATTRIBUTE((__nonnull__(2)));

void rdb_get_global_perf_counters(Rdb_perf_counters *counters)
    MY_ATTRIBUTE((__nonnull__(1)));

void rdb_queue_save_stats_request();

extern const std::string TRUNCATE_TABLE_PREFIX;

/*
  Access to singleton objects.
*/

rocksdb::TransactionDB *rdb_get_rocksdb_db();

class Rdb_cf_manager;
Rdb_cf_manager &rdb_get_cf_manager();

const rocksdb::BlockBasedTableOptions &rdb_get_table_options();
bool rdb_is_table_scan_index_stats_calculation_enabled();
bool rdb_is_ttl_enabled();
#ifndef NDEBUG
int rdb_dbug_set_ttl_rec_ts();
int rdb_dbug_set_ttl_snapshot_ts();
bool rdb_dbug_set_ttl_ignore_pk();
#endif

/* Whether WSEnvironment is enabled */
bool rdb_has_wsenv();

/* Whether SyncWAL is supported in current scenario */
bool rdb_sync_wal_supported();

enum operation_type : int;
void rdb_update_global_stats(const operation_type &type, uint count,
                             Rdb_tbl_def *td = nullptr);

class Rdb_dict_manager_selector;
Rdb_dict_manager_selector *rdb_get_dict_manager(void)
    MY_ATTRIBUTE((__warn_unused_result__));

class Rdb_ddl_manager;
Rdb_ddl_manager *rdb_get_ddl_manager(void)
    MY_ATTRIBUTE((__warn_unused_result__));

class Rdb_binlog_manager;
Rdb_binlog_manager *rdb_get_binlog_manager(void)
    MY_ATTRIBUTE((__warn_unused_result__));
}  // namespace myrocks
