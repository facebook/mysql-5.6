/*
   Copyright (c) 2012, Monty Program Ab

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

#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation        // gcc: Class implementation
#endif

#include <mysql/plugin.h>
#include "ha_rocksdb.h"
#include "ha_rocksdb_proto.h"

#include "sql_class.h"
#include "sql_array.h"

#include "my_bit.h"

#include <sstream>

#include "rdb_datadic.h"
#include "rdb_locks.h"
#include "rdb_rowmods.h"

#include "rdb_cf_options.h"
#include "rdb_cf_manager.h"
#include "rdb_dropped_indices.h"
#include "rdb_i_s.h"

#include "rocksdb/compaction_filter.h"
#include "rocksdb/env.h"
#include "rocksdb/rate_limiter.h"
#include "rocksdb/slice.h"
#include "rocksdb/slice_transform.h"
#include "rocksdb/table.h"
#include "rocksdb/metadata.h"
#include "rocksdb/utilities/convenience.h"
#include "rocksdb/utilities/flashcache.h"
#include "rocksdb/slice_transform.h"
#include "rocksdb/perf_context.h"
#include "properties_collector.h"

/* This is here to get PRIu64, PRId64 */
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>

#ifdef TARGET_OS_LINUX
#include <sys/syscall.h>
#include <sys/ioctl.h>
#include "flashcache_ioctl.h"
extern my_bool cachedev_enabled;
#endif /* TARGET_OS_LINUX */

void dbug_dump_database(rocksdb::DB *db);
static handler *rocksdb_create_handler(handlerton *hton,
                                       TABLE_SHARE *table,
                                       MEM_ROOT *mem_root);
void key_copy(uchar *to_key, uchar *from_record, KEY *key_info,
              uint key_length);
void* background_thread(void*);
void* drop_index_thread(void*);
void signal_drop_index_thread(bool stop_thread = false);
bool can_use_bloom_filter(RDBSE_KEYDEF *keydef,
                          const rocksdb::Slice &eq_cond,
                          const bool use_all_keys,
                          bool is_ascending);
static ROCKSDB_SHARE *get_share(const char *table_name, TABLE *table);
static int free_share(ROCKSDB_SHARE *share);

///////////////////////////////////////////////////////////
// Parameters and settings
///////////////////////////////////////////////////////////
extern const longlong ROCKSDB_WRITE_BUFFER_SIZE_DEFAULT;

static char * rocksdb_default_cf_options;
static char * rocksdb_cf_options_file;
Cf_options rocksdb_cf_options_map;

///////////////////////////////////////////////////////////
// Globals
///////////////////////////////////////////////////////////
handlerton *rocksdb_hton;

rocksdb::DB *rdb= NULL;

static std::shared_ptr<rocksdb::Statistics> rocksdb_stats;
static std::unique_ptr<rocksdb::Env> flashcache_aware_env;

rocksdb::ColumnFamilyOptions default_cf_opts;


Dict_manager dict_manager;
Column_family_manager cf_manager;
Table_ddl_manager ddl_manager;
Binlog_info_manager binlog_manager;
LockTable row_locks;

/*
   Hash used to track the number of open tables; variable for example share
   methods
*/
static HASH rocksdb_open_tables;

/* The mutex used to init the hash; variable for example share methods */
mysql_mutex_t rocksdb_mutex;
mysql_mutex_t background_mutex;
mysql_mutex_t stop_cond_mutex;
mysql_cond_t stop_cond;
bool stop_background_thread;


//////////////////////////////////////////////////////////////////////////////
// Options parse support functions
//////////////////////////////////////////////////////////////////////////////

static int
rocksdb_cf_options_file_validate(THD* thd,
                                 struct st_mysql_sys_var* var,
                                 void* save,
                                 struct st_mysql_value* value)
{
  /* The option is read-only, it should never be updated */
  DBUG_ASSERT(0);
  return 1;
}


static void
rocksdb_cf_options_file_update(THD* thd,
                               struct st_mysql_sys_var* var,
                               void* var_ptr,
                               const void* save)
{
  /* The option is read-only, it should never be updated */
  DBUG_ASSERT(0);
}

static void
rocksdb_compact_column_family(THD* thd,
                              struct st_mysql_sys_var* var,
                              void* var_ptr,
                              const void* save)
{
  if (const char* cf = *static_cast<const char*const*>(save)) {
    bool is_automatic;
    auto cfh = cf_manager.get_cf(cf, nullptr, nullptr, &is_automatic);
    if (cfh != nullptr && rdb != nullptr) {
      sql_print_information("RocksDB: Manual compaction of column family: %s\n", cf);
      rdb->CompactRange(cfh, nullptr, nullptr);
    }
  }
}

static void
rocksdb_force_flush_memtable_now(THD* thd,
                                 struct st_mysql_sys_var* var,
                                 void* var_ptr,
                                 const void* save)
{
  sql_print_information("RocksDB: Manual memtable flush\n");
  if (rdb)
    rdb->Flush(rocksdb::FlushOptions());
}

static void
rocksdb_drop_index_wakeup_thread(THD* thd,
                                 struct st_mysql_sys_var* var,
                                 void* var_ptr,
                                 const void* save)
{
  if (static_cast<bool>(save)) {
    signal_drop_index_thread();
  }
}


//////////////////////////////////////////////////////////////////////////////
// Options definitions
//////////////////////////////////////////////////////////////////////////////
static long long rocksdb_block_cache_size;
static uint64_t rocksdb_rate_limiter_bytes_per_sec;
static uint64_t rocksdb_info_log_level;
static char * rocksdb_wal_dir;
static uint64_t rocksdb_index_type;
static char rocksdb_background_sync;
static uint32_t rocksdb_debug_optimizer_n_rows;
static my_bool rocksdb_debug_optimizer_no_zero_cardinality;
static uint32_t rocksdb_perf_context_level;
static char * compact_cf_name;
static my_bool rocksdb_signal_drop_index_thread;
static my_bool rocksdb_collect_sst_properties = 1;
static my_bool rocksdb_force_flush_memtable_now_var = 0;
static uint64_t rocksdb_number_stat_computes = 0;
static uint32_t rocksdb_seconds_between_stat_computes = 3600;

static rocksdb::DBOptions init_db_options() {
  rocksdb::DBOptions o;
  o.create_if_missing = true;
  return o;
}

static rocksdb::DBOptions db_options = init_db_options();
static rocksdb::BlockBasedTableOptions table_options;

static const char* info_log_level_names[] = {
  "debug_level",
  "info_level",
  "warn_level",
  "error_level",
  "fatal_level",
  NullS
};

static TYPELIB info_log_level_typelib = {
  array_elements(info_log_level_names) - 1,
  "info_log_level_typelib",
  info_log_level_names,
  nullptr
};

static const char* index_type_names[] = {
  "kBinarySearch",
  "kHashSearch",
  NullS
};

static TYPELIB index_type_typelib = {
  array_elements(index_type_names) - 1,
  "index_type_typelib",
  index_type_names,
  nullptr
};

//TODO: 0 means don't wait at all, and we don't support it yet?
static MYSQL_THDVAR_ULONG(lock_wait_timeout, PLUGIN_VAR_RQCMDARG,
  "Number of seconds to wait for lock",
  NULL, NULL, /*default*/ 1, /*min*/ 1, /*max*/ 1024*1024*1024, 0);

static MYSQL_THDVAR_BOOL(bulk_load, PLUGIN_VAR_RQCMDARG,
  "Use bulk-load mode for inserts", NULL, NULL, FALSE);

static MYSQL_THDVAR_ULONG(max_row_locks, PLUGIN_VAR_RQCMDARG,
  "Maximum number of locks a transaction can have",
  NULL, NULL, /*default*/ 1024*1024*1024, /*min*/ 1, /*max*/ 1024*1024*1024, 0);

static MYSQL_THDVAR_ULONG(bulk_load_size, PLUGIN_VAR_RQCMDARG,
  "Max #records in a batch for bulk-load mode",
  NULL, NULL, /*default*/ 1000, /*min*/ 1, /*max*/ 1024*1024*1024, 0);

static MYSQL_SYSVAR_BOOL(create_if_missing,
  *reinterpret_cast<my_bool*>(&db_options.create_if_missing),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::create_if_missing for RocksDB",
  NULL, NULL, db_options.create_if_missing);

static MYSQL_SYSVAR_BOOL(create_missing_column_families,
  *reinterpret_cast<my_bool*>(&db_options.create_missing_column_families),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::create_missing_column_families for RocksDB",
  NULL, NULL, db_options.create_missing_column_families);

static MYSQL_SYSVAR_BOOL(error_if_exists,
  *reinterpret_cast<my_bool*>(&db_options.error_if_exists),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::error_if_exists for RocksDB",
  NULL, NULL, db_options.error_if_exists);

static MYSQL_SYSVAR_BOOL(paranoid_checks,
  *reinterpret_cast<my_bool*>(&db_options.paranoid_checks),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::paranoid_checks for RocksDB",
  NULL, NULL, db_options.paranoid_checks);

static MYSQL_SYSVAR_ULONG(rate_limiter_bytes_per_sec,
  rocksdb_rate_limiter_bytes_per_sec,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::rate_limiter bytes_per_sec for RocksDB",
  NULL, NULL, 0L,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ENUM(info_log_level,
  rocksdb_info_log_level,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::info_log_level for RocksDB",
  NULL, NULL, db_options.info_log_level, &info_log_level_typelib);

static MYSQL_SYSVAR_UINT(perf_context_level,
  rocksdb_perf_context_level,
  PLUGIN_VAR_RQCMDARG,
  "Perf Context Level for rocksdb internal timer stat collection",
  NULL, NULL, rocksdb::kEnableCount,
  /* min */ 0L, /* max */ UINT_MAX, 0);


static MYSQL_SYSVAR_INT(max_open_files,
  db_options.max_open_files,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_open_files for RocksDB",
  NULL, NULL, db_options.max_open_files,
  /* min */ -1, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_ULONG(max_total_wal_size,
  db_options.max_total_wal_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_total_wal_size for RocksDB",
  NULL, NULL, db_options.max_total_wal_size,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(disableDataSync,
  *reinterpret_cast<my_bool*>(&db_options.disableDataSync),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::disableDataSync for RocksDB",
  NULL, NULL, db_options.disableDataSync);

static MYSQL_SYSVAR_BOOL(use_fsync,
  *reinterpret_cast<my_bool*>(&db_options.use_fsync),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::use_fsync for RocksDB",
  NULL, NULL, db_options.use_fsync);

static MYSQL_SYSVAR_STR(wal_dir, rocksdb_wal_dir,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::wal_dir for RocksDB",
  NULL, NULL, db_options.wal_dir.c_str());

static MYSQL_SYSVAR_ULONG(delete_obsolete_files_period_micros,
  db_options.delete_obsolete_files_period_micros,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::delete_obsolete_files_period_micros for RocksDB",
  NULL, NULL, db_options.delete_obsolete_files_period_micros,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_INT(max_background_compactions,
  db_options.max_background_compactions,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_background_compactions for RocksDB",
  NULL, NULL, db_options.max_background_compactions,
  /* min */ 1, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_INT(max_background_flushes,
  db_options.max_background_flushes,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_background_flushes for RocksDB",
  NULL, NULL, db_options.max_background_flushes,
  /* min */ 1, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_ULONG(max_log_file_size,
  db_options.max_log_file_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_log_file_size for RocksDB",
  NULL, NULL, db_options.max_log_file_size,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(log_file_time_to_roll,
  db_options.log_file_time_to_roll,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::log_file_time_to_roll for RocksDB",
  NULL, NULL, db_options.log_file_time_to_roll,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(keep_log_file_num,
  db_options.keep_log_file_num,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::keep_log_file_num for RocksDB",
  NULL, NULL, db_options.keep_log_file_num,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(max_manifest_file_size,
  db_options.max_manifest_file_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_manifest_file_size for RocksDB",
  NULL, NULL, db_options.max_manifest_file_size,
  /* min */ 0L, /* max */ ULONG_MAX, 0);

static MYSQL_SYSVAR_INT(table_cache_numshardbits,
  db_options.table_cache_numshardbits,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::table_cache_numshardbits for RocksDB",
  NULL, NULL, db_options.table_cache_numshardbits,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_ULONG(WAL_ttl_seconds,
  db_options.WAL_ttl_seconds,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::WAL_ttl_seconds for RocksDB",
  NULL, NULL, db_options.WAL_ttl_seconds,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(WAL_size_limit_MB,
  db_options.WAL_size_limit_MB,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::WAL_size_limit_MB for RocksDB",
  NULL, NULL, db_options.WAL_size_limit_MB,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(manifest_preallocation_size,
  db_options.manifest_preallocation_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::manifest_preallocation_size for RocksDB",
  NULL, NULL, db_options.manifest_preallocation_size,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(allow_os_buffer,
  *reinterpret_cast<my_bool*>(&db_options.allow_os_buffer),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::allow_os_buffer for RocksDB",
  NULL, NULL, db_options.allow_os_buffer);

static MYSQL_SYSVAR_BOOL(allow_mmap_reads,
  *reinterpret_cast<my_bool*>(&db_options.allow_mmap_reads),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::allow_mmap_reads for RocksDB",
  NULL, NULL, db_options.allow_mmap_reads);

static MYSQL_SYSVAR_BOOL(allow_mmap_writes,
  *reinterpret_cast<my_bool*>(&db_options.allow_mmap_writes),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::allow_mmap_writes for RocksDB",
  NULL, NULL, db_options.allow_mmap_writes);

static MYSQL_SYSVAR_BOOL(is_fd_close_on_exec,
  *reinterpret_cast<my_bool*>(&db_options.is_fd_close_on_exec),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::is_fd_close_on_exec for RocksDB",
  NULL, NULL, db_options.is_fd_close_on_exec);

static MYSQL_SYSVAR_BOOL(skip_log_error_on_recovery,
  *reinterpret_cast<my_bool*>(&db_options.skip_log_error_on_recovery),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::skip_log_error_on_recovery for RocksDB",
  NULL, NULL, db_options.skip_log_error_on_recovery);

static MYSQL_SYSVAR_UINT(stats_dump_period_sec,
  db_options.stats_dump_period_sec,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::stats_dump_period_sec for RocksDB",
  NULL, NULL, db_options.stats_dump_period_sec,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_BOOL(advise_random_on_open,
  *reinterpret_cast<my_bool*>(&db_options.advise_random_on_open),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::advise_random_on_open for RocksDB",
  NULL, NULL, db_options.advise_random_on_open);

static MYSQL_SYSVAR_ULONG(db_write_buffer_size,
  db_options.db_write_buffer_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::db_write_buffer_size for RocksDB",
  NULL, NULL, db_options.db_write_buffer_size,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(use_adaptive_mutex,
  *reinterpret_cast<my_bool*>(&db_options.use_adaptive_mutex),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::use_adaptive_mutex for RocksDB",
  NULL, NULL, db_options.use_adaptive_mutex);

static MYSQL_SYSVAR_ULONG(bytes_per_sync,
  db_options.bytes_per_sync,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::bytes_per_sync for RocksDB",
  NULL, NULL, db_options.bytes_per_sync,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(wal_bytes_per_sync,
  db_options.wal_bytes_per_sync,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::wal_bytes_per_sync for RocksDB",
  NULL, NULL, db_options.wal_bytes_per_sync,
  /* min */ 0L, /* max */ LONG_MAX, 0);


static MYSQL_SYSVAR_BOOL(enable_thread_tracking,
  *reinterpret_cast<my_bool*>(&db_options.enable_thread_tracking),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::enable_thread_tracking for RocksDB",
  NULL, NULL, db_options.enable_thread_tracking);

static MYSQL_SYSVAR_LONGLONG(block_cache_size, rocksdb_block_cache_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "block_cache size for RocksDB",
  NULL, NULL, /* RocksDB's default is 8 MB: */ 8*1024*1024L,
  /* min */ 1024L, /* max */ LONGLONG_MAX, /* Block size */1024L);

static MYSQL_SYSVAR_BOOL(cache_index_and_filter_blocks,
  *reinterpret_cast<my_bool*>(&table_options.cache_index_and_filter_blocks),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::cache_index_and_filter_blocks for RocksDB",
  NULL, NULL, true);

static MYSQL_SYSVAR_ENUM(index_type,
  rocksdb_index_type,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::index_type for RocksDB",
  NULL, NULL, (uint64_t)table_options.index_type, &index_type_typelib);

static MYSQL_SYSVAR_BOOL(hash_index_allow_collision,
  *reinterpret_cast<my_bool*>(&table_options.hash_index_allow_collision),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::hash_index_allow_collision for RocksDB",
  NULL, NULL, table_options.hash_index_allow_collision);

static MYSQL_SYSVAR_BOOL(no_block_cache,
  *reinterpret_cast<my_bool*>(&table_options.no_block_cache),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::no_block_cache for RocksDB",
  NULL, NULL, table_options.no_block_cache);

static MYSQL_SYSVAR_ULONG(block_size,
  table_options.block_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::block_size for RocksDB",
  NULL, NULL, table_options.block_size,
  /* min */ 1L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_INT(block_size_deviation,
  table_options.block_size_deviation,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::block_size_deviation for RocksDB",
  NULL, NULL, table_options.block_size_deviation,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_INT(block_restart_interval,
  table_options.block_restart_interval,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::block_restart_interval for RocksDB",
  NULL, NULL, table_options.block_restart_interval,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_BOOL(whole_key_filtering,
  *reinterpret_cast<my_bool*>(&table_options.whole_key_filtering),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::whole_key_filtering for RocksDB",
  NULL, NULL, table_options.whole_key_filtering);

static MYSQL_SYSVAR_STR(default_cf_options, rocksdb_default_cf_options,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "default cf options for RocksDB",
  NULL, NULL, "");

static MYSQL_SYSVAR_STR(cf_options_file, rocksdb_cf_options_file,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "path to cnf file with cf options for RocksDB",
  rocksdb_cf_options_file_validate,
  rocksdb_cf_options_file_update, "");

static MYSQL_SYSVAR_BOOL(background_sync,
  rocksdb_background_sync,
  PLUGIN_VAR_RQCMDARG,
  "turns on background syncs for RocksDB",
  NULL, NULL, FALSE);

static MYSQL_THDVAR_BOOL(write_sync,
  PLUGIN_VAR_RQCMDARG,
  "WriteOptions::sync for RocksDB",
  NULL, NULL, rocksdb::WriteOptions().sync);

static MYSQL_THDVAR_BOOL(write_disable_wal,
  PLUGIN_VAR_RQCMDARG,
  "WriteOptions::disableWAL for RocksDB",
  NULL, NULL, rocksdb::WriteOptions().disableWAL);

static MYSQL_THDVAR_ULONG(write_timeout_hint_us,
  PLUGIN_VAR_RQCMDARG,
  "WriteOptions::timeout_hint_us for RocksDB",
  NULL, NULL, rocksdb::WriteOptions().timeout_hint_us,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_THDVAR_BOOL(write_ignore_missing_column_families,
  PLUGIN_VAR_RQCMDARG,
  "WriteOptions::ignore_missing_column_families for RocksDB",
  NULL, NULL, rocksdb::WriteOptions().ignore_missing_column_families);

static MYSQL_THDVAR_BOOL(skip_fill_cache,
  PLUGIN_VAR_RQCMDARG,
  "Skip filling block cache on read requests",
  NULL, NULL, FALSE);

static MYSQL_THDVAR_UINT(records_in_range,
  PLUGIN_VAR_RQCMDARG,
  "Used to override the result of records_in_range(). Set to a positive number to override",
  NULL, NULL, 0,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_UINT(debug_optimizer_n_rows,
  rocksdb_debug_optimizer_n_rows,
  PLUGIN_VAR_RQCMDARG,
  "Used for info(). Testing purpose only and will be deprecated",
  NULL, NULL, 1000,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_BOOL(debug_optimizer_no_zero_cardinality,
  rocksdb_debug_optimizer_no_zero_cardinality,
  PLUGIN_VAR_RQCMDARG,
  "In case if cardinality is zero, overrides it with some value",
  NULL, NULL, TRUE);

static MYSQL_SYSVAR_STR(compact_cf, compact_cf_name,
  PLUGIN_VAR_RQCMDARG,
  "Compact column family",
  NULL, rocksdb_compact_column_family, "");

static MYSQL_SYSVAR_BOOL(signal_drop_index_thread,
  rocksdb_signal_drop_index_thread,
  PLUGIN_VAR_RQCMDARG,
  "Wake up drop index thread",
  NULL, rocksdb_drop_index_wakeup_thread, FALSE);

static MYSQL_SYSVAR_BOOL(collect_sst_properties,
  rocksdb_collect_sst_properties,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "Enables collecting SST file properties on each flush",
  NULL, NULL, rocksdb_collect_sst_properties);

static MYSQL_SYSVAR_BOOL(
  force_flush_memtable_now,
  rocksdb_force_flush_memtable_now_var,
  PLUGIN_VAR_RQCMDARG,
  "Forces memstore flush which may block all write requests so be careful",
  NULL, rocksdb_force_flush_memtable_now, FALSE);

static MYSQL_SYSVAR_UINT(seconds_between_stat_computes,
  rocksdb_seconds_between_stat_computes,
  PLUGIN_VAR_RQCMDARG,
  "Sets a number of seconds to wait between optimizer stats recomputation. "
  "Only changed indexes will be refreshed.",
  NULL, NULL, rocksdb_seconds_between_stat_computes,
  /* min */ 0L, /* max */ UINT_MAX, 0);

const longlong ROCKSDB_WRITE_BUFFER_SIZE_DEFAULT=4194304;

static struct st_mysql_sys_var* rocksdb_system_variables[]= {
  MYSQL_SYSVAR(lock_wait_timeout),
  MYSQL_SYSVAR(max_row_locks),
  MYSQL_SYSVAR(bulk_load),
  MYSQL_SYSVAR(bulk_load_size),

  MYSQL_SYSVAR(create_if_missing),
  MYSQL_SYSVAR(create_missing_column_families),
  MYSQL_SYSVAR(error_if_exists),
  MYSQL_SYSVAR(paranoid_checks),
  MYSQL_SYSVAR(rate_limiter_bytes_per_sec),
  MYSQL_SYSVAR(info_log_level),
  MYSQL_SYSVAR(max_open_files),
  MYSQL_SYSVAR(max_total_wal_size),
  MYSQL_SYSVAR(disableDataSync),
  MYSQL_SYSVAR(use_fsync),
  MYSQL_SYSVAR(wal_dir),
  MYSQL_SYSVAR(delete_obsolete_files_period_micros),
  MYSQL_SYSVAR(max_background_compactions),
  MYSQL_SYSVAR(max_background_flushes),
  MYSQL_SYSVAR(max_log_file_size),
  MYSQL_SYSVAR(log_file_time_to_roll),
  MYSQL_SYSVAR(keep_log_file_num),
  MYSQL_SYSVAR(max_manifest_file_size),
  MYSQL_SYSVAR(table_cache_numshardbits),
  MYSQL_SYSVAR(WAL_ttl_seconds),
  MYSQL_SYSVAR(WAL_size_limit_MB),
  MYSQL_SYSVAR(manifest_preallocation_size),
  MYSQL_SYSVAR(allow_os_buffer),
  MYSQL_SYSVAR(allow_mmap_reads),
  MYSQL_SYSVAR(allow_mmap_writes),
  MYSQL_SYSVAR(is_fd_close_on_exec),
  MYSQL_SYSVAR(skip_log_error_on_recovery),
  MYSQL_SYSVAR(stats_dump_period_sec),
  MYSQL_SYSVAR(advise_random_on_open),
  MYSQL_SYSVAR(db_write_buffer_size),
  MYSQL_SYSVAR(use_adaptive_mutex),
  MYSQL_SYSVAR(bytes_per_sync),
  MYSQL_SYSVAR(wal_bytes_per_sync),
  MYSQL_SYSVAR(enable_thread_tracking),
  MYSQL_SYSVAR(perf_context_level),

  MYSQL_SYSVAR(block_cache_size),
  MYSQL_SYSVAR(cache_index_and_filter_blocks),
  MYSQL_SYSVAR(index_type),
  MYSQL_SYSVAR(hash_index_allow_collision),
  MYSQL_SYSVAR(no_block_cache),
  MYSQL_SYSVAR(block_size),
  MYSQL_SYSVAR(block_size_deviation),
  MYSQL_SYSVAR(block_restart_interval),
  MYSQL_SYSVAR(whole_key_filtering),

  MYSQL_SYSVAR(default_cf_options),
  MYSQL_SYSVAR(cf_options_file),

  MYSQL_SYSVAR(background_sync),

  MYSQL_SYSVAR(write_sync),
  MYSQL_SYSVAR(write_disable_wal),
  MYSQL_SYSVAR(write_timeout_hint_us),
  MYSQL_SYSVAR(write_ignore_missing_column_families),

  MYSQL_SYSVAR(skip_fill_cache),

  MYSQL_SYSVAR(records_in_range),
  MYSQL_SYSVAR(debug_optimizer_n_rows),
  MYSQL_SYSVAR(debug_optimizer_no_zero_cardinality),

  MYSQL_SYSVAR(compact_cf),
  MYSQL_SYSVAR(signal_drop_index_thread),
  MYSQL_SYSVAR(collect_sst_properties),
  MYSQL_SYSVAR(force_flush_memtable_now),
  MYSQL_SYSVAR(seconds_between_stat_computes),
  NULL
};

rocksdb::WriteOptions get_write_options(THD* thd)
{
  rocksdb::WriteOptions opt;

  opt.sync = THDVAR(thd, write_sync);
  opt.disableWAL = THDVAR(thd, write_disable_wal);
  opt.timeout_hint_us = THDVAR(thd, write_timeout_hint_us);
  opt.ignore_missing_column_families =
    THDVAR(thd, write_ignore_missing_column_families);
  return opt;
}

///////////////////////////////////////////////////////////////////////////////////////////

/**
  @brief
  Function we use in the creation of our hash to get key.
*/

static uchar* rocksdb_get_key(ROCKSDB_SHARE *share, size_t *length,
                             my_bool not_used __attribute__((unused)))
{
  *length=share->table_name_length;
  return (uchar*) share->table_name;
}

/*
  The following is needed as an argument for thd_enter_cond, irrespectively of
  whether we're compiling with P_S or not.
*/
PSI_stage_info stage_waiting_on_row_lock= { 0, "Waiting for row lock", 0};

#ifdef HAVE_PSI_INTERFACE
static PSI_thread_key key_thread_background;
static PSI_thread_key key_thread_drop_index;

static PSI_stage_info *all_rocksdb_stages[]=
{
  & stage_waiting_on_row_lock
};


static PSI_mutex_key ex_key_mutex_example, ex_key_mutex_ROCKSDB_SHARE_mutex,
  key_mutex_background, key_mutex_stop_background,
  key_mutex_drop_index, key_drop_index_interrupt_mutex;

static PSI_mutex_info all_rocksdb_mutexes[]=
{
  { &ex_key_mutex_example, "rocksdb", PSI_FLAG_GLOBAL},
  { &ex_key_mutex_ROCKSDB_SHARE_mutex, "ROCKSDB_SHARE::mutex", 0},
  { &key_mutex_background, "background", PSI_FLAG_GLOBAL},
  { &key_mutex_stop_background, "stop background", PSI_FLAG_GLOBAL},
  { &key_mutex_drop_index, "drop index", PSI_FLAG_GLOBAL},
  { &key_drop_index_interrupt_mutex, "drop index interrupt", PSI_FLAG_GLOBAL},
  { &key_mutex_dropped_indices_manager, "dropped indices manager", PSI_FLAG_GLOBAL},
};

PSI_cond_key key_cond_stop, key_drop_index_interrupt_cond;

static PSI_cond_info all_rocksdb_conds[]=
{
  { &key_cond_stop, "cond_stop", PSI_FLAG_GLOBAL},
  { &key_drop_index_interrupt_cond, "cond_stop_drop_index", PSI_FLAG_GLOBAL},
};

static PSI_thread_info all_rocksdb_threads[]=
{
  { &key_thread_background, "background", PSI_FLAG_GLOBAL},
  { &key_thread_drop_index, "drop index", PSI_FLAG_GLOBAL},
};

static void init_rocksdb_psi_keys()
{
  const char* category= "rocksdb";
  int count;

  if (PSI_server == NULL)
    return;

  count= array_elements(all_rocksdb_mutexes);
  PSI_server->register_mutex(category, all_rocksdb_mutexes, count);

  count= array_elements(all_rocksdb_conds);
  PSI_server->register_cond(category, all_rocksdb_conds, count);

  count= array_elements(all_rocksdb_stages);
  mysql_stage_register(category, all_rocksdb_stages, count);

  count= array_elements(all_rocksdb_threads);
  mysql_thread_register(category, all_rocksdb_threads, count);
}
#endif

static int bytewise_compare(const rocksdb::Slice& a, const rocksdb::Slice& b)
{
  size_t a_size= a.size();
  size_t b_size= b.size();
  size_t len= (a_size < b_size) ? a_size : b_size;
  int res;

  if ((res= memcmp(a.data(), b.data(), len)))
    return res;

  /* Ok, res== 0 */
  if (a_size != b_size)
  {
    return a_size < b_size? -1 : 1;
  }
  return 0;
}


/*
  The keys are in form: {index_number} {mem-comparable-key}

  (todo: knowledge about this format is shared between this class and
   RDBSE_KEYDEF)
*/

class Primary_key_comparator : public rocksdb::Comparator
{
public:
  int Compare(const rocksdb::Slice& a, const rocksdb::Slice& b) const
  {
    return bytewise_compare(a,b);
  }

  /* The following is not needed by RocksDB, but conceptually should be here: */
  static ulong get_hashnr(const char *key, size_t key_len);
  const char* Name() const { return "RocksDB_SE_v3.7"; }

  //TODO: advanced funcs:
  // - FindShortestSeparator
  // - FindShortSuccessor
  // for now, do-nothing implementations:
  void FindShortestSeparator(std::string* start, const rocksdb::Slice& limit) const {}
  void FindShortSuccessor(std::string* key) const {}
};


class Reverse_comparator : public rocksdb::Comparator
{
  int Compare(const rocksdb::Slice& a, const rocksdb::Slice& b) const
  {
    return -bytewise_compare(a,b);
  }
  const char* Name() const { return "rev:RocksDB_SE_v3.7"; }
  void FindShortestSeparator(std::string* start, const rocksdb::Slice& limit) const {}
  void FindShortSuccessor(std::string* key) const {}
};


Primary_key_comparator rocksdb_pk_comparator;
Reverse_comparator     rocksdb_rev_pk_comparator;

/*
  This function doesn't support reverse comparisons. They are handled by the
  caller.
*/
int compare_mem_comparable_keys(const uchar *a, size_t a_len, const uchar *b, size_t b_len)
{
  rocksdb::Slice a_slice((char*)a, a_len);
  rocksdb::Slice b_slice((char*)b, b_len);
  return rocksdb_pk_comparator.Compare(a_slice, b_slice);
}

static Dropped_indices_manager dropped_indices_manager;
static mysql_mutex_t drop_index_mutex;
static mysql_mutex_t drop_index_interrupt_mutex;
static mysql_cond_t drop_index_interrupt_cond;
static bool stop_drop_index_thread;

class Rdb_CompactionFilter : public rocksdb::CompactionFilter
{
public:
  Rdb_CompactionFilter() {}
  ~Rdb_CompactionFilter()
  {
    print_compaction_status();
  }

  // keys are passed in sorted order within the same sst.
  // V1 Filter is thread safe on our usage (creating from Factory).
  // Make sure to protect instance variables when switching to thread
  // unsafe in the future.
  virtual bool Filter(int level,
                      const rocksdb::Slice& key,
                      const rocksdb::Slice& existing_value,
                      std::string* new_value,
                      bool* value_changed) const override {

    DBUG_ASSERT(key.size() >= sizeof(uint32));
    uint32 index= read_big_uint4((const uchar*)key.data());
    DBUG_ASSERT(index >= 1);

    if (index != prev_index) // processing new index id
    {
      if (num_deleted > 0)
      {
        print_compaction_status();
        num_deleted= 0;
      }
      should_delete= dropped_indices_manager.has_index(index);
      prev_index= index;
    }

    if (should_delete)
      num_deleted++;

    return should_delete;
  }

  virtual const char* Name() const override {
    return "Rdb_CompactionFilter";
  }
private:
  void print_compaction_status() const
  {
    if (num_deleted > 0)
      sql_print_information("RocksDB: Compacting away elements from dropped "
                            "index %u: %llu", prev_index, num_deleted);
  }

  // Index id of the previous record
  mutable uint32 prev_index= 0;
  // Number of rows deleted for the same index id
  mutable uint64 num_deleted= 0;
  // Current index id should be deleted or not (should be deleted if true)
  mutable bool should_delete= false;
};

class Rdb_CompactionFilterFactory : public rocksdb::CompactionFilterFactory
{
public:
  Rdb_CompactionFilterFactory() {}

  ~Rdb_CompactionFilterFactory() {}

  const char* Name() const override {
    return "Rdb_CompactionFilterFactory";
  }

  std::unique_ptr<rocksdb::CompactionFilter> CreateCompactionFilter(
      const rocksdb::CompactionFilter::Context& context) {
    return std::unique_ptr<rocksdb::CompactionFilter>(
      new Rdb_CompactionFilter);
  }
};

/*
  This is a rocksdb connection. Its members represent the current transaction,
  which consists of:
  - the snapshot
  - the changes we've made but are not seeing yet.

  The changes are made to individual tables, which store them here and then
  this object commits them on commit.
*/

class Rdb_transaction
{
public:
  /* The snapshot the transaction is used */
  const rocksdb::Snapshot *snapshot;

  /* The changes made by this transaction (and not yet applied) */
  Row_table changes;

  uint n_writes;

  const char* mysql_log_file_name;
  my_off_t mysql_log_offset;
  const char* mysql_gtid;

  /*
    These are needed to use LF-Hash. They are allocated per-thread. Logically,
    they are not part of the transaction but it's convenient to have them here.
  */
  LF_PINS *pins;

  /* Row locks taken by this transaction */
  Dynamic_array<Row_lock*> trx_locks;

  /* Number of elements in trx_locks array when the current statement started */
  int trx_locks_at_stmt_start;

  int timeout_sec; /* Cached value of @@rocksdb_lock_wait_timeout */
  int max_row_locks;

  void set_params(int timeout_sec_arg, int max_row_locks_arg)
  {
    timeout_sec= timeout_sec_arg;
    max_row_locks= max_row_locks_arg;
  }

  Row_lock *get_lock(const uchar* key, size_t keylen, bool *timed_out)
  {
    Row_lock *lock;
    if (trx_locks.elements() > max_row_locks)
    {
      *timed_out= false;
      return NULL;
    }
    if (!(lock= row_locks.get_lock(pins, key, keylen, timeout_sec)))
    {
      *timed_out= true;
      return NULL;
    }
    return lock;
  }

  void add_lock(Row_lock* lock)
  {
    trx_locks.append(lock);
  }

  void release_last_lock()
  {
    row_locks.release_lock(pins, trx_locks.at(trx_locks.elements() - 1));
    trx_locks.pop();
  }

  void release_locks()
  {
    int size= trx_locks.elements();
    for (int i= 0; i < size; i++)
      row_locks.release_lock(pins, trx_locks.at(i));
    trx_locks.clear();
  }

  bool commit(THD *thd)
  {
    bool res= false;
    thd_binlog_pos(thd, &mysql_log_file_name, &mysql_log_offset,
                   &mysql_gtid);
    res= flush_batch(thd, true);
    /* rollback() will delete snapshot, batch and locks */
    rollback();
    return res;
  }

private:

  int flush_batch_intern(THD* thd, bool on_commit)
  {
    bool res= false;
    rocksdb::WriteBatch batch;

    if (changes.is_empty())
      return false;

    if (on_commit)
    {
      binlog_manager.update(mysql_log_file_name,
                            mysql_log_offset,
                            mysql_gtid,
                            batch);
    }

    for (int reverse=0; reverse <= 1; reverse++)
    {
      Row_table_iter iter(&changes, (bool)reverse);

      for (iter.SeekToFirst(); iter.Valid(); iter.Next())
      {
        if (iter.is_tombstone())
        {
          batch.Delete(iter.keydef()->get_cf(), iter.key());
        }
        else
        {
          batch.Put(iter.keydef()->get_cf(), iter.key(), iter.value());
        }
      }
    }
    rocksdb::Status s= rdb->Write(get_write_options(thd), &batch);
    res= !s.ok();
    if (res)
      rocksdb_handle_io_error(s, ROCKSDB_IO_ERROR_TRX_COMMIT);

    return res;
  }

public:
  int flush_batch(THD *thd, bool on_commit=false)
  {
    bool bres= flush_batch_intern(thd, on_commit);
    changes.reinit();
    n_writes= 0;
    return bres;
  }

  void prepare_for_write()
  {
    /* Currently, we don't do anything here */
  }

  /*
    Start a statement inside a multi-statement transaction.

    For hooking to start of statement that is its own transaction, see
    ha_rocksdb::external_lock().
  */
  void start_stmt()
  {
    changes.start_stmt();
    trx_locks_at_stmt_start= trx_locks.elements();
  }

  /*
    This must be called when last statement is rolled back, but the transaction
    continues
  */
  void rollback_stmt()
  {
    changes.rollback_stmt();

    /* Release the locks taken by the last statement */
    int size= trx_locks.elements();
    DBUG_ASSERT(trx_locks_at_stmt_start <= size);
    for (int i= trx_locks_at_stmt_start; i < size; i++)
      row_locks.release_lock(pins, trx_locks.at(i));
    trx_locks.elements(trx_locks_at_stmt_start);
  }
  void rollback()
  {
    if (snapshot)
    {
      rdb->ReleaseSnapshot(snapshot);
      snapshot= NULL;
    }

    changes.reinit();
    release_locks();
    n_writes= 0;
  }

  Rdb_transaction() :
    snapshot(NULL), n_writes(0), pins(NULL), trx_locks_at_stmt_start(0)
  {
    changes.init();
  }

  ~Rdb_transaction()
  {
    if (snapshot)
    {
      rdb->ReleaseSnapshot(snapshot);
      snapshot= NULL;
    }
    changes.cleanup();
  }
};


static Rdb_transaction* &get_trx_from_thd(THD *thd)
{
  return *(Rdb_transaction**) thd_ha_data(thd, rocksdb_hton);
}


/*
  TODO: maybe, call this in external_lock() and store in ha_rocksdb..
*/

static Rdb_transaction *get_or_create_trx(THD *thd)
{
  Rdb_transaction*& trx= get_trx_from_thd(thd);
  if (trx == NULL)
  {
    if (!(trx= new Rdb_transaction))
      return NULL;
    trx->pins= row_locks.get_pins();
    trx->snapshot= rdb->GetSnapshot();
  }
  else
  {
    if (!trx->snapshot)
      trx->snapshot= rdb->GetSnapshot();
  }
  return trx;
}


static int rocksdb_close_connection(handlerton* hton, THD* thd)
{
  Rdb_transaction*& trx= get_trx_from_thd(thd);
  if (trx && trx->pins)
    row_locks.put_pins(trx->pins);
  delete trx;
  trx= NULL;
  return 0;
}

/**
  Doing nothing at prepare(). But defining handler::prepare() is needed
  for Xid binlog event to be written at commit.
*/
static int rocksdb_prepare(handlerton* hton, THD* thd, bool all, bool async)
{
  return 0;
}

/**
  Reading last committed binary log info from RocksDB system row.
  The info is needed for crash safe slave/master to work.
*/
static int rocksdb_recover(handlerton* hton, XID* xid_list, uint len,
                           char* binlog_file, my_off_t* binlog_pos)
{
  if (binlog_file && binlog_pos)
  {
    char file_buf[FN_REFLEN+1]= {0};
    my_off_t pos;
    char gtid_buf[FN_REFLEN+1]= {0};
    if (binlog_manager.read(file_buf, pos, gtid_buf))
    {
      if (is_binlog_advanced(binlog_file, *binlog_pos, file_buf, pos))
      {
        strcpy(binlog_file, file_buf);
        *binlog_pos= pos;
        fprintf(stderr, "RocksDB: Last binlog file position %llu,"
                " file name %s\n", pos, file_buf);
        if (*gtid_buf)
        {
          fprintf(stderr, "RocksDB: Last MySQL Gtid %s\n", gtid_buf);
        }
      }
    }
  }
  return 0;
}


static int rocksdb_commit(handlerton* hton, THD* thd, bool commit_trx, bool)
{
  DBUG_ENTER("rocksdb_commit");
  if (commit_trx)
  {
    Rdb_transaction*& trx= get_trx_from_thd(thd);
    if (trx && trx->commit(thd))
      DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }
  else
  {
    /*
      Committing a statement within a transaction. We don't need to do anything
      here. trx->start_stmt() will notify Rdb_transaction that another
      statement has started.
    */
  }
  DBUG_RETURN(0);
}


static int rocksdb_rollback(handlerton* hton, THD* thd, bool rollback_trx)
{
  if (rollback_trx)
  {
    /* Discard the changes made by the transaction */
    Rdb_transaction*& trx= get_trx_from_thd(thd);
    if (trx)
      trx->rollback();
  }
  else
  {
    Rdb_transaction*& trx= get_trx_from_thd(thd);
    if (trx)
      trx->rollback_stmt();
  }
  return 0;
}


/*
  This is called for SHOW ENGINE ROCKSDB STATUS|LOGS|etc.

  For now, produce info about live files (which gives an imprecise idea about
  what column families are there)
*/

static bool rocksdb_show_status(handlerton*		hton,
                                THD*			thd,
                                stat_print_fn*		stat_print,
                                enum ha_stat_type	stat_type)
{
  bool res= false;
  if (stat_type == HA_ENGINE_STATUS)
  {
    std::string str;

    /* Per DB stats */
    if (rdb->GetProperty("rocksdb.dbstats", &str)) {
      res |= stat_print(thd, STRING_WITH_LEN("DBSTATS"),
                        STRING_WITH_LEN("rocksdb"),
                        str.c_str(), str.size());
    }

    /* Per column family stats */
    for (auto cf_name : cf_manager.get_cf_names())
    {
      rocksdb::ColumnFamilyHandle* cfh;
      bool is_automatic;

      /*
        Only the cf name is important. Whether it was generated automatically
        does not matter, so is_automatic is ignored.
      */
      cfh= cf_manager.get_cf(cf_name.c_str(), nullptr, nullptr, &is_automatic);
      if (cfh == nullptr)
        continue;

      if (!rdb->GetProperty(cfh, "rocksdb.cfstats", &str))
        continue;

      res |= stat_print(thd, STRING_WITH_LEN("CF_COMPACTION"),
                        cf_name.c_str(), cf_name.size(),
                        str.c_str(), str.size());
    }
  }
  return res;
}

/*
    Supporting START TRANSACTION WITH CONSISTENT [ROCKSDB] SNAPSHOT

    Features:
    1. Supporting START TRANSACTION WITH CONSISTENT SNAPSHOT
    2. Getting current binlog position in addition to #1.

    The second feature is done by START TRANSACTION WITH
    CONSISTENT ROCKSDB SNAPSHOT. This is Facebook's extension, and
    it works like existing START TRANSACTION WITH CONSISTENT INNODB SNAPSHOT.

    - When not setting engine, START TRANSACTION WITH CONSISTENT SNAPSHOT
    takes both InnoDB and RocksDB snapshots, and both InnoDB and RocksDB
    participate in transaction. When executing COMMIT, both InnoDB and
    RocksDB modifications are committed. Remember that XA is not supported yet,
    so mixing engines is not recommended anyway.

    - When setting engine, START TRANSACTION WITH CONSISTENT.. takes
    snapshot for the specified engine only. But it starts both
    InnoDB and RocksDB transactions.
*/
static int rocksdb_start_trx_and_assign_read_view(
        handlerton*     hton,           /*!< in: RocksDB handlerton */
        THD*            thd,            /*!< in: MySQL thread handle of the
                                        user for whom the transaction should
                                        be committed */
        char*           binlog_file,    /* out: binlog file for last commit */
        ulonglong*      binlog_pos,     /* out: binlog pos for last commit */
        char**  gtid_executed,  /* out: Gtids logged until last commit */
        int*    gtid_executed_length)   /*out: Length of gtid_executed string */
{
  ulong const tx_isolation = thd_tx_isolation(thd);

  if (tx_isolation != ISO_REPEATABLE_READ)
  {
    my_printf_error(ER_UNKNOWN_ERROR,
                    "Only REPEATABLE READ isolation level is supported "
                    "for START TRANSACTION WITH CONSISTENT SNAPSHOT "
                    "in RocksDB Storage Engine.", MYF(0));
    return 1;
  }

  Rdb_transaction*& trx= get_trx_from_thd(thd);
  if (trx == NULL)
  {
    if (!(trx= new Rdb_transaction))
      return 1;
    trx->pins= row_locks.get_pins();
  }

  if (trx->snapshot)
    rdb->ReleaseSnapshot(trx->snapshot);

  if (binlog_file)
  {
    if (binlog_pos && mysql_bin_log_is_open())
      mysql_bin_log_lock_commits();
    else
      return 1;
  }

  trx->snapshot= rdb->GetSnapshot();

  if (binlog_file)
    mysql_bin_log_unlock_commits(binlog_file, binlog_pos, gtid_executed,
                                 gtid_executed_length);

  return 0;
}

/*
  This is called for INFORMATION_SCHEMA
*/
static void rocksdb_update_table_stats(
	/* per-table stats callback */
	void (*cb)(const char* db, const char* tbl, bool is_partition,
		   my_io_perf_t* r, my_io_perf_t* w, my_io_perf_t* r_blob,
		   my_io_perf_t* r_primary, my_io_perf_t* r_secondary,
		   page_stats_t *page_stats, comp_stats_t *comp_stats,
		   int n_lock_wait, int n_lock_wait_timeout,
		   const char* engine))
{
	my_io_perf_t io_perf_read;
	my_io_perf_t io_perf;
  page_stats_t page_stats;
  comp_stats_t comp_stats;
  std::vector<std::string> tablenames;

  /*
    Most of these are for innodb, so setting them to 0.
    TODO: possibly separate out primary vs. secondary index reads
   */
  memset(&io_perf, 0, sizeof(io_perf));
  memset(&page_stats, 0, sizeof(page_stats));
  memset(&comp_stats, 0, sizeof(comp_stats));

  tablenames= get_share_names();

  for (auto it : tablenames)
  {
    ROCKSDB_SHARE *share;
    StringBuffer<256> buf, dbname, tablename, partname;
    bool is_partition;

    rocksdb_normalize_tablename(it.c_str(), &buf);
    if (rocksdb_split_normalized_tablename(buf.c_ptr(), &dbname, &tablename,
                                           &partname))
      continue;

    is_partition= (partname.length() != 0);

    share= get_share(it.c_str(), nullptr);
    if (!share)
      continue;

    io_perf_read.bytes= share->io_perf_read.bytes.load();
    io_perf_read.requests= share->io_perf_read.requests.load();

    /*
      Convert from rocksdb timer to mysql timer. RocksDB values are
      in nanoseconds, but table statistics expect the value to be
      in my_timer format.
     */
    io_perf_read.svc_time=
      microseconds_to_my_timer(share->io_perf_read.svc_time.load() / 1000);
    io_perf_read.svc_time_max=
      microseconds_to_my_timer(share->io_perf_read.svc_time_max.load() / 1000);
    io_perf_read.wait_time=
      microseconds_to_my_timer(share->io_perf_read.wait_time.load() / 1000);
    io_perf_read.wait_time_max=
      microseconds_to_my_timer(share->io_perf_read.wait_time_max.load() / 1000);
    io_perf_read.slow_ios= share->io_perf_read.slow_ios.load();
    free_share(share);

    (*cb)(dbname.c_ptr(), tablename.c_ptr(), is_partition, &io_perf_read,
          &io_perf, &io_perf, &io_perf, &io_perf, &page_stats, &comp_stats, 0,
          0, rocksdb_hton_name);
  }
}


void get_cf_options(const std::string &cf_name, rocksdb::ColumnFamilyOptions *opts)
{
  *opts = default_cf_opts;
  rocksdb_cf_options_map.Get(cf_name, opts);

  // Set the comparator according to 'rev:'
  if (is_cf_name_reverse(cf_name.c_str()))
    opts->comparator= &rocksdb_rev_pk_comparator;
  else
    opts->comparator= &rocksdb_pk_comparator;
}

/*
  Engine initialization function
*/

static int rocksdb_init_func(void *p)
{
  DBUG_ENTER("rocksdb_init_func");

#ifdef HAVE_PSI_INTERFACE
  init_rocksdb_psi_keys();
#endif

  rocksdb_hton= (handlerton *)p;
  mysql_mutex_init(ex_key_mutex_example, &rocksdb_mutex, MY_MUTEX_INIT_FAST);
  mysql_mutex_init(key_mutex_background, &background_mutex, MY_MUTEX_INIT_FAST);
  mysql_mutex_init(key_mutex_stop_background, &stop_cond_mutex,
                   MY_MUTEX_INIT_FAST);
  mysql_cond_init(key_cond_stop, &stop_cond, NULL);
  (void) my_hash_init(&rocksdb_open_tables,system_charset_info,32,0,0,
                      (my_hash_get_key) rocksdb_get_key,0,0);

  rocksdb_hton->state=    SHOW_OPTION_YES;
  rocksdb_hton->create=   rocksdb_create_handler;
  rocksdb_hton->close_connection= rocksdb_close_connection;
  rocksdb_hton->prepare=   rocksdb_prepare;
  rocksdb_hton->recover=   rocksdb_recover;
  rocksdb_hton->commit=   rocksdb_commit;
  rocksdb_hton->rollback= rocksdb_rollback;
  rocksdb_hton->db_type=  DB_TYPE_ROCKSDB;
  rocksdb_hton->show_status= rocksdb_show_status;
  rocksdb_hton->start_consistent_snapshot=
    rocksdb_start_trx_and_assign_read_view;
  rocksdb_hton->update_table_stats = rocksdb_update_table_stats;

  rocksdb_hton->flags= HTON_TEMPORARY_NOT_SUPPORTED |
                       HTON_SUPPORTS_EXTENDED_KEYS |
                       HTON_CAN_RECREATE;

  mysql_mutex_init(key_mutex_drop_index, &drop_index_mutex, MY_MUTEX_INIT_FAST);
  mysql_mutex_init(key_drop_index_interrupt_mutex, &drop_index_interrupt_mutex,
                   MY_MUTEX_INIT_FAST);
  mysql_cond_init(key_drop_index_interrupt_cond, &drop_index_interrupt_cond, NULL);

  DBUG_ASSERT(!mysqld_embedded);

  row_locks.init(compare_mem_comparable_keys,
                 Primary_key_comparator::get_hashnr);

  rocksdb_stats= rocksdb::CreateDBStatistics();
  db_options.statistics = rocksdb_stats;

  std::string rocksdb_db_name=  "./.rocksdb";

  std::vector<std::string> cf_names;

  rocksdb::Status status;

  if (rocksdb_rate_limiter_bytes_per_sec != 0) {
    db_options.rate_limiter.reset(
      rocksdb::NewGenericRateLimiter(rocksdb_rate_limiter_bytes_per_sec));
  }
  db_options.info_log_level = (rocksdb::InfoLogLevel)rocksdb_info_log_level;
  db_options.wal_dir = rocksdb_wal_dir;

  status= rocksdb::DB::ListColumnFamilies(db_options, rocksdb_db_name,
                                          &cf_names);
  if (!status.ok())
  {
    /*
      When we start on an empty datadir, ListColumnFamilies returns IOError,
      and RocksDB doesn't provide any way to check what kind of error it was.
      Checking system errno happens to work right now.
    */
    if (status.IsIOError() && errno == ENOENT)
    {
      sql_print_information("RocksDB: Got ENOENT when listing column families");
      sql_print_information("RocksDB:   assuming that we're creating a new database");
    }
    else
    {
      std::string err_text= status.ToString();
      sql_print_error("RocksDB: Error listing column families: %s", err_text.c_str());
      DBUG_RETURN(1);
    }
  }
  else
    sql_print_information("RocksDB: %ld column families found", cf_names.size());

  std::vector<rocksdb::ColumnFamilyDescriptor> cf_descr;
  std::vector<rocksdb::ColumnFamilyHandle*> cf_handles;

  default_cf_opts.comparator= &rocksdb_pk_comparator;
  default_cf_opts.compaction_filter_factory.reset(new Rdb_CompactionFilterFactory);

  default_cf_opts.write_buffer_size = ROCKSDB_WRITE_BUFFER_SIZE_DEFAULT;

  table_options.index_type =
    (rocksdb::BlockBasedTableOptions::IndexType)rocksdb_index_type;

  if (!table_options.no_block_cache) {
    table_options.block_cache = rocksdb::NewLRUCache(rocksdb_block_cache_size);
  }
  // Using newer BlockBasedTable format version for better compression
  // and better memory allocation.
  // See: https://github.com/facebook/rocksdb/commit/9ab5adfc59a621d12357580c94451d9f7320c2dd
  table_options.format_version= 2;
  default_cf_opts.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_options));
  if (rocksdb_collect_sst_properties) {
    default_cf_opts.table_properties_collector_factories.push_back(
      std::make_shared<MyRocksTablePropertiesCollectorFactory>(&ddl_manager)
    );
  }

  if (!rocksdb_cf_options_map.SetDefault(
        std::string(rocksdb_default_cf_options)) ||
      !rocksdb_cf_options_map.ParseConfigFile(
        std::string(rocksdb_cf_options_file))) {
    DBUG_RETURN(1);
  }

  /*
    If there are no column families, we're creating the new database.
    Create one column family named "default".
  */
  if (cf_names.size() == 0)
    cf_names.push_back(DEFAULT_CF_NAME);

  sql_print_information("RocksDB: Column Families at start:");
  for (size_t i = 0; i < cf_names.size(); ++i)
  {
    rocksdb::ColumnFamilyOptions opts;
    get_cf_options(cf_names[i], &opts);

    sql_print_information("  cf=%s", cf_names[i].c_str());
    sql_print_information("    write_buffer_size=%ld", opts.write_buffer_size);
    sql_print_information("    target_file_size_base=%" PRIu64,
                          opts.target_file_size_base);

    cf_descr.push_back(rocksdb::ColumnFamilyDescriptor(cf_names[i], opts));
  }

  rocksdb::Options main_opts(db_options, default_cf_opts);

  /*
    Flashcache configuration:
    When running on Flashcache, mysqld opens Flashcache device before
    initializing storage engines, and setting file descriptor at
    cachedev_fd global variable.
    RocksDB has Flashcache-aware configuration. When this is enabled,
    RocksDB adds background threads into Flashcache blacklists, which
    makes sense for Flashcache use cases.
  */
  if (cachedev_enabled)
  {
    flashcache_aware_env=
      rocksdb::NewFlashcacheAwareEnv(rocksdb::Env::Default(),
                                     cachedev_fd);
    if (flashcache_aware_env.get() == nullptr)
    {
      sql_print_error("RocksDB: Failed to open flashcahce device at fd %d",
                      cachedev_fd);
      DBUG_RETURN(1);
    }
    sql_print_information("RocksDB: Disabling flashcache on background "
                          "writer threads, fd %d", cachedev_fd);
    main_opts.env= flashcache_aware_env.get();
  }

  main_opts.env->SetBackgroundThreads(main_opts.max_background_flushes,
                                      rocksdb::Env::Priority::HIGH);
  main_opts.env->SetBackgroundThreads(main_opts.max_background_compactions,
                                      rocksdb::Env::Priority::LOW);
  status= rocksdb::DB::Open(main_opts, rocksdb_db_name, cf_descr,
                            &cf_handles, &rdb);


  if (!status.ok())
  {
    std::string err_text= status.ToString();
    sql_print_error("RocksDB: Error opening instance: %s", err_text.c_str());
    DBUG_RETURN(1);
  }
  cf_manager.init(&cf_handles);

  if (dict_manager.init(rdb, &cf_manager))
  {
    sql_print_error("RocksDB: Failed to initialize data dictionary.");
    DBUG_RETURN(1);
  }

  if (binlog_manager.init(&dict_manager))
    DBUG_RETURN(1);

  if (ddl_manager.init(&dict_manager, &cf_manager))
    DBUG_RETURN(1);

  dropped_indices_manager.init(&ddl_manager, &dict_manager);

  stop_background_thread = false;
  pthread_t thread_handle;
  auto err = mysql_thread_create(
    key_thread_background, &thread_handle,
    nullptr,
    background_thread, NULL
  );
  if (err != 0) {
    sql_print_error("RocksDB: Couldn't start the background thread: (errno=%d)",
                    err);
    DBUG_RETURN(1);
  }

  stop_drop_index_thread = false;
  err = mysql_thread_create(
    key_thread_drop_index, &thread_handle,
    nullptr,
    drop_index_thread, NULL
  );
  if (err != 0) {
    sql_print_error("RocksDB: Couldn't start the drop index thread: (errno=%d)",
                    err);
    DBUG_RETURN(1);
  }

  sql_print_information("RocksDB instance opened");
  DBUG_RETURN(0);
}

static int rocksdb_done_func(void *p)
{
  int error= 0;
  DBUG_ENTER("rocksdb_done_func");

  // signal the background thread to stop
  mysql_mutex_lock(&stop_cond_mutex);
  stop_background_thread = true;
  mysql_cond_signal(&stop_cond);
  mysql_mutex_unlock(&stop_cond_mutex);

  // signal the drop index thread to stop
  signal_drop_index_thread(true);

  // wait for the background thread to finish
  mysql_mutex_lock(&background_mutex);
  mysql_mutex_unlock(&background_mutex);

  // wait for the drop index thread to finish
  mysql_mutex_lock(&drop_index_mutex);
  mysql_mutex_unlock(&drop_index_mutex);

  // Stop all rocksdb background work
  rocksdb::CancelAllBackgroundWork(rdb, true);

  if (rocksdb_open_tables.records)
    error= 1;
  my_hash_free(&rocksdb_open_tables);
  mysql_mutex_destroy(&rocksdb_mutex);

  row_locks.cleanup();
  ddl_manager.cleanup();
  binlog_manager.cleanup();
  dropped_indices_manager.cleanup();
  dict_manager.cleanup();
  cf_manager.cleanup();

  delete rdb;
  rdb= NULL;

  // Disown the cache data since we're shutting down.
  // This results in memory leaks but it improved the shutdown time.
  // Don't disown when running under valgrind
#ifndef HAVE_purify
  if (table_options.block_cache)
    table_options.block_cache->DisownData();
#endif /* HAVE_purify */

  DBUG_RETURN(error);
}


/**
  @brief
  Example of simple lock controls. The "share" it creates is a
  structure we will pass to each rocksdb handler. Do you have to have
  one of these? Well, you have pieces that are used for locking, and
  they are needed to function.
*/

static ROCKSDB_SHARE *get_share(const char *table_name, TABLE *table)
{
  ROCKSDB_SHARE *share;
  uint length;
  char *tmp_name;

  mysql_mutex_lock(&rocksdb_mutex);
  length=(uint) strlen(table_name);

  if (!(share=(ROCKSDB_SHARE*) my_hash_search(&rocksdb_open_tables,
                                              (uchar*) table_name,
                                              length)))
  {
    if (!(share=(ROCKSDB_SHARE *)
          my_multi_malloc(MYF(MY_WME | MY_ZEROFILL),
                          &share, sizeof(*share),
                          &tmp_name, length+1,
                          NullS)))
    {
      mysql_mutex_unlock(&rocksdb_mutex);
      return NULL;
    }

    share->use_count=0;
    share->table_name_length=length;
    share->table_name=tmp_name;
    strmov(share->table_name,table_name);

    if (my_hash_insert(&rocksdb_open_tables, (uchar*) share))
      goto error;
    thr_lock_init(&share->lock);
    //mysql_mutex_init(ex_key_mutex_ROCKSDB_SHARE_mutex,
    //                 &share->mutex, MY_MUTEX_INIT_FAST);
    my_io_perf_atomic_init(&share->io_perf_read);
  }
  share->use_count++;
  mysql_mutex_unlock(&rocksdb_mutex);

  return share;

error:
 // mysql_mutex_destroy(&share->mutex);
  my_free(share);

  return NULL;
}


std::vector<std::string> get_share_names(void)
{
  int i;
  ROCKSDB_SHARE *share;
  std::vector<std::string> names;

  mysql_mutex_lock(&rocksdb_mutex);
  for (i = 0;
       (share= (ROCKSDB_SHARE*) my_hash_element(&rocksdb_open_tables, i));
       i++)
  {
    names.push_back(share->table_name);
  }
  mysql_mutex_unlock(&rocksdb_mutex);

  return names;
}


void ha_rocksdb::load_auto_incr_value()
{
  int save_active_index= active_index;
  active_index= table->s->primary_key;
  uint8 save_table_status= table->status;

  // Do a lookup.
  if (!index_last(table->record[0]))
    update_auto_incr_val();

  table->status= save_table_status;
  active_index= save_active_index;

  /*
    Do what ha_rocksdb::index_end() does.
    (Why don't we use index_init/index_end? class handler defines index_init
     as private, for some reason).
  */
  if (scan_it)
  {
    delete scan_it;
    scan_it= NULL;
  }
}


/* Get PK value from table->record[0]. */
void ha_rocksdb::update_auto_incr_val()
{
  Field *pk_field;
  longlong new_val;
  pk_field= table->key_info[table->s->primary_key].key_part[0].field;

  my_bitmap_map *old_map= dbug_tmp_use_all_columns(table, table->read_set);
  new_val= pk_field->val_int() + 1;
  dbug_tmp_restore_column_map(table->read_set, old_map);

  /* TODO: switch to compare-and-swap? */
  mysql_mutex_lock(&tbl_def->mutex);
  if (tbl_def->auto_incr_val < new_val)
    tbl_def->auto_incr_val= new_val;
  mysql_mutex_unlock(&tbl_def->mutex);
}


/**
  @brief
  Free lock controls. We call this whenever we close a table. If the table had
  the last reference to the share, then we free memory associated with it.
*/

static int free_share(ROCKSDB_SHARE *share)
{
  mysql_mutex_lock(&rocksdb_mutex);
  if (!--share->use_count)
  {
    my_hash_delete(&rocksdb_open_tables, (uchar*) share);
    thr_lock_delete(&share->lock);
    //mysql_mutex_destroy(&share->mutex);
    my_free(share);
  }
  mysql_mutex_unlock(&rocksdb_mutex);

  return 0;
}


static handler* rocksdb_create_handler(handlerton *hton,
                                       TABLE_SHARE *table,
                                       MEM_ROOT *mem_root)
{
  return new (mem_root) ha_rocksdb(hton, table);
}


ha_rocksdb::ha_rocksdb(handlerton *hton, TABLE_SHARE *table_arg)
  : handler(hton, table_arg), scan_it(NULL),
    tbl_def(NULL),
    pk_descr(NULL),
    key_descr(NULL),
    pk_can_be_decoded(false),
    pk_tuple(NULL), pk_packed_tuple(NULL),
    sec_key_packed_tuple(NULL), sec_key_tails(NULL),
    sec_key_match_prefix(NULL), sec_key_match_prefix_buf(NULL),
    sec_key_packed_tuple_old(NULL), sec_key_tails_old(NULL),
    pack_buffer(NULL),
    lock_rows(FALSE),
    keyread_only(FALSE),
    field_enc(NULL)
{}


static const char *ha_rocksdb_exts[] = {
  NullS
};


const char **ha_rocksdb::bas_ext() const
{
  return ha_rocksdb_exts;
}


/*
  Convert record from table->record[0] form into a form that can be written
  into rocksdb.

  @param packed_rec OUT Data slice with record data.
*/

void ha_rocksdb::convert_record_to_storage_format(rocksdb::Slice *packed_rec)
{
  storage_record.length(0);
  /* All NULL bits are initially 0 */
  storage_record.fill(null_bytes_in_rec, 0);

  for (uint i=0; i < table->s->fields; i++)
  {
    /* Don't pack decodable PK key parts */
    if (field_enc[i].dont_store)
      continue;

    Field *field= table->field[i];
    if (field_enc[i].maybe_null())
    {
      char *data= (char*)storage_record.ptr();
      if (field->is_null())
      {
        data[field_enc[i].null_offset] |= field_enc[i].null_mask;
        /* Don't write anything for NULL values */
        continue;
      }
    }

    if (field_enc[i].field_type == MYSQL_TYPE_BLOB)
    {
      Field_blob *blob= (Field_blob*)field;
      /* Get the number of bytes needed to store length*/
      uint length_bytes= blob->pack_length() - portable_sizeof_char_ptr;

      /* Store the length of the value */
      storage_record.append((char*)blob->ptr, length_bytes);

      /* Store the blob value itself */
      char *data_ptr;
      memcpy(&data_ptr, blob->ptr + length_bytes, sizeof(uchar**));
      storage_record.append(data_ptr, blob->get_length());
    }
    else if (field_enc[i].field_type == MYSQL_TYPE_VARCHAR)
    {
      Field_varstring* field_var= (Field_varstring*)field;
      uint data_len;
      /* field_var->length_bytes is 1 or 2 */
      if (field_var->length_bytes == 1)
      {
        data_len= field_var->ptr[0];
      }
      else
      {
        DBUG_ASSERT(field_var->length_bytes==2);
        data_len= uint2korr(field_var->ptr);
      }
      storage_record.append((char*)field_var->ptr,
                            field_var->length_bytes + data_len);
    }
    else
    {
      /* Copy the field data */
      uint len= field->pack_length_in_rec();
      storage_record.append((char*)field->ptr, len);
    }
  }
  *packed_rec= rocksdb::Slice(storage_record.ptr(), storage_record.length());
}


/*
  @param  slice Record in storage format
  @param  buf   Store record in table->record[0] form here
*/
int ha_rocksdb::convert_record_from_storage_format(rocksdb::Slice *slice,
                                                    uchar *buf)
{
  retrieved_record.assign(slice->data(), slice->size());
  return convert_record_from_storage_format(buf);
}

#ifndef NDEBUG
void dbug_append_garbage_at_end(std::string &on_disk_rec)
{
  on_disk_rec.append("abc");
}

void dbug_truncate_record(std::string &on_disk_rec)
{
  on_disk_rec.resize(0);
}

void dbug_modify_rec_varchar12(std::string &on_disk_rec)
{
  std::string res;
  // The record is NULL-byte followed by VARCHAR(10).
  // Put the NULL-byte
  res.append("\0", 1);
  // Then, add a valid VARCHAR(12) value.
  res.append("\xC", 1);
  res.append("123456789ab", 12);

  on_disk_rec.assign(res);
}


void dbug_modify_key_varchar8(String &on_disk_rec)
{
  std::string res;
  // The key starts with index number
  res.append(on_disk_rec.ptr(), RDBSE_KEYDEF::INDEX_NUMBER_SIZE);

  // Then, a mem-comparable form of a varchar(8) value.
  res.append("ABCDE\0\0\0\xFC", 9);
  on_disk_rec.length(0);
  on_disk_rec.append(res.data(), res.size());
}

#endif

/*
  Unpack the record in this->retrieved_record from storage format into
  buf (which can be table->record[0] or table->record[1]).

  psergey-todo: this->last_rowkey has a part of the data.

  If the table has blobs, the unpacked data in buf may keep pointers to the
  data in this->retrieved_record.

  @return
    0      OK
    other  Error inpacking the data
*/

int ha_rocksdb::convert_record_from_storage_format(uchar * buf)
{
  DBUG_EXECUTE_IF("myrocks_simulate_bad_row_read1",
                  dbug_append_garbage_at_end(retrieved_record););
  DBUG_EXECUTE_IF("myrocks_simulate_bad_row_read2",
                  dbug_truncate_record(retrieved_record););
  DBUG_EXECUTE_IF("myrocks_simulate_bad_row_read3",
                  dbug_modify_rec_varchar12(retrieved_record););

  Stream_reader reader(retrieved_record);
  my_ptrdiff_t ptr_diff= buf - table->record[0];

  /*
    Decode PK fields from the key
  */
  DBUG_EXECUTE_IF("myrocks_simulate_bad_pk_read1",
                  dbug_modify_key_varchar8(last_rowkey););
  rocksdb::Slice rowkey_slice(last_rowkey.ptr(), last_rowkey.length());


  DBUG_ASSERT(pk_descr->get_unpack_data_len() == 0);
  rocksdb::Slice pk_unpack_info_slice;
  if (pk_descr->unpack_record(table, buf, &rowkey_slice, &pk_unpack_info_slice))
    return HA_ERR_INTERNAL_ERROR;

  /* Other fields are decoded from the value */
  const char * UNINIT_VAR(null_bytes);
  if (null_bytes_in_rec && !(null_bytes= reader.read(null_bytes_in_rec)))
    return HA_ERR_INTERNAL_ERROR;

  for (uint i=0; i < table->s->fields; i++)
  {
    if (field_enc[i].dont_store)
      continue;

    Field *field= table->field[i];

    int isNull = field_enc[i].maybe_null() &&
      (null_bytes[field_enc[i].null_offset] & field_enc[i].null_mask) != 0;
    if (isNull)
      field->set_null(ptr_diff);
    else
      field->set_notnull(ptr_diff);

    if (field_enc[i].field_type == MYSQL_TYPE_BLOB)
    {
      Field_blob *blob= (Field_blob*)field;
      /* Get the number of bytes needed to store length*/
      uint length_bytes= blob->pack_length() - portable_sizeof_char_ptr;

      blob->move_field_offset(ptr_diff);

      if (isNull)
      {
        memset(blob->ptr, 0, length_bytes + sizeof(uchar**));
        blob->move_field_offset(-ptr_diff);
        /* NULL value means no data is stored */
        continue;
      }

      const char *data_len_str;
      if (!(data_len_str= reader.read(length_bytes)))
      {
        blob->move_field_offset(-ptr_diff);
        return HA_ERR_INTERNAL_ERROR;
      }

      memcpy(blob->ptr, data_len_str, length_bytes);

      uint32 data_len= blob->get_length((uchar*)data_len_str, length_bytes,
                                        table->s->db_low_byte_first);
      const char *blob_ptr;
      if (!(blob_ptr= reader.read(data_len)))
      {
        blob->move_field_offset(-ptr_diff);
        return HA_ERR_INTERNAL_ERROR;
      }

      // set 8-byte pointer to 0, like innodb does (relevant for 32-bit platforms)
      memset(blob->ptr + length_bytes, 0, 8);
      memcpy(blob->ptr + length_bytes, &blob_ptr, sizeof(uchar**));
      blob->move_field_offset(-ptr_diff);
    }
    else if (field_enc[i].field_type == MYSQL_TYPE_VARCHAR)
    {
      if (isNull)
        continue;

      Field_varstring* field_var= (Field_varstring*)field;
      const char *data_len_str;
      if (!(data_len_str= reader.read(field_var->length_bytes)))
        return HA_ERR_INTERNAL_ERROR;

      uint data_len;
      /* field_var->length_bytes is 1 or 2 */
      if (field_var->length_bytes == 1)
      {
        data_len= (uchar)data_len_str[0];
      }
      else
      {
        DBUG_ASSERT(field_var->length_bytes==2);
        data_len= uint2korr(data_len_str);
      }
      if (data_len > field->field_length)
      {
        /* The data on disk is longer than table DDL allows? */
        return HA_ERR_INTERNAL_ERROR;
      }
      if (!(reader.read(data_len)))
        return HA_ERR_INTERNAL_ERROR;
      field_var->move_field_offset(ptr_diff);
      memcpy(field_var->ptr, data_len_str, field_var->length_bytes + data_len);
      field_var->move_field_offset(-ptr_diff);
    }
    else
    {
      if (isNull)
        continue;

      const char *data_bytes;
      uint len= field->pack_length_in_rec();
      if (!(data_bytes= reader.read(len)))
        return HA_ERR_INTERNAL_ERROR;
      field->move_field_offset(ptr_diff);
      memcpy((char*)field->ptr, data_bytes, len);
      field->move_field_offset(-ptr_diff);
    }
  }

  if (reader.remaining_bytes())
    return HA_ERR_INTERNAL_ERROR;

  return 0;
}


/*
  Setup data needed to convert table->record[] to and from record storage format.

  @seealso
     ha_rocksdb::convert_record_to_storage_format,
     ha_rocksdb::convert_record_from_storage_format
*/

void ha_rocksdb::setup_field_converters()
{
  uint i;
  uint null_bytes= 0;
  uchar cur_null_mask=0x1;

  if (!(field_enc= (FIELD_ENCODER*)my_malloc(table->s->fields*sizeof(FIELD_ENCODER), MYF(0))))
    return;

  for (i= 0; i < table->s->fields; i++)
  {
    Field *field= table->field[i];
    field_enc[i].dont_store= false;

    /*
      Check if this field is
      - a part of primary key, and
      - it can be decoded back from its key image.
      If both hold, we don't need to store this field in the value part of
      RocksDB's key-value pair.
    */
    if (field->part_of_key.is_set(table->s->primary_key))
    {
      KEY *pk_info= &table->key_info[table->s->primary_key];
      for (uint kp= 0; kp < pk_info->user_defined_key_parts; kp++)
      {
        /* key_part->fieldnr is counted from 1 */
        if (field->field_index + 1 == pk_info->key_part[kp].fieldnr)
        {
          if (pk_descr->can_unpack(kp))
            field_enc[i].dont_store= true;
          break;
        }
      }
    }

    field_enc[i].field_type= field->real_type();

    if (field->real_maybe_null())
    {
      field_enc[i].null_mask= cur_null_mask;
      field_enc[i].null_offset=null_bytes;
      if (cur_null_mask == 0x80)
      {
        cur_null_mask= 0x1;
        null_bytes++;
      }
      else
        cur_null_mask= cur_null_mask << 1;
    }
    else
      field_enc[i].null_mask= 0;
  }

  /* Count the last, unfinished NULL-bits byte */
  if (cur_null_mask != 0x1)
    null_bytes++;

  null_bytes_in_rec= null_bytes;
}

void ha_rocksdb::io_perf_start(void)
{
  rocksdb::SetPerfLevel(
    static_cast<rocksdb::PerfLevel>(rocksdb_perf_context_level));

#define IO_PERF_INIT(_field_) io_perf._field_= rocksdb::perf_context._field_
  IO_PERF_INIT(block_read_byte);
  IO_PERF_INIT(block_read_count);
  IO_PERF_INIT(block_read_time);
  IO_PERF_INIT(internal_key_skipped_count);
  IO_PERF_INIT(internal_delete_skipped_count);
#undef IO_PERF_INIT
}

void ha_rocksdb::io_perf_end_and_record(void)
{
  /*
    This seems to be needed to prevent gdb from crashing if it breaks
    or enters this function.
   */
  rocksdb::SetPerfLevel(
    static_cast<rocksdb::PerfLevel>(rocksdb_perf_context_level));

#define IO_PERF_DIFF(_field_) io_perf._field_= rocksdb::perf_context._field_ - \
                                               io_perf._field_
  IO_PERF_DIFF(block_read_byte);
  IO_PERF_DIFF(block_read_count);
  IO_PERF_DIFF(block_read_time);
  IO_PERF_DIFF(internal_key_skipped_count);
  IO_PERF_DIFF(internal_delete_skipped_count);
#undef IO_PERF_DIFF

  if (io_perf.block_read_byte + io_perf.block_read_count +
      io_perf.block_read_time != 0)
  {
    my_io_perf_t io_perf_read;

    my_io_perf_init(&io_perf_read);
    io_perf_read.bytes= io_perf.block_read_byte;
    io_perf_read.requests= io_perf.block_read_count;

    /*
      Rocksdb does not distinguish between I/O service and wait time, so just
      use svc time.
     */
    io_perf_read.svc_time_max= io_perf_read.svc_time= io_perf.block_read_time;

    my_io_perf_sum_atomic_helper(&share->io_perf_read, &io_perf_read);
    my_io_perf_sum(&stats.table_io_perf_read, &io_perf_read);
  }

  if (io_perf.internal_key_skipped_count)
  {
    share->internal_key_skipped_count += io_perf.internal_key_skipped_count;
    stats.key_skipped += io_perf.internal_key_skipped_count;
  }

  if (io_perf.internal_delete_skipped_count)
  {
    share->internal_delete_skipped_count +=
        io_perf.internal_delete_skipped_count;
    stats.delete_skipped += io_perf.internal_delete_skipped_count;
  }
}

int ha_rocksdb::open(const char *name, int mode, uint test_if_locked)
{
  DBUG_ENTER("ha_rocksdb::open");

  if (!(share = get_share(name, table)))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  thr_lock_data_init(&share->lock,&lock,NULL);

  /*
    note: pk_descr may be non-NULL here, when using partitions. It seems,
    it's ok to discard it
  */
  StringBuffer<256> fullname;
  rocksdb_normalize_tablename(name, &fullname);
  if (!(tbl_def= ddl_manager.find((uchar*)fullname.c_ptr(), fullname.length())))
  {
    my_error(ER_INTERNAL_ERROR, MYF(0),
             "Attempt to open a table that is not present in RocksDB-SE data dictionary");
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  key_descr= tbl_def->key_descr;
  pk_descr= key_descr[table->s->primary_key];

  pk_key_parts= table->key_info[table->s->primary_key].user_defined_key_parts;

  uint key_len= table->key_info[table->s->primary_key].key_length;

  pk_descr->setup(table);  // move this into get_share() ??

  uint packed_key_len= pk_descr->max_storage_fmt_length();

  if (!(pk_tuple= (uchar*)my_malloc(key_len, MYF(0))) ||
      !(pk_packed_tuple= (uchar*)my_malloc(packed_key_len, MYF(0))))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  /* Sometimes, we may use sec_key_packed_tuple for storing packed PK */
  uint max_packed_sec_key_len= packed_key_len;
  for (uint i= 0; i < table->s->keys; i++)
  {
    if (i == table->s->primary_key) /* Primary key was processed above */
      continue;

    key_descr[i]->setup(table);  // move this into get_share() ??

    uint packed_len= key_descr[i]->max_storage_fmt_length();
    if (packed_len > max_packed_sec_key_len)
      max_packed_sec_key_len= packed_len;
  }

  setup_field_converters();

  if (!(sec_key_packed_tuple= (uchar*)my_malloc(max_packed_sec_key_len,
                                                MYF(0))) ||
      !(sec_key_match_prefix_buf= (uchar*)my_malloc(max_packed_sec_key_len,
                                                    MYF(0))) ||
      !(sec_key_packed_tuple_old= (uchar*)my_malloc(max_packed_sec_key_len,
                                                 MYF(0))) ||
      !((sec_key_tails= (uchar*)my_malloc(max_packed_sec_key_len, MYF(0)))) ||
      !((sec_key_tails_old= (uchar*)my_malloc(max_packed_sec_key_len, MYF(0)))) ||
      !((pack_buffer=(uchar*)my_malloc(max_packed_sec_key_len, MYF(0)))))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  info(HA_STATUS_NO_LOCK | HA_STATUS_VARIABLE | HA_STATUS_CONST);

  /* TODO: move the following to where TABLE_SHARE is opened: */
  if (table->found_next_number_field)
    load_auto_incr_value();

  DBUG_RETURN(0);
}


int ha_rocksdb::close(void)
{
  DBUG_ENTER("ha_rocksdb::close");

  pk_descr= NULL;
  key_descr= NULL;

  if (pk_tuple)
    my_free(pk_tuple);
  pk_tuple= NULL;

  if (pk_packed_tuple)
    my_free(pk_packed_tuple);
  pk_packed_tuple= NULL;

  if (sec_key_packed_tuple)
    my_free(sec_key_packed_tuple);
  sec_key_packed_tuple= NULL;

  if (sec_key_match_prefix_buf)
    my_free(sec_key_match_prefix_buf);
  sec_key_match_prefix_buf= NULL;

  if (sec_key_packed_tuple_old)
    my_free(sec_key_packed_tuple_old);
  sec_key_packed_tuple_old= NULL;

  if (sec_key_tails)
    my_free(sec_key_tails);
  sec_key_tails= NULL;

  if (sec_key_tails_old)
    my_free(sec_key_tails_old);
  sec_key_tails_old= NULL;

  if (pack_buffer)
    my_free(pack_buffer);
  pack_buffer= NULL;

  if (field_enc)
    my_free(field_enc);
  field_enc= NULL;

  DBUG_RETURN(free_share(share));
}


const int HA_ERR_ROCKSDB_UNIQUE_NOT_SUPPORTED= HA_ERR_LAST+1;
const int HA_ERR_ROCKSDB_PK_REQUIRED= HA_ERR_LAST+2;
const int HA_ERR_ROCKSDB_TOO_MANY_LOCKS= HA_ERR_LAST+3;

bool ha_rocksdb::get_error_message(int error, String *buf)
{
  if (error == HA_ERR_ROCKSDB_PK_REQUIRED)
    buf->append("Table must have a PRIMARY KEY");
  else if (error == HA_ERR_ROCKSDB_UNIQUE_NOT_SUPPORTED)
    buf->append("Unique indexes are not supported");
  else if (error == HA_ERR_ROCKSDB_TOO_MANY_LOCKS)
    buf->append("Number of locks held reached @@rocksdb_max_row_locks");
  return FALSE; /* not a temporary error */
}


/*
  Create structures needed for storing data in rocksdb. This is called when the
  table is created. The structures will be shared by all TABLE* objects.

  @param
    table_arg  Table with definition
    db_table   "dbname.tablename"
    len        strlen of the above

  @return
    0      - Ok
    other  - error, either given table ddl is not supported by rocksdb or OOM.
*/

int ha_rocksdb::create_key_defs(TABLE *table_arg, const char *db_table, uint len)
{
  uint i;
  DBUG_ENTER("ha_rocksdb::create_key_defs");
  uint n_keys= table_arg->s->keys;

  bool write_err= false;
  std::unique_ptr<rocksdb::WriteBatch> wb= dict_manager.begin();
  rocksdb::WriteBatch *batch= wb.get();

  /* Create table/key descriptions and put them into the data dictionary */
  if (!(tbl_def= new RDBSE_TABLE_DEF))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  if (!(key_descr= new RDBSE_KEYDEF*[n_keys]))
    goto error;

  memset(key_descr, 0, sizeof(RDBSE_KEYDEF*) * n_keys);
  tbl_def->n_keys= n_keys;
  tbl_def->key_descr= key_descr;

  for (i= 0; i < table_arg->s->keys; i++)
  {
    rocksdb::ColumnFamilyHandle* cf_handle;
    /*
      index comment has Column Family name. If there was no comment, we get
      NULL, and it means use the default column family.
    */
    const char *comment= table_arg->key_info[i].comment.str;
    if (looks_like_per_index_cf_typo(comment))
    {
      my_error(ER_NOT_SUPPORTED_YET, MYF(0),
               "column family name looks like a typo of $per_index_cf");
      goto error;
    }
    /* Prevent create from using the system column family */
    if (comment && strcmp(DEFAULT_SYSTEM_CF_NAME, comment) == 0)
    {
      my_error(ER_WRONG_ARGUMENTS, MYF(0),
               "column family not valid for storing index data");
      goto error;
    }
    bool is_auto_cf;
    cf_handle= cf_manager.get_or_create_cf(rdb, comment, db_table,
                                           table_arg->key_info[i].name,
                                           &is_auto_cf);
    if (!cf_handle)
      goto error;

    if (!(key_descr[i]= new RDBSE_KEYDEF(ddl_manager.get_next_number(), i,
                                         cf_handle,
                                         is_cf_name_reverse(comment),
                                         is_auto_cf,
                                         table_arg->key_info[i].name
                                        )))
      goto error;
  }
  pk_descr= key_descr[table_arg->s->primary_key];

  tbl_def->dbname_tablename.append(db_table, len);
  dict_manager.lock();
  write_err= ddl_manager.put_and_write(tbl_def, batch) || dict_manager.commit(batch);
  dict_manager.unlock();

  if (write_err)
    goto error;

  DBUG_RETURN(0);

error:
  /* Delete what we have allocated so far */
  delete tbl_def;

  DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
}


int rocksdb_normalize_tablename(const char *tablename,
                                StringBuffer<256> *strbuf)
{
  DBUG_ASSERT(tablename[0] == '.' && tablename[1] == '/');
  tablename += 2;

  const char *p= tablename;
  for (; *p != '/'; p++)
  {
    if (*p =='\0')
    {
      DBUG_ASSERT(0); // We were not passed table name?
      return HA_ERR_INTERNAL_ERROR ;
    }
  }
  strbuf->append(tablename, p - tablename);
  strbuf->append('.');
  strbuf->append(p + 1);
  return 0;
}


/**
  @brief
  splits the normalized table name of <dbname>.<tablename>#P#<part_no> into
  the <dbname>, <tablename> and <part_no> components.

  @param dbbuf returns database name/table_schema
  @param tablebuf returns tablename
  @param partitionbuf returns partition suffix if there is one
  @return 0 on success, non-zero on failure to split
*/
int rocksdb_split_normalized_tablename(const char *fullname,
                                       StringBuffer<256> *dbbuf,
                                       StringBuffer<256> *tablebuf,
                                       StringBuffer<256> *partitionbuf)
{
#define PARTITION_STR "#P#"

  /* Normalize returns dbname.tablename */
  const char *tb= strstr(fullname, ".");

  /* Invalid table name? */
  if (tb == NULL)
    return HA_ERR_INTERNAL_ERROR;

  dbbuf->append(fullname, tb - fullname);
  tb++;

  const char *pt= strstr(tb, PARTITION_STR);

  if (pt)
  {
    tablebuf->append(tb, pt - tb);
    pt += sizeof(PARTITION_STR);
    partitionbuf->append(pt);
  }
  else
    tablebuf->append(tb);
  return 0;
}


/**
  @brief
  create() is called to create a database. The variable name will have the name
  of the table.

  @details
  When create() is called you do not need to worry about
  opening the table. Also, the .frm file will have already been
  created so adjusting create_info is not necessary. You can overwrite
  the .frm file at this point if you wish to change the table
  definition, but there are no methods currently provided for doing
  so.

  Called from handle.cc by ha_create_table().

  @see
  ha_create_table() in handle.cc
*/

int ha_rocksdb::create(const char *name, TABLE *table_arg,
                       HA_CREATE_INFO *create_info)
{
  int res;
  StringBuffer<256> strbuf;
  DBUG_ENTER("ha_rocksdb::create");

  /*
    Construct dbname.tablename ourselves, because parititioning
    passes strings like "./test/t14#P#p0" for individual partitions,
    while table_arg->s->table_name has none of that.
  */
  if (rocksdb_normalize_tablename(name, &strbuf))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  /*
    We have HA_REQUIRE_PRIMARY_KEY flag, which guarantees there will be a
    primary key.
    But there is an exception: the query

      ALTER TABLE ... DROP COLUMN primary_key_column

    will attempt to create a table without primary key.
  */
  if (table_arg->s->primary_key == MAX_INDEXES)
  {
    DBUG_RETURN(HA_ERR_ROCKSDB_PK_REQUIRED);
  }

  if ((res= create_key_defs(table_arg, strbuf.ptr(), strbuf.length())))
    DBUG_RETURN(res);

  if (create_info->auto_increment_value)
     tbl_def->auto_incr_val= create_info->auto_increment_value;

  /*
    We only support auto_increment at start of the PRIMARY KEY.
  */
  //Field *field;
  //if ((field= table_arg->next_number_field))
  /* TODO mdcallag: disable this for now to let UNIQUE indexes kind of work
  if ((field= table_arg->found_next_number_field))
  {
    int pk= table_arg->s->primary_key;
    Field *pk_field= table_arg->key_info[pk].key_part[0].field;
    if (field->field_index !=  pk_field->field_index)
      DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }
  */
  DBUG_RETURN(0);
}


/*
  This function is used only when the table has not yet been opened, and
  keyread_allowed bitmap doesn't have the correct values yet.

  See comment it ha_rocksdb::index_flags() for details.
*/

bool ha_rocksdb::check_keyread_allowed(uint inx, uint part, bool all_parts) const
{
  bool res= true;
  KEY *key_info= &table_share->key_info[inx];

  Field_pack_info dummy1;
  res= dummy1.setup(key_info->key_part[part].field, inx, part);

  if (res && all_parts)
  {
    for (uint i= 0; i < part; i++)
    {
      Field *field;
      if ((field= key_info->key_part[i].field))
      {
        Field_pack_info dummy;
        if (!dummy.setup(field, inx, i))
        {
          /* Cannot do index-only reads for this column */
          res= false;
          break;
        }
      }
    }
  }

  uint pk= table_share->primary_key;
  if (inx == pk && all_parts &&
      part == table_share->key_info[pk].user_defined_key_parts)
  {
    pk_can_be_decoded= res;
  }

  return res;
}

int ha_rocksdb::read_key_exact(RDBSE_KEYDEF *kd,
                               Apply_changes_iter *iter,
                               bool full_key_match,
                               rocksdb::Slice& key_slice)
{
  /*
    We are looking for the first record such that
      index_tuple= lookup_tuple.
    lookup_tuple may be a prefix of the index.
  */
  if (kd->is_reverse_cf)
  {
    if (!full_key_match)
    {
      if (!iter->Valid())
        iter->SeekToLast();
      else
        iter->Prev();
    }
  }

  if (!iter->Valid() || !kd->value_matches_prefix(iter->key(), key_slice))
  {
    /*
      Got a record that is not equal to the lookup value, or even a record
      from another table.index.
    */
    return HA_ERR_KEY_NOT_FOUND;
  }
  return 0;
}

/*
  @note
    The problem with this function is that SQL layer calls it, when
     - the table has not been yet opened (no ::open() call done)
     - this->table_share already exists, but it is in the process of being
       filled, so some of fields are still NULL.
     - In particular, table_share->key_info[inx].key_part[] is filled only up
       to part #part. Subsequent key parts are not yet filled.

    To complicate things further, SQL layer will call index_flags() with
    all_parts=TRUE. Essentially, we're asked to provide flags for reading
    keyparts whose datatype is not yet known.

    We walk around this problem by using check_keyread_allowed(), which uses
    table_share object and is careful not to step on unitialized data.

    When we get a call with all_parts=TRUE, we try to analyze all parts but
    ignore those that have key_part->field==NULL (these are not initialized
    yet).
*/

ulong ha_rocksdb::index_flags(uint inx, uint part, bool all_parts) const
{
  ulong base_flags= HA_READ_NEXT  |   // doesn't seem to be used
                    HA_READ_ORDER |
                    HA_READ_RANGE |
                    HA_READ_PREV;

  if (check_keyread_allowed(inx, part, all_parts))
    base_flags |= HA_KEYREAD_ONLY;

  if (inx == table_share->primary_key)
  {
    /*
      Index-only reads on primary key are the same as table scan for us. Still,
      we need to explicitly "allow" them, otherwise SQL layer will miss some
      plans.
    */
    base_flags |= HA_KEYREAD_ONLY;
  }
  else
  {
    /*
      We can Index Condition Pushdown any key except the primary. With primary
      key, we get (pk, record) pair immediately, there is no place to put the
      ICP check.
    */
    base_flags |= HA_DO_INDEX_COND_PUSHDOWN;
  }

  return base_flags;
}


/*
  Read next index tuple through the secondary index.

  @detail
    scan_it points at the index key-value pair that we should read the (pk,row)
    pair for.
*/

int ha_rocksdb::secondary_index_read(int keyno, uchar *buf)
{
  stats.rows_requested++;

  if (scan_it->Valid())
  {
    rocksdb::Slice key= scan_it->key();

    /* Check if we've ran out of records of this index */
    if (key_descr[keyno]->covers_key(key.data(), key.size()))
    {
      int rc;

      //TODO: We could here check if we have ran out of range we're scanning
      uint size= key_descr[keyno]->get_primary_key_tuple(table, pk_descr, &key,
                                                         (char*) pk_packed_tuple);
      if (size == (uint)-1)
        return HA_ERR_INTERNAL_ERROR;

      last_rowkey.copy((const char*)pk_packed_tuple, size, &my_charset_bin);

      if (keyread_only && !lock_rows)
      {
        rocksdb::Slice value= scan_it->value();
        if (key_descr[keyno]->unpack_record(table, buf, &key, &value))
          return HA_ERR_INTERNAL_ERROR;
        rc= 0;
      }
      else
        rc= get_row_by_rowid(buf, (const char*)pk_packed_tuple, size);

      if (!rc)
      {
        table->status= 0;
        stats.rows_read++;
        stats.rows_index_next++;
        return rc;
      }
    }
  }
  table->status= STATUS_NOT_FOUND;
  return HA_ERR_END_OF_FILE;
}


/*
   See storage/rocksdb/rocksdb-range-access.txt for description of how MySQL
   index navigation commands are converted into RocksDB lookup commands.
*/

int ha_rocksdb::index_read_map(uchar *buf, const uchar *key,
                               key_part_map keypart_map,
                               enum ha_rkey_function find_flag)
{
  int rc= 0;
  DBUG_ENTER("ha_rocksdb::index_read_map");

  RDBSE_KEYDEF *kd= key_descr[active_index];
  uint actual_key_parts= table->key_info[active_index].actual_key_parts;
  bool using_full_key= (keypart_map == HA_WHOLE_KEY) ||
                       (keypart_map == ((key_part_map(1) << actual_key_parts)
                                         - 1));

  /* By default, we don't need the retrieved records to match the prefix */
  sec_key_match_prefix= NULL;
  stats.rows_requested++;
  io_perf_start();

  if (active_index == table->s->primary_key &&
      find_flag == HA_READ_KEY_EXACT && using_full_key)
  {
    /*
      Equality lookup over primary key, using full tuple.
      This is a special case, use DB::Get.
    */
    uint size= kd->pack_index_tuple(table, pack_buffer, pk_packed_tuple, key,
                                    keypart_map);
    rc= get_row_by_rowid(buf, (const char*)pk_packed_tuple, size);
    if (!rc)
    {
      stats.rows_read++;
      stats.rows_index_first++;
    }
    io_perf_end_and_record();
    DBUG_RETURN(rc);
  }

  /*
    Unique secondary index performs lookups without the extended key fields
  */
  uint packed_size; if (active_index != table->s->primary_key &&
      table->key_info[active_index].flags & HA_NOSAME &&
      find_flag == HA_READ_KEY_EXACT && using_full_key)
  {
    key_part_map tmp_map= (key_part_map(1) <<
                           table->key_info[active_index].user_defined_key_parts) - 1;
    packed_size= kd->pack_index_tuple(table, pack_buffer, sec_key_packed_tuple,
                                      key, tmp_map);
    if (table->key_info[active_index].user_defined_key_parts !=
        kd->get_m_key_parts())
      using_full_key= false;
  }
  else
    packed_size= kd->pack_index_tuple(table, pack_buffer,
                                      sec_key_packed_tuple, key,
                                      keypart_map);
  if ((pushed_idx_cond && pushed_idx_cond_keyno == active_index) &&
      (find_flag == HA_READ_KEY_EXACT || find_flag == HA_READ_PREFIX_LAST))
  {
    /*
      We are doing a point index lookup, and ICP is enabled. It is possible
      that this call will be followed by ha_rocksdb->index_next_same() call.

      Do what InnoDB does: save the lookup tuple now. We will need it in
      index_next_same/find_icp_matching_index_rec in order to stop scanning
      as soon as index record doesn't match the lookup tuple.

      When not using ICP, handler::index_next_same() will make sure that rows
      that don't match the lookup prefix are not returned.
      row matches the lookup prefix.
    */
    sec_key_match_prefix= sec_key_match_prefix_buf;
    sec_key_match_length= packed_size;
    memcpy(sec_key_match_prefix, sec_key_packed_tuple, packed_size);
  }

  if (find_flag == HA_READ_PREFIX_LAST_OR_PREV ||
      find_flag == HA_READ_PREFIX_LAST ||
      find_flag == HA_READ_AFTER_KEY)
  {
    /* See below */
    kd->successor(sec_key_packed_tuple, packed_size);
  }

  rocksdb::Slice slice((char*)sec_key_packed_tuple, packed_size);


  bool use_all_keys= false;
  if (find_flag == HA_READ_KEY_EXACT &&
      my_count_bits(keypart_map) == kd->get_m_key_parts())
    use_all_keys= true;
  rocksdb::Slice rkey;
  /*
    This will open the iterator and position it at a record that's equal or
    greater than the lookup tuple.
  */
  setup_index_scan(kd, &slice, use_all_keys, is_ascending(kd, find_flag));
  bool move_forward= true;

  switch (find_flag) {
  case HA_READ_KEY_EXACT:
  {
    rc = read_key_exact(key_descr[active_index], scan_it, using_full_key,
                        slice);
    if (rc == 0)
      rkey= scan_it->key();
    break;
  }
  case HA_READ_BEFORE_KEY:
  {
    move_forward= false;
    /*
      We are looking for record with the biggest t.key such that
      t.key < lookup_tuple.
    */
    if (key_descr[active_index]->is_reverse_cf)
    {
      rkey= scan_it->key();
      if (scan_it->Valid() && using_full_key &&
          kd->value_matches_prefix(rkey, slice))
      {
        /* We are using full key and we've hit an exact match */
        scan_it->Next();
      }
    }
    else
    {
      if (scan_it->Valid())
        scan_it->Prev();
      else
        scan_it->SeekToLast();
    }

    if (!scan_it->Valid())
      rc= HA_ERR_KEY_NOT_FOUND;
    else
    {
      rkey= scan_it->key();
      if (!kd->covers_key(rkey.data(), rkey.size()))
      {
        /* The record we've got is not from this index */
        rc= HA_ERR_KEY_NOT_FOUND;
      }
    }
    break;
  }
  case HA_READ_AFTER_KEY:
  case HA_READ_KEY_OR_NEXT:
  {
    /*
      We are looking for the first record such that

        index_tuple $GT lookup_tuple

      with HA_READ_AFTER_KEY, $GT = '>',
      with HA_READ_KEY_OR_NEXT, $GT = '>='
    */
    if (key_descr[active_index]->is_reverse_cf)
    {
      if (!scan_it->Valid())
      {
        scan_it->SeekToLast();
      }
      else
      {
        /*
          We should step back
           - when not using full extended key
           - when using full extended key and when we've got an exact match
        */
        rkey= scan_it->key();
        if (!using_full_key || !kd->value_matches_prefix(rkey, slice))
        {
          scan_it->Prev();
        }
      }
    }

    if (!scan_it->Valid())
    {
      rc= HA_ERR_KEY_NOT_FOUND;
    }
    else
    {
      rkey= scan_it->key();
      if (!kd->covers_key(rkey.data(), rkey.size()))
      {
        /* The record we've got is not from this index */
        rc= HA_ERR_KEY_NOT_FOUND;
      }
    }
    break;
  }
  case HA_READ_KEY_OR_PREV:
  case HA_READ_PREFIX:
  {
    /* This flag is not used by the SQL layer, so we don't support it yet. */
    rc= HA_ERR_UNSUPPORTED;
    break;
  }
  case HA_READ_PREFIX_LAST:
  case HA_READ_PREFIX_LAST_OR_PREV:
  {
    move_forward= false;
    /*
      Find the last record with the specified index prefix lookup_tuple.
      - HA_READ_PREFIX_LAST requires that the record has the
        prefix=lookup_tuple (if there are no such records,
        HA_ERR_KEY_NOT_FOUND should be returned).
      - HA_READ_PREFIX_LAST_OR_PREV has no such requirement. If there are no
        records with prefix=lookup_tuple, we should return the last record
        before that.
    */
    if (key_descr[active_index]->is_reverse_cf)
    {
      if (using_full_key && scan_it->Valid())
      {
        rkey= scan_it->key();
        if (kd->value_matches_prefix(rkey, slice))
        {
          scan_it->Next();
        }
      }
    }
    else
    {
      if (!scan_it->Valid())
        scan_it->SeekToLast();
      else
        scan_it->Prev();
    }

    if (scan_it->Valid())
    {
      rc= 0;
      rkey= scan_it->key();
      if (!kd->covers_key(rkey.data(), rkey.size()))
      {
        /* The record we've got is not from this index */
        rc= HA_ERR_KEY_NOT_FOUND;
        break;
      }

      if (find_flag == HA_READ_PREFIX_LAST)
      {
        /*
          We need to compare the key we've got (rkey) with the original
          search prefix.
          We don't have the original search prefix, because we've called
          kd->successor() on it.  We'll need to prepare packed lookup tuple
          again.
        */
        uint size = kd->pack_index_tuple(table, pack_buffer,
                                         sec_key_packed_tuple, key,
                                         keypart_map);
        rocksdb::Slice lookup_tuple((char*)sec_key_packed_tuple, size);
        if (!kd->value_matches_prefix(rkey, lookup_tuple))
        {
          rc= HA_ERR_KEY_NOT_FOUND;
        }
      }
    }
    else
      rc= HA_ERR_KEY_NOT_FOUND;

    break;
  }
  default:
    DBUG_ASSERT(0);
    break;
  }

  if (rc)
  {
    table->status= STATUS_NOT_FOUND; /* Only not found error is possible */
    io_perf_end_and_record();
    DBUG_RETURN(rc);
  }

  skip_scan_it_next_call= FALSE;

  if (active_index == table->s->primary_key)
  {
    uint pk_size= rkey.size();
    memcpy(pk_packed_tuple, rkey.data(), pk_size);
    last_rowkey.copy(rkey.data(), pk_size, &my_charset_bin);
    if (lock_rows)
    {
      /* We need to put a lock and re-read */
      // TODO: what if we find that the row is gone? Need a re-read?
      rc= get_row_by_rowid(buf, (const char*)pk_packed_tuple, pk_size);
    }
    else
    {
      /* Unpack from the row we've read */
      rocksdb::Slice value= scan_it->value();
      rc= convert_record_from_storage_format(&value, buf);
    }
  }
  else
  {
    uint pk_size;
    if (keyread_only && !lock_rows)
    {
      /* Get the key columns and primary key value */
      pk_size= kd->get_primary_key_tuple(table, pk_descr, &rkey,
                                         (char*)pk_packed_tuple);
      rocksdb::Slice value= scan_it->value();
      if (pk_size == INVALID_LEN ||
          kd->unpack_record(table, buf, &rkey, &value))
      {
        rc= HA_ERR_INTERNAL_ERROR;
      }
      else
        rc= 0;
    }
    else
    {
      if (key_descr[active_index]->is_reverse_cf)
        move_forward= !move_forward;

      rc= find_icp_matching_index_rec(move_forward, buf);
      if (!rc)
      {
        rocksdb::Slice cur_key= scan_it->key();
        pk_size= kd->get_primary_key_tuple(table, pk_descr, &cur_key,
                                           (char*) pk_packed_tuple);
        if (pk_size == INVALID_LEN)
          rc= HA_ERR_INTERNAL_ERROR;
        else
          rc= get_row_by_rowid(buf, (const char*)pk_packed_tuple, pk_size);
      }
    }

    if (!rc)
      last_rowkey.copy((const char*)pk_packed_tuple, pk_size, &my_charset_bin);
  }

  if (rc)
  {
    /* the only possible error condition is record-not-found */
    table->status= STATUS_NOT_FOUND;
  }
  else
  {
    table->status= 0;
    stats.rows_read++;
    stats.rows_index_first++;
  }
  io_perf_end_and_record();
  DBUG_RETURN(rc);
}


/*
  @brief
  Scan the secondary index until we find an index record that satisfies ICP

  @param move_forward   TRUE  <=> move scan_it forward
                        FALSE <=> move scan_it backward
  @param buf            Record buffer (must be the same buffer that
                        pushed index condition points to, in practice
                        it is table->record[0])

  @detail
  Move the current iterator scan_it until we get an index tuple that
  satisfies the pushed Index Condition.
  (if there is no pushed index condition, return right away)

  @return
    0     - Index tuple satisfies ICP, can do index read.
    other - error code
*/

int ha_rocksdb::find_icp_matching_index_rec(bool move_forward, uchar *buf)
{
  if (pushed_idx_cond && pushed_idx_cond_keyno == active_index)
  {
    RDBSE_KEYDEF *kd= key_descr[active_index];
    while (1)
    {
      if (!scan_it->Valid())
      {
        table->status= STATUS_NOT_FOUND;
        return HA_ERR_END_OF_FILE;
      }
      rocksdb::Slice rkey= scan_it->key();

      if (!kd->covers_key(rkey.data(), rkey.size()))
      {
        table->status= STATUS_NOT_FOUND;
        return HA_ERR_END_OF_FILE;
      }

      if (sec_key_match_prefix)
      {
        rocksdb::Slice prefix((const char*)sec_key_match_prefix, sec_key_match_length);
        if (!kd->value_matches_prefix(rkey, prefix))
        {
          table->status= STATUS_NOT_FOUND;
          return HA_ERR_END_OF_FILE;
        }
      }

      rocksdb::Slice value= scan_it->value();
      if (kd->unpack_record(table, buf, &rkey, &value))
      {
        return HA_ERR_INTERNAL_ERROR;
      }

      enum icp_result icp_status= check_index_cond();
      if (icp_status == ICP_NO_MATCH)
      {
        if (move_forward)
          scan_it->Next();
        else
          scan_it->Prev();
        continue; /* Get the next (or prev) index tuple */
      }
      else if (icp_status == ICP_OUT_OF_RANGE)
      {
        /* We have walked out of range we are scanning */
        table->status= STATUS_NOT_FOUND;
        return HA_ERR_END_OF_FILE;
      }
      else /* icp_status == ICP_MATCH */
      {
        /* Index Condition is satisfied. We have rc==0, proceed to fetch the row. */
        break;
      }
      /*
        TODO: should we have this here, or RockDB handles this internally?
        if (thd_killed(current_thd))
        {
          rc= HA_ERR_INTERNAL_ERROR; // doesn't matter
          break;
        }
      */
    }
  }
  return 0;
}


int ha_rocksdb::index_read_last_map(uchar * buf, const uchar * key,
                                    key_part_map keypart_map)
{
  return index_read_map(buf, key, keypart_map, HA_READ_PREFIX_LAST);
}


/*
  Print a value into provided buffer, in hex form.
*/
void hexdump_value(char *strbuf, size_t strbuf_size, const rocksdb::Slice &val)
{
  char *p= strbuf;
  strbuf_size -= 3;
  for (size_t i= 0; i < val.size(); i++)
  {
    uint c= (uint)(uchar)val[i];
    DBUG_ASSERT( c < 0x100);
    int len= snprintf(p, strbuf + strbuf_size - p, "%02x", c);
    DBUG_ASSERT(len == 2);
    p += len;
    if (p >= strbuf + strbuf_size - 10)
    {
      // End the value with '..' if we ran out of buffer space
      p[0]= '.';
      p[1]= '.';
      p[2]=0;
      return;
    }
  }
  *p= 0;
}


int ha_rocksdb::check(THD* thd, HA_CHECK_OPT* check_opt)
{
  uint pk= table->s->primary_key;
  String rowkey_copy;
  String sec_key_copy;

  // Only when debugging: don't use snapshot when reading
  // Rdb_transaction *trx= get_or_create_trx(table->in_use);
  // trx->snapshot= NULL;

  /* For each secondary index, check that we can get a PK value from it */
  sql_print_information("Checking table %s", table->s->table_name.str);
  for (uint keyno= 0; keyno < table->s->keys; keyno++)
  {
    if (keyno != pk)
    {
      extra(HA_EXTRA_KEYREAD);
      ha_index_init(keyno, true);
      ha_rows rows= 0;
      int res;
      sql_print_information("  Checking index %s", table->key_info[keyno].name);
      while (1)
      {
        if (!rows)
          res= index_first(table->record[0]);
        else
          res= index_next(table->record[0]);

        if (res == HA_ERR_END_OF_FILE)
          break;
        if (res)
        {
          // error
          sql_print_error("  .. row %lld: index scan error %d", rows, res);
          goto error;
        }
        rocksdb::Slice key= scan_it->key();
        sec_key_copy.copy(key.data(), key.size(), &my_charset_bin);
        rowkey_copy.copy(last_rowkey.ptr(), last_rowkey.length(),
                         &my_charset_bin);
        if ((res= get_row_by_rowid(table->record[0], rowkey_copy.ptr(),
                                   rowkey_copy.length())))
        {
          sql_print_error("  .. row %lld: failed to fetch row by rowid", rows);
          goto error;
        }
        /* Check if we get the same PK value */
        uint packed_size= pk_descr->pack_record(table, pack_buffer, table->record[0],
                                                pk_packed_tuple, NULL, NULL);
        if (packed_size != rowkey_copy.length() ||
            memcmp(pk_packed_tuple, rowkey_copy.ptr(), packed_size))
        {
          sql_print_error("  .. row %lld: PK value mismatch", rows);
          goto print_and_error;
        }

        /* Check if we get the same secondary key value */
        int tail_size;
        packed_size= key_descr[keyno]->pack_record(table, pack_buffer,
                                                   table->record[0],
                                                   sec_key_packed_tuple,
                                                   sec_key_tails, &tail_size);
        if (packed_size != sec_key_copy.length() ||
            memcmp(sec_key_packed_tuple, sec_key_copy.ptr(), packed_size))
        {
          sql_print_error("  .. row %lld: secondary index value mismatch", rows);
          goto print_and_error;
        }
        rows++;
        continue;

print_and_error:
        {
          char buf[1000];
          hexdump_value(buf, 1000, rocksdb::Slice(rowkey_copy.ptr(),
                                                  rowkey_copy.length()));
          sql_print_error("  rowkey: %s", buf);

          hexdump_value(buf, 1000, rocksdb::Slice(retrieved_record.data(),
                                                  retrieved_record.size()));
          sql_print_error("  record: %s", buf);

          hexdump_value(buf, 1000, rocksdb::Slice(sec_key_copy.ptr(),
                                                  sec_key_copy.length()));
          sql_print_error("  index: %s", buf);

          goto error;
        }
      }
      sql_print_information("  ... %lld records found", rows);
      ha_index_end();
    }
  }
  extra(HA_EXTRA_NO_KEYREAD);

  /*
    TODO: we should check also for PK records that are missing in the secondary
    indexes.
    For that, need to walk through the PK and check that every PK record has a
    proper counterpart in each secondary index.
  */
  return HA_ADMIN_OK;
error:
  ha_index_or_rnd_end();
  extra(HA_EXTRA_NO_KEYREAD);
  return HA_ADMIN_CORRUPT;
}


static void dbug_dump_str(FILE *out, const char *str, int len)
{
  fprintf(out, "\"");
  for (int i= 0; i < len; i++)
  {
    if (str[i] > 32)
      fprintf(out, "%c", str[i]);
    else
      fprintf(out, "\\%d", str[i]);
  }
  fprintf(out, "\"");
}


/*
  Debugging help: dump the whole database into a human-readable file.
  Usage:
    dbug_dump_database(rdb);
*/

void dbug_dump_database(rocksdb::DB *db)
{
  FILE *out= fopen("/tmp/rocksdb.dump", "wt");
  if (!out)
    return;

  rocksdb::Iterator* it = db->NewIterator(rocksdb::ReadOptions());
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    rocksdb::Slice key= it->key();
    rocksdb::Slice val= it->value();
    dbug_dump_str(out, key.data(), key.size());
    fprintf(out, " -> ");
    dbug_dump_str(out, val.data(), val.size());
    fprintf(out, "\n");
  }
  //assert(it->status().ok());  // Check for any errors found during the scan
  delete it;
  fclose(out);
}


/*
  Given a rowid (i.e. packed PK) as a parameter, get the record.

  @return
    0      - Ok
    other  - HA_ERR error code.
*/

int ha_rocksdb::get_row_by_rowid(uchar *buf, const char *rowid, uint rowid_size)
{
  int rc;
  DBUG_ENTER("ha_rocksdb::get_row_by_rowid");

  rocksdb::Slice key_slice(rowid, rowid_size);

  Rdb_transaction *trx= get_or_create_trx(table->in_use);
  Row_lock *UNINIT_VAR(lock); // protected by 'if (lock_rows)'
  bool timed_out;
  if (lock_rows)
  {
    if (!(lock= trx->get_lock((const uchar*)rowid, rowid_size, &timed_out)))
      DBUG_RETURN(return_lock_failure(timed_out));
  }

  rocksdb::ReadOptions options;
  if (!lock_rows)
    options.snapshot= trx->snapshot;

  bool found;
  if (!trx->changes.Get(pk_descr, key_slice, &retrieved_record, &found))
  {
    rocksdb::Status s= rdb->Get(options, pk_descr->get_cf(), key_slice,
                                &retrieved_record);
    found= !s.IsNotFound();
  }

  if (found)
  {
    if (lock_rows)
      trx->add_lock(lock);
    last_rowkey.copy((const char*)rowid, rowid_size, &my_charset_bin);
    rc= convert_record_from_storage_format(buf);
    table->status= 0;
  }
  else
  {
    if (lock_rows)
      row_locks.release_lock(trx->pins, lock);
    table->status= STATUS_NOT_FOUND;
    rc= HA_ERR_KEY_NOT_FOUND;
  }

  DBUG_RETURN(rc);
}


int ha_rocksdb::index_next(uchar *buf)
{
  bool moves_forward= true;
  if (key_descr[active_index]->is_reverse_cf)
    moves_forward= false;

  io_perf_start();
  int rc= index_next_with_direction(buf, moves_forward);
  io_perf_end_and_record();

  return rc;
}


int ha_rocksdb::index_prev(uchar *buf)
{
  bool moves_forward= false;
  if (key_descr[active_index]->is_reverse_cf)
    moves_forward= true;

  io_perf_start();
  int rc= index_next_with_direction(buf, moves_forward);
  io_perf_end_and_record();

  return rc;
}


int ha_rocksdb::index_next_with_direction(uchar *buf, bool move_forward)
{
  int rc;
  DBUG_ENTER("ha_rocksdb::index_next");

  if (active_index == table->s->primary_key)
    rc= rnd_next_with_direction(buf, move_forward);
  else
  {
    if (skip_scan_it_next_call)
      skip_scan_it_next_call= FALSE;
    else
    {
      if (move_forward)
        scan_it->Next();  /* this call cannot fail */
      else
        scan_it->Prev();
    }
    rc= find_icp_matching_index_rec(move_forward, buf);
    if (!rc)
      rc= secondary_index_read(active_index, buf);
  }

  DBUG_RETURN(rc);
}


int ha_rocksdb::index_first(uchar *buf)
{
  io_perf_start();
  sec_key_match_prefix= NULL;
  int rc= key_descr[active_index]->is_reverse_cf ? index_last_intern(buf):
                                                   index_first_intern(buf);
  io_perf_end_and_record();
  return rc;
}


int ha_rocksdb::index_last(uchar *buf)
{
  io_perf_start();
  sec_key_match_prefix= NULL;
  int rc= key_descr[active_index]->is_reverse_cf ? index_first_intern(buf):
                                                   index_last_intern(buf);
  io_perf_end_and_record();
  return rc;
}


/*
  Start scanning from the "first" value.

  The 'first' here means "the first from start of the key space".
  For reverse-ordered key spaces, the first value will be the biggest, 'zzzz'.

  An picture of a forward-ordered keyspace (remember, the keys have form
  'indexnr-keyval'. Suppose the index we are at has number n)

      (n-1) - ...
      ( n )          <--- 1. (n) doesn't exist in the db but it would be here.
      ( n ) - aaa       <--- 2. Seek("n") will put us here on the first index
      ( n ) - bbb               record.
      ( n ) - cc

  So, need to do: Seek(n);

  A backward-ordered keyspace:

      (n+1) - bbb
      (n+1) - aaa
      (n+1)        <--- (n+1) doesn't exist in the db but would be here.
      ( n ) - ccc       <--- 1. We need to be here.
      ( n ) - bbb
      ( n ) - aaa
      ( n )

  So, need to: Seek(n+1);

*/

int ha_rocksdb::index_first_intern(uchar *buf)
{
  uchar *key;
  uint key_size;
  int rc;
  DBUG_ENTER("ha_rocksdb::index_first");

  key= (active_index == table->s->primary_key)? pk_packed_tuple :
                                                sec_key_packed_tuple;

  if (key_descr[active_index]->is_reverse_cf)
    key_descr[active_index]->get_supremum_key(key, &key_size);
  else
    key_descr[active_index]->get_infimum_key(key, &key_size);

  rocksdb::Slice index_key((const char*)key, key_size);

  setup_index_scan(key_descr[active_index], &index_key);
  skip_scan_it_next_call= TRUE;

  rc= index_next_with_direction(buf, true);

  if (!rc)
  {
    /*
      index_next is always incremented on success, so decrement if it is
      index_first instead
     */
    stats.rows_index_first++;
    stats.rows_index_next--;
  }
  DBUG_RETURN(rc);
}


/*
  Start scanning from the "last" value

  The 'last' here means "the last from start of the key space".
  For reverse-ordered key spaces, we will actually read the smallest value.

  An picture of a forward-ordered keyspace (remember, the keys have form
  'indexnr-keyval'. Suppose the we are at a key that has number n)

     (n-1)-something
     ( n )-aaa
     ( n )-bbb
     ( n )-ccc            <----------- Need to seek to here.
     (n+1)      <---- Doesn't exist, but would be here.
     (n+1)-smth, or no value at all

   RocksDB's Iterator::Seek($val) seeks to "at $val or first value that's
   greater". We can't see to "(n)-ccc" directly, because we don't know what
   is the value of 'ccc' (the biggest record with prefix (n)). Instead, we seek
   to "(n+1)", which is the least possible value that's greater than any value
   in index #n. Then we step one record back.

   So, need to:  it->Seek(n+1) || it->SeekToLast(); it->Prev();

   A backward-ordered keyspace:

      (n+1)-something
      ( n ) - ccc
      ( n ) - bbb
      ( n ) - aaa       <---------------- (*) Need to seek here.
      ( n ) <--- Doesn't exist, but would be here.
      (n-1)-smth, or no value at all

   So, need to:  it->Seek(n) || it->SeekToLast(); it->Prev();
*/

int ha_rocksdb::index_last_intern(uchar *buf)
{
  uchar *key;
  uint key_size;
  int rc;
  DBUG_ENTER("ha_rocksdb::index_last");

  key= (active_index == table->s->primary_key)? pk_packed_tuple :
                                                sec_key_packed_tuple;

  if (key_descr[active_index]->is_reverse_cf)
    key_descr[active_index]->get_infimum_key(key, &key_size);
  else
    key_descr[active_index]->get_supremum_key(key, &key_size);

  rocksdb::Slice index_key((const char*)key, key_size);

  setup_index_scan(key_descr[active_index], &index_key);

  if (!scan_it->Valid())
  {
    /*
      RocksDB keyspace has no records that follow our index. It could be, our
      index is the last in the keyspace. Navigate to last record in the
      keyspace.
    */
    scan_it->SeekToLast();
  }
  else
  {
    /*
      Iterator is pointing at some record right after the end of the index.
      Move one record back.
    */
    scan_it->Prev();
  }
  skip_scan_it_next_call= FALSE;

  if (active_index == table->s->primary_key)
  {
    skip_scan_it_next_call= true;
    rc= rnd_next_with_direction(buf, false);
  }
  else
  {
    rc= find_icp_matching_index_rec(false /*move_forward*/, buf);
    if (!rc)
      rc= secondary_index_read(active_index, buf);
  }

  if (!rc)
  {
    /*
      index_next is always incremented on success, so decrement if it is
      index_first instead
     */
    stats.rows_index_first++;
    stats.rows_index_next--;
  }
  DBUG_RETURN(rc);
}


void ha_rocksdb::unlock_row()
{
  if (lock_rows)
  {
    Rdb_transaction *trx= get_or_create_trx(table->in_use);
    trx->release_last_lock();
  }
}


int ha_rocksdb::write_row(uchar *buf)
{
  DBUG_ENTER("ha_rocksdb::write_row");
  DBUG_ASSERT(buf == table->record[0]);
  DBUG_ASSERT(lock_rows);

  io_perf_start();

  /*
    Note: "buf == table->record[0]" is copied from innodb. I am not aware of
    any use cases where this condition is not true.
  */
  if (table->next_number_field && buf == table->record[0])
  {
    int err;
    if ((err= update_auto_increment()))
    {
      io_perf_end_and_record();
      DBUG_RETURN(err);
    }
  }

  bool do_locking= !THDVAR(table->in_use, bulk_load);
  int rv= update_write_row(NULL, buf, do_locking);
  Rdb_transaction *trx= get_or_create_trx(table->in_use);

  if (rv == 0 && !do_locking &&
      trx->n_writes > THDVAR(table->in_use, bulk_load_size))
  {
    /* Bulk insert mode: flush the write batch if it became too big */
    if (trx->flush_batch(table->in_use, false))
      rv= HA_ERR_INTERNAL_ERROR;
  }

  if (rv == 0)
    stats.rows_inserted++;
  io_perf_end_and_record();
  DBUG_RETURN(rv);
}

int ha_rocksdb::update_write_row(const uchar *old_data,
                                 uchar *new_data,
                                 bool do_locking)
{
  int locks_added= 0;

  int pk_packed_size;
  bool pk_key_changed= false;

  int new_packed_size;
  uchar *new_key_packed_tuple;
  int new_key_tail_size;

  int old_packed_size;
  int old_key_tail_size;

  rocksdb::Slice old_pk_key_slice;
  rocksdb::Slice new_key_slice;
  rocksdb::Slice new_value_slice;
  rocksdb::Slice old_key_slice;

  DBUG_ENTER("ha_rocksdb::write_row_common");

  Rdb_transaction *trx= get_or_create_trx(table->in_use);

  trx->prepare_for_write();

  /* Get a rowkey for the new record */
  pk_packed_size= pk_descr->pack_record(table, pack_buffer, new_data,
                                        pk_packed_tuple, NULL, NULL);
  if (old_data)
  {
    old_pk_key_slice= rocksdb::Slice(last_rowkey.ptr(), last_rowkey.length());

    /* Determine which indexes need updating. */
    calc_updated_indexes();
  }

  /*
    Go through each index and determine if the index has uniqueness
    requirements. If it does, then try to obtain a row lock on the new values.
    Once all locks have been obtained, then perform the changes needed to
    update/insert the row.
  */
  for (uint i= 0; i < table->s->keys; i++)
  {
    uint n_null_fields= 0;
    uint user_defined_key_parts= table->key_info[i].user_defined_key_parts;

    /*
      If there are no uniqueness requirements, there's no need to obtain a
      lock for this key. The primary key should have this flag set.
    */
    if (!do_locking || !(table->key_info[i].flags & HA_NOSAME))
      continue;

    /*
      For UPDATEs, if the key has changed, we need to obtain a lock. INSERTs
      are always require locking.
    */
    if (old_data)
    {
      if (i == table->s->primary_key)
        old_key_slice= old_pk_key_slice;
      else
      {
        if (!updated_indexes.is_set(i))
          continue;

        old_packed_size= key_descr[i]->pack_record(table, pack_buffer,
                                                   old_data,
                                                   sec_key_packed_tuple_old,
                                                   NULL,
                                                   NULL,
                                                   user_defined_key_parts);
        old_key_slice= rocksdb::Slice((const char*)sec_key_packed_tuple_old,
                                      old_packed_size);
      }
    }

    /*
      Calculate the new key for obtaining the lock
    */
    if (i == table->s->primary_key)
    {
      new_key_packed_tuple= pk_packed_tuple;
      new_packed_size= pk_packed_size;
    }
    else
    {
      /*
        For unique secondary indexes, the key used for locking does not
        include the extended fields.
      */
      new_packed_size= key_descr[i]->pack_record(table, pack_buffer, new_data,
                                                 sec_key_packed_tuple,
                                                 NULL, NULL,
                                                 user_defined_key_parts,
                                                 &n_null_fields);
      new_key_packed_tuple = sec_key_packed_tuple;
    }
    new_key_slice= rocksdb::Slice((const char*)new_key_packed_tuple,
                                   new_packed_size);

    /*
      For updates, if the keys are the same, then no lock is needed
      For inserts, the old_key_slice is "", so this check should always fail.

      Also check to see if the key has any fields set to NULL. If it does, then
      this key is unique since NULL is not equal to each other, so no lock is
      needed. Primary keys never have NULLable fields.
    */
    if (!rocksdb_pk_comparator.Compare(new_key_slice, old_key_slice) ||
        n_null_fields > 0)
      continue;

    /*
      Get a record lock to make sure we do not overwrite somebody's changes.
    */
    bool timed_out;
    Row_lock *lock= NULL; // init to shut up the compiler
    if (!(lock= trx->get_lock(new_key_packed_tuple, new_packed_size,
                              &timed_out)))
    {
      /*
        On failure, need to unlock all locks obtained during this process.
      */
      while (--locks_added >= 0)
        trx->release_last_lock();
      DBUG_RETURN(return_lock_failure(timed_out));
    }

    trx->add_lock(lock);
    locks_added++;

    /*
      Perform a read to determine if a duplicate entry exists. For primary
      keys, a point lookup will be sufficient. For secondary indexes, a
      range scan is needed.

      note: we intentionally don't set options.snapshot here. We want to read
      the latest committed data.
    */
    rocksdb::ReadOptions options;

    bool found;
    if (i == table->s->primary_key)
    {
      /*
        Primary key has changed, it should be deleted later.
      */
      if (old_data)
        pk_key_changed= true;

      if (!trx->changes.Get(pk_descr, new_key_slice, &retrieved_record, &found))
      {
        rocksdb::Status s= rdb->Get(options, pk_descr->get_cf(), new_key_slice,
                                    &retrieved_record);
        found= !s.IsNotFound();
      }
    }
    else
    {
      Apply_changes_iter iter;
      bool all_parts_used= (user_defined_key_parts ==
                            key_descr[i]->get_m_key_parts());

      /*
        This iterator seems expensive since we need to allocate and free
        memory for each unique index.

        If this needs to be optimized, for keys without NULL fields, the
        extended primary key fields can be migrated to the value portion of the
        key. This enables using Get() instead of Seek() as in the primary key
        case.

        The bloom filter may need to be disabled for this lookup.
      */
      if (!can_use_bloom_filter(key_descr[i], new_key_slice,
                                all_parts_used,
                                is_ascending(key_descr[i], HA_READ_KEY_EXACT)))
        options.total_order_seek= true;
      iter.init(key_descr[i]->is_reverse_cf,
                &trx->changes,
                rdb->NewIterator(options, key_descr[i]->get_cf()));

      /*
        Need to scan the transaction to see if there is a duplicate key.
        Also need to scan RocksDB and verify the key has not been deleted
        in the transaction.
      */
      iter.Seek(new_key_slice);
      found= !read_key_exact(key_descr[i], &iter, all_parts_used,
                             new_key_slice);
    }

    if (found)
    {
      /*
        There is a row with this rowid already, so error out.
        Need to unlock all locks obtained during this process.
      */
      while (--locks_added >= 0)
        trx->release_last_lock();
      dupp_errkey= i;
      DBUG_RETURN(HA_ERR_FOUND_DUPP_KEY);
    }
  }

  /*
    At this point, all locks have been obtained, and all checks for duplicate
    keys have been performed. No further errors can be allowed to occur from
    here because updates to the transaction will be made and those updates
    cannot be easily removed without rolling back the entire transaction.
  */
  for (uint i= 0; i < table->s->keys; i++)
  {
    /*
      Determine if the old key needs to be deleted.
    */
    if (i == table->s->primary_key)
    {
      if (pk_key_changed)
        trx->changes.Delete(key_descr[i], old_pk_key_slice);
      continue;
    }

    /*
      Can skip updating this key if none of the key fields have changed.
    */
    if (old_data && !updated_indexes.is_set(i))
      continue;

    new_packed_size= key_descr[i]->pack_record(table, pack_buffer,
                                               new_data,
                                               sec_key_packed_tuple,
                                               sec_key_tails,
                                               &new_key_tail_size);
    new_key_slice= rocksdb::Slice((const char*)sec_key_packed_tuple,
                                   new_packed_size);
    new_value_slice= rocksdb::Slice((const char*)sec_key_tails,
                                    new_key_tail_size);

    if (old_data)
    {
      // The old value (just used for delete)
      old_packed_size= key_descr[i]->pack_record(table, pack_buffer,
                                                 old_data,
                                                 sec_key_packed_tuple_old,
                                                 sec_key_tails_old,
                                                 &old_key_tail_size);
      old_key_slice= rocksdb::Slice((const char*)sec_key_packed_tuple_old,
                                    old_packed_size);

      /*
        Check if we are going to write the same value. This can happen when
        one does
          UPDATE tbl SET col='foo'
        and we are looking at the row that already has col='foo'.

        We also need to compare the unpack info. Suppose, the collation is
        case-insensitive, and unpack info contains information about whether
        the letters were uppercase and lowercase.  Then, both 'foo' and 'FOO'
        will have the same key value, but different data in unpack_info.

        (note: anyone changing bytewise_compare should take this code into
        account)
      */
      if (old_packed_size == new_packed_size &&
          memcmp(sec_key_packed_tuple_old, sec_key_packed_tuple,
                 old_packed_size) == 0 &&
          old_key_tail_size == new_key_tail_size &&
          memcmp(sec_key_tails_old, sec_key_tails, old_key_tail_size) == 0)
        continue;

      trx->changes.Delete(key_descr[i], old_key_slice);
    }

    trx->changes.Put(key_descr[i], new_key_slice, new_value_slice);
    trx->n_writes++;
  }

  if (table->next_number_field)
    update_auto_incr_val();

  /* Write primary_key -> record */
  new_key_slice= rocksdb::Slice((const char*)pk_packed_tuple, pk_packed_size);
  rocksdb::Slice value_slice;
  convert_record_to_storage_format(&value_slice);
  trx->changes.Put(pk_descr, new_key_slice, value_slice);
  trx->n_writes++;

  DBUG_RETURN(0);
}


/*
  Open a cursor and position it at the passed record
*/

void ha_rocksdb::setup_index_scan(RDBSE_KEYDEF *keydef,
                                  rocksdb::Slice *slice,
                                  bool use_all_keys,
                                  bool is_ascending)
{
  Rdb_transaction *trx= get_or_create_trx(table->in_use);

  /*
    SQL layer can call rnd_init() multiple times in a row.
    In that case, re-use the iterator, but re-position it at the table start.
  */
  if (!scan_it)
  {
    rocksdb::ReadOptions options;
    if (THDVAR(ha_thd(), skip_fill_cache))
      options.fill_cache= false;
    if (!lock_rows)
      options.snapshot= trx->snapshot;
    if (!can_use_bloom_filter(keydef, *slice, use_all_keys, is_ascending))
      options.total_order_seek= true;
    rocksdb::Iterator* rocksdb_it= rdb->NewIterator(options, keydef->get_cf());
    scan_it= new Apply_changes_iter;
    scan_it->init(keydef->is_reverse_cf, &trx->changes, rocksdb_it);
  }

  /*
    Seek() will "Position at the first key in the source that at or past target".
    The operation cannot fail.
  */
  scan_it->Seek(*slice);
}


int ha_rocksdb::rnd_init(bool scan)
{
  DBUG_ENTER("ha_rocksdb::rnd_init");

  io_perf_start();
  if (scan)
  {
    uint key_size;
    if (pk_descr->is_reverse_cf)
      pk_descr->get_supremum_key(pk_packed_tuple, &key_size);
    else
      pk_descr->get_infimum_key(pk_packed_tuple, &key_size);

    rocksdb::Slice table_key((const char*)pk_packed_tuple, key_size);

    setup_index_scan(pk_descr, &table_key);
    skip_scan_it_next_call= true;
  }
  else
  {
    /* We don't need any preparations for rnd_pos() calls. */
  }
  io_perf_end_and_record();

  DBUG_RETURN(0);
}


int ha_rocksdb::rnd_next(uchar *buf)
{
  io_perf_start();
  int rc= rnd_next_with_direction(buf, true);
  io_perf_end_and_record();
  return rc;
}


/*
  See also secondary_index_read().
*/
int ha_rocksdb::rnd_next_with_direction(uchar *buf, bool move_forward)
{
  int rc;
  DBUG_ENTER("ha_rocksdb::rnd_next");

  stats.rows_requested++;

  if (!scan_it)
  {
    /*
      We can get here when SQL layer has called

        h->index_init(PRIMARY);
        h->index_read_map(full index tuple, HA_READ_KEY_EXACT);

      In this case, we should return EOF.
    */
    table->status= STATUS_NOT_FOUND;
    io_perf_end_and_record();
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }

retry:
  if (skip_scan_it_next_call)
    skip_scan_it_next_call= false;
  else
  {
    if (move_forward)
      scan_it->Next();  /* this call cannot fail */
    else
      scan_it->Prev();  /* this call cannot fail */
  }

  if (scan_it->Valid())
  {
    /* check if we're out of this table */
    rocksdb::Slice key= scan_it->key();
    if (!pk_descr->covers_key(key.data(), key.size()))
    {
      rc= HA_ERR_END_OF_FILE;
      table->status= STATUS_NOT_FOUND;
    }
    else
    {
      if (lock_rows)
      {
        Rdb_transaction *trx= get_or_create_trx(table->in_use);
        /*
          Here is a problem. We've read a row without locking it.
          We could not lock the row before reading it, because we didn't know
          its rowkey.

          Current solution is: lock the row and re-read it.
        */
        Row_lock *lock;
        bool timed_out;
        if (!(lock= trx->get_lock((const uchar*)key.data(), key.size(), &timed_out)))
        {
          io_perf_end_and_record();
          DBUG_RETURN(return_lock_failure(timed_out));
        }

        rocksdb::ReadOptions options;
        /* lock_rows==TRUE, so not setting options.snapshot */
        bool found;
        if (!trx->changes.Get(pk_descr, key, &retrieved_record, &found))
        {
          rocksdb::Status s= rdb->Get(options, pk_descr->get_cf(), key,
                                      &retrieved_record);
          found= !s.IsNotFound();
        }

        if (!found)
        {
          /* The row is not there already. Try another one */
          row_locks.release_lock(trx->pins, lock);
          goto retry;
        }
        trx->add_lock(lock);

        last_rowkey.copy(key.data(), key.size(), &my_charset_bin);
        rc= convert_record_from_storage_format(buf);
        table->status= 0;
      }
      else
      {
        last_rowkey.copy(key.data(), key.size(), &my_charset_bin);
        rocksdb::Slice value= scan_it->value();
        rc= convert_record_from_storage_format(&value, buf);
        table->status= 0;
      }
    }
  }
  else
    rc= HA_ERR_END_OF_FILE;

  if (!rc) {
    stats.rows_read++;
    stats.rows_index_next++;
  }

  DBUG_RETURN(rc);
}


int ha_rocksdb::rnd_end()
{
  DBUG_ENTER("ha_rocksdb::rnd_end");

  if (scan_it)
  {
    delete scan_it;
    scan_it= NULL;
  }
  DBUG_RETURN(0);
}


int ha_rocksdb::index_end()
{

  if (scan_it)
  {
    delete scan_it;
    scan_it= NULL;
  }

  active_index= MAX_KEY;
  in_range_check_pushed_down= FALSE;
  return 0;
}


int ha_rocksdb::delete_all_rows(ha_rows* nrows)
{
  DBUG_ENTER("ha_rocksdb::delete_all_rows");
  /*
    We will have to delete records one-by-one anyway. Let the SQL layer do
    it.
  */
  DBUG_RETURN(HA_ERR_WRONG_COMMAND);
}

int ha_rocksdb::truncate()
{
  mysql_mutex_lock(&tbl_def->mutex);
  tbl_def->auto_incr_val= 1;
  mysql_mutex_unlock(&tbl_def->mutex);
  remove_rows(tbl_def);
  return 0;
}

/*
  Delete the row we've last read. Ignore the parameter.
  (Check out InnoDB: row_update_for_mysql() has "UT_NOT_USED(mysql_rec)")
*/
int ha_rocksdb::delete_row(const uchar *buf)
{
  DBUG_ENTER("ha_rocksdb::delete_row");

  io_perf_start();

  rocksdb::Slice key_slice(last_rowkey.ptr(), last_rowkey.length());
  Rdb_transaction *trx= get_or_create_trx(table->in_use);
  trx->prepare_for_write();
  trx->changes.Delete(pk_descr, key_slice);
  DBUG_ASSERT(buf == table->record[0]);

  // Delete the record for every secondary index
  for (uint i= 0; i < table->s->keys; i++)
  {
    if (i != table->s->primary_key)
    {
      int packed_size;
      packed_size= key_descr[i]->pack_record(table, pack_buffer,
                                             buf, sec_key_packed_tuple,
                                             NULL, NULL);
      rocksdb::Slice secondary_key_slice((char*)sec_key_packed_tuple,
                                         packed_size);
      trx->changes.Delete(key_descr[i], secondary_key_slice);
    }
  }

  stats.rows_deleted++;
  io_perf_end_and_record();

  DBUG_RETURN(0);
}


int ha_rocksdb::info(uint flag)
{
  DBUG_ENTER("ha_rocksdb::info");

  if (!table)
    return 1;

  if (flag & HA_STATUS_VARIABLE)
  {
    stats.records= 0;
    stats.index_file_length= 0ul;
    stats.data_file_length= 0ul;
    for (uint i=0; i < table->s->keys; i++)
    {
      if (i == table->s->primary_key)
      {
        stats.data_file_length= key_descr[i]->stats.approximate_size;
        stats.records = key_descr[i]->stats.rows;
      }
      else
      {
        stats.index_file_length+= key_descr[i]->stats.approximate_size;
      }
    }
    if (stats.records == 0)
      stats.records = rocksdb_debug_optimizer_n_rows;
  }
  if (flag & HA_STATUS_CONST)
  {
    DBUG_ASSERT(table->s->primary_key != MAX_KEY);

    ref_length= pk_descr->max_storage_fmt_length();

    // TODO: Needs to reimplement after having real index statistics
    for (uint i= 0; i < table->s->keys; i++)
    {
      for (uint j = 0; j < table->key_info[i].actual_key_parts; j++)
      {
        uint x = key_descr[i]->stats.distinct_keys_per_prefix.size() > j && key_descr[i]->stats.distinct_keys_per_prefix[j] > 0 ?
          key_descr[i]->stats.rows / key_descr[i]->stats.distinct_keys_per_prefix[j] :
          0;
        if (x > stats.records)
          x = stats.records;
        if (x == 0 && rocksdb_debug_optimizer_no_zero_cardinality)
        {
          // Fake cardinality implementation. For example, (idx1, idx2, idx3) index
          // will have rec_per_key for (idx1)=4, (idx1,2)=2, and (idx1,2,3)=1.
          // rec_per_key for the whole index is 1, and multiplied by 2^n if
          // n suffix columns of the index are not used.
          x = 1 << (table->key_info[i].actual_key_parts-j-1);
        }
        table->key_info[i].rec_per_key[j]= x;
      }
    }
  }

  if (flag & HA_STATUS_ERRKEY)
  {
    /*
      Currently we support only primary keys so we know which key had a
      uniqueness violation.
    */
    errkey= dupp_errkey;
    dup_ref= pk_tuple; //TODO: this should store packed PK.
  }

  if (flag & HA_STATUS_AUTO)
  {
    stats.auto_increment_value= tbl_def->auto_incr_val;
  }

  DBUG_RETURN(0);
}


void ha_rocksdb::position(const uchar *record)
{
  DBUG_ENTER("ha_rocksdb::position");
  /*
    Get packed primary key value from the record.
    (Note: last_rowkey has the packed PK of last-read-row, which allows to
    handle most cases, but there is an exception to this: when slave applies
    RBR events, it fills the record and then calls position(); rnd_pos())
  */
  pk_descr->pack_record(table, pack_buffer, record, ref,
                        NULL, NULL);
  DBUG_VOID_RETURN;
}


int ha_rocksdb::rnd_pos(uchar *buf, uchar *pos)
{
  int rc;
  DBUG_ENTER("ha_rocksdb::rnd_pos");

  rc= get_row_by_rowid(buf, (const char*)pos, ref_length);

  DBUG_RETURN(rc);
}


int ha_rocksdb::return_lock_failure(bool timed_out)
{
  THD *thd;
  if ((thd= table->in_use))
  {
    thd_mark_transaction_to_rollback(thd, false);
  }
  return timed_out? HA_ERR_LOCK_WAIT_TIMEOUT : HA_ERR_ROCKSDB_TOO_MANY_LOCKS;
}


/*
  @brief
    Calculate (if needed) the bitmap of indexes that are modified by the
    current query

  @detail
    The calculation done by checking index definitions against the table->write_set
    column bitmap.
*/

void ha_rocksdb::calc_updated_indexes()
{
  if (!updated_indexes_is_valid)
  {
    updated_indexes_is_valid= true;
    updated_indexes.clear_all();

    for (uint keynr= 0; keynr < table->s->keys; keynr++)
    {
      KEY_PART_INFO *parts_info= table->key_info[keynr].key_part;
      /* Walk over all key parts, including the "extended key" suffix */
      uint key_parts= table->key_info[keynr].actual_key_parts;
      for (uint kp= 0; kp < key_parts; kp++)
      {
        if (bitmap_is_set(table->write_set, parts_info[kp].field->field_index))
        {
          updated_indexes.set_bit(keynr);
          break;
        }
      }
    }
  }
}


int ha_rocksdb::update_row(const uchar *old_data, uchar *new_data)
{
  DBUG_ENTER("ha_rocksdb::update_row");
  DBUG_ASSERT(lock_rows);
  /*
    old_data points to record we're updating. It is the same as the record
    we've just read (for multi-table UPDATE, too, because SQL layer will make
    an rnd_pos() call to re-read the record before calling update_row())
  */
  DBUG_ASSERT(new_data == table->record[0]);

  io_perf_start();

  //psergey-todo: how come we don't get locks for the 'old_data'?  because
  // we've already taken them when reading??

  int rv= update_write_row(old_data, new_data, true);
  io_perf_end_and_record();

  if (rv == 0)
    stats.rows_updated++;
  DBUG_RETURN(rv);
}


/* The following function was copied from ha_blackhole::store_lock: */
THR_LOCK_DATA **ha_rocksdb::store_lock(THD *thd,
                                         THR_LOCK_DATA **to,
                                         enum thr_lock_type lock_type)
{
  DBUG_ENTER("ha_rocksdb::store_lock");
  if (lock_type != TL_IGNORE && lock.type == TL_UNLOCK)
  {
    /*
      Here is where we get into the guts of a row level lock.
      If TL_UNLOCK is set
      If we are not doing a LOCK TABLE or DISCARD/IMPORT
      TABLESPACE, then allow multiple writers
    */

    if ((lock_type >= TL_WRITE_CONCURRENT_INSERT &&
         lock_type <= TL_WRITE) && !thd_in_lock_tables(thd)
        && !thd_tablespace_op(thd))
      lock_type = TL_WRITE_ALLOW_WRITE;

    /*
      In queries of type INSERT INTO t1 SELECT ... FROM t2 ...
      MySQL would use the lock TL_READ_NO_INSERT on t2, and that
      would conflict with TL_WRITE_ALLOW_WRITE, blocking all inserts
      to t2. Convert the lock to a normal read lock to allow
      concurrent inserts to t2.
    */

    if (lock_type == TL_READ_NO_INSERT && !thd_in_lock_tables(thd))
      lock_type = TL_READ;

    lock.type= lock_type;
  }
  *to++= &lock;
  DBUG_RETURN(to);
}


int ha_rocksdb::external_lock(THD *thd, int lock_type)
{
  int res= 0;
  DBUG_ENTER("ha_rocksdb::external_lock");
  if (lock_type == F_UNLCK)
  {
    Rdb_transaction* trx= get_or_create_trx(thd);
    if (!thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN))
    {
      /*
        Do like InnoDB: when we get here, it's time to commit a
        single-statement transaction.

        If the statement involved multiple tables, this code will be executed
        for each of them, but that's ok because non-first trx->commit() calls
        will be no-ops.
      */
      res= trx->commit(thd);
      delete scan_it;
      scan_it= NULL;
    }
    else if (thd_tx_isolation(thd) <= ISO_READ_COMMITTED)
    {
      if (trx->snapshot)
      {
        rdb->ReleaseSnapshot(trx->snapshot);
        trx->snapshot= NULL;
      }
    }
    lock_rows= FALSE;
  }
  else
  {
    /*
      It's nice to do the following on start of every statement. The problem
      is, handler->start_stmt() is not called for INSERTs.
      So, we put this code here.
    */
    Rdb_transaction* trx= get_or_create_trx(thd);
    trx->trx_locks_at_stmt_start= trx->trx_locks.elements();

    trx->set_params(THDVAR(table->in_use, lock_wait_timeout),
                    THDVAR(table->in_use, max_row_locks));

    updated_indexes_is_valid= false;

    if (lock_type == F_WRLCK)
    {
      /*
        SQL layer signals us to take a write lock. It does so when starting DML
        statement. We should put locks on the rows we're reading.
      */
      lock_rows= TRUE;
    }

    trans_register_ha(thd, FALSE, rocksdb_hton);
    if (thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN))
    {
      trx->start_stmt();
      trans_register_ha(thd, TRUE, rocksdb_hton);
    }
  }

  DBUG_RETURN(res);
}


/*
  @note
  A quote from ha_innobase::start_stmt():
  <quote>
  MySQL calls this function at the start of each SQL statement inside LOCK
  TABLES. Inside LOCK TABLES the ::external_lock method does not work to
  mark SQL statement borders.
  </quote>
*/

int ha_rocksdb::start_stmt(THD *thd, thr_lock_type lock_type)
{
  if (thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN))
  {
    Rdb_transaction* trx= get_or_create_trx(thd);
    trx->set_params(THDVAR(table->in_use, lock_wait_timeout),
                    THDVAR(table->in_use, max_row_locks));
    trx->start_stmt();
  }
  return 0;
}

static rocksdb::Range get_range(
  RDBSE_KEYDEF* keydef,
  uchar buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*2],
  int offset1, int offset2)
{
  uchar* buf_begin = buf;
  uchar* buf_end = buf+RDBSE_KEYDEF::INDEX_NUMBER_SIZE;
  store_index_number(buf_begin, keydef->get_index_number() + offset1);
  store_index_number(buf_end, keydef->get_index_number() + offset2);

  return rocksdb::Range(
    rocksdb::Slice((const char*) buf_begin, RDBSE_KEYDEF::INDEX_NUMBER_SIZE),
    rocksdb::Slice((const char*) buf_end, RDBSE_KEYDEF::INDEX_NUMBER_SIZE));
}

rocksdb::Range get_range(
  RDBSE_KEYDEF* keydef, uchar buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*2])
{
  if (keydef->is_reverse_cf)
    return ::get_range(keydef, buf, 1, 0);
  else
    return ::get_range(keydef, buf, 0, 1);
}

rocksdb::Range ha_rocksdb::get_range(
  int i, uchar buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*2]) const
{
  return ::get_range(key_descr[i], buf);
}

void signal_drop_index_thread(bool stop_thread)
{
  mysql_mutex_lock(&drop_index_interrupt_mutex);
  if (stop_thread) {
    stop_drop_index_thread = true;
  }
  mysql_cond_signal(&drop_index_interrupt_cond);
  mysql_mutex_unlock(&drop_index_interrupt_mutex);
}

void* drop_index_thread(void*)
{
  mysql_mutex_lock(&drop_index_mutex);
  mysql_mutex_lock(&drop_index_interrupt_mutex);

  for (;;) {
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += dropped_indices_manager.empty()
      ? 24*60*60 // no filtering
      : 60; // filtering
    auto ret = mysql_cond_timedwait(&drop_index_interrupt_cond,
                                    &drop_index_interrupt_mutex, &ts);
    if (stop_drop_index_thread) {
      break;
    }
    // make sure, no program error is returned
    assert(ret == 0 || ret == ETIMEDOUT);

    Dropped_Index_Map indices = dropped_indices_manager.get_indices();
    if (!indices.empty()) {
      std::unordered_set<uint32> finished;
      uchar buf[2 * RDBSE_KEYDEF::INDEX_NUMBER_SIZE];
      rocksdb::ReadOptions read_opts;
      read_opts.total_order_seek = true; // disable bloom filter

      for (auto d : indices) {
        std::unique_ptr<rocksdb::Iterator> it(
          rdb->NewIterator(read_opts, d.second->get_cf()));

        rocksdb::Range r = get_range(d.second, buf,
                                     d.second->is_reverse_cf ? 1 : 0, 0);

        // look for entry with index's prefix
        // if none exists, we're finished with this index
        it->Seek(r.start);
        if (!it->Valid()) {

          // for reverse column families, results are actually before the index,
          // so we seek to index+1, then try to find index.
          if (d.second->is_reverse_cf
              && rocksdb::Slice(it->key()).starts_with(r.start)) {
            // if 4 byte key with r.start does exist, then we need to seek next.
            // if it doesn't exist, then the iterator should already be pointing
            // to the first key in r.limit.
            it->Next();
          }

          // at this point, the iterator will point to an entry for the index,
          // if one exists.
        }

        if (!it->Valid() || !rocksdb::Slice(it->key()).starts_with(r.limit)) {
          finished.insert(d.first);
        }
      }

      if (!finished.empty()) {
        dropped_indices_manager.remove_indices(finished, &dict_manager);
      }
    }
  }

  mysql_mutex_unlock(&drop_index_interrupt_mutex);
  mysql_mutex_unlock(&drop_index_mutex);

  return nullptr;
}

/*
  Note: the following function is called when the table is not open. That is,
  this->table==NULL, pk_key_descr==NULL, etc.

  tablename points to line in form "./dbname/tablename".
*/

int ha_rocksdb::delete_table(const char *tablename)
{
  StringBuffer<256> strbuf;
  DBUG_ENTER("ha_rocksdb::delete_table");
  std::unique_ptr<rocksdb::WriteBatch> wb= dict_manager.begin();
  rocksdb::WriteBatch *batch= wb.get();

  if (rocksdb_normalize_tablename(tablename, &strbuf))
  {
    DBUG_ASSERT(0); // We were not passed table name?
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }


  /* Find the table in the hash */
  RDBSE_TABLE_DEF *tbl;
  if (!(tbl= ddl_manager.find((uchar*)strbuf.c_ptr(), strbuf.length())))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  dropped_indices_manager.add_indices(tbl->key_descr, tbl->n_keys, batch);

  signal_drop_index_thread();

  /*
    Remove the table entry in data dictionary (this will also remove it from
    the persistent data dictionary).
  */
  ddl_manager.remove(tbl, batch, true);
  if (dict_manager.commit(batch))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  DBUG_RETURN(0);
}


void ha_rocksdb::remove_rows(RDBSE_TABLE_DEF *tbl)
{
  rocksdb::WriteOptions wo = get_write_options(ha_thd());

  rocksdb::ReadOptions opts;
  opts.total_order_seek= true;
  rocksdb::Iterator* it= rdb->NewIterator(opts);

  char key_buf[MAX_KEY_LENGTH];
  uint key_len;
  /*
    Remove all records in each index.
    (This is is not crash-safe, but it doesn't matter, because bulk row
    deletion will be handled on rocksdb side)
  */
  for (uint i= 0; i < tbl->n_keys ; i++)
  {
    tbl->key_descr[i]->get_infimum_key((uchar*)key_buf, &key_len);

    rocksdb::Slice table_key(key_buf, key_len);
    it->Seek(table_key);
    while (it->Valid())
    {
      rocksdb::Slice key= it->key();
      if (!tbl->key_descr[i]->covers_key(key.data(), key.size()))
        break;
      rdb->Delete(wo, key);
      it->Next();
    }
  }
  delete it;
}


int ha_rocksdb::rename_table(const char *from, const char *to)
{
  bool write_err= false;
  StringBuffer<256> from_str;
  StringBuffer<256> to_str;

  if (rocksdb_normalize_tablename(from, &from_str))
    return HA_ERR_INTERNAL_ERROR;

  if (rocksdb_normalize_tablename(to, &to_str))
    return HA_ERR_INTERNAL_ERROR;

  /* Check if any index has a per-index column family */
  {
    /*
      We can't check this directly, because
      1. the object that referred to the old table has been close()d.
      2. this ha_rocksdb object has no connection to any table at all, it has
         been just created with (TABLE*)NULL.

      So, we have to find the old table in the ddl_manager (it's there because
      it has been opened by this statement), and check is_auto_cf there.
    */
    RDBSE_TABLE_DEF *tdef;
    if (!(tdef= ddl_manager.find((uchar*)from_str.c_ptr(), from_str.length())))
      return HA_ERR_INTERNAL_ERROR;

    for (uint i= 0; i < tdef->n_keys; i++)
    {
      if (tdef->key_descr[i]->is_auto_cf)
      {
        my_error(ER_NOT_SUPPORTED_YET, MYF(0),
                 "ALTER TABLE on table with per-index CF");
        return HA_ERR_INTERNAL_ERROR;
      }
    }
  }

  std::unique_ptr<rocksdb::WriteBatch> wb= dict_manager.begin();
  rocksdb::WriteBatch *batch= wb.get();
  dict_manager.lock();
  write_err= ddl_manager.rename((uchar*)from_str.ptr(), from_str.length(),
                         (uchar*)to_str.ptr(), to_str.length(), batch)
             || dict_manager.commit(batch);
  dict_manager.unlock();

  return write_err ? HA_ERR_INTERNAL_ERROR : 0;
}

/**
  check_if_incompatible_data() called if ALTER TABLE can't detect otherwise
  if new and old definition are compatible

  @details If there are no other explicit signs like changed number of
  fields this function will be called by compare_tables()
  (sql/sql_tables.cc) to decide should we rewrite whole table or only .frm
  file.

*/

bool ha_rocksdb::check_if_incompatible_data(HA_CREATE_INFO *info,
                                            uint table_changes)
{
  DBUG_ENTER("ha_rocksdb::check_if_incompatible_data");
  // this function is needed only for online alter-table
  DBUG_RETURN(COMPATIBLE_DATA_NO);
}


int ha_rocksdb::extra(enum ha_extra_function operation)
{
  DBUG_ENTER("ha_rocksdb::extra");

  switch (operation) {
  case HA_EXTRA_KEYREAD:
    keyread_only= TRUE;
    break;
  case HA_EXTRA_NO_KEYREAD:
    keyread_only= FALSE;
    break;
  case HA_EXTRA_FLUSH:
    /*
      If the table has blobs, then they are part of retrieved_record. This call
      invalidates them.
    */
    retrieved_record.clear();
    break;
  default:
    break;
  }
  DBUG_RETURN(0);
}


ha_rows ha_rocksdb::records_in_range(uint inx, key_range *min_key,
                                     key_range *max_key)
{
  DBUG_ENTER("ha_rocksdb::records_in_range");
  ha_rows ret = THDVAR(ha_thd(), records_in_range);
  if (ret) {
    DBUG_RETURN(ret);
  }

  RDBSE_KEYDEF *kd= key_descr[inx];
  assert(kd);

  uint size1=0;
  if (min_key) {
    size1 = kd->pack_index_tuple(
      table,
      pack_buffer,
      sec_key_packed_tuple,
      min_key->key,
      min_key->keypart_map
    );
  } else {
    kd->get_infimum_key(pk_packed_tuple, &size1);
  }

  uint size2=0;
  if (max_key) {
    size2 = kd->pack_index_tuple(
      table,
      pack_buffer,
      sec_key_packed_tuple_old,
      max_key->key,
      max_key->keypart_map
    );
    // pad the upper key with FFFFs to make sure it is more than the lower
    if (size1 > size2) {
      memset(sec_key_packed_tuple_old+size2, 0xff, size1-size2);
      size2 = size1;
    }
  } else {
    kd->get_supremum_key(sec_key_packed_tuple_old, &size2);
  }

  rocksdb::Slice slice1((const char*) sec_key_packed_tuple, size1);
  rocksdb::Slice slice2((const char*) sec_key_packed_tuple_old, size2);

  rocksdb::Range r(
    kd->is_reverse_cf ? slice2 : slice1,
    kd->is_reverse_cf ? slice1 : slice2
  );

  uint64_t sz=0;
  rdb->GetApproximateSizes(
    kd->get_cf(),
    &r, 1,
    &sz);
  ret = kd->stats.approximate_size != 0 ?
    kd->stats.rows*sz/kd->stats.approximate_size :
    0;
  if (ret == 0) {
    ret = 1;
  }

  DBUG_RETURN(ret);
}


void ha_rocksdb::update_create_info(HA_CREATE_INFO *create_info)
{
  if (!(create_info->used_fields & HA_CREATE_USED_AUTO))
  {
    create_info->auto_increment_value= tbl_def->auto_incr_val;
  }
}

/**
  Doing manual compaction on OPTIMIZE TABLE in RocksDB.
  Compaction itself is executed by background thread in RocksDB, but
  CompactRange() waits until compaction completes so this function
  may take a long time.
  Since RocksDB dataset is allocated per index id, OPTIMIZE TABLE
  triggers manual compaction for all indexes of the table.

  Compaction range is from the beginning of the index id to
  the first row of the next index id. When using reverse order
  column family, the first row of the next index id should be
  the last row of the previous index id.
*/
int ha_rocksdb::optimize(THD *thd, HA_CHECK_OPT* check_opt)
{
  int rc= 0;
  for (uint i= 0; i < table->s->keys; i++)
  {
    uchar buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*2];
    auto range = get_range(i, buf);
    if (!rdb->CompactRange(key_descr[i]->get_cf(),
                           &range.start, &range.limit).ok())
    {
      rc= 1;
      break;
    }
  }
  return rc;
}

void compute_optimizer_stats()
{
  auto changed_indexes = ddl_manager.get_changed_indexes();
  if (changed_indexes.empty())
    return;

  // Find keydefs for all changed indexes
  // Collect all column families which contain updated tables
  std::vector<std::unique_ptr<RDBSE_KEYDEF>> keydefs;
  std::unordered_set<rocksdb::ColumnFamilyHandle*> cfs;
  for (auto index_number : changed_indexes)
  {
    auto keydef = ddl_manager.find(index_number);
    if (keydef) {
      cfs.insert(keydef->get_cf());
      keydefs.push_back(std::move(keydef));
    }
  }

  std::map<uint32_t, MyRocksTablePropertiesCollector::IndexStats>
    statsFromProps;
  for (auto cf : cfs) {
    rocksdb::TablePropertiesCollection props;
    auto s = rdb->GetPropertiesOfAllTables(
      cf,
      &props
    );
    MyRocksTablePropertiesCollector::GetStats(
      props, changed_indexes, statsFromProps);
  }

  std::vector<MyRocksTablePropertiesCollector::IndexStats> stats;
  for (auto& keydef : keydefs)
  {
    auto it = statsFromProps.find(keydef->get_index_number());
    if (it != statsFromProps.end()) {
      stats.push_back(it->second);
    } else {
      // This index didn't have any associated stats in SST properties.
      // It might be an empty table, for example.
      stats.emplace_back(keydef->get_index_number());
    }

    uchar buf[2*RDBSE_KEYDEF::INDEX_NUMBER_SIZE];
    auto range = get_range(keydef.get(), buf);
    uint64_t size;
    rdb->GetApproximateSizes(keydef->get_cf(),
                             &range, 1, &size);

    stats.back().approximate_size= size;
  }

  ddl_manager.set_stats(stats);

  // Persist stats
  std::unique_ptr<rocksdb::WriteBatch> wb = dict_manager.begin();
  dict_manager.add_stats(wb.get(), stats);
  dict_manager.commit(wb.get(), false);
}

void ha_rocksdb::get_auto_increment(ulonglong offset, ulonglong increment,
                                    ulonglong nb_desired_values,
                                    ulonglong *first_value,
                                    ulonglong *nb_reserved_values)
{
  if (!nb_desired_values)
    nb_desired_values= 1;

  mysql_mutex_lock(&tbl_def->mutex);
  *first_value= tbl_def->auto_incr_val;
  *nb_reserved_values= nb_desired_values;
  tbl_def->auto_incr_val += nb_desired_values;
  mysql_mutex_unlock(&tbl_def->mutex);
}


#ifndef DBUG_OFF

/* Debugger help function */
static char dbug_item_print_buf[512];

const char *dbug_print_item(Item *item)
{
  char *buf= dbug_item_print_buf;
  String str(buf, sizeof(dbug_item_print_buf), &my_charset_bin);
  str.length(0);
  if (!item)
    return "(Item*)NULL";
  item->print(&str ,QT_ORDINARY);
  if (str.c_ptr() == buf)
    return buf;
  else
    return "Couldn't fit into buffer";
}

#endif /*DBUG_OFF*/

/*
  SQL layer calls this function to push an index condition.

  @detail
    The condition is for index keyno (only one condition can be pushed at a
    time).
    The caller guarantees that condition refers only to index fields; besides
    that, fields must have

      $field->part_of_key.set_bit(keyno)

    which means that

       (handler->index_flags(keyno, $keypart, 0) & HA_KEYREAD_ONLY) == 1

    which means that field value can be restored from the index tuple.

  @return
    Part of condition we couldn't check (always NULL).
*/

class Item* ha_rocksdb::idx_cond_push(uint keyno, class Item* idx_cond)
{
  DBUG_ASSERT(keyno != MAX_KEY);
  DBUG_ASSERT(idx_cond != NULL);

  pushed_idx_cond = idx_cond;
  pushed_idx_cond_keyno = keyno;
  in_range_check_pushed_down = TRUE;

  /* We will check the whole condition */
  return NULL;
}


/*
  @brief
  Check the index condition.

  @detail
  Check the index condition. (The caller has unpacked all needed index
  columns into table->record[0])

  @return
    ICP_NO_MATCH - Condition not satisfied (caller should continue
                   scanning)
    OUT_OF_RANGE - We've left the range we're scanning (caller should
                   stop scanning and return HA_ERR_END_OF_FILE)

    ICP_MATCH    - Condition is satisfied (caller should fetch the record
                   and return it)
*/

enum icp_result ha_rocksdb::check_index_cond()
{
  DBUG_ASSERT(pushed_idx_cond);
  DBUG_ASSERT(pushed_idx_cond_keyno != MAX_KEY);

  if (end_range && compare_key_icp(end_range) > 0)
  {
    /* caller should return HA_ERR_END_OF_FILE already */
    return ICP_OUT_OF_RANGE;
  }

  return pushed_idx_cond->val_int() ? ICP_MATCH : ICP_NO_MATCH;
}


/////////////////////////////////////////////////////////////////////////

/**
  Checking if an index is used for ascending scan or not

  @detail
  Currently RocksDB does not support bloom filter for
  prefix lookup + descending scan, but supports bloom filter for
  prefix lookup + ascending scan. This function returns true if
  the scan pattern is absolutely ascending.
  @param keydef
  @param find_flag
*/
bool ha_rocksdb::is_ascending(RDBSE_KEYDEF *keydef, enum ha_rkey_function find_flag)
{
  bool is_ascending= false;
  switch (find_flag) {
  case HA_READ_KEY_EXACT:
  {
    if (keydef->is_reverse_cf)
      is_ascending= false;
    else
      is_ascending= true;
    break;
  }
  case HA_READ_PREFIX:
  {
    is_ascending= true;
    break;
  }
  case HA_READ_KEY_OR_NEXT:
  case HA_READ_AFTER_KEY:
  {
    if (keydef->is_reverse_cf)
      is_ascending= false;
    else
      is_ascending= true;
    break;
  }
  case HA_READ_KEY_OR_PREV:
  case HA_READ_BEFORE_KEY:
  case HA_READ_PREFIX_LAST:
  case HA_READ_PREFIX_LAST_OR_PREV:
  {
    if (keydef->is_reverse_cf)
      is_ascending= true;
    else
      is_ascending= false;
    break;
  }
  default:
    is_ascending= false;
  }
  return is_ascending;
}


#define SHOW_FNAME(name) rocksdb_show_##name

#define DEF_SHOW_FUNC(name, key) \
  static int SHOW_FNAME(name)(MYSQL_THD thd, SHOW_VAR *var, char *buff)  \
  {                                                                      \
    rocksdb_status_counters.name =                                       \
      rocksdb_stats->getTickerCount(rocksdb::key);                       \
    var->type = SHOW_LONG;                                               \
    var->value = (char *)&rocksdb_status_counters.name;                  \
    return 0;                                                            \
  }

#define DEF_STATUS_VAR(name) \
  {"rocksdb_" #name, (char*) &SHOW_FNAME(name), SHOW_FUNC}

#define DEF_STATUS_VAR_PTR(name, ptr, option) \
  {"rocksdb_" name, (char*) ptr, option}

struct rocksdb_status_counters_t {
  uint64_t block_cache_miss;
  uint64_t block_cache_hit;
  uint64_t block_cache_add;
  uint64_t block_cache_index_miss;
  uint64_t block_cache_index_hit;
  uint64_t block_cache_filter_miss;
  uint64_t block_cache_filter_hit;
  uint64_t block_cache_data_miss;
  uint64_t block_cache_data_hit;
  uint64_t bloom_filter_useful;
  uint64_t memtable_hit;
  uint64_t memtable_miss;
  uint64_t compaction_key_drop_new;
  uint64_t compaction_key_drop_obsolete;
  uint64_t compaction_key_drop_user;
  uint64_t number_keys_written;
  uint64_t number_keys_read;
  uint64_t number_keys_updated;
  uint64_t bytes_written;
  uint64_t bytes_read;
  uint64_t no_file_closes;
  uint64_t no_file_opens;
  uint64_t no_file_errors;
  uint64_t l0_slowdown_micros;
  uint64_t memtable_compaction_micros;
  uint64_t l0_num_files_stall_micros;
  uint64_t rate_limit_delay_millis;
  uint64_t num_iterators;
  uint64_t number_multiget_get;
  uint64_t number_multiget_keys_read;
  uint64_t number_multiget_bytes_read;
  uint64_t number_deletes_filtered;
  uint64_t number_merge_failures;
  uint64_t sequence_number;
  uint64_t bloom_filter_prefix_checked;
  uint64_t bloom_filter_prefix_useful;
  uint64_t number_reseeks_iteration;
  uint64_t getupdatessince_calls;
  uint64_t block_cachecompressed_miss;
  uint64_t block_cachecompressed_hit;
  uint64_t wal_synced;
  uint64_t wal_bytes;
  uint64_t write_self;
  uint64_t write_other;
  uint64_t write_timedout;
  uint64_t write_wal;
  uint64_t flush_write_bytes;
  uint64_t compact_read_bytes;
  uint64_t compact_write_bytes;
  uint64_t number_superversion_acquires;
  uint64_t number_superversion_releases;
  uint64_t number_superversion_cleanups;
  uint64_t number_block_not_compressed;
};

static rocksdb_status_counters_t rocksdb_status_counters;

DEF_SHOW_FUNC(block_cache_miss, BLOCK_CACHE_MISS)
DEF_SHOW_FUNC(block_cache_hit, BLOCK_CACHE_HIT)
DEF_SHOW_FUNC(block_cache_add, BLOCK_CACHE_ADD)
DEF_SHOW_FUNC(block_cache_index_miss, BLOCK_CACHE_INDEX_MISS)
DEF_SHOW_FUNC(block_cache_index_hit, BLOCK_CACHE_INDEX_HIT)
DEF_SHOW_FUNC(block_cache_filter_miss, BLOCK_CACHE_FILTER_MISS)
DEF_SHOW_FUNC(block_cache_filter_hit, BLOCK_CACHE_FILTER_HIT)
DEF_SHOW_FUNC(block_cache_data_miss, BLOCK_CACHE_DATA_MISS)
DEF_SHOW_FUNC(block_cache_data_hit, BLOCK_CACHE_DATA_HIT)
DEF_SHOW_FUNC(bloom_filter_useful, BLOOM_FILTER_USEFUL)
DEF_SHOW_FUNC(memtable_hit, MEMTABLE_HIT)
DEF_SHOW_FUNC(memtable_miss, MEMTABLE_MISS)
DEF_SHOW_FUNC(compaction_key_drop_new, COMPACTION_KEY_DROP_NEWER_ENTRY)
DEF_SHOW_FUNC(compaction_key_drop_obsolete, COMPACTION_KEY_DROP_OBSOLETE)
DEF_SHOW_FUNC(compaction_key_drop_user, COMPACTION_KEY_DROP_USER)
DEF_SHOW_FUNC(number_keys_written, NUMBER_KEYS_WRITTEN)
DEF_SHOW_FUNC(number_keys_read, NUMBER_KEYS_READ)
DEF_SHOW_FUNC(number_keys_updated, NUMBER_KEYS_UPDATED)
DEF_SHOW_FUNC(bytes_written, BYTES_WRITTEN)
DEF_SHOW_FUNC(bytes_read, BYTES_READ)
DEF_SHOW_FUNC(no_file_closes, NO_FILE_CLOSES)
DEF_SHOW_FUNC(no_file_opens, NO_FILE_OPENS)
DEF_SHOW_FUNC(no_file_errors, NO_FILE_ERRORS)
DEF_SHOW_FUNC(l0_slowdown_micros, STALL_L0_SLOWDOWN_MICROS)
DEF_SHOW_FUNC(memtable_compaction_micros, STALL_MEMTABLE_COMPACTION_MICROS)
DEF_SHOW_FUNC(l0_num_files_stall_micros, STALL_L0_NUM_FILES_MICROS)
DEF_SHOW_FUNC(rate_limit_delay_millis, RATE_LIMIT_DELAY_MILLIS)
DEF_SHOW_FUNC(num_iterators, NO_ITERATORS)
DEF_SHOW_FUNC(number_multiget_get, NUMBER_MULTIGET_CALLS)
DEF_SHOW_FUNC(number_multiget_keys_read, NUMBER_MULTIGET_KEYS_READ)
DEF_SHOW_FUNC(number_multiget_bytes_read, NUMBER_MULTIGET_BYTES_READ)
DEF_SHOW_FUNC(number_deletes_filtered, NUMBER_FILTERED_DELETES)
DEF_SHOW_FUNC(number_merge_failures, NUMBER_MERGE_FAILURES)
DEF_SHOW_FUNC(sequence_number, SEQUENCE_NUMBER)
DEF_SHOW_FUNC(bloom_filter_prefix_checked, BLOOM_FILTER_PREFIX_CHECKED)
DEF_SHOW_FUNC(bloom_filter_prefix_useful, BLOOM_FILTER_PREFIX_USEFUL)
DEF_SHOW_FUNC(number_reseeks_iteration, NUMBER_OF_RESEEKS_IN_ITERATION)
DEF_SHOW_FUNC(getupdatessince_calls, GET_UPDATES_SINCE_CALLS)
DEF_SHOW_FUNC(block_cachecompressed_miss, BLOCK_CACHE_COMPRESSED_MISS)
DEF_SHOW_FUNC(block_cachecompressed_hit, BLOCK_CACHE_COMPRESSED_HIT)
DEF_SHOW_FUNC(wal_synced, WAL_FILE_SYNCED)
DEF_SHOW_FUNC(wal_bytes, WAL_FILE_BYTES)
DEF_SHOW_FUNC(write_self, WRITE_DONE_BY_SELF)
DEF_SHOW_FUNC(write_other, WRITE_DONE_BY_OTHER)
DEF_SHOW_FUNC(write_timedout, WRITE_TIMEDOUT)
DEF_SHOW_FUNC(write_wal, WRITE_WITH_WAL)
DEF_SHOW_FUNC(flush_write_bytes, FLUSH_WRITE_BYTES)
DEF_SHOW_FUNC(compact_read_bytes, COMPACT_READ_BYTES)
DEF_SHOW_FUNC(compact_write_bytes, COMPACT_WRITE_BYTES)
DEF_SHOW_FUNC(number_superversion_acquires, NUMBER_SUPERVERSION_ACQUIRES)
DEF_SHOW_FUNC(number_superversion_releases, NUMBER_SUPERVERSION_RELEASES)
DEF_SHOW_FUNC(number_superversion_cleanups, NUMBER_SUPERVERSION_CLEANUPS)
DEF_SHOW_FUNC(number_block_not_compressed, NUMBER_BLOCK_NOT_COMPRESSED)

static SHOW_VAR rocksdb_status_vars[]= {
  DEF_STATUS_VAR(block_cache_miss),
  DEF_STATUS_VAR(block_cache_hit),
  DEF_STATUS_VAR(block_cache_add),
  DEF_STATUS_VAR(block_cache_index_miss),
  DEF_STATUS_VAR(block_cache_index_hit),
  DEF_STATUS_VAR(block_cache_filter_miss),
  DEF_STATUS_VAR(block_cache_filter_hit),
  DEF_STATUS_VAR(block_cache_data_miss),
  DEF_STATUS_VAR(block_cache_data_hit),
  DEF_STATUS_VAR(bloom_filter_useful),
  DEF_STATUS_VAR(memtable_hit),
  DEF_STATUS_VAR(memtable_miss),
  DEF_STATUS_VAR(compaction_key_drop_new),
  DEF_STATUS_VAR(compaction_key_drop_obsolete),
  DEF_STATUS_VAR(compaction_key_drop_user),
  DEF_STATUS_VAR(number_keys_written),
  DEF_STATUS_VAR(number_keys_read),
  DEF_STATUS_VAR(number_keys_updated),
  DEF_STATUS_VAR(bytes_written),
  DEF_STATUS_VAR(bytes_read),
  DEF_STATUS_VAR(no_file_closes),
  DEF_STATUS_VAR(no_file_opens),
  DEF_STATUS_VAR(no_file_errors),
  DEF_STATUS_VAR(l0_slowdown_micros),
  DEF_STATUS_VAR(memtable_compaction_micros),
  DEF_STATUS_VAR(l0_num_files_stall_micros),
  DEF_STATUS_VAR(rate_limit_delay_millis),
  DEF_STATUS_VAR(num_iterators),
  DEF_STATUS_VAR(number_multiget_get),
  DEF_STATUS_VAR(number_multiget_keys_read),
  DEF_STATUS_VAR(number_multiget_bytes_read),
  DEF_STATUS_VAR(number_deletes_filtered),
  DEF_STATUS_VAR(number_merge_failures),
  DEF_STATUS_VAR(sequence_number),
  DEF_STATUS_VAR(bloom_filter_prefix_checked),
  DEF_STATUS_VAR(bloom_filter_prefix_useful),
  DEF_STATUS_VAR(number_reseeks_iteration),
  DEF_STATUS_VAR(getupdatessince_calls),
  DEF_STATUS_VAR(block_cachecompressed_miss),
  DEF_STATUS_VAR(block_cachecompressed_hit),
  DEF_STATUS_VAR(wal_synced),
  DEF_STATUS_VAR(wal_bytes),
  DEF_STATUS_VAR(write_self),
  DEF_STATUS_VAR(write_other),
  DEF_STATUS_VAR(write_timedout),
  DEF_STATUS_VAR(write_wal),
  DEF_STATUS_VAR(flush_write_bytes),
  DEF_STATUS_VAR(compact_read_bytes),
  DEF_STATUS_VAR(compact_write_bytes),
  DEF_STATUS_VAR(number_superversion_acquires),
  DEF_STATUS_VAR(number_superversion_releases),
  DEF_STATUS_VAR(number_superversion_cleanups),
  DEF_STATUS_VAR(number_block_not_compressed),
  DEF_STATUS_VAR_PTR("number_stat_computes", &rocksdb_number_stat_computes, SHOW_LONGLONG),
  {NullS, NullS, SHOW_LONG}
};


struct st_mysql_storage_engine rocksdb_storage_engine=
{ MYSQL_HANDLERTON_INTERFACE_VERSION };

mysql_declare_plugin(rocksdb_se)
{
  MYSQL_STORAGE_ENGINE_PLUGIN,
  &rocksdb_storage_engine,
  "ROCKSDB",
  "Monty Program Ab",
  "RocksDB storage engine",
  PLUGIN_LICENSE_GPL,
  rocksdb_init_func,                            /* Plugin Init */
  rocksdb_done_func,                            /* Plugin Deinit */
  0x0001,                                       /* version number (0.1) */
  rocksdb_status_vars,                          /* status variables */
  rocksdb_system_variables,                     /* system variables */
  NULL,                                         /* config options */
  0,                                            /* flags */
},
i_s_rocksdb_cfstats,
i_s_rocksdb_dbstats,
i_s_rocksdb_perf_context,
i_s_rocksdb_cfoptions
mysql_declare_plugin_end;


/*
  Compute a hash number for a PK value in RowKeyFormat.

  @note
    RowKeyFormat is comparable with memcmp. This means, any hash function will
    work correctly. We use my_charset_bin's hash function.

    Note from Bar: could also use crc32 function.
*/

ulong Primary_key_comparator::get_hashnr(const char *key, size_t key_len)
{
  ulong nr=1, nr2=4;
  my_charset_bin.coll->hash_sort(&my_charset_bin, (const uchar*)key, key_len,
                                 &nr, &nr2);
  return((ulong) nr);
}

void* background_thread(void*)
{
  mysql_mutex_lock(&background_mutex);
  mysql_mutex_lock(&stop_cond_mutex);

  rocksdb::WriteBatch wb;
  time_t last_stat_recompute = 0;
  for (;;)
  {
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec++;
    // wait for 1 second or until we received a condition to stop the thread
    auto ret = mysql_cond_timedwait(&stop_cond, &stop_cond_mutex, &ts);
    if (stop_background_thread) {
      break;
    }
    // make sure, no program error is returned
    assert(ret == 0 || ret == ETIMEDOUT);

    if (rdb && rocksdb_background_sync) {
      auto wo = rocksdb::WriteOptions();
      wo.sync = true;
      rocksdb::Status s= rdb->Write(wo, &wb);
      if (!s.ok())
        rocksdb_handle_io_error(s, ROCKSDB_IO_ERROR_BG_THREAD);
    }

    if (ts.tv_sec - last_stat_recompute > rocksdb_seconds_between_stat_computes)
    {
      compute_optimizer_stats();
      rocksdb_number_stat_computes++;
      last_stat_recompute = ts.tv_sec;
    }
  }

  mysql_mutex_unlock(&stop_cond_mutex);
  mysql_mutex_unlock(&background_mutex);

  return nullptr;
}


/**
  Deciding if it is possible to use bloom filter or not.

  @detail
   Even if bloom filter exists, it is not always possible
   to use bloom filter. If using bloom filter when you shouldn't,
   false negative may happen -- fewer rows than expected may be returned.
   It is users' responsibility to use bloom filter correctly.

   If bloom filter does not exist, return value does not matter because
   RocksDB does not use bloom filter internally.

  @param keydef
  @param eq_cond      Equal condition part of the key. This always includes
                      system index id (4 bytes).
  @param use_all_keys True if all key parts are set with equal conditions.
                      This is aware of extended keys.
*/
bool can_use_bloom_filter(RDBSE_KEYDEF *keydef,
                          const rocksdb::Slice &eq_cond,
                          const bool use_all_keys,
                          bool is_ascending)
{
  bool can_use= false;
  rocksdb::Options opt = rdb->GetOptions(keydef->get_cf());
  if (opt.prefix_extractor)
  {
    /*
      This is an optimized use case for CappedPrefixTransform.
      If eq_cond length >= prefix extractor length and if
      all keys are used for equal lookup, it is
      always possible to use bloom filter.

      Prefix bloom filter can't be used on descending scan with
      prefix lookup (i.e. WHERE id1=1 ORDER BY id2 DESC), because of
      RocksDB's limitation. On ascending (or not sorting) scan,
      keys longer than the capped prefix length will be truncated down
      to the capped length and the resulting key is added to the bloom filter.

      Keys shorter than the capped prefix length will be added to
      the bloom filter. When keys are looked up, key conditionals
      longer than the capped length can be used; key conditionals
      shorter require all parts of the key to be available
      for the short key match.
    */
    if (use_all_keys && opt.prefix_extractor->InRange(eq_cond))
      can_use= true;
    else if (!is_ascending)
      can_use= false;
    else if (opt.prefix_extractor->SameResultWhenAppended(eq_cond))
      can_use= true;
    else
      can_use= false;
  } else
  {
    /*
      if prefix extractor is not defined, all key parts have to be
      used by eq_cond.
    */
    if (use_all_keys)
      can_use= true;
    else
      can_use= false;
  }

  return can_use;
}

/* For modules that need access to the global data structures */
rocksdb::DB *rocksdb_get_rdb()
{
  return rdb;
}

Column_family_manager& rocksdb_get_cf_manager()
{
  return cf_manager;
}

rocksdb::BlockBasedTableOptions& rocksdb_get_table_options()
{
  return table_options;
}

int rocksdb_get_share_perf_counters(const char *tablename,
                                    SHARE_PERF_COUNTERS *counters)
{
  ROCKSDB_SHARE *share;
  share= get_share(tablename, nullptr);
  if (!share)
    return HA_ERR_INTERNAL_ERROR;

  counters->value[PC_KEY_SKIPPED_IDX]=
      share->internal_key_skipped_count.load(std::memory_order_relaxed),
  counters->value[PC_DELETE_SKIPPED_IDX]=
      share->internal_delete_skipped_count.load(std::memory_order_relaxed),
  free_share(share);
  return 0;
}

void rocksdb_handle_io_error(rocksdb::Status status, enum io_error_type type)
{
  if (status.IsIOError())
  {
    switch (type) {
    case ROCKSDB_IO_ERROR_TRX_COMMIT:
    case ROCKSDB_IO_ERROR_DICT_COMMIT:
    {
      sql_print_error("RocksDB: Failed to write to WAL - status %d",
                      status.code());
      sql_print_error("RocksDB: Aborting on WAL write error.");
      exit(EXIT_FAILURE);
      break;
    }
    case ROCKSDB_IO_ERROR_BG_THREAD:
    {
      sql_print_warning("RocksDB: BG Thread failed to write to RocksDB "
                        "- status %d", status.code());
      break;
    }
    default:
      DBUG_ASSERT(0);
      break;
    }
  }
}
