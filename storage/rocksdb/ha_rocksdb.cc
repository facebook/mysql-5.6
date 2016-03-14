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

#define MYSQL_SERVER 1

/* The C++ file's header */
#include "./ha_rocksdb.h"

/* C++ standard header files */
#include <regex>
#include <set>
#include <string>
#include <vector>

/* MySQL includes */
#include "./debug_sync.h"
#include "./my_bit.h"
#include "./my_stacktrace.h"
#include "./sql_table.h"
#include <mysql/psi/mysql_table.h>
#include <mysql/thread_pool_priv.h>

/* RocksDB includes */
#include "rocksdb/compaction_filter.h"
#include "rocksdb/rate_limiter.h"
#include "rocksdb/slice_transform.h"
#include "rocksdb/utilities/checkpoint.h"
#include "rocksdb/utilities/convenience.h"
#include "rocksdb/utilities/flashcache.h"
#include "rocksdb/utilities/memory_util.h"

/* MyRocks includes */
#include "./event_listener.h"
#include "./ha_rocksdb_proto.h"
#include "./logger.h"
#include "./rdb_cf_manager.h"
#include "./rdb_cf_options.h"
#include "./rdb_comparator.h"
#include "./rdb_datadic.h"
#include "./rdb_i_s.h"
#include "./rdb_mutex_wrapper.h"

#ifdef TARGET_OS_LINUX
extern my_bool cachedev_enabled;
#endif /* TARGET_OS_LINUX */

namespace myrocks {

static st_global_stats global_stats;
static st_export_stats export_stats;

/**
  Updates row counters based on the table type and operation type.
*/
void ha_rocksdb::update_row_stats(operation_type type) {
  DBUG_ASSERT(type < ROWS_MAX);
  // Find if we are modifying system databases.
  if (table->s && tbl_def->mysql_system_table)
    global_stats.system_rows[type].inc();
  else
    global_stats.rows[type].inc();
}

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
static char * rocksdb_override_cf_options;
Cf_options rocksdb_cf_options_map;

///////////////////////////////////////////////////////////
// Globals
///////////////////////////////////////////////////////////
handlerton *rocksdb_hton;

rocksdb::TransactionDB *rdb= nullptr;

static std::shared_ptr<rocksdb::Statistics> rocksdb_stats;
static std::unique_ptr<rocksdb::Env> flashcache_aware_env;
static std::shared_ptr<MyRocksTablePropertiesCollectorFactory>
  properties_collector_factory;
static rdb_perf_context_shared global_perf_context;
static std::vector<std::string> split(const std::string& input,
                                      char               delimiter);

rocksdb::ColumnFamilyOptions default_cf_opts;


Dict_manager dict_manager;
Column_family_manager cf_manager;
Table_ddl_manager ddl_manager;
Binlog_info_manager binlog_manager;

/*
   Hash used to track the number of open tables; variable for example share
   methods
*/
static HASH rocksdb_open_tables;

/* The mutex used to init the hash; variable for example share methods */
mysql_mutex_t rocksdb_mutex;
mysql_mutex_t background_mutex;
mysql_mutex_t stop_cond_mutex;
mysql_mutex_t collation_exception_list_mutex;
mysql_cond_t stop_cond;
struct background_thread_control {
  bool stop = false;
  bool save_stats = false;
};
background_thread_control bg_control;

// List of table names (using regex) that are exceptions to the strict
// collation check requirement.
std::vector<std::string> collation_exception_list;

static const char* const ERRSTR_ROLLBACK_ONLY
  = "This transaction was rolled back and cannot be "
    "committed. Only supported operation is to roll it back, "
    "so all pending changes will be discarded. "
    "Please restart another transaction.";


static void
rocksdb_flush_all_memtables()
{
  Column_family_manager& cf_manager = rocksdb_get_cf_manager();
  for (auto cf_handle : cf_manager.get_all_cf()) {
    rdb->Flush(rocksdb::FlushOptions(), cf_handle);
  }
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
      rdb->CompactRange(rocksdb::CompactRangeOptions(), cfh, nullptr, nullptr);
    }
  }
}

std::string normalize_dir(std::string dir)
{
  while (dir.size() > 0 &&
          dir.back() == '/') {
    dir.resize(dir.size() - 1);
  }
  return dir;
}

static int
rocksdb_create_checkpoint(THD* thd,
                        struct st_mysql_sys_var* var,
                        void* save,
                        struct st_mysql_value* value)
{
  char buf[512];
  int len = sizeof(buf);
  const char* checkpoint_dir_raw = value->val_str(value, buf, &len);
  if (checkpoint_dir_raw) {
    if (rdb != nullptr) {
      std::string checkpoint_dir = normalize_dir(checkpoint_dir_raw);
      // NO_LINT_DEBUG
      sql_print_information("RocksDB: creating checkpoint in directory : %s\n",
          checkpoint_dir.c_str());
      rocksdb::Checkpoint* checkpoint;
      auto status = rocksdb::Checkpoint::Create(rdb, &checkpoint);
      if (status.ok()) {
        status = checkpoint->CreateCheckpoint(checkpoint_dir.c_str());
        if (status.ok()) {
          sql_print_information(
              "RocksDB: created checkpoint in directory : %s\n",
              checkpoint_dir.c_str());
        } else {
          my_printf_error(
              ER_UNKNOWN_ERROR,
              "RocksDB: Failed to create checkpoint directory. status %d %s",
              MYF(0), status.code(), status.ToString().c_str());
        }
        delete checkpoint;
      } else {
        std::string err_text(status.ToString());
        my_printf_error(ER_UNKNOWN_ERROR,
            "RocksDB: failed to initialize checkpoint. status %d %s\n",
            MYF(0), status.code(), err_text.c_str());
      }
      return status.code();
     }
  }
  return HA_ERR_INTERNAL_ERROR;
}

/* This method is needed to indicate that the
   ROCKSDB_CREATE_CHECKPOINT command is not read-only */
static void
rocksdb_create_checkpoint_stub(THD* thd,
                               struct st_mysql_sys_var* var,
                               void* var_ptr,
                               const void* save)
{
}

static void
rocksdb_force_flush_memtable_now(THD* thd,
                                 struct st_mysql_sys_var* var,
                                 void* var_ptr,
                                 const void* save)
{
  sql_print_information("RocksDB: Manual memtable flush\n");
  rocksdb_flush_all_memtables();
}

static void
rocksdb_drop_index_wakeup_thread(THD* thd,
                                 struct st_mysql_sys_var* var,
                                 void* var_ptr,
                                 const void* save)
{
  if (*static_cast<const bool*>(save)) {
    signal_drop_index_thread();
  }
}

static my_bool rocksdb_pause_background_work = 0;
static mysql_mutex_t sysvar_mutex;

static void
rocksdb_set_pause_background_work(THD* thd,
                                 struct st_mysql_sys_var* var,
                                 void* var_ptr,
                                 const void* save)
{
  mysql_mutex_lock(&sysvar_mutex);
  bool pause_requested = *static_cast<const bool*>(save);
  if (rocksdb_pause_background_work != pause_requested) {
    if (pause_requested) {
      rdb->PauseBackgroundWork();
    } else {
      rdb->ContinueBackgroundWork();
    }
    rocksdb_pause_background_work = pause_requested;
  }
  mysql_mutex_unlock(&sysvar_mutex);
}

static void
set_compaction_options(THD* thd,
                       struct st_mysql_sys_var* var,
                       void* var_ptr,
                       const void* save);

static void
myrocks_set_table_stats_sampling_pct(THD* thd,
                                     struct st_mysql_sys_var* var,
                                     void* var_ptr,
                                     const void* save);

static void
set_rate_limiter_bytes_per_sec(THD*                     thd,
                               struct st_mysql_sys_var* var,
                               void*                    var_ptr,
                               const void*              save);

static void
rocksdb_set_collation_exception_list(THD*                     thd,
                                     struct st_mysql_sys_var* var,
                                     void*                    var_ptr,
                                     const void*              save);

//////////////////////////////////////////////////////////////////////////////
// Options definitions
//////////////////////////////////////////////////////////////////////////////
static long long rocksdb_block_cache_size;
/* Use unsigned long long instead of uint64_t because of MySQL compatibility */
static unsigned long long  // NOLINT(runtime/int)
    rocksdb_rate_limiter_bytes_per_sec;
static uint64_t rocksdb_info_log_level;
static char * rocksdb_wal_dir;
static uint64_t rocksdb_index_type;
static char rocksdb_background_sync;
static uint32_t rocksdb_debug_optimizer_n_rows;
static my_bool rocksdb_debug_optimizer_no_zero_cardinality;
static uint32_t rocksdb_perf_context_level;
static uint32_t rocksdb_wal_recovery_mode;
static uint32_t rocksdb_access_hint_on_compaction_start;
static char * compact_cf_name;
static char * checkpoint_name;
static my_bool rocksdb_signal_drop_index_thread;
static my_bool rocksdb_strict_collation_check = 1;
static char * rocksdb_strict_collation_exceptions;
static my_bool rocksdb_collect_sst_properties = 1;
static my_bool rocksdb_force_flush_memtable_now_var = 0;
static uint64_t rocksdb_number_stat_computes = 0;
static uint32_t rocksdb_seconds_between_stat_computes = 3600;
static long long rocksdb_compaction_sequential_deletes= 0l;
static long long rocksdb_compaction_sequential_deletes_window= 0l;
static long long rocksdb_compaction_sequential_deletes_file_size= 0l;
static uint32_t rocksdb_validate_tables = 1;
static char * rocksdb_datadir;
static uint32_t rocksdb_table_stats_sampling_pct;

static rocksdb::DBOptions init_db_options() {
  rocksdb::DBOptions o;
  o.create_if_missing = true;
  o.listeners.push_back(std::make_shared<MyRocksEventListener>(&ddl_manager));
  o.info_log_level= rocksdb::InfoLogLevel::INFO_LEVEL;
  return o;
}

static rocksdb::DBOptions db_options = init_db_options();
static rocksdb::BlockBasedTableOptions table_options;

static std::shared_ptr<rocksdb::RateLimiter> rate_limiter;

/* This enum needs to be kept up to date with rocksdb::InfoLogLevel */
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

static void
rocksdb_set_rocksdb_info_log_level(THD* thd,
                                   struct st_mysql_sys_var* var,
                                   void* var_ptr,
                                   const void* save)
{
  mysql_mutex_lock(&sysvar_mutex);
  rocksdb_info_log_level = *static_cast<const uint64_t*>(save);
  db_options.info_log->SetInfoLogLevel(
      static_cast<const rocksdb::InfoLogLevel>(rocksdb_info_log_level));
  mysql_mutex_unlock(&sysvar_mutex);
}

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
  nullptr, nullptr, /*default*/ 1, /*min*/ 1, /*max*/ 1024*1024*1024, 0);

static MYSQL_THDVAR_BOOL(bulk_load, PLUGIN_VAR_RQCMDARG,
  "Use bulk-load mode for inserts. This enables both "
  "rocksdb_skip_unique_check and rocksdb_commit_in_the_middle. "
  "This parameter will be deprecated in future release.",
  nullptr, nullptr, FALSE);

static MYSQL_THDVAR_BOOL(skip_unique_check, PLUGIN_VAR_RQCMDARG,
  "Skip unique constraint checking", nullptr, nullptr, FALSE);

static MYSQL_THDVAR_BOOL(commit_in_the_middle, PLUGIN_VAR_RQCMDARG,
  "Commit rows implicitly every rocksdb_bulk_load_size, on bulk load/insert",
  nullptr, nullptr, FALSE);

static MYSQL_THDVAR_ULONG(max_row_locks, PLUGIN_VAR_RQCMDARG,
  "Maximum number of locks a transaction can have",
  nullptr, nullptr, /*default*/ 1024*1024*1024, /*min*/ 1,
  /*max*/ 1024*1024*1024, 0);

static MYSQL_THDVAR_ULONG(bulk_load_size, PLUGIN_VAR_RQCMDARG,
  "Max #records in a batch for bulk-load mode",
  nullptr, nullptr, /*default*/ 1000, /*min*/ 1, /*max*/ 1024*1024*1024, 0);

static MYSQL_SYSVAR_BOOL(create_if_missing,
  *reinterpret_cast<my_bool*>(&db_options.create_if_missing),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::create_if_missing for RocksDB",
  nullptr, nullptr, db_options.create_if_missing);

static MYSQL_SYSVAR_BOOL(create_missing_column_families,
  *reinterpret_cast<my_bool*>(&db_options.create_missing_column_families),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::create_missing_column_families for RocksDB",
  nullptr, nullptr, db_options.create_missing_column_families);

static MYSQL_SYSVAR_BOOL(error_if_exists,
  *reinterpret_cast<my_bool*>(&db_options.error_if_exists),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::error_if_exists for RocksDB",
  nullptr, nullptr, db_options.error_if_exists);

static MYSQL_SYSVAR_BOOL(paranoid_checks,
  *reinterpret_cast<my_bool*>(&db_options.paranoid_checks),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::paranoid_checks for RocksDB",
  nullptr, nullptr, db_options.paranoid_checks);

static MYSQL_SYSVAR_ULONGLONG(rate_limiter_bytes_per_sec,
  rocksdb_rate_limiter_bytes_per_sec,
  PLUGIN_VAR_RQCMDARG,
  "DBOptions::rate_limiter bytes_per_sec for RocksDB",
  nullptr, set_rate_limiter_bytes_per_sec, /* default */ 0L,
  /* min */ 0L, /* max */ MAX_RATE_LIMITER_BYTES_PER_SEC, 0);

static MYSQL_SYSVAR_ENUM(info_log_level,
  rocksdb_info_log_level,
  PLUGIN_VAR_RQCMDARG,
  "Filter level for info logs to be written mysqld error log. "
  "Valid values include 'debug_level', 'info_level', 'warn_level'"
  "'error_level' and 'fatal_level'.",
  nullptr, rocksdb_set_rocksdb_info_log_level,
  rocksdb::InfoLogLevel::ERROR_LEVEL, &info_log_level_typelib);

static MYSQL_SYSVAR_UINT(perf_context_level,
  rocksdb_perf_context_level,
  PLUGIN_VAR_RQCMDARG,
  "Perf Context Level for rocksdb internal timer stat collection",
  nullptr, nullptr, rocksdb::kEnableCount,
  /* min */ 0L, /* max */ UINT_MAX, 0);

static MYSQL_SYSVAR_UINT(wal_recovery_mode,
  rocksdb_wal_recovery_mode,
  PLUGIN_VAR_RQCMDARG,
  "DBOptions::wal_recovery_mode for RocksDB",
  nullptr, nullptr, 2,
  /* min */ 0L, /* max */ 3, 0);

static MYSQL_SYSVAR_ULONG(compaction_readahead_size,
  db_options.compaction_readahead_size,
  PLUGIN_VAR_RQCMDARG,
  "DBOptions::compaction_readahead_size for RocksDB",
  nullptr, nullptr, db_options.compaction_readahead_size,
  /* min */ 0L, /* max */ ULONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(new_table_reader_for_compaction_inputs,
  *reinterpret_cast<my_bool*>
    (&db_options.new_table_reader_for_compaction_inputs),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::new_table_reader_for_compaction_inputs for RocksDB",
  nullptr, nullptr, db_options.new_table_reader_for_compaction_inputs);

static MYSQL_SYSVAR_UINT(access_hint_on_compaction_start,
  rocksdb_access_hint_on_compaction_start,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::access_hint_on_compaction_start for RocksDB",
  nullptr, nullptr, 1,
  /* min */ 0L, /* max */ 3, 0);

static MYSQL_SYSVAR_BOOL(allow_concurrent_memtable_write,
  *reinterpret_cast<my_bool*>(&db_options.allow_concurrent_memtable_write),
  PLUGIN_VAR_RQCMDARG,
  "DBOptions::allow_concurrent_memtable_write for RocksDB",
  nullptr, nullptr, db_options.allow_concurrent_memtable_write);

static MYSQL_SYSVAR_BOOL(enable_write_thread_adaptive_yield,
  *reinterpret_cast<my_bool*>(&db_options.enable_write_thread_adaptive_yield),
  PLUGIN_VAR_RQCMDARG,
  "DBOptions::enable_write_thread_adaptive_yield for RocksDB",
  nullptr, nullptr, db_options.enable_write_thread_adaptive_yield);

static MYSQL_SYSVAR_INT(max_open_files,
  db_options.max_open_files,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_open_files for RocksDB",
  nullptr, nullptr, db_options.max_open_files,
  /* min */ -1, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_ULONG(max_total_wal_size,
  db_options.max_total_wal_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_total_wal_size for RocksDB",
  nullptr, nullptr, db_options.max_total_wal_size,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(disabledatasync,
  *reinterpret_cast<my_bool*>(&db_options.disableDataSync),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::disableDataSync for RocksDB",
  nullptr, nullptr, db_options.disableDataSync);

static MYSQL_SYSVAR_BOOL(use_fsync,
  *reinterpret_cast<my_bool*>(&db_options.use_fsync),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::use_fsync for RocksDB",
  nullptr, nullptr, db_options.use_fsync);

static MYSQL_SYSVAR_STR(wal_dir, rocksdb_wal_dir,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::wal_dir for RocksDB",
  nullptr, nullptr, db_options.wal_dir.c_str());

static MYSQL_SYSVAR_ULONG(delete_obsolete_files_period_micros,
  db_options.delete_obsolete_files_period_micros,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::delete_obsolete_files_period_micros for RocksDB",
  nullptr, nullptr, db_options.delete_obsolete_files_period_micros,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_INT(base_background_compactions,
  db_options.base_background_compactions,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::base_background_compactions for RocksDB",
  nullptr, nullptr, db_options.base_background_compactions,
  /* min */ -1, /* max */ MAX_BACKGROUND_COMPACTIONS, 0);

static MYSQL_SYSVAR_INT(max_background_compactions,
  db_options.max_background_compactions,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_background_compactions for RocksDB",
  nullptr, nullptr, db_options.max_background_compactions,
  /* min */ 1, /* max */ MAX_BACKGROUND_COMPACTIONS, 0);

static MYSQL_SYSVAR_INT(max_background_flushes,
  db_options.max_background_flushes,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_background_flushes for RocksDB",
  nullptr, nullptr, db_options.max_background_flushes,
  /* min */ 1, /* max */ MAX_BACKGROUND_FLUSHES, 0);

static MYSQL_SYSVAR_ULONG(max_log_file_size,
  db_options.max_log_file_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_log_file_size for RocksDB",
  nullptr, nullptr, db_options.max_log_file_size,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(log_file_time_to_roll,
  db_options.log_file_time_to_roll,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::log_file_time_to_roll for RocksDB",
  nullptr, nullptr, db_options.log_file_time_to_roll,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(keep_log_file_num,
  db_options.keep_log_file_num,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::keep_log_file_num for RocksDB",
  nullptr, nullptr, db_options.keep_log_file_num,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(max_manifest_file_size,
  db_options.max_manifest_file_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_manifest_file_size for RocksDB",
  nullptr, nullptr, db_options.max_manifest_file_size,
  /* min */ 0L, /* max */ ULONG_MAX, 0);

static MYSQL_SYSVAR_INT(table_cache_numshardbits,
  db_options.table_cache_numshardbits,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::table_cache_numshardbits for RocksDB",
  nullptr, nullptr, db_options.table_cache_numshardbits,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_ULONG(wal_ttl_seconds,
  db_options.WAL_ttl_seconds,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::WAL_ttl_seconds for RocksDB",
  nullptr, nullptr, db_options.WAL_ttl_seconds,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(wal_size_limit_mb,
  db_options.WAL_size_limit_MB,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::WAL_size_limit_MB for RocksDB",
  nullptr, nullptr, db_options.WAL_size_limit_MB,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(manifest_preallocation_size,
  db_options.manifest_preallocation_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::manifest_preallocation_size for RocksDB",
  nullptr, nullptr, db_options.manifest_preallocation_size,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(allow_os_buffer,
  *reinterpret_cast<my_bool*>(&db_options.allow_os_buffer),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::allow_os_buffer for RocksDB",
  nullptr, nullptr, db_options.allow_os_buffer);

static MYSQL_SYSVAR_BOOL(allow_mmap_reads,
  *reinterpret_cast<my_bool*>(&db_options.allow_mmap_reads),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::allow_mmap_reads for RocksDB",
  nullptr, nullptr, db_options.allow_mmap_reads);

static MYSQL_SYSVAR_BOOL(allow_mmap_writes,
  *reinterpret_cast<my_bool*>(&db_options.allow_mmap_writes),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::allow_mmap_writes for RocksDB",
  nullptr, nullptr, db_options.allow_mmap_writes);

static MYSQL_SYSVAR_BOOL(is_fd_close_on_exec,
  *reinterpret_cast<my_bool*>(&db_options.is_fd_close_on_exec),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::is_fd_close_on_exec for RocksDB",
  nullptr, nullptr, db_options.is_fd_close_on_exec);

static MYSQL_SYSVAR_UINT(stats_dump_period_sec,
  db_options.stats_dump_period_sec,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::stats_dump_period_sec for RocksDB",
  nullptr, nullptr, db_options.stats_dump_period_sec,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_BOOL(advise_random_on_open,
  *reinterpret_cast<my_bool*>(&db_options.advise_random_on_open),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::advise_random_on_open for RocksDB",
  nullptr, nullptr, db_options.advise_random_on_open);

static MYSQL_SYSVAR_ULONG(db_write_buffer_size,
  db_options.db_write_buffer_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::db_write_buffer_size for RocksDB",
  nullptr, nullptr, db_options.db_write_buffer_size,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(use_adaptive_mutex,
  *reinterpret_cast<my_bool*>(&db_options.use_adaptive_mutex),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::use_adaptive_mutex for RocksDB",
  nullptr, nullptr, db_options.use_adaptive_mutex);

static MYSQL_SYSVAR_ULONG(bytes_per_sync,
  db_options.bytes_per_sync,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::bytes_per_sync for RocksDB",
  nullptr, nullptr, db_options.bytes_per_sync,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(wal_bytes_per_sync,
  db_options.wal_bytes_per_sync,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::wal_bytes_per_sync for RocksDB",
  nullptr, nullptr, db_options.wal_bytes_per_sync,
  /* min */ 0L, /* max */ LONG_MAX, 0);


static MYSQL_SYSVAR_BOOL(enable_thread_tracking,
  *reinterpret_cast<my_bool*>(&db_options.enable_thread_tracking),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::enable_thread_tracking for RocksDB",
  nullptr, nullptr, db_options.enable_thread_tracking);

static MYSQL_SYSVAR_LONGLONG(block_cache_size, rocksdb_block_cache_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "block_cache size for RocksDB",
  nullptr, nullptr, /* RocksDB's default is 8 MB: */ 8*1024*1024L,
  /* min */ 1024L, /* max */ LONGLONG_MAX, /* Block size */1024L);

static MYSQL_SYSVAR_BOOL(cache_index_and_filter_blocks,
  *reinterpret_cast<my_bool*>(&table_options.cache_index_and_filter_blocks),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::cache_index_and_filter_blocks for RocksDB",
  nullptr, nullptr, true);

static MYSQL_SYSVAR_ENUM(index_type,
  rocksdb_index_type,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::index_type for RocksDB",
  nullptr, nullptr, (uint64_t)table_options.index_type, &index_type_typelib);

static MYSQL_SYSVAR_BOOL(hash_index_allow_collision,
  *reinterpret_cast<my_bool*>(&table_options.hash_index_allow_collision),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::hash_index_allow_collision for RocksDB",
  nullptr, nullptr, table_options.hash_index_allow_collision);

static MYSQL_SYSVAR_BOOL(no_block_cache,
  *reinterpret_cast<my_bool*>(&table_options.no_block_cache),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::no_block_cache for RocksDB",
  nullptr, nullptr, table_options.no_block_cache);

static MYSQL_SYSVAR_ULONG(block_size,
  table_options.block_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::block_size for RocksDB",
  nullptr, nullptr, table_options.block_size,
  /* min */ 1L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_INT(block_size_deviation,
  table_options.block_size_deviation,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::block_size_deviation for RocksDB",
  nullptr, nullptr, table_options.block_size_deviation,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_INT(block_restart_interval,
  table_options.block_restart_interval,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::block_restart_interval for RocksDB",
  nullptr, nullptr, table_options.block_restart_interval,
  /* min */ 1, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_BOOL(whole_key_filtering,
  *reinterpret_cast<my_bool*>(&table_options.whole_key_filtering),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::whole_key_filtering for RocksDB",
  nullptr, nullptr, table_options.whole_key_filtering);

static MYSQL_SYSVAR_STR(default_cf_options, rocksdb_default_cf_options,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "default cf options for RocksDB",
  nullptr, nullptr, "");

static MYSQL_SYSVAR_STR(override_cf_options, rocksdb_override_cf_options,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "option overrides per cf for RocksDB",
  nullptr, nullptr, "");

static MYSQL_SYSVAR_BOOL(background_sync,
  rocksdb_background_sync,
  PLUGIN_VAR_RQCMDARG,
  "turns on background syncs for RocksDB",
  nullptr, nullptr, FALSE);

static MYSQL_THDVAR_BOOL(write_sync,
  PLUGIN_VAR_RQCMDARG,
  "WriteOptions::sync for RocksDB",
  nullptr, nullptr, rocksdb::WriteOptions().sync);

static MYSQL_THDVAR_BOOL(write_disable_wal,
  PLUGIN_VAR_RQCMDARG,
  "WriteOptions::disableWAL for RocksDB",
  nullptr, nullptr, rocksdb::WriteOptions().disableWAL);

static MYSQL_THDVAR_BOOL(write_ignore_missing_column_families,
  PLUGIN_VAR_RQCMDARG,
  "WriteOptions::ignore_missing_column_families for RocksDB",
  nullptr, nullptr, rocksdb::WriteOptions().ignore_missing_column_families);

static MYSQL_THDVAR_BOOL(skip_fill_cache,
  PLUGIN_VAR_RQCMDARG,
  "Skip filling block cache on read requests",
  nullptr, nullptr, FALSE);

static MYSQL_THDVAR_BOOL(unsafe_for_binlog,
  PLUGIN_VAR_RQCMDARG,
  "Allowing statement based binary logging which may break consistency",
  nullptr, nullptr, FALSE);

static MYSQL_THDVAR_UINT(records_in_range,
  PLUGIN_VAR_RQCMDARG,
  "Used to override the result of records_in_range(). Set to a positive number to override",
  nullptr, nullptr, 0,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_THDVAR_UINT(force_index_records_in_range,
  PLUGIN_VAR_RQCMDARG,
  "Used to override the result of records_in_range() when FORCE INDEX is used.",
  nullptr, nullptr, 0,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_UINT(debug_optimizer_n_rows,
  rocksdb_debug_optimizer_n_rows,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY | PLUGIN_VAR_NOSYSVAR,
  "Test only to override rocksdb estimates of table size in a memtable",
  nullptr, nullptr, 0, /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_BOOL(debug_optimizer_no_zero_cardinality,
  rocksdb_debug_optimizer_no_zero_cardinality,
  PLUGIN_VAR_RQCMDARG,
  "In case if cardinality is zero, overrides it with some value",
  nullptr, nullptr, TRUE);

static MYSQL_SYSVAR_STR(compact_cf, compact_cf_name,
  PLUGIN_VAR_RQCMDARG,
  "Compact column family",
  nullptr, rocksdb_compact_column_family, "");

static MYSQL_SYSVAR_STR(create_checkpoint, checkpoint_name,
  PLUGIN_VAR_RQCMDARG,
  "Checkpoint directory",
  rocksdb_create_checkpoint, rocksdb_create_checkpoint_stub, "");

static MYSQL_SYSVAR_BOOL(signal_drop_index_thread,
  rocksdb_signal_drop_index_thread,
  PLUGIN_VAR_RQCMDARG,
  "Wake up drop index thread",
  nullptr, rocksdb_drop_index_wakeup_thread, FALSE);

static MYSQL_SYSVAR_BOOL(pause_background_work,
  rocksdb_pause_background_work,
  PLUGIN_VAR_RQCMDARG,
  "Disable all rocksdb background operations",
  nullptr, rocksdb_set_pause_background_work, FALSE);

static MYSQL_SYSVAR_BOOL(strict_collation_check,
  rocksdb_strict_collation_check,
  PLUGIN_VAR_RQCMDARG,
  "Enforce case sensitive collation for MyRocks indexes",
  nullptr, nullptr, TRUE);

static MYSQL_SYSVAR_STR(strict_collation_exceptions,
  rocksdb_strict_collation_exceptions,
  PLUGIN_VAR_RQCMDARG|PLUGIN_VAR_MEMALLOC,
  "List of tables (using regex) that are excluded "
  "from the case sensitive collation enforcement",
  nullptr, rocksdb_set_collation_exception_list, "");

static MYSQL_SYSVAR_BOOL(collect_sst_properties,
  rocksdb_collect_sst_properties,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "Enables collecting SST file properties on each flush",
  nullptr, nullptr, rocksdb_collect_sst_properties);

static MYSQL_SYSVAR_BOOL(
  force_flush_memtable_now,
  rocksdb_force_flush_memtable_now_var,
  PLUGIN_VAR_RQCMDARG,
  "Forces memstore flush which may block all write requests so be careful",
  nullptr, rocksdb_force_flush_memtable_now, FALSE);

static MYSQL_THDVAR_BOOL(
  flush_memtable_on_analyze,
  PLUGIN_VAR_RQCMDARG,
  "Forces memtable flush on ANALZYE table to get accurate cardinality",
  nullptr, nullptr, true);

static MYSQL_SYSVAR_UINT(seconds_between_stat_computes,
  rocksdb_seconds_between_stat_computes,
  PLUGIN_VAR_RQCMDARG,
  "Sets a number of seconds to wait between optimizer stats recomputation. "
  "Only changed indexes will be refreshed.",
  nullptr, nullptr, rocksdb_seconds_between_stat_computes,
  /* min */ 0L, /* max */ UINT_MAX, 0);

static MYSQL_SYSVAR_LONGLONG(
  compaction_sequential_deletes,
  rocksdb_compaction_sequential_deletes,
  PLUGIN_VAR_RQCMDARG,
  "RocksDB will trigger compaction for the file if it has more than this number sequential deletes per window",
  nullptr, set_compaction_options, DEFAULT_COMPACTION_SEQUENTIAL_DELETES,
  /* min */ 0L, /* max */ MAX_COMPACTION_SEQUENTIAL_DELETES, 0);

static MYSQL_SYSVAR_LONGLONG(
  compaction_sequential_deletes_window,
  rocksdb_compaction_sequential_deletes_window,
  PLUGIN_VAR_RQCMDARG,
  "Size of the window for counting rocksdb_compaction_sequential_deletes",
  nullptr, set_compaction_options, DEFAULT_COMPACTION_SEQUENTIAL_DELETES_WINDOW,
  /* min */ 0L, /* max */ MAX_COMPACTION_SEQUENTIAL_DELETES_WINDOW, 0);

static MYSQL_SYSVAR_LONGLONG(
  compaction_sequential_deletes_file_size,
  rocksdb_compaction_sequential_deletes_file_size,
  PLUGIN_VAR_RQCMDARG,
  "Minimum file size required for compaction_sequential_deletes",
  nullptr, set_compaction_options, 0L,
  /* min */ -1L, /* max */ LONGLONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(compaction_sequential_deletes_count_sd,
  rocksdb_compaction_sequential_deletes_count_sd,
  PLUGIN_VAR_RQCMDARG,
  "Counting SingleDelete as rocksdb_compaction_sequential_deletes",
  nullptr, nullptr, rocksdb_compaction_sequential_deletes_count_sd);

static MYSQL_THDVAR_INT(checksums_pct,
  PLUGIN_VAR_RQCMDARG,
  "How many percentages of rows to be checksummed",
  nullptr, nullptr, 100,
  /* min */ 0, /* max */ 100, 0);

static MYSQL_THDVAR_BOOL(store_checksums,
  PLUGIN_VAR_RQCMDARG,
  "Include checksums when writing index/table records",
  nullptr, nullptr, false /* default value */);

static MYSQL_THDVAR_BOOL(verify_checksums,
  PLUGIN_VAR_RQCMDARG,
  "Verify checksums when reading index/table records",
  nullptr, nullptr, false /* default value */);

static MYSQL_SYSVAR_UINT(validate_tables,
  rocksdb_validate_tables,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "Verify all .frm files match all RocksDB tables (0 means no verification, "
  "1 means verify and fail on error, and 2 means verify but continue",
  nullptr, nullptr, 1 /* default value */, 0 /* min value */,
  2 /* max value */, 0);

static MYSQL_SYSVAR_STR(datadir,
  rocksdb_datadir,
  PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_READONLY,
  "RocksDB data directory",
  nullptr, nullptr, "./.rocksdb");

static MYSQL_SYSVAR_UINT(
  table_stats_sampling_pct,
  rocksdb_table_stats_sampling_pct,
  PLUGIN_VAR_RQCMDARG,
  "Percentage of entries to sample when collecting statistics about table "
  "properties. Specify either 0 to sample everything or percentage ["
  STRINGIFY_ARG(MYROCKS_SAMPLE_PCT_MIN) ".."
  STRINGIFY_ARG(MYROCKS_SAMPLE_PCT_MAX) "]. " "By default "
  STRINGIFY_ARG(MYROCKS_DEFAULT_SAMPLE_PCT) "% of entries are "
  "sampled.",
  nullptr, myrocks_set_table_stats_sampling_pct, /* default */
  MYROCKS_DEFAULT_SAMPLE_PCT, /* everything */ 0,
  /* max */ MYROCKS_SAMPLE_PCT_MAX, 0);

const longlong ROCKSDB_WRITE_BUFFER_SIZE_DEFAULT=4194304;
const int ROCKSDB_ASSUMED_KEY_VALUE_DISK_SIZE= 100;

static struct st_mysql_sys_var* rocksdb_system_variables[]= {
  MYSQL_SYSVAR(lock_wait_timeout),
  MYSQL_SYSVAR(max_row_locks),
  MYSQL_SYSVAR(bulk_load),
  MYSQL_SYSVAR(skip_unique_check),
  MYSQL_SYSVAR(commit_in_the_middle),
  MYSQL_SYSVAR(bulk_load_size),

  MYSQL_SYSVAR(create_if_missing),
  MYSQL_SYSVAR(create_missing_column_families),
  MYSQL_SYSVAR(error_if_exists),
  MYSQL_SYSVAR(paranoid_checks),
  MYSQL_SYSVAR(rate_limiter_bytes_per_sec),
  MYSQL_SYSVAR(info_log_level),
  MYSQL_SYSVAR(max_open_files),
  MYSQL_SYSVAR(max_total_wal_size),
  MYSQL_SYSVAR(disabledatasync),
  MYSQL_SYSVAR(use_fsync),
  MYSQL_SYSVAR(wal_dir),
  MYSQL_SYSVAR(delete_obsolete_files_period_micros),
  MYSQL_SYSVAR(base_background_compactions),
  MYSQL_SYSVAR(max_background_compactions),
  MYSQL_SYSVAR(max_background_flushes),
  MYSQL_SYSVAR(max_log_file_size),
  MYSQL_SYSVAR(log_file_time_to_roll),
  MYSQL_SYSVAR(keep_log_file_num),
  MYSQL_SYSVAR(max_manifest_file_size),
  MYSQL_SYSVAR(table_cache_numshardbits),
  MYSQL_SYSVAR(wal_ttl_seconds),
  MYSQL_SYSVAR(wal_size_limit_mb),
  MYSQL_SYSVAR(manifest_preallocation_size),
  MYSQL_SYSVAR(allow_os_buffer),
  MYSQL_SYSVAR(allow_mmap_reads),
  MYSQL_SYSVAR(allow_mmap_writes),
  MYSQL_SYSVAR(is_fd_close_on_exec),
  MYSQL_SYSVAR(stats_dump_period_sec),
  MYSQL_SYSVAR(advise_random_on_open),
  MYSQL_SYSVAR(db_write_buffer_size),
  MYSQL_SYSVAR(use_adaptive_mutex),
  MYSQL_SYSVAR(bytes_per_sync),
  MYSQL_SYSVAR(wal_bytes_per_sync),
  MYSQL_SYSVAR(enable_thread_tracking),
  MYSQL_SYSVAR(perf_context_level),
  MYSQL_SYSVAR(wal_recovery_mode),
  MYSQL_SYSVAR(access_hint_on_compaction_start),
  MYSQL_SYSVAR(new_table_reader_for_compaction_inputs),
  MYSQL_SYSVAR(compaction_readahead_size),
  MYSQL_SYSVAR(allow_concurrent_memtable_write),
  MYSQL_SYSVAR(enable_write_thread_adaptive_yield),

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
  MYSQL_SYSVAR(override_cf_options),

  MYSQL_SYSVAR(background_sync),

  MYSQL_SYSVAR(write_sync),
  MYSQL_SYSVAR(write_disable_wal),
  MYSQL_SYSVAR(write_ignore_missing_column_families),

  MYSQL_SYSVAR(skip_fill_cache),
  MYSQL_SYSVAR(unsafe_for_binlog),

  MYSQL_SYSVAR(records_in_range),
  MYSQL_SYSVAR(force_index_records_in_range),
  MYSQL_SYSVAR(debug_optimizer_n_rows),
  MYSQL_SYSVAR(debug_optimizer_no_zero_cardinality),

  MYSQL_SYSVAR(compact_cf),
  MYSQL_SYSVAR(signal_drop_index_thread),
  MYSQL_SYSVAR(pause_background_work),
  MYSQL_SYSVAR(strict_collation_check),
  MYSQL_SYSVAR(strict_collation_exceptions),
  MYSQL_SYSVAR(collect_sst_properties),
  MYSQL_SYSVAR(force_flush_memtable_now),
  MYSQL_SYSVAR(flush_memtable_on_analyze),
  MYSQL_SYSVAR(seconds_between_stat_computes),

  MYSQL_SYSVAR(compaction_sequential_deletes),
  MYSQL_SYSVAR(compaction_sequential_deletes_window),
  MYSQL_SYSVAR(compaction_sequential_deletes_file_size),
  MYSQL_SYSVAR(compaction_sequential_deletes_count_sd),

  MYSQL_SYSVAR(datadir),
  MYSQL_SYSVAR(create_checkpoint),

  MYSQL_SYSVAR(checksums_pct),
  MYSQL_SYSVAR(store_checksums),
  MYSQL_SYSVAR(verify_checksums),

  MYSQL_SYSVAR(validate_tables),
  MYSQL_SYSVAR(table_stats_sampling_pct),

  nullptr
};

rocksdb::WriteOptions get_write_options(THD* thd)
{
  rocksdb::WriteOptions opt;

  opt.sync = THDVAR(thd, write_sync);
  opt.disableWAL = THDVAR(thd, write_disable_wal);
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
                             my_bool not_used MY_ATTRIBUTE((__unused__)))
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
  key_mutex_drop_index, key_drop_index_interrupt_mutex,
  key_mutex_trx_list, key_mutex_collation_exception_list,
  key_mutex_sysvar;

static PSI_mutex_info all_rocksdb_mutexes[]=
{
  { &ex_key_mutex_example, "rocksdb", PSI_FLAG_GLOBAL},
  { &ex_key_mutex_ROCKSDB_SHARE_mutex, "ROCKSDB_SHARE::mutex", 0},
  { &key_mutex_background, "background", PSI_FLAG_GLOBAL},
  { &key_mutex_stop_background, "stop background", PSI_FLAG_GLOBAL},
  { &key_mutex_drop_index, "drop index", PSI_FLAG_GLOBAL},
  { &key_drop_index_interrupt_mutex, "drop index interrupt", PSI_FLAG_GLOBAL},
  { &key_mutex_trx_list, "trx_list", PSI_FLAG_GLOBAL},
  { &key_mutex_collation_exception_list, "collation_exception_list",
      PSI_FLAG_GLOBAL},
  { &key_mutex_sysvar, "setting sysvar", PSI_FLAG_GLOBAL},
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

  if (PSI_server == nullptr)
    return;

  count= array_elements(all_rocksdb_mutexes);
  PSI_server->register_mutex(category, all_rocksdb_mutexes, count);

  count= array_elements(all_rocksdb_conds);
  // TODO Disabling PFS for conditions due to the bug https://github.com/MySQLOnRocksDB/mysql-5.6/issues/92
  // PSI_server->register_cond(category, all_rocksdb_conds, count);

  count= array_elements(all_rocksdb_stages);
  mysql_stage_register(category, all_rocksdb_stages, count);

  count= array_elements(all_rocksdb_threads);
  mysql_thread_register(category, all_rocksdb_threads, count);
}
#endif

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

static mysql_mutex_t drop_index_mutex;
static mysql_mutex_t drop_index_interrupt_mutex;
static mysql_cond_t drop_index_interrupt_cond;
static bool stop_drop_index_thread;

class Rdb_CompactionFilter : public rocksdb::CompactionFilter
{
public:
  explicit Rdb_CompactionFilter(uint32_t _cf_id) : cf_id(_cf_id) {}
  ~Rdb_CompactionFilter()
  {
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

    GL_INDEX_ID gl_index_id;
    gl_index_id.cf_id= cf_id;
    gl_index_id.index_id= read_big_uint4((const uchar*)key.data());
    DBUG_ASSERT(gl_index_id.index_id >= 1);

    if (gl_index_id != prev_index)  // processing new index id
    {
      if (num_deleted > 0)
      {
        num_deleted= 0;
      }
      should_delete= dict_manager.is_drop_index_ongoing(gl_index_id);
      prev_index= gl_index_id;
    }

    if (should_delete)
      num_deleted++;

    return should_delete;
  }

  virtual bool IgnoreSnapshots() const override { return true; }

  virtual const char* Name() const override {
    return "Rdb_CompactionFilter";
  }
private:

  // Column family for this compaction filter
  const uint32_t cf_id;
  // Index id of the previous record
  mutable GL_INDEX_ID prev_index= {0, 0};
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
      new Rdb_CompactionFilter(context.column_family_id));
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
  ulonglong n_writes= 0;
  ulonglong n_locks= 0;

  rocksdb::Transaction *txn= nullptr;

  bool is_trx_failed= false;
  bool is_delayed_snapshot= false;
  bool tx_read_only= false;
  bool rollback_only= false;

  THD* thd_= nullptr;

  rocksdb::ReadOptions read_opts;

  static std::multiset<Rdb_transaction*> trx_list;
  static mysql_mutex_t trx_list_mutex;

  /*
    This is a helper class that is passed to RocksDB to get notifications when
    a snapshot gets created.
  */
  class SnapshotNotifier : public rocksdb::TransactionNotifier
  {
    Rdb_transaction* rtrans_;

    void SnapshotCreated(const rocksdb::Snapshot *snapshot) override
    {
      if (rtrans_ != nullptr)
        rtrans_->snapshot_created(snapshot);
    }

   public:
    explicit SnapshotNotifier(Rdb_transaction* rtrans) : rtrans_(rtrans) {}

    // If the owning Rdb_transaction gets destructed we need to not reference
    // it anymore.
    void RemoveTransaction()
    {
      rtrans_ = nullptr;
    }
  };
  std::shared_ptr<SnapshotNotifier> notifier;

public:
  const char* mysql_log_file_name;
  my_off_t mysql_log_offset;
  const char* mysql_gtid;
  String detailed_error;

  class TransactionListWalker {
   public:
    virtual ~TransactionListWalker() {}
    virtual void processTransaction(const Rdb_transaction*) = 0;
  };

  static void init_mutex()
  {
    mysql_mutex_init(key_mutex_trx_list, &trx_list_mutex, MY_MUTEX_INIT_FAST);
  }

  static void term_mutex()
  {
    DBUG_ASSERT(trx_list.size() == 0);
    mysql_mutex_destroy(&trx_list_mutex);
  }

  static void walk_trx_list(TransactionListWalker* walker)
  {
    mysql_mutex_lock(&trx_list_mutex);

    for (auto it = trx_list.begin(); it != trx_list.end(); it++)
      walker->processTransaction(*it);

    mysql_mutex_unlock(&trx_list_mutex);
  }

  void set_trx_failed(bool failed_arg) { is_trx_failed= failed_arg; }

  int set_status_error(THD *thd, const rocksdb::Status &s,
                       RDBSE_KEYDEF *key_descr, RDBSE_TABLE_DEF *tbl_def)
  {
    DBUG_ASSERT(!s.ok());

    if (s.IsTimedOut())
    {
      /*
        SQL layer has weird expectations. If we return an error when
        doing a read in DELETE IGNORE, it will ignore the error ("because it's
        an IGNORE command!) but then will fail an assert, because "error code
        was returned, but no error happened".  Do what InnoDB's
        convert_error_code_to_mysql() does: force a statement
        rollback before returning HA_ERR_LOCK_WAIT_TIMEOUT:
        */
      thd_mark_transaction_to_rollback(thd, false /*just statement*/);
      detailed_error.copy(timeout_message("index",
                                          tbl_def->dbname_tablename.c_ptr(),
                                          key_descr->get_name().c_str()));

      return HA_ERR_LOCK_WAIT_TIMEOUT;
    }
    if (s.IsBusy())
    {
      return HA_ERR_LOCK_DEADLOCK;
    }
    /* TODO: who returns HA_ERR_ROCKSDB_TOO_MANY_LOCKS now?? */

    my_error(ER_INTERNAL_ERROR, MYF(0), s.ToString().c_str());
    return HA_ERR_INTERNAL_ERROR;
  }

  int timeout_sec; /* Cached value of @@rocksdb_lock_wait_timeout */

  /* Maximum number of locks the transaction can have */
  ulonglong max_row_locks;

  bool ddl_transaction;

  THD* get_thd() const { return thd_; }

  void set_params(int timeout_sec_arg, int max_row_locks_arg)
  {
    timeout_sec= timeout_sec_arg;
    max_row_locks= max_row_locks_arg;
    if (txn)
      txn->SetLockTimeout(timeout_sec * 1000);
  }

  ulonglong get_n_writes() { return n_writes; }

  void release_lock(rocksdb::ColumnFamilyHandle* column_family,
                    const std::string &rowkey)
  {
    /*
      TODO: Need RocksDB's transaction API implement this call:
      txn->UnLock(column_family->GetID(), rowkey);
    */
  }

  bool commit_or_rollback()
  {
    bool res;
    if (is_trx_failed)
    {
      rollback();
      res= false;
    }
    else
      res= commit();
    return res;
  }

  bool commit()
  {
    if (get_n_writes() == 0)
    {
      rollback();
      return false;
    }
    else if (rollback_only)
    {
     /*
       Transactions marked as rollback_only are expected to be rolled back at
       prepare(). But there are some exceptions like below that prepare() is
       never called and commit() is called instead.
        1. Binlog is disabled
        2. No modification exists in binlog cache for the transaction (#195)
       In both cases, rolling back transaction is safe. Nothing is written to
       binlog.
      */
      my_printf_error(ER_UNKNOWN_ERROR, ERRSTR_ROLLBACK_ONLY, MYF(0));
      rollback();
      return true;
    }
    else
    {
      thd_binlog_pos(thd_, &mysql_log_file_name, &mysql_log_offset,
                     &mysql_gtid);
      binlog_manager.update(mysql_log_file_name,
                            mysql_log_offset,
                            mysql_gtid, txn->GetWriteBatch());
      return commit_no_binlog();
    }
  }

  bool commit_no_binlog()
  {
    bool res= false;
    release_snapshot();
    rocksdb::Status s= txn->Commit();
    if (!s.ok())
    {
      rocksdb_handle_io_error(s, ROCKSDB_IO_ERROR_TRX_COMMIT);
      res= true;
    }
    delete txn;
    txn= nullptr;
    n_writes= 0;
    n_locks= 0;
    set_tx_read_only(false);
    rollback_only= false;
    return res;
  }

  void rollback()
  {
    n_writes= 0;
    n_locks= 0;
    ddl_transaction= false;
    if (txn)
    {
      release_snapshot();
      /* This will also release all of the locks: */
      txn->Rollback();
      delete txn;
      txn= nullptr;
      set_tx_read_only(false);
      rollback_only= false;
    }
  }

  int64_t snapshot_timestamp_= 0;

  void snapshot_created(const rocksdb::Snapshot *snapshot)
  {
    read_opts.snapshot = snapshot;
    rdb->GetEnv()->GetCurrentTime(&snapshot_timestamp_);
    is_delayed_snapshot = false;
  }

  void acquire_snapshot(bool acquire_now)
  {
    if (read_opts.snapshot == nullptr) {
      if (is_tx_read_only()) {
        snapshot_created(rdb->GetSnapshot());
      }
      else if (acquire_now) {
        txn->SetSnapshot();
        snapshot_created(txn->GetSnapshot());
      }
      else if (!is_delayed_snapshot) {
        txn->SetSnapshotOnNextOperation(notifier);
        is_delayed_snapshot = true;
      }
    }
  }

  void release_snapshot()
  {
    bool need_clear = is_delayed_snapshot;

    if (read_opts.snapshot != nullptr)
    {
      snapshot_timestamp_ = 0;
      if (is_tx_read_only())
      {
        rdb->ReleaseSnapshot(read_opts.snapshot);
        need_clear = false;
      }
      else
      {
        need_clear = true;
      }
      read_opts.snapshot = nullptr;
    }

    if (need_clear && txn != nullptr)
      txn->ClearSnapshot();
  }

  bool has_snapshot()
  {
    return read_opts.snapshot != nullptr;
  }

  /*
    Flush the data accumulated so far. This assumes we're doing a bulk insert.

    @detail
      This should work like transaction commit, except that we don't
      synchronize with the binlog (there is no API that would allow to have
      binlog flush the changes accumulated so far and return its current
      position)

    @todo
      Add test coverage for what happens when somebody attempts to do bulk
      inserts while inside a multi-statement transaction.
  */
  bool flush_batch()
  {
    if (get_n_writes() == 0)
      return false;

    /* Commit the current transaction */
    bool res;
    if ((res= commit_no_binlog()))
      return res;

    /* Start another one */
    start_trx();
    return false;
  }

  const char *err_too_many_locks=
    "Number of locks held by the transaction exceeded @@rocksdb_max_row_locks";

  rocksdb::Status Put(rocksdb::ColumnFamilyHandle* column_family,
                      const rocksdb::Slice& key, const rocksdb::Slice& value)
  {
    ++n_writes;
    ++n_locks;
    if (n_writes > max_row_locks || n_locks > max_row_locks)
      return rocksdb::Status::Aborted(rocksdb::Slice(err_too_many_locks));
    return txn->Put(column_family, key, value);
  }

  rocksdb::Status Delete(rocksdb::ColumnFamilyHandle* column_family,
                         const rocksdb::Slice& key)
  {
    ++n_writes;
    ++n_locks;
    if (n_writes > max_row_locks || n_locks > max_row_locks)
      return rocksdb::Status::Aborted(rocksdb::Slice(err_too_many_locks));
    return txn->Delete(column_family, key);
  }

  rocksdb::Status SingleDelete(rocksdb::ColumnFamilyHandle* column_family,
                               const rocksdb::Slice& key)
  {
    ++n_writes;
    ++n_locks;
    if (n_writes > max_row_locks || n_locks > max_row_locks)
      return rocksdb::Status::Aborted(rocksdb::Slice(err_too_many_locks));
    return txn->SingleDelete(column_family, key);
  }

  bool has_modifications()
  {
    return txn->GetWriteBatch() && txn->GetWriteBatch()->GetWriteBatch() &&
           txn->GetWriteBatch()->GetWriteBatch()->Count() > 0;
  }

  /*
    Return a WriteBatch that one can write to. The writes will skip any
    transaction locking. The writes WILL be visible to the transaction.
  */
  rocksdb::WriteBatchBase* GetIndexedWriteBatch()
  {
    ++n_writes;
    return txn->GetWriteBatch();
  }

  /*
    Return a WriteBatch that one can write to. The writes will skip any
    transaction locking. The writes will NOT be visible to the transaction.
  */
  rocksdb::WriteBatchBase* GetBlindWriteBatch()
  {
    ++n_writes;
    return txn->GetWriteBatch()->GetWriteBatch();
  }

  rocksdb::Status Get(rocksdb::ColumnFamilyHandle* column_family,
                      const rocksdb::Slice& key, std::string* value)
  {
    return txn->Get(read_opts, column_family, key, value);
  }

  rocksdb::Status GetForUpdate(rocksdb::ColumnFamilyHandle* column_family,
                               const rocksdb::Slice& key, std::string* value)
  {
    if (++n_locks > max_row_locks)
      return rocksdb::Status::Aborted(rocksdb::Slice(err_too_many_locks));
    return txn->GetForUpdate(read_opts, column_family, key, value);
  }

  rocksdb::Iterator *GetIterator(rocksdb::ColumnFamilyHandle* column_family,
                                 bool skip_bloom_filter,
                                 bool fill_cache,
                                 bool read_current= false,
                                 bool create_snapshot= true)
  {
    // Make sure we are not doing both read_current (which implies we don't
    // want a snapshot) and create_snapshot which makes sure we create
    // a snapshot
    DBUG_ASSERT(!read_current || !create_snapshot);

    if (create_snapshot)
      acquire_snapshot(true);

    rocksdb::ReadOptions options= read_opts;

    if (skip_bloom_filter)
    {
      options.total_order_seek= true;
    }
    else
    {
      // With this option, Iterator::Valid() returns false if key
      // is outside of the prefix bloom filter range set at Seek().
      // Must not be set to true if not using bloom filter.
      options.prefix_same_as_start= true;
    }
    options.fill_cache= fill_cache;
    if (read_current)
      options.snapshot= nullptr;
    rocksdb::Iterator* rocksdb_it= txn->GetIterator(options, column_family);
    return rocksdb_it;
  }

  bool is_trx_started()
  {
    return (txn != nullptr);
  }

  void start_trx()
  {
    rocksdb::TransactionOptions txn_opts;
    rocksdb::WriteOptions write_opts;
    txn_opts.set_snapshot= false;
    txn_opts.lock_timeout= timeout_sec * 1000;

    write_opts.sync= THDVAR(thd_, write_sync);
    write_opts.disableWAL= THDVAR(thd_, write_disable_wal);
    write_opts.ignore_missing_column_families=
      THDVAR(thd_, write_ignore_missing_column_families);

    txn= rdb->BeginTransaction(write_opts, txn_opts);

    read_opts= rocksdb::ReadOptions();

    ddl_transaction= false;
  }

  /*
    Start a statement inside a multi-statement transaction.

    @todo: are we sure this is called once (and not several times) per
    statement start?

    For hooking to start of statement that is its own transaction, see
    ha_rocksdb::external_lock().
  */
  void start_stmt()
  {
    // Set the snapshot to delayed acquisition (SetSnapshotOnNextOperation)
    acquire_snapshot(false);
    txn->SetSavePoint();
  }

  /*
    This must be called when last statement is rolled back, but the transaction
    continues
  */
  void rollback_stmt()
  {
    /* TODO: here we must release the locks taken since the start_stmt() call */
    if (txn)
    {
      const rocksdb::Snapshot *org_snapshot = txn->GetSnapshot();
      txn->RollbackToSavePoint();

      const rocksdb::Snapshot *cur_snapshot = txn->GetSnapshot();
      if (org_snapshot != cur_snapshot)
      {
        if (org_snapshot != nullptr)
          snapshot_timestamp_ = 0;

        read_opts.snapshot = cur_snapshot;
        if (cur_snapshot != nullptr)
          rdb->GetEnv()->GetCurrentTime(&snapshot_timestamp_);
        else
          is_delayed_snapshot = true;
      }
    }
  }

  bool can_prepare()
  {
    if (rollback_only)
    {
      my_printf_error(ER_UNKNOWN_ERROR, ERRSTR_ROLLBACK_ONLY, MYF(0));
      return false;
    }
    return true;
  }

  int rollback_to_savepoint(void *savepoint)
  {
    if (has_modifications())
    {
      my_printf_error(ER_UNKNOWN_ERROR,
                      "MyRocks currently does not support ROLLBACK TO "
                      "SAVEPOINT if modifying rows.",
                      MYF(0));
      rollback_only= true;
      return 1;
    }
    return 0;
  }

  /*
    This is used by transactions started with "START TRANSACTION WITH "
    "CONSISTENT [ROCKSDB] SNAPSHOT". When tx_read_only is turned on,
    snapshot has to be created via DB::GetSnapshot(), not via Transaction
    API.
  */
  bool is_tx_read_only() const
  {
    return tx_read_only;
  }

  void set_tx_read_only(bool val)
  {
    tx_read_only= val;
  }

  explicit Rdb_transaction(THD *thd) : txn(nullptr), thd_(thd)
  {
    // Create a notifier that can be called when a snapshot gets generated.
    notifier = std::make_shared<SnapshotNotifier>(this);

    mysql_mutex_lock(&trx_list_mutex);
    trx_list.insert(this);
    mysql_mutex_unlock(&trx_list_mutex);
  }

  virtual ~Rdb_transaction()
  {
    rollback();

    mysql_mutex_lock(&trx_list_mutex);
    trx_list.erase(this);
    mysql_mutex_unlock(&trx_list_mutex);

    // Theoretically the notifier could outlive the Rdb_transaction (because
    // of the shared_ptr), so let it know it can't reference the transaction
    // anymore.
    notifier->RemoveTransaction();
  }
};

std::multiset<Rdb_transaction*> Rdb_transaction::trx_list;
mysql_mutex_t Rdb_transaction::trx_list_mutex;

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
  // TODO: this is called too many times.. O(#rows)
  if (trx == nullptr)
  {
    trx= new Rdb_transaction(thd);
    trx->set_params(THDVAR(thd, lock_wait_timeout),
                    THDVAR(thd, max_row_locks));
    trx->start_trx();
  }
  else
  {
    trx->set_params(THDVAR(thd, lock_wait_timeout),
                    THDVAR(thd, max_row_locks));
    if (!trx->is_trx_started())
    {
      trx->start_trx();
    }
  }
  return trx;
}


static int rocksdb_close_connection(handlerton* hton, THD* thd)
{
  Rdb_transaction*& trx= get_trx_from_thd(thd);
  delete trx;
  trx= nullptr;
  return 0;
}

/**
  Doing nothing at prepare() on master. But defining handler::prepare() is
  needed for Xid binlog event to be written at commit.

  For a slave, prepare() updates the slave_gtid_info table which tracks the
  replication progress.
*/
static int rocksdb_prepare(handlerton* hton, THD* thd, bool prepare_trx,
                           bool async)
{
  Rdb_transaction*& trx= get_trx_from_thd(thd);
  if (!trx->can_prepare())
  {
    return 1;
  }
  if (prepare_trx ||
      (!thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN))) {
    /* We were instructed to prepare the whole transaction, or
    this is an SQL statement end and autocommit is on */
    std::vector<st_slave_gtid_info> slave_gtid_info;
    thd_slave_gtid_info(thd, &slave_gtid_info);
    for (auto it : slave_gtid_info) {
      rocksdb::WriteBatchBase* write_batch = trx->GetBlindWriteBatch();
      binlog_manager.update_slave_gtid_info(it.id, it.db, it.gtid, write_batch);
    }
  }

  return 0;
}

/**
 do nothing for prepare/commit by xid
 this is needed to avoid crashes in XA scenarios
*/
static int rocksdb_commit_by_xid(handlerton* hton,	XID* xid)
{
  return 0;
}

static int rocksdb_rollback_by_xid(handlerton* hton,	XID* xid)
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
        memcpy(binlog_file, file_buf, FN_REFLEN + 1);
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

  rdb_perf_context_local local_perf_context;
  RDB_PERF_CONTEXT_GUARD(local_perf_context, nullptr, global_perf_context);

  /* note: h->external_lock(F_UNLCK) is called after this function is called) */

  Rdb_transaction*& trx= get_trx_from_thd(thd);
  if (trx != nullptr)
  {
    if (commit_trx ||
       (!thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN)))
    {
      /*
        We get here
         - For a COMMIT statement that finishes a multi-statement transaction
         - For a statement that has its own transaction
      */
      if (trx->commit())
        DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
    }
    else
    {
      /*
        We get here when committing a statement within a transaction.

        We don't need to do anything here. trx->start_stmt() will notify
        Rdb_transaction that another statement has started.
      */
      trx->set_trx_failed(false);
    }

    if (thd_tx_isolation(thd) <= ISO_READ_COMMITTED)
    {
      // For READ_COMMITTED, we release any existing snapshot so that we will
      // see any changes that occurred since the last statement.
      trx->release_snapshot();
    }
  }
  DBUG_RETURN(0);
}


static int rocksdb_rollback(handlerton* hton, THD* thd, bool rollback_trx)
{
  rdb_perf_context_local local_perf_context;
  RDB_PERF_CONTEXT_GUARD(local_perf_context, nullptr, global_perf_context);

  Rdb_transaction*& trx= get_trx_from_thd(thd);
  if (trx != nullptr)
  {
    if (rollback_trx)
    {
      /*
        We get here, when
        - ROLLBACK statement is issued.

        Discard the changes made by the transaction
      */
      trx->rollback();
    }
    else
    {
      /*
        We get here when
        - a statement with AUTOCOMMIT=1 is being rolled back (because of some
          error)
        - a statement inside a transaction is rolled back
      */

      trx->rollback_stmt();
      trx->set_trx_failed(true);
    }

    if (thd_tx_isolation(thd) <= ISO_READ_COMMITTED)
    {
      // For READ_COMMITTED, we release any existing snapshot so that we will
      // see any changes that occurred since the last statement.
      trx->release_snapshot();
    }
  }
  return 0;
}

static bool print_stats(THD* thd,
                        std::string const& type,
                        std::string const& name,
                        std::string const& status,
                        stat_print_fn *stat_print)
{
  return stat_print(thd, type.c_str(), type.size(), name.c_str(), name.size(),
                    status.c_str(), status.size());
}

static std::string format_string(
  const char *format,
  ...)
{
  std::string res;
  va_list     args;
  va_list     args_copy;

  va_start(args, format);
  va_copy(args_copy, args);

  size_t len = vsnprintf(nullptr, 0, format, args) + 1;
  va_end(args);

  if (len == 0) {
    res = std::string("");
  }
  else {
    char buff[len];
    (void) vsnprintf(buff, len, format, args_copy);

    res = std::string(buff);
  }

  va_end(args_copy);

  return res;
}

class ShowSnapshotStatus : public Rdb_transaction::TransactionListWalker
{
 private:
  std::string data;

  std::string current_timestamp(void)
  {
    static const char *const format = "%d-%02d-%02d %02d:%02d:%02d";
    time_t currtime;
    struct tm currtm;

    time(&currtime);

    localtime_r(&currtime, &currtm);

    return format_string(format, currtm.tm_year + 1900, currtm.tm_mon + 1,
                         currtm.tm_mday, currtm.tm_hour, currtm.tm_min,
                         currtm.tm_sec);
  }

  std::string get_header(void)
  {
    return
      "\n============================================================\n" +
      current_timestamp() +
      " ROCKSDB TRANSACTION MONITOR OUTPUT\n"
      "============================================================\n"
      "---------\n"
      "SNAPSHOTS\n"
      "---------\n"
      "LIST OF SNAPSHOTS FOR EACH SESSION:\n";
  }

  std::string get_footer(void)
  {
    return
        "-----------------------------------------\n"
        "END OF ROCKSDB TRANSACTION MONITOR OUTPUT\n"
        "=========================================\n";
  }

 public:
  ShowSnapshotStatus() : data(get_header()) {}

  std::string getResult() { return data + get_footer(); }

  /* Create one row in the snapshot status table */
  void processTransaction(const Rdb_transaction *trx)
  {
    /* Calculate the duration the snapshot has existed */
    int64_t snapshot_timestamp = trx->snapshot_timestamp_;
    if (snapshot_timestamp != 0)
    {
      int64_t curr_time;
      rdb->GetEnv()->GetCurrentTime(&curr_time);

      THD* thd = trx->get_thd();

      data += format_string("---SNAPSHOT, ACTIVE %lld sec\n"
                            "MySQL thread id %lu, OS thread handle %p\n",
                            curr_time - snapshot_timestamp,
                            thd_get_thread_id(thd), thd);
    }
  }
};

/* Generate the snapshot status table */
static bool rocksdb_show_snapshot_status(handlerton*    hton,
                                         THD*           thd,
                                         stat_print_fn* stat_print)
{
  ShowSnapshotStatus showStatus;

  Rdb_transaction::walk_trx_list(&showStatus);

  // Send the result data back to MySQL */
  return print_stats(thd, "SNAPSHOTS", "rocksdb", showStatus.getResult(),
      stat_print);
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
      res |= print_stats(thd, "DBSTATS", "rocksdb", str, stat_print);
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

      res |= print_stats(thd, "CF_COMPACTION", cf_name, str, stat_print);
    }

    /* Memory Statistics */
    std::vector<rocksdb::DB*> dbs;
    std::unordered_set<const rocksdb::Cache*> cache_set;
    size_t internal_cache_count = 0;
    size_t kDefaultInternalCacheSize = 8 * 1024 * 1024;
    char buf[100];

    dbs.push_back(rdb);
    cache_set.insert(table_options.block_cache.get());
    for (const auto& cf_handle : cf_manager.get_all_cf())
    {
      rocksdb::ColumnFamilyDescriptor cf_desc;
      cf_handle->GetDescriptor(&cf_desc);
      auto* table_factory = cf_desc.options.table_factory.get();
      if (table_factory != nullptr)
      {
        std::string tf_name = table_factory->Name();
        if (tf_name.find("BlockBasedTable") != std::string::npos)
        {
          const rocksdb::BlockBasedTableOptions* bbt_opt =
            reinterpret_cast<rocksdb::BlockBasedTableOptions*>(
              table_factory->GetOptions());
          if (bbt_opt != nullptr)
          {
            if (bbt_opt->block_cache.get() != nullptr)
            {
              cache_set.insert(bbt_opt->block_cache.get());
            }
            else
            {
              internal_cache_count++;
            }
            cache_set.insert(bbt_opt->block_cache_compressed.get());
          }
        }
      }
    }

    std::map<rocksdb::MemoryUtil::UsageType, uint64_t> temp_usage_by_type;
    str.clear();
    rocksdb::MemoryUtil::GetApproximateMemoryUsageByType(
      dbs, cache_set, &temp_usage_by_type);
    snprintf(buf, sizeof(buf), "\nMemTable Total: %lu",
             temp_usage_by_type[rocksdb::MemoryUtil::kMemTableTotal]);
    str.append(buf);
    snprintf(buf, sizeof(buf), "\nMemTable Unflushed: %lu",
             temp_usage_by_type[rocksdb::MemoryUtil::kMemTableUnFlushed]);
    str.append(buf);
    snprintf(buf, sizeof(buf), "\nTable Readers Total: %lu",
             temp_usage_by_type[rocksdb::MemoryUtil::kTableReadersTotal]);
    str.append(buf);
    snprintf(buf, sizeof(buf), "\nCache Total: %lu",
             temp_usage_by_type[rocksdb::MemoryUtil::kCacheTotal]);
    str.append(buf);
    snprintf(buf, sizeof(buf), "\nDefault Cache Capacity: %lu",
             internal_cache_count * kDefaultInternalCacheSize);
    str.append(buf);
    res |= print_stats(thd, "Memory_Stats", "rocksdb", str, stat_print);
  }
  else if (stat_type == HA_ENGINE_TRX)
  {
    /* Handle the SHOW ENGINE ROCKSDB TRANSACTION STATUS command */
    res |= rocksdb_show_snapshot_status(hton, thd, stat_print);
  }

  return res;
}

static inline void rocksdb_register_trx(handlerton *hton, THD *thd,
                                        Rdb_transaction *trx)
{
  trans_register_ha(thd, FALSE, rocksdb_hton);
  if (thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN))
  {
    trx->start_stmt();
    trans_register_ha(thd, TRUE, rocksdb_hton);
  }
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
  rdb_perf_context_local local_perf_context;
  RDB_PERF_CONTEXT_GUARD(local_perf_context, nullptr, global_perf_context);

  ulong const tx_isolation = thd_tx_isolation(thd);

  if (tx_isolation != ISO_REPEATABLE_READ)
  {
    my_printf_error(ER_UNKNOWN_ERROR,
                    "Only REPEATABLE READ isolation level is supported "
                    "for START TRANSACTION WITH CONSISTENT SNAPSHOT "
                    "in RocksDB Storage Engine.", MYF(0));
    return 1;
  }

  if (binlog_file)
  {
    if (binlog_pos && mysql_bin_log_is_open())
      mysql_bin_log_lock_commits();
    else
      return 1;
  }

  Rdb_transaction* trx= get_or_create_trx(thd);
  DBUG_ASSERT(!trx->has_snapshot());
  trx->set_tx_read_only(true);
  rocksdb_register_trx(hton, thd, trx);
  trx->acquire_snapshot(true);

  if (binlog_file)
    mysql_bin_log_unlock_commits(binlog_file, binlog_pos, gtid_executed,
                                 gtid_executed_length);

  return 0;
}

/* Dummy SAVEPOINT support. This is needed for long running transactions
 * like mysqldump (https://bugs.mysql.com/bug.php?id=71017).
 * Current SAVEPOINT does not correctly handle ROLLBACK and does not return
 * errors. This needs to be addressed in future versions (Issue#96).
 */
static int rocksdb_savepoint(handlerton *hton, THD *thd, void *savepoint)
{
  return 0;
}

static int rocksdb_rollback_to_savepoint(handlerton *hton, THD *thd,
                                         void *savepoint)
{
  Rdb_transaction*& trx= get_trx_from_thd(thd);
  return trx->rollback_to_savepoint(savepoint);
}

static bool rocksdb_rollback_to_savepoint_can_release_mdl(handlerton *hton,
                                                          THD *thd)
{
  return true;
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
    char dbname_sys[NAME_LEN + 1];
    char tablename_sys[NAME_LEN + 1];
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

    /*
      Table stats expects our database and table name to be in system encoding,
      not filename format. Convert before calling callback.
     */
    filename_to_tablename(dbname.c_ptr(), dbname_sys, sizeof(dbname_sys));
    filename_to_tablename(tablename.c_ptr(), tablename_sys,
                          sizeof(tablename_sys));
    (*cb)(dbname_sys, tablename_sys, is_partition, &io_perf_read, &io_perf,
          &io_perf, &io_perf, &io_perf, &page_stats, &comp_stats, 0, 0,
          rocksdb_hton_name);
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

static rocksdb::Status check_rocksdb_options_compatibility(
        const char *dbpath,
        const rocksdb::Options& main_opts,
        const std::vector<rocksdb::ColumnFamilyDescriptor>& cf_descr)
{
  DBUG_ASSERT(rocksdb_datadir != nullptr);

  rocksdb::DBOptions loaded_db_opt;
  std::vector<rocksdb::ColumnFamilyDescriptor> loaded_cf_descs;
  rocksdb::Status status = LoadLatestOptions(dbpath,
                            rocksdb::Env::Default(), &loaded_db_opt,
                            &loaded_cf_descs);

  // If we're starting from scratch and there are no options saved yet then this
  // is a valid case. Therefore we can't compare the current set of options to
  // anything.
  if (status.IsNotFound()) {
    return rocksdb::Status::OK();
  }

  if (!status.ok()) {
    return status;
  }

  if (loaded_cf_descs.size() != cf_descr.size()) {
    return rocksdb::Status::NotSupported("Mismatched size of column family " \
            "descriptors.");
  }

  // Please see RocksDB documentation for more context about why we need to set
  // user-defined functions and pointer-typed options manually.
  for (size_t i = 0; i < loaded_cf_descs.size(); i++) {
    loaded_cf_descs[i].options.compaction_filter =
      cf_descr[i].options.compaction_filter;
    loaded_cf_descs[i].options.compaction_filter_factory  =
      cf_descr[i].options.compaction_filter_factory;
    loaded_cf_descs[i].options.comparator = cf_descr[i].options.comparator;
    loaded_cf_descs[i].options.memtable_factory =
      cf_descr[i].options.memtable_factory;
    loaded_cf_descs[i].options.merge_operator =
      cf_descr[i].options.merge_operator;
    loaded_cf_descs[i].options.prefix_extractor =
      cf_descr[i].options.prefix_extractor;
    loaded_cf_descs[i].options.table_factory =
      cf_descr[i].options.table_factory;
  }

  // This is the essence of the function - determine if it's safe to open the
  // database or not.
  status = CheckOptionsCompatibility(dbpath, rocksdb::Env::Default(),
                                     main_opts, loaded_cf_descs);

  return status;
}

/*
  Storage Engine initialization function, invoked when plugin is loaded.
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
  mysql_mutex_init(key_mutex_collation_exception_list,
                   &collation_exception_list_mutex, MY_MUTEX_INIT_FAST);
  mysql_mutex_init(key_mutex_sysvar, &sysvar_mutex, MY_MUTEX_INIT_FAST);
  mysql_cond_init(key_cond_stop, &stop_cond, nullptr);
  (void) my_hash_init(&rocksdb_open_tables,system_charset_info,32,0,0,
                      (my_hash_get_key) rocksdb_get_key,0,0);
  Rdb_transaction::init_mutex();

  rocksdb_hton->state=    SHOW_OPTION_YES;
  rocksdb_hton->create=   rocksdb_create_handler;
  rocksdb_hton->close_connection= rocksdb_close_connection;
  rocksdb_hton->prepare=   rocksdb_prepare;
  rocksdb_hton->commit_by_xid=   rocksdb_commit_by_xid;
  rocksdb_hton->rollback_by_xid=   rocksdb_rollback_by_xid;
  rocksdb_hton->recover=   rocksdb_recover;
  rocksdb_hton->commit=   rocksdb_commit;
  rocksdb_hton->rollback= rocksdb_rollback;
  rocksdb_hton->db_type=  DB_TYPE_ROCKSDB;
  rocksdb_hton->show_status= rocksdb_show_status;
  rocksdb_hton->start_consistent_snapshot=
    rocksdb_start_trx_and_assign_read_view;
  rocksdb_hton->savepoint_set= rocksdb_savepoint;
  rocksdb_hton->savepoint_rollback= rocksdb_rollback_to_savepoint;
  rocksdb_hton->savepoint_rollback_can_release_mdl=
    rocksdb_rollback_to_savepoint_can_release_mdl;
  rocksdb_hton->update_table_stats = rocksdb_update_table_stats;

  rocksdb_hton->flags= HTON_TEMPORARY_NOT_SUPPORTED |
                       HTON_SUPPORTS_EXTENDED_KEYS |
                       HTON_CAN_RECREATE;

  mysql_mutex_init(key_mutex_drop_index, &drop_index_mutex, MY_MUTEX_INIT_FAST);
  mysql_mutex_init(key_drop_index_interrupt_mutex, &drop_index_interrupt_mutex,
                   MY_MUTEX_INIT_FAST);
  mysql_mutex_init(key_drop_index_interrupt_mutex, &drop_index_interrupt_mutex,
                   MY_MUTEX_INIT_FAST);
  mysql_cond_init(key_drop_index_interrupt_cond, &drop_index_interrupt_cond,
                  nullptr);

  DBUG_ASSERT(!mysqld_embedded);

  rocksdb_stats= rocksdb::CreateDBStatistics();
  db_options.statistics = rocksdb_stats;

  std::vector<std::string> cf_names;

  rocksdb::Status status;

  if (rocksdb_rate_limiter_bytes_per_sec != 0) {
    rate_limiter.reset(rocksdb::NewGenericRateLimiter(
          rocksdb_rate_limiter_bytes_per_sec));
    db_options.rate_limiter = rate_limiter;
  }

  std::shared_ptr<Logger> myrocks_logger = std::make_shared<Logger>();
  rocksdb::Status s = CreateLoggerFromOptions(rocksdb_datadir, db_options,
                                              &db_options.info_log);
  if (s.ok()) {
    myrocks_logger->SetRocksDBLogger(db_options.info_log);
  }

  db_options.info_log = myrocks_logger;
  myrocks_logger->SetInfoLogLevel(
    static_cast<rocksdb::InfoLogLevel>(rocksdb_info_log_level));
  db_options.wal_dir = rocksdb_wal_dir;

  db_options.wal_recovery_mode=
    static_cast<rocksdb::WALRecoveryMode>(rocksdb_wal_recovery_mode);

  db_options.access_hint_on_compaction_start=
    static_cast<rocksdb::Options::AccessHint>
      (rocksdb_access_hint_on_compaction_start);

  status= rocksdb::DB::ListColumnFamilies(db_options, rocksdb_datadir,
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
    properties_collector_factory = std::make_shared
      <MyRocksTablePropertiesCollectorFactory>(
        &ddl_manager
      );

    set_compaction_options(nullptr, nullptr, nullptr, nullptr);

    mysql_mutex_lock(&sysvar_mutex);

    DBUG_ASSERT(rocksdb_table_stats_sampling_pct <= MYROCKS_SAMPLE_PCT_MAX);
    properties_collector_factory->SetTableStatsSamplingPct(
      rocksdb_table_stats_sampling_pct);

    mysql_mutex_unlock(&sysvar_mutex);

    default_cf_opts.table_properties_collector_factories.push_back(
      properties_collector_factory
    );
  }

  if (!rocksdb_cf_options_map.SetDefault(
        std::string(rocksdb_default_cf_options)) ||
      !rocksdb_cf_options_map.SetOverride(
        std::string(rocksdb_override_cf_options))) {
    DBUG_RETURN(1);
  }

  /*
    If there are no column families, we're creating the new database.
    Create one column family named "default".
  */
  if (cf_names.size() == 0)
    cf_names.push_back(DEFAULT_CF_NAME);

  std::vector<int> compaction_enabled_cf_indices;
  sql_print_information("RocksDB: Column Families at start:");
  for (size_t i = 0; i < cf_names.size(); ++i)
  {
    rocksdb::ColumnFamilyOptions opts;
    get_cf_options(cf_names[i], &opts);

    sql_print_information("  cf=%s", cf_names[i].c_str());
    sql_print_information("    write_buffer_size=%ld", opts.write_buffer_size);
    sql_print_information("    target_file_size_base=%" PRIu64,
                          opts.target_file_size_base);

    /*
      Temporarily disable compactions to prevent a race condition where
      compaction starts before compaction filter is ready.
    */
    if (!opts.disable_auto_compactions)
    {
      compaction_enabled_cf_indices.push_back(i);
      opts.disable_auto_compactions = true;
    }
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
  rocksdb::TransactionDBOptions txn_db_options;
  txn_db_options.transaction_lock_timeout= 2; //*1000*1000; // 2 seconds
  txn_db_options.custom_mutex_factory=
  std::make_shared<Wrapped_mysql_mutex_factory>();

  status= check_rocksdb_options_compatibility(rocksdb_datadir, main_opts,
                                              cf_descr);

  // We won't start if we'll determine that there's a chance of data corruption
  // because of incompatible options.
  if (!status.ok()) {
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: compatibility check against existing database " \
                    "options failed. %s", status.ToString().c_str());
    DBUG_RETURN(1);
  }

  status= rocksdb::TransactionDB::Open(main_opts, txn_db_options,
                                       rocksdb_datadir, cf_descr,
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

  if (ddl_manager.init(&dict_manager, &cf_manager, rocksdb_validate_tables))
    DBUG_RETURN(1);

  /*
    Enable auto compaction, things needed for compaction filter are finished
    initializing
  */
  std::vector<rocksdb::ColumnFamilyHandle*> compaction_enabled_cf_handles;
  compaction_enabled_cf_handles.reserve(compaction_enabled_cf_indices.size());
  for (auto index : compaction_enabled_cf_indices)
  {
    compaction_enabled_cf_handles.push_back(cf_handles[index]);
  }

  status= rdb->EnableAutoCompaction(compaction_enabled_cf_handles);

  if (!status.ok())
  {
    std::string err_text= status.ToString();
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Error enabling compaction: %s", err_text.c_str());
    DBUG_RETURN(1);
  }

  pthread_t thread_handle;
  auto err = mysql_thread_create(
    key_thread_background, &thread_handle,
    nullptr,
    background_thread, nullptr
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
    drop_index_thread, nullptr
  );
  if (err != 0) {
    sql_print_error("RocksDB: Couldn't start the drop index thread: (errno=%d)",
                    err);
    DBUG_RETURN(1);
  }

  collation_exception_list = split(rocksdb_strict_collation_exceptions, ',');

  if (rocksdb_pause_background_work) {
    rdb->PauseBackgroundWork();
  }

  sql_print_information("RocksDB instance opened");
  DBUG_RETURN(0);
}

/*
  Storage Engine deinitialization function, invoked when plugin is unloaded.
*/

static int rocksdb_done_func(void *p)
{
  int error= 0;
  DBUG_ENTER("rocksdb_done_func");

  // signal the drop index thread to stop
  signal_drop_index_thread(true);

  // Flush all memtables for not lose data, even if WAL is disabled.
  rocksdb_flush_all_memtables();

  // Stop all rocksdb background work
  CancelAllBackgroundWork(rdb->GetBaseDB(), true);

  // Signal the background thread to stop and to persist all stats collected
  // from background flushes and compactions. This will add more keys to a new
  // memtable, but since the memtables were just flushed, it should not trigger
  // a flush that can stall due to background threads being stopped. As long
  // as these keys are stored in a WAL file, they can be retrieved on restart.
  mysql_mutex_lock(&stop_cond_mutex);
  bg_control.stop = true;
  mysql_cond_signal(&stop_cond);
  mysql_mutex_unlock(&stop_cond_mutex);

  // wait for the background thread to finish
  mysql_mutex_lock(&background_mutex);
  mysql_mutex_unlock(&background_mutex);

  // wait for the drop index thread to finish
  mysql_mutex_lock(&drop_index_mutex);
  mysql_mutex_unlock(&drop_index_mutex);

  if (rocksdb_open_tables.records)
    error= 1;
  my_hash_free(&rocksdb_open_tables);
  mysql_mutex_destroy(&collation_exception_list_mutex);
  mysql_mutex_destroy(&rocksdb_mutex);
  mysql_mutex_destroy(&sysvar_mutex);
  Rdb_transaction::term_mutex();

  ddl_manager.cleanup();
  binlog_manager.cleanup();
  dict_manager.cleanup();
  cf_manager.cleanup();

  delete rdb;
  rdb= nullptr;

  // Disown the cache data since we're shutting down.
  // This results in memory leaks but it improved the shutdown time.
  // Don't disown when running under valgrind
#ifndef HAVE_VALGRIND
  if (table_options.block_cache)
    table_options.block_cache->DisownData();
#endif /* HAVE_VALGRIND */

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
      return nullptr;
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

  return nullptr;
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
  active_index= table->s->next_number_index;
  uint8 save_table_status= table->status;

  /*
    load_auto_incr_value() may be called by statements that
    do not execute implicit commits (i.e. SHOW CREATE TABLE).
    index_last() creates a snapshot. When a snapshot is created
    here, it has to be released as well. (GitHub issue#189)
  */
  Rdb_transaction *trx= get_or_create_trx(table->in_use);
  bool is_new_snapshot= !trx->has_snapshot();

  // Do a lookup.
  if (!index_last(table->record[0]))
    update_auto_incr_val();

  if (is_new_snapshot)
  {
    trx->release_snapshot();
  }

  table->status= save_table_status;
  active_index= save_active_index;

  /*
    Do what ha_rocksdb::index_end() does.
    (Why don't we use index_init/index_end? class handler defines index_init
     as private, for some reason).
  */
  release_scan_iterator();
}

/* Get PK value from table->record[0]. */
/*
  TODO(alexyang): No existing support for auto_increment on non-pk columns, see
  end of ha_rocksdb::create. Also see opened issue here:
  https://github.com/facebook/mysql-5.6/issues/153
*/
void ha_rocksdb::update_auto_incr_val()
{
  Field *field;
  longlong new_val;
  field= table->key_info[table->s->next_number_index].key_part[0].field;

  my_bitmap_map *old_map= dbug_tmp_use_all_columns(table, table->read_set);
  new_val= field->val_int() + 1;
  dbug_tmp_restore_column_map(table->read_set, old_map);

  longlong auto_incr_val= tbl_def->auto_incr_val;
  while (auto_incr_val < new_val &&
         !tbl_def->auto_incr_val.compare_exchange_weak(auto_incr_val,
                                                       new_val)) {
    // Do nothing - just loop until auto_incr_val is >= new_val or
    // we successfully set it
  }
}

int ha_rocksdb::load_hidden_pk_value()
{
  int save_active_index= active_index;
  active_index= tbl_def->n_keys - 1;
  uint8 save_table_status= table->status;

  Rdb_transaction *trx= get_or_create_trx(table->in_use);
  bool is_new_snapshot= !trx->has_snapshot();

  // Do a lookup.
  if (!index_last(table->record[0]))
  {
    /*
      Decode PK field from the key
    */
    longlong hidden_pk_id= 0;
    if (read_hidden_pk_id_from_rowkey(&hidden_pk_id))
    {
      if (is_new_snapshot)
      {
        trx->release_snapshot();
      }
      return HA_ERR_INTERNAL_ERROR;
    }

    hidden_pk_id++;
    longlong old = tbl_def->hidden_pk_val;
    while (old < hidden_pk_id &&
        !tbl_def->hidden_pk_val.compare_exchange_weak(old, hidden_pk_id)){}
  }

  if (is_new_snapshot)
  {
    trx->release_snapshot();
  }

  table->status= save_table_status;
  active_index= save_active_index;

  release_scan_iterator();

  return 0;
}

/* Get PK value from tbl_def->hidden_pk_info. */
longlong ha_rocksdb::update_hidden_pk_val()
{
  DBUG_ASSERT(has_hidden_pk(table));
  longlong new_val;
  new_val= tbl_def->hidden_pk_val++;
  return new_val;
}

/* Get the id of the hidden pk id from last_rowkey */
int ha_rocksdb::read_hidden_pk_id_from_rowkey(longlong* hidden_pk_id)
{
  DBUG_ASSERT(has_hidden_pk(table));
  rocksdb::Slice rowkey_slice(last_rowkey.ptr(), last_rowkey.length());

  // Get hidden primary key from old key slice
  Stream_reader reader(&rowkey_slice);
  if ((!reader.read(RDBSE_KEYDEF::INDEX_NUMBER_SIZE)))
    return 1;

  const int length= Field_longlong::PACK_LENGTH;
  const char *from;
  if (!(from= reader.read(length)))
    return 1; /* Mem-comparable image doesn't have enough bytes */

  *hidden_pk_id= read_int64(&from);
  return 0;
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
  : handler(hton, table_arg), scan_it(nullptr),
    scan_it_skips_bloom(false),
    tbl_def(nullptr),
    pk_descr(nullptr),
    key_descr(nullptr),
    pk_can_be_decoded(false),
    pk_tuple(nullptr), pk_packed_tuple(nullptr),
    sec_key_packed_tuple(nullptr), sec_key_tails(nullptr),
    sec_key_match_prefix(nullptr), sec_key_match_prefix_buf(nullptr),
    sec_key_packed_tuple_old(nullptr), sec_key_tails_old(nullptr),
    pack_buffer(nullptr),
    lock_rows(FALSE),
    keyread_only(FALSE),
    field_enc(nullptr),
    row_checksums_checked(0)
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

  @param pk_packed_tuple      Packed PK tuple. We need it in order to compute
                              and store its CRC.
  @param pk_packed_size       Length of the above
  @param packed_rec      OUT  Data slice with record data.
*/

void ha_rocksdb::convert_record_to_storage_format(const char *pk_packed_tuple,
                                                  size_t pk_packed_size,
                                                  rocksdb::Slice *packed_rec)
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

  if (should_store_checksums())
  {
    uint32_t key_crc32= crc32(0, (uchar*)pk_packed_tuple, pk_packed_size);
    uint32_t val_crc32= crc32(0, (uchar*)storage_record.ptr(),
                              storage_record.length());
    uchar key_crc_buf[CHECKSUM_SIZE];
    uchar val_crc_buf[CHECKSUM_SIZE];
    store_big_uint4(key_crc_buf, key_crc32);
    store_big_uint4(val_crc_buf, val_crc32);
    storage_record.append((const char*)&CHECKSUM_DATA_TAG, 1);
    storage_record.append((const char*)key_crc_buf, CHECKSUM_SIZE);
    storage_record.append((const char*)val_crc_buf, CHECKSUM_SIZE);
  }

  *packed_rec= rocksdb::Slice(storage_record.ptr(), storage_record.length());
}


/*
  @param  key   Table record's Key in storage format
  @param  value Table record in storage format
  @param  buf   Store record in table->record[0] form here
*/
int ha_rocksdb::convert_record_from_storage_format(const rocksdb::Slice *key,
                                                   const rocksdb::Slice *value,
                                                   uchar *buf)
{
  retrieved_record.assign(value->data(), value->size());
  return convert_record_from_storage_format(key, buf);
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
  @brief
  Unpack the record in this->retrieved_record and this->last_rowkey from
  storage format into buf (which can be table->record[0] or table->record[1]).

  @param  key   Table record's key in mem-comparable form.
  @param  buf   Store record in table->record[0] format here

  @detail
    If the table has blobs, the unpacked data in buf may keep pointers to the
    data in this->retrieved_record.

    The key is only needed to check its checksum value (the checksum is in
    retrieved_record).

  @return
    0      OK
    other  Error inpacking the data
*/

int ha_rocksdb::convert_record_from_storage_format(const rocksdb::Slice *key,
                                                   uchar * buf)
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
  if (pk_descr->unpack_record(this, table, buf, &rowkey_slice,
      &pk_unpack_info_slice)) {
    return HA_ERR_INTERNAL_ERROR;
  }

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
    {
      /* This sets the NULL-bit of this record */
      field->set_null(ptr_diff);
      /*
        Besides that, set the field value to default value. CHECKSUM TABLE
        depends on this.
      */
      uint field_offset= field->ptr - table->record[0];
      memcpy(buf + field_offset,
             table->s->default_values + field_offset,
             field->pack_length());
    }
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

  if (reader.remaining_bytes() == CHECKSUM_CHUNK_SIZE)
  {
    if (reader.read(1)[0] == CHECKSUM_DATA_TAG)
    {
      if (verify_checksums)
      {
        uint32_t stored_key_chksum;
        uint32_t stored_val_chksum;

        stored_key_chksum= read_big_uint4((const uchar*)reader.read(CHECKSUM_SIZE));
        stored_val_chksum= read_big_uint4((const uchar*)reader.read(CHECKSUM_SIZE));

        uint32_t computed_key_chksum= crc32(0, (const uchar*)key->data(),
                                            key->size());
        uint32_t computed_val_chksum=
          crc32(0, (const uchar*) retrieved_record.data(),
                retrieved_record.size()-CHECKSUM_CHUNK_SIZE);

        DBUG_EXECUTE_IF("myrocks_simulate_bad_pk_checksum1",
                        stored_key_chksum++;);

        if (stored_key_chksum != computed_key_chksum)
        {
          report_checksum_mismatch(pk_descr, true, key->data(), key->size());
          return HA_ERR_INTERNAL_ERROR;
        }

        DBUG_EXECUTE_IF("myrocks_simulate_bad_pk_checksum2",
                        stored_val_chksum++;);
        if (stored_val_chksum != computed_val_chksum)
        {
          report_checksum_mismatch(pk_descr, false, retrieved_record.data(),
                                   retrieved_record.size());
          return HA_ERR_INTERNAL_ERROR;
        }

        row_checksums_checked++;
      }
      else
      {
        /* Not checking the checksums */
        reader.read(CHECKSUM_CHUNK_SIZE-1);
      }
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

      If hidden pk exists, we skip this check since the field will never be
      part of the hidden pk.
    */
    if (!has_hidden_pk(table) &&
        field->part_of_key.is_set(table->s->primary_key))
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
  rocksdb::PerfLevel perf_level=
    static_cast<rocksdb::PerfLevel>(rocksdb_perf_context_level);

  rocksdb::SetPerfLevel(perf_level);

  if (perf_level == rocksdb::kDisable)
    return;

#define IO_PERF_INIT(_field_) io_perf._field_= rocksdb::perf_context._field_
  IO_PERF_INIT(block_read_byte);
  IO_PERF_INIT(block_read_count);
  IO_PERF_INIT(block_read_time);
  rdb_perf_context_start(local_perf_context);
#undef IO_PERF_INIT
}

void ha_rocksdb::io_perf_end_and_record(void)
{
  rocksdb::PerfLevel perf_level=
    static_cast<rocksdb::PerfLevel>(rocksdb_perf_context_level);

  if (perf_level == rocksdb::kDisable)
    return;

  /*
    This seems to be needed to prevent gdb from crashing if it breaks
    or enters this function.
   */
  rocksdb::SetPerfLevel(perf_level);

#define IO_PERF_DIFF(_field_) io_perf._field_= rocksdb::perf_context._field_ - \
                                               io_perf._field_
  IO_PERF_DIFF(block_read_byte);
  IO_PERF_DIFF(block_read_count);
  IO_PERF_DIFF(block_read_time);
  rdb_perf_context_stop(local_perf_context,
                        &share->table_perf_context,
                        global_perf_context);
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

  if (local_perf_context.value[PC_KEY_SKIPPED])
  {
    stats.key_skipped += local_perf_context.value[PC_KEY_SKIPPED];
  }

  if (local_perf_context.value[PC_DELETE_SKIPPED])
  {
    stats.delete_skipped += local_perf_context.value[PC_DELETE_SKIPPED];
  }
}

int ha_rocksdb::open(const char *name, int mode, uint test_if_locked)
{
  DBUG_ENTER("ha_rocksdb::open");

  if (!(share = get_share(name, table)))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  thr_lock_data_init(&share->lock, &lock, nullptr);

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
  lock_rows= false;
  lock_for_write= false;

  key_descr= tbl_def->key_descr;

  uint key_len= 0;
  pk_descr= key_descr[pk_index(table, tbl_def)];
  if (has_hidden_pk(table))
    pk_key_parts= 1;
  else
  {
    pk_key_parts= table->key_info[table->s->primary_key].user_defined_key_parts;
    key_len= table->key_info[table->s->primary_key].key_length;
  }

  pk_descr->setup(table, tbl_def);  // move this into get_share() ??

  uint packed_key_len= pk_descr->max_storage_fmt_length();

  if (!(pk_tuple= (uchar*)my_malloc(key_len, MYF(0))) ||
      !(pk_packed_tuple= (uchar*)my_malloc(packed_key_len, MYF(0))))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  /*
    Full table scan actually uses primary key
    (UPDATE needs to know this, otherwise it will go into infinite loop on
    queries like "UPDATE tbl SET pk=pk+100")
  */
  key_used_on_scan= table->s->primary_key;

  /* Sometimes, we may use sec_key_packed_tuple for storing packed PK */
  uint max_packed_sec_key_len= packed_key_len;
  for (uint i= 0; i < table->s->keys; i++)
  {
    if (i == table->s->primary_key) /* Primary key was processed above */
      continue;

    key_descr[i]->setup(table, tbl_def);  // move this into get_share() ??

    uint packed_len= key_descr[i]->max_storage_fmt_length();
    if (packed_len > max_packed_sec_key_len)
      max_packed_sec_key_len= packed_len;
  }

  setup_field_converters();

  size_t tails_size= max_packed_sec_key_len + CHECKSUM_CHUNK_SIZE;

  if (!(sec_key_packed_tuple= (uchar*)my_malloc(max_packed_sec_key_len,
                                                MYF(0))) ||
      !(sec_key_match_prefix_buf= (uchar*)my_malloc(max_packed_sec_key_len,
                                                    MYF(0))) ||
      !(sec_key_packed_tuple_old= (uchar*)my_malloc(max_packed_sec_key_len,
                                                 MYF(0))) ||
      !(end_key_packed_tuple= (uchar*)my_malloc(max_packed_sec_key_len,
                                                MYF(0))) ||
      !((sec_key_tails= (uchar*)my_malloc(tails_size, MYF(0)))) ||
      !((sec_key_tails_old= (uchar*)my_malloc(tails_size, MYF(0)))) ||
      !((pack_buffer=(uchar*)my_malloc(max_packed_sec_key_len, MYF(0)))))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  info(HA_STATUS_NO_LOCK | HA_STATUS_VARIABLE | HA_STATUS_CONST);

  /* TODO: move the following to where TABLE_SHARE is opened: */
  if (table->found_next_number_field)
    load_auto_incr_value();

  if (has_hidden_pk(table) && load_hidden_pk_value())
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  DBUG_RETURN(0);
}


int ha_rocksdb::close(void)
{
  DBUG_ENTER("ha_rocksdb::close");

  pk_descr= nullptr;
  key_descr= nullptr;

  my_free(pk_tuple);
  pk_tuple= nullptr;

  my_free(pk_packed_tuple);
  pk_packed_tuple= nullptr;

  my_free(sec_key_packed_tuple);
  sec_key_packed_tuple= nullptr;

  my_free(sec_key_match_prefix_buf);
  sec_key_match_prefix_buf= nullptr;

  my_free(sec_key_packed_tuple_old);
  sec_key_packed_tuple_old= nullptr;

  my_free(end_key_packed_tuple);
  end_key_packed_tuple= nullptr;

  my_free(sec_key_tails);
  sec_key_tails= nullptr;

  my_free(sec_key_tails_old);
  sec_key_tails_old= nullptr;

  my_free(pack_buffer);
  pack_buffer= nullptr;

  my_free(field_enc);
  field_enc= nullptr;

  DBUG_RETURN(free_share(share));
}


const int HA_ERR_ROCKSDB_UNIQUE_NOT_SUPPORTED= HA_ERR_LAST+1;
const int HA_ERR_ROCKSDB_PK_REQUIRED= HA_ERR_LAST+2;
const int HA_ERR_ROCKSDB_TOO_MANY_LOCKS= HA_ERR_LAST+3;

bool ha_rocksdb::get_error_message(int error, String *buf)
{
  Rdb_transaction* trx = get_trx_from_thd(ha_thd());

  if (error == HA_ERR_ROCKSDB_PK_REQUIRED)
    buf->append("Table must have a PRIMARY KEY");
  else if (error == HA_ERR_ROCKSDB_UNIQUE_NOT_SUPPORTED)
    buf->append("Unique indexes are not supported");
  else if (error == HA_ERR_ROCKSDB_TOO_MANY_LOCKS)
    buf->append("Number of locks held reached @@rocksdb_max_row_locks");
  else if (error == HA_ERR_LOCK_WAIT_TIMEOUT)
  {
    buf->append(trx->detailed_error);
    return TRUE;
  }
  return FALSE; /* not a temporary error */
}

static bool is_table_in_collation_exception_list(const std::string& table_name)
{
  bool result = false;

  // Make sure no one else changes the list while we are accessing it.
  mysql_mutex_lock(&collation_exception_list_mutex);

  // See if this table name matches any in the list
  for (const auto& exception_elem : collation_exception_list)
  {
    // Use regular expressions for the match
    if (std::regex_match(table_name, std::regex(exception_elem)))
    {
      // This table name matches
      result = true;
      break;
    }
  }

  // Release the mutex
  mysql_mutex_unlock(&collation_exception_list_mutex);

  return result;
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

int ha_rocksdb::create_key_defs(TABLE *table_arg, const char *db_table,
                                uint len, HA_CREATE_INFO *create_info)
{
  uint i;
  DBUG_ENTER("ha_rocksdb::create_key_defs");
  uint n_keys= table_arg->s->keys;

  // These need to be one greater than MAX_INDEXES since the user can create
  // MAX_INDEXES secondary keys and no primary key which would cause us
  // to generate a hidden one.
  std::array<rocksdb::ColumnFamilyHandle*, MAX_INDEXES + 1> cf_handles;
  std::array<bool, MAX_INDEXES + 1> is_cf_reverse;
  std::array<bool, MAX_INDEXES + 1> is_auto_cf;

  bool write_err= false;
  std::unique_ptr<rocksdb::WriteBatch> wb= dict_manager.begin();
  rocksdb::WriteBatch *batch= wb.get();

  /* Create table/key descriptions and put them into the data dictionary */
  tbl_def= new RDBSE_TABLE_DEF;

  /*
    If no primary key found, create a hidden PK and place it inside table
    definition
  */
  if (has_hidden_pk(table_arg))
  {
    n_keys += 1;
  }

  key_descr= new RDBSE_KEYDEF*[n_keys];

  memset(key_descr, 0, sizeof(RDBSE_KEYDEF*) * n_keys);
  tbl_def->n_keys= n_keys;
  tbl_def->key_descr= key_descr;

  /*
     The first loop checks the index parameters and creates
     column families if necessary.
  */
  for (i= 0; i < tbl_def->n_keys; i++)
  {
    rocksdb::ColumnFamilyHandle* cf_handle;

    if (rocksdb_strict_collation_check && !is_hidden_pk(i, table_arg, tbl_def))
    {
      for (uint part= 0; part < table_arg->key_info[i].actual_key_parts; part++)
      {
        if (!is_myrocks_collation_supported(
            table_arg->key_info[i].key_part[part].field) &&
            !is_table_in_collation_exception_list(table_arg->s->table_name.str))
        {
          std::string collation_err;
          for (auto coll: MYROCKS_INDEX_COLLATIONS)
          {
            if (collation_err != "")
            {
              collation_err += ", ";
            }
            collation_err += coll->name;
          }
          my_printf_error(ER_UNKNOWN_ERROR,
                          "Unsupported collation on string indexed "
                          "column %s.%s Use binary collation (%s).", MYF(0),
                          db_table,
                          table_arg->key_info[i].key_part[part].field->field_name,
                          collation_err.c_str());
          goto error;
        }
      }
    }

    /*
      index comment has Column Family name. If there was no comment, we get
      NULL, and it means use the default column family.
    */
    const char *comment;
    const char *key_name;
    if (is_hidden_pk(i, table_arg, tbl_def)) {
      comment= nullptr;
      key_name= const_cast<char*>("HIDDEN_PK_ID");
    } else {
      comment= table_arg->key_info[i].comment.str;
      key_name= table_arg->key_info[i].name;
    }

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
    bool is_auto_cf_flag;
    cf_handle= cf_manager.get_or_create_cf(rdb, comment, db_table,
                                           key_name,
                                           &is_auto_cf_flag);
    if (!cf_handle)
      goto error;

    cf_handles[i]= cf_handle;
    is_cf_reverse[i]= is_cf_name_reverse(comment);
    is_auto_cf[i]= is_auto_cf_flag;
  }

  /*
    Get the index numbers (this will update the next_index_number)
    and create RDBSE_KEYDEF structures.
  */
  for (i= 0; i < tbl_def->n_keys; i++)
  {
    uint index_id= ddl_manager.get_and_update_next_number(&dict_manager);
    uint16_t index_dict_version= RDBSE_KEYDEF::INDEX_INFO_VERSION_GLOBAL_ID;
    uchar index_type;
    uint16_t kv_version;

    if (is_hidden_pk(i, table_arg, tbl_def))
    {
      index_type= RDBSE_KEYDEF::INDEX_TYPE_HIDDEN_PRIMARY;
      kv_version= RDBSE_KEYDEF::PRIMARY_FORMAT_VERSION_INITIAL;
    }
    else if (i == table_arg->s->primary_key)
    {
      index_type= RDBSE_KEYDEF::INDEX_TYPE_PRIMARY;
      kv_version= RDBSE_KEYDEF::PRIMARY_FORMAT_VERSION_INITIAL;
    }
    else
    {
      index_type= RDBSE_KEYDEF::INDEX_TYPE_SECONDARY;
      kv_version= RDBSE_KEYDEF::SECONDARY_FORMAT_VERSION_INITIAL;
    }

    const char *key_name;
    if (is_hidden_pk(i, table_arg, tbl_def)) {
      key_name= const_cast<char*>("HIDDEN_PK_ID");
    } else {
      key_name= table_arg->key_info[i].name;
    }

    key_descr[i]= new RDBSE_KEYDEF(index_id, i,
                                   cf_handles[i],
                                   index_dict_version,
                                   index_type,
                                   kv_version,
                                   is_cf_reverse[i],
                                   is_auto_cf[i],
                                   key_name);
  }

  pk_descr= key_descr[pk_index(table_arg, tbl_def)];

  tbl_def->set_name(db_table, len);
  dict_manager.lock();
  write_err= ddl_manager.put_and_write(tbl_def, batch)
             || dict_manager.commit(batch);
  dict_manager.unlock();

  if (write_err)
    goto error;

  DBUG_RETURN(0);

error:
  /* Delete what we have allocated so far */
  delete tbl_def;
  tbl_def = nullptr;

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
  if (tb == nullptr)
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

  if (get_table_if_exists(name)) {
    THD *thd = thd_get_current_thd();
    if (thd->lex->sql_command == SQLCOM_TRUNCATE) {
      if (delete_table(name)) {
        DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
      }
    } else {
      if (rocksdb_normalize_tablename(name, &strbuf)) {
        DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
      }
      my_printf_error(ER_UNKNOWN_ERROR,
        "Table '%s' does not exist, but metadata information exists inside "
        "MyRocks. This is a sign of data inconsistency. Please check if "
        "'%s.frm' exists, and try to restore it if it does not exist.",
        MYF(0), strbuf.c_ptr(), name);
      DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
    }
  }

  /*
    Construct dbname.tablename ourselves, because parititioning
    passes strings like "./test/t14#P#p0" for individual partitions,
    while table_arg->s->table_name has none of that.
  */
  if (rocksdb_normalize_tablename(name, &strbuf))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  /*
    TODO(alexyang): Temporarily disable unique indexes support when there is no
    Primary Key
  */
  for (uint keyno= 0; keyno < table_arg->s->keys; keyno++)
  {
    if ((table_arg->key_info[keyno].flags & HA_NOSAME) &&
        has_hidden_pk(table_arg))
    {
      my_printf_error(ER_UNKNOWN_ERROR,
        "Unique index support is disabled when the table has no primary key. ",
        MYF(0));
      DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
    }
  }

  if ((res= create_key_defs(table_arg, strbuf.ptr(), strbuf.length(),
                            create_info)))
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
                               rocksdb::Iterator *iter,
                               bool full_key_match,
                               const rocksdb::Slice& key_slice)
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

int ha_rocksdb::read_before_key(RDBSE_KEYDEF *kd,
                                bool full_key_match,
                                const rocksdb::Slice& key_slice)
{
  /*
    We are looking for record with the biggest t.key such that
    t.key < lookup_tuple.
  */
  if (kd->is_reverse_cf)
  {
    if (scan_it->Valid() && full_key_match &&
          kd->value_matches_prefix(scan_it->key(), key_slice))
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

  return scan_it->Valid() ? 0 : HA_ERR_KEY_NOT_FOUND;
}

int ha_rocksdb::read_after_key(RDBSE_KEYDEF *kd,
                               bool full_key_match,
                               const rocksdb::Slice& key_slice)
{
  /*
    We are looking for the first record such that

      index_tuple $GT lookup_tuple

    with HA_READ_AFTER_KEY, $GT = '>',
    with HA_READ_KEY_OR_NEXT, $GT = '>='
  */
  if (kd->is_reverse_cf)
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
      if (!full_key_match ||
          !kd->value_matches_prefix(scan_it->key(), key_slice))
      {
        scan_it->Prev();
      }
    }
  }

  return scan_it->Valid() ? 0 : HA_ERR_KEY_NOT_FOUND;
}

int ha_rocksdb::position_to_correct_key(RDBSE_KEYDEF *kd,
                                        enum ha_rkey_function find_flag,
                                        bool full_key_match,
                                        const uchar* key,
                                        key_part_map keypart_map,
                                        const rocksdb::Slice& key_slice,
                                        bool* move_forward)
{
  int rc= 0;

  *move_forward= true;

  switch (find_flag) {
  case HA_READ_KEY_EXACT:
    rc= read_key_exact(kd, scan_it, full_key_match, key_slice);
    break;
  case HA_READ_BEFORE_KEY:
    *move_forward= false;
    rc= read_before_key(kd, full_key_match, key_slice);
    if (rc == 0 && !kd->covers_key(scan_it->key()))
    {
      /* The record we've got is not from this index */
      rc= HA_ERR_KEY_NOT_FOUND;
    }
    break;
  case HA_READ_AFTER_KEY:
  case HA_READ_KEY_OR_NEXT:
    rc= read_after_key(kd, full_key_match, key_slice);
    if (rc == 0 && !kd->covers_key(scan_it->key()))
    {
      /* The record we've got is not from this index */
      rc= HA_ERR_KEY_NOT_FOUND;
    }
    break;
  case HA_READ_KEY_OR_PREV:
  case HA_READ_PREFIX:
    /* This flag is not used by the SQL layer, so we don't support it yet. */
    rc= HA_ERR_UNSUPPORTED;
    break;
  case HA_READ_PREFIX_LAST:
  case HA_READ_PREFIX_LAST_OR_PREV:
    *move_forward= false;
    /*
      Find the last record with the specified index prefix lookup.
      - HA_READ_PREFIX_LAST requires that the record has the
        prefix=lookup (if there are no such records,
        HA_ERR_KEY_NOT_FOUND should be returned).
      - HA_READ_PREFIX_LAST_OR_PREV has no such requirement. If there are no
        records with prefix=lookup, we should return the last record
        before that.
    */
    rc= read_before_key(kd, full_key_match, key_slice);
    if (rc == 0)
    {
      const rocksdb::Slice& rkey= scan_it->key();
      if (!kd->covers_key(rkey))
      {
        /* The record we've got is not from this index */
        rc= HA_ERR_KEY_NOT_FOUND;
      }
      else if (find_flag == HA_READ_PREFIX_LAST)
      {
        uint size = kd->pack_index_tuple(this, table, pack_buffer,
                                         sec_key_packed_tuple, key,
                                         keypart_map);
        rocksdb::Slice lookup_tuple(
            reinterpret_cast<char*>(sec_key_packed_tuple), size);

        // We need to compare the key we've got with the original search prefix.
        if (!kd->value_matches_prefix(rkey, lookup_tuple))
        {
          rc= HA_ERR_KEY_NOT_FOUND;
        }
      }
    }
    break;
  default:
    DBUG_ASSERT(0);
    break;
  }

  return rc;
}

int ha_rocksdb::calc_eq_cond_len(RDBSE_KEYDEF *kd,
                                 enum ha_rkey_function find_flag,
                                 const rocksdb::Slice& slice,
                                 int bytes_changed_by_succ,
                                 const key_range *end_key,
                                 uint* end_key_packed_size)
{
  if (find_flag == HA_READ_KEY_EXACT)
    return slice.size();

  if (find_flag == HA_READ_PREFIX_LAST)
  {
    /*
      We have made the kd->successor(sec_key_packed_tuple) call above.

      The slice is at least RDBSE_KEYDEF::INDEX_NUMBER_SIZE bytes long.
    */
    return slice.size() - bytes_changed_by_succ;
  }

  if (end_key)
  {
    *end_key_packed_size= kd->pack_index_tuple(this, table, pack_buffer,
                                              end_key_packed_tuple,
                                              end_key->key,
                                              end_key->keypart_map);
    /*
      Calculating length of the equal conditions here. 4 byte index id is
      included.
      Example1: id1 BIGINT, id2 INT, id3 BIGINT, PRIMARY KEY (id1, id2, id3)
       WHERE id1=1 AND id2=1 AND id3>=2 => eq_cond_len= 4+8+4= 16
       WHERE id1=1 AND id2>=1 AND id3>=2 => eq_cond_len= 4+8= 12
      Example2: id1 VARCHAR(30), id2 INT, PRIMARY KEY (id1, id2)
       WHERE id1 = 'AAA' and id2 < 3; => eq_cond_len=13 (varchar used 9 bytes)
    */
    rocksdb::Slice end_slice(reinterpret_cast<char*>(end_key_packed_tuple),
                             *end_key_packed_size);
    return slice.difference_offset(end_slice);
  }

  /*
    On range scan without any end key condition, there is no
    eq cond, and eq cond length is the same as index_id size (4 bytes).
    Example1: id1 BIGINT, id2 INT, id3 BIGINT, PRIMARY KEY (id1, id2, id3)
     WHERE id1>=1 AND id2 >= 2 and id2 <= 5 => eq_cond_len= 4
  */
  return RDBSE_KEYDEF::INDEX_NUMBER_SIZE;
}

int ha_rocksdb::read_row_from_primary_key(uchar *buf)
{
  int rc;
  const rocksdb::Slice& rkey= scan_it->key();
  uint pk_size= rkey.size();
  const char *pk_data= rkey.data();

  memcpy(pk_packed_tuple, pk_data, pk_size);
  last_rowkey.copy(pk_data, pk_size, &my_charset_bin);

  if (lock_rows)
  {
    /* We need to put a lock and re-read */
    rc= get_row_by_rowid(buf, pk_packed_tuple, pk_size);
  }
  else
  {
    /* Unpack from the row we've read */
    const rocksdb::Slice& value = scan_it->value();
    rc= convert_record_from_storage_format(&rkey, &value, buf);
  }

  return rc;
}

int ha_rocksdb::read_row_from_secondary_key(uchar *buf, RDBSE_KEYDEF* kd,
                                            bool move_forward)
{
  int rc= 0;
  uint pk_size;

  if (keyread_only && !lock_rows && !has_hidden_pk(table))
  {
    /* Get the key columns and primary key value */
    const rocksdb::Slice& rkey= scan_it->key();
    pk_size= kd->get_primary_key_tuple(table, pk_descr, &rkey, pk_packed_tuple);
    const rocksdb::Slice& value= scan_it->value();
    if (pk_size == INVALID_LEN ||
        kd->unpack_record(this, table, buf, &rkey, &value))
    {
      rc= HA_ERR_INTERNAL_ERROR;
    }
  }
  else
  {
    if (kd->is_reverse_cf)
      move_forward= !move_forward;

    rc= find_icp_matching_index_rec(move_forward, buf);
    if (!rc)
    {
      const rocksdb::Slice& rkey= scan_it->key();
      pk_size= kd->get_primary_key_tuple(table, pk_descr, &rkey,
                                         pk_packed_tuple);
      if (pk_size == INVALID_LEN)
        rc= HA_ERR_INTERNAL_ERROR;
      else
        rc= get_row_by_rowid(buf, pk_packed_tuple, pk_size);
    }
  }

  if (!rc)
    last_rowkey.copy((const char*)pk_packed_tuple, pk_size, &my_charset_bin);

  return rc;
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
    ignore those that have key_part->field==nullptr (these are not initialized
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

  /* Use STATUS_NOT_FOUND when record not found or some error occurred */
  table->status= STATUS_NOT_FOUND;

  if (scan_it->Valid())
  {
    rocksdb::Slice key= scan_it->key();

    /* Check if we've ran out of records of this index */
    if (key_descr[keyno]->covers_key(key))
    {
      int rc;

      //TODO: We could here check if we have ran out of range we're scanning
      uint size= key_descr[keyno]->get_primary_key_tuple(table, pk_descr, &key,
                                                         pk_packed_tuple);
      if (size == (uint)-1)
        return HA_ERR_INTERNAL_ERROR;

      last_rowkey.copy((const char*)pk_packed_tuple, size, &my_charset_bin);

      if (keyread_only && !lock_rows && !has_hidden_pk(table))
      {
        rocksdb::Slice value= scan_it->value();
        if (key_descr[keyno]->unpack_record(this, table, buf, &key, &value))
          return HA_ERR_INTERNAL_ERROR;
        rc= 0;
      }
      else
        rc= get_row_by_rowid(buf, pk_packed_tuple, size);

      if (!rc)
      {
        table->status= 0;
        stats.rows_read++;
        stats.rows_index_next++;
        update_row_stats(ROWS_READ);
      }
      return rc;
    }
  }
  return HA_ERR_END_OF_FILE;
}


/*
  ha_rocksdb::read_range_first overrides handler::read_range_first.
  The only difference from handler::read_range_first is that
  ha_rocksdb::read_range_first passes end_key to
  ha_rocksdb::index_read_map_impl function.
*/
int ha_rocksdb::read_range_first(const key_range *start_key,
                                 const key_range *end_key,
                                 bool eq_range_arg, bool sorted)
{
  int result;
  DBUG_ENTER("ha_rocksdb::read_range_first");

  eq_range= eq_range_arg;
  set_end_range(end_key, RANGE_SCAN_ASC);

  range_key_part= table->key_info[active_index].key_part;

  if (!start_key)                       // Read first record
    result= ha_index_first(table->record[0]);
  else
  {
    MYSQL_TABLE_IO_WAIT(m_psi, PSI_TABLE_FETCH_ROW, active_index, 0,
      { result= index_read_map_impl(table->record[0],
                                start_key->key,
                                start_key->keypart_map,
                                start_key->flag,
                                end_key); })
  }
  if (result)
    DBUG_RETURN((result == HA_ERR_KEY_NOT_FOUND)
                ? HA_ERR_END_OF_FILE
                : result);

  if (compare_key(end_range) <= 0)
  {
    DBUG_RETURN(0);
  }
  else
  {
    /*
      The last read row does not fall in the range. So request
      storage engine to release row lock if possible.
    */
    unlock_row();
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }
}


int ha_rocksdb::index_read_map(uchar *buf, const uchar *key,
                               key_part_map keypart_map,
                               enum ha_rkey_function find_flag)
{
  return index_read_map_impl(buf, key, keypart_map, find_flag, nullptr);
}

/*
   See storage/rocksdb/rocksdb-range-access.txt for description of how MySQL
   index navigation commands are converted into RocksDB lookup commands.

   This function takes end_key as an argument, and it is set on range scan.
   MyRocks needs to decide whether prefix bloom filter can be used or not.
   To decide to use prefix bloom filter or not, calculating equal condition length
   is needed. On equal lookups (find_flag == HA_READ_KEY_EXACT), equal
   condition length is the same as rocksdb::Slice.size() of the start key.
   On range scan, equal condition length is MIN(start_key, end_key) of the
   rocksdb::Slice expression.
*/
int ha_rocksdb::index_read_map_impl(uchar *buf, const uchar *key,
                                    key_part_map keypart_map,
                                    enum ha_rkey_function find_flag,
                                    const key_range *end_key)
{
  int rc= 0;
  DBUG_ENTER("ha_rocksdb::index_read_map");

  RDBSE_KEYDEF *kd= key_descr[active_index];
  uint actual_key_parts= kd->get_m_key_parts();
  bool using_full_key= (keypart_map == HA_WHOLE_KEY) ||
                       (keypart_map == ((key_part_map(1) << actual_key_parts)
                                         - 1));

  if (!end_key)
    end_key= end_range;

  /* By default, we don't need the retrieved records to match the prefix */
  sec_key_match_prefix= nullptr;
  stats.rows_requested++;
  io_perf_start();

  if (active_index == table->s->primary_key &&
      find_flag == HA_READ_KEY_EXACT && using_full_key)
  {
    /*
      Equality lookup over primary key, using full tuple.
      This is a special case, use DB::Get.
    */
    uint size= kd->pack_index_tuple(this, table, pack_buffer, pk_packed_tuple,
                                    key, keypart_map);
    rc= get_row_by_rowid(buf, pk_packed_tuple, size);
    if (!rc)
    {
      stats.rows_read++;
      stats.rows_index_first++;
      update_row_stats(ROWS_READ);
    }
    io_perf_end_and_record();
    DBUG_RETURN(rc);
  }

  /*
    Unique secondary index performs lookups without the extended key fields
  */
  uint packed_size;
  if (active_index != table->s->primary_key &&
      table->key_info[active_index].flags & HA_NOSAME &&
      find_flag == HA_READ_KEY_EXACT && using_full_key)
  {
    key_part_map tmp_map= (key_part_map(1) <<
                           table->key_info[active_index].user_defined_key_parts) - 1;
    packed_size= kd->pack_index_tuple(this, table, pack_buffer,
                                      sec_key_packed_tuple, key, tmp_map);
    if (table->key_info[active_index].user_defined_key_parts !=
        kd->get_m_key_parts())
      using_full_key= false;
  }
  else
  {
    packed_size= kd->pack_index_tuple(this, table, pack_buffer,
                                      sec_key_packed_tuple, key,
                                      keypart_map);
  }
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

  int bytes_changed_by_succ= 0;
  if (find_flag == HA_READ_PREFIX_LAST_OR_PREV ||
      find_flag == HA_READ_PREFIX_LAST ||
      find_flag == HA_READ_AFTER_KEY)
  {
    /* See below */
    bytes_changed_by_succ= kd->successor(sec_key_packed_tuple, packed_size);
  }

  rocksdb::Slice slice((char*)sec_key_packed_tuple, packed_size);

  uint end_key_packed_size= 0;
  uint eq_cond_len= calc_eq_cond_len(kd, find_flag, slice,
                                     bytes_changed_by_succ, end_key,
                                     &end_key_packed_size);

  bool use_all_keys= false;
  if (find_flag == HA_READ_KEY_EXACT &&
      my_count_bits(keypart_map) == kd->get_m_key_parts())
    use_all_keys= true;

  Rdb_transaction *trx= get_or_create_trx(table->in_use);
  bool is_new_snapshot= !trx->has_snapshot();
  // Loop as long as we get a deadlock error AND we end up creating the
  // snapshot here (i.e. it did not exist prior to this)
  for (;;)
  {
    /*
      This will open the iterator and position it at a record that's equal or
      greater than the lookup tuple.
    */
    setup_scan_iterator(kd, &slice, use_all_keys, is_ascending(kd, find_flag),
                        eq_cond_len);

    /*
      Once we are positioned on from above, move to the position we really
      want: See storage/rocksdb/rocksdb-range-access.txt
    */
    bool move_forward;
    rc = position_to_correct_key(kd, find_flag, using_full_key, key,
                                 keypart_map, slice, &move_forward);

    if (rc)
    {
      /* This status is returned on any error */
      table->status= STATUS_NOT_FOUND;
      io_perf_end_and_record();
      DBUG_RETURN(rc);
    }

    skip_scan_it_next_call= FALSE;

    /*
      Now get the data for the row into 'buf'.  If we were using a primary key
      then we have all the rows we need.  For a secondary key we now need to
      lookup the primary key.
    */
    if (active_index == table->s->primary_key)
      rc= read_row_from_primary_key(buf);
    else
      rc= read_row_from_secondary_key(buf, kd, move_forward);

    if (rc != HA_ERR_LOCK_DEADLOCK || !is_new_snapshot)
      break; /* Exit the loop */

    // release the snapshot and iterator so they will be regenerated
    trx->release_snapshot();
    release_scan_iterator();
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
    update_row_stats(ROWS_READ);
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

      if (!kd->covers_key(rkey))
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
      if (kd->unpack_record(this, table, buf, &rkey, &value))
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
  uint pk= pk_index(table, tbl_def);
  String rowkey_copy;
  String sec_key_copy;
  const char* table_name = table->s->table_name.str;

  // Only when debugging: don't use snapshot when reading
  // Rdb_transaction *trx= get_or_create_trx(table->in_use);
  // trx->snapshot= nullptr;

  bool save_verify_checksums= verify_checksums;
  verify_checksums= true;

  /* For each secondary index, check that we can get a PK value from it */
  // NO_LINT_DEBUG
  sql_print_information("CHECKTABLE %s: Checking table %s", table_name,
                        table_name);
  ha_rows UNINIT_VAR(row_checksums_at_start); // set/used iff first_index==true
  ha_rows row_checksums= ha_rows(-1);
  bool first_index= true;

  for (uint keyno= 0; keyno < table->s->keys; keyno++)
  {
    if (keyno != pk)
    {
      extra(HA_EXTRA_KEYREAD);
      ha_index_init(keyno, true);
      ha_rows rows= 0;
      ha_rows checksums= 0;
      if (first_index)
        row_checksums_at_start= row_checksums_checked;
      int res;
      // NO_LINT_DEBUG
      sql_print_information("CHECKTABLE %s:   Checking index %s", table_name,
                            table->key_info[keyno].name);
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
          // NO_LINT_DEBUG
          sql_print_error("CHECKTABLE %s:   .. row %lld: index scan error %d",
                          table_name, rows, res);
          goto error;
        }
        rocksdb::Slice key= scan_it->key();
        sec_key_copy.copy(key.data(), key.size(), &my_charset_bin);
        rowkey_copy.copy(last_rowkey.ptr(), last_rowkey.length(),
                         &my_charset_bin);

        if (key_descr[keyno]->unpack_info_has_checksum(scan_it->value()))
        {
          checksums++;
        }

        if ((res= get_row_by_rowid(table->record[0], rowkey_copy.ptr(),
                                   rowkey_copy.length())))
        {
          // NO_LINT_DEBUG
          sql_print_error("CHECKTABLE %s:   .. row %lld: "
                          "failed to fetch row by rowid",
                          table_name, rows);
          goto error;
        }

        longlong hidden_pk_id= 0;
        if (has_hidden_pk(table) &&
            read_hidden_pk_id_from_rowkey(&hidden_pk_id))
          goto error;

        /* Check if we get the same PK value */
        uint packed_size= pk_descr->pack_record(this, table, pack_buffer,
                                                table->record[0],
                                                pk_packed_tuple, nullptr,
                                                nullptr, 0, nullptr,
                                                hidden_pk_id);
        if (packed_size != rowkey_copy.length() ||
            memcmp(pk_packed_tuple, rowkey_copy.ptr(), packed_size))
        {
          // NO_LINT_DEBUG
          sql_print_error("CHECKTABLE %s:   .. row %lld: PK value mismatch",
                          table_name, rows);
          goto print_and_error;
        }

        /* Check if we get the same secondary key value */
        int tail_size;
        packed_size= key_descr[keyno]->pack_record(this, table, pack_buffer,
                                                   table->record[0],
                                                   sec_key_packed_tuple,
                                                   sec_key_tails, &tail_size,
                                                   0, nullptr,
                                                   hidden_pk_id);
        if (packed_size != sec_key_copy.length() ||
            memcmp(sec_key_packed_tuple, sec_key_copy.ptr(), packed_size))
        {
          // NO_LINT_DEBUG
          sql_print_error("CHECKTABLE %s:   .. row %lld: "
                          "secondary index value mismatch",
                          table_name, rows);
          goto print_and_error;
        }
        rows++;
        continue;

print_and_error:
        {
          char buf[1000];
          hexdump_value(buf, 1000, rocksdb::Slice(rowkey_copy.ptr(),
                                                  rowkey_copy.length()));
          // NO_LINT_DEBUG
          sql_print_error("CHECKTABLE %s:   rowkey: %s", table_name, buf);

          hexdump_value(buf, 1000, rocksdb::Slice(retrieved_record.data(),
                                                  retrieved_record.size()));
          // NO_LINT_DEBUG
          sql_print_error("CHECKTABLE %s:   record: %s", table_name, buf);

          hexdump_value(buf, 1000, rocksdb::Slice(sec_key_copy.ptr(),
                                                  sec_key_copy.length()));
          // NO_LINT_DEBUG
          sql_print_error("CHECKTABLE %s:   index: %s", table_name, buf);

          goto error;
        }
      }
      // NO_LINT_DEBUG
      sql_print_information("CHECKTABLE %s:   ... %lld index entries checked "
                            "(%lld had checksums)", table_name, rows,
                            checksums);

      if (first_index)
      {
        row_checksums= row_checksums_checked - row_checksums_at_start;
        first_index= false;
      }
      ha_index_end();
    }
  }
  if (row_checksums != ha_rows(-1))
  {
    // NO_LINT_DEBUG
    sql_print_information("CHECKTABLE %s:   %lld table records had checksums",
                          table_name, row_checksums);
  }
  extra(HA_EXTRA_NO_KEYREAD);

  verify_checksums= save_verify_checksums;
  /*
    TODO: we should check also for PK records that are missing in the secondary
    indexes.
    For that, need to walk through the PK and check that every PK record has a
    proper counterpart in each secondary index.
  */
  return HA_ADMIN_OK;
error:
  verify_checksums= save_verify_checksums;
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

  DEBUG_SYNC(ha_thd(), "rocksdb.get_row_by_rowid");
  DBUG_EXECUTE_IF("dbug.rocksdb.get_row_by_rowid",
                  {
                    THD *thd= ha_thd();
                    const char act[]=
                      "now signal Reached "
                      "wait_for signal.rocksdb.get_row_by_rowid_let_running";
                    DBUG_ASSERT(opt_debug_sync_timeout > 0);
                    DBUG_ASSERT(!debug_sync_set_action(thd,
                                                       STRING_WITH_LEN(act)));
                  };);

  bool found;
  rocksdb::Status s;

  if (!lock_rows)
  {
    trx->acquire_snapshot(true);
    s= trx->Get(pk_descr->get_cf(), key_slice, &retrieved_record);
  }
  else
    s= trx->GetForUpdate(pk_descr->get_cf(), key_slice, &retrieved_record);

  if (!s.IsNotFound() && !s.ok())
  {
    DBUG_RETURN(trx->set_status_error(table->in_use, s, pk_descr, tbl_def));
  }
  found= !s.IsNotFound();

  table->status= STATUS_NOT_FOUND;
  if (found)
  {
    last_rowkey.copy((const char*)rowid, rowid_size, &my_charset_bin);
    rc= convert_record_from_storage_format(&key_slice, buf);
    if (!rc)
      table->status= 0;
  }
  else
  {
    /*
      Note: we don't need to unlock the row. It is intentional that we keep
      locks on rows that don't exist.
    */
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
  if (rc == HA_ERR_KEY_NOT_FOUND)
    rc = HA_ERR_END_OF_FILE;
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
  if (rc == HA_ERR_KEY_NOT_FOUND)
    rc = HA_ERR_END_OF_FILE;

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
  sec_key_match_prefix= nullptr;
  int rc= key_descr[active_index]->is_reverse_cf ? index_last_intern(buf):
                                                   index_first_intern(buf);
  io_perf_end_and_record();
  if (rc == HA_ERR_KEY_NOT_FOUND)
    rc = HA_ERR_END_OF_FILE;
  return rc;
}


int ha_rocksdb::index_last(uchar *buf)
{
  io_perf_start();
  sec_key_match_prefix= nullptr;
  int rc= key_descr[active_index]->is_reverse_cf ? index_first_intern(buf):
                                                   index_last_intern(buf);
  io_perf_end_and_record();
  if (rc == HA_ERR_KEY_NOT_FOUND)
    rc = HA_ERR_END_OF_FILE;
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

  if (is_pk(active_index, table, tbl_def))
    key= pk_packed_tuple;
  else
    key= sec_key_packed_tuple;

  if (key_descr[active_index]->is_reverse_cf)
    key_descr[active_index]->get_supremum_key(key, &key_size);
  else
    key_descr[active_index]->get_infimum_key(key, &key_size);

  rocksdb::Slice index_key((const char*)key, key_size);

  Rdb_transaction *trx= get_or_create_trx(table->in_use);
  bool is_new_snapshot= !trx->has_snapshot();
  // Loop as long as we get a deadlock error AND we end up creating the
  // snapshot here (i.e. it did not exist prior to this)
  for (;;)
  {
    setup_scan_iterator(key_descr[active_index], &index_key, false,
                        !key_descr[active_index]->is_reverse_cf,
                        RDBSE_KEYDEF::INDEX_NUMBER_SIZE);
    skip_scan_it_next_call= TRUE;

    rc= index_next_with_direction(buf, true);
    if (rc != HA_ERR_LOCK_DEADLOCK || !is_new_snapshot)
      break;  // exit the loop

    // release the snapshot and iterator so they will be regenerated
    trx->release_snapshot();
    release_scan_iterator();
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

  if (is_pk(active_index, table, tbl_def))
    key= pk_packed_tuple;
  else
    key= sec_key_packed_tuple;

  if (key_descr[active_index]->is_reverse_cf)
    key_descr[active_index]->get_infimum_key(key, &key_size);
  else
    key_descr[active_index]->get_supremum_key(key, &key_size);

  rocksdb::Slice index_key((const char*)key, key_size);

  Rdb_transaction *trx= get_or_create_trx(table->in_use);
  bool is_new_snapshot= !trx->has_snapshot();
  // Loop as long as we get a deadlock error AND we end up creating the
  // snapshot here (i.e. it did not exist prior to this)
  for (;;)
  {
    setup_scan_iterator(key_descr[active_index], &index_key, false,
                        key_descr[active_index]->is_reverse_cf,
                        RDBSE_KEYDEF::INDEX_NUMBER_SIZE);

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

    if (is_pk(active_index, table, tbl_def))
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

    if (rc != HA_ERR_LOCK_DEADLOCK || !is_new_snapshot)
      break;  /* exit the loop */

    // release the snapshot and iterator so they will be regenerated
    trx->release_snapshot();
    release_scan_iterator();
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
    trx->release_lock(pk_descr->get_cf(), std::string(last_rowkey.ptr(),
                                                      last_rowkey.length()));
  }
}

/*
  Returning true if SingleDelete can be used.
  - Secondary Indexes can always use SingleDelete.
  - If the index is PRIMARY KEY, and if all of the columns of the table
    are covered by the PRIMARY KEY, SingleDelete can be used.
*/
bool ha_rocksdb::can_use_single_delete(uint index)
{
  return (index != table->s->primary_key ||
          table->key_info[index].actual_key_parts == table->s->fields);
}

bool ha_rocksdb::skip_unique_check()
{
  return THDVAR(table->in_use, bulk_load) ||
         THDVAR(table->in_use, skip_unique_check);
}

bool ha_rocksdb::commit_in_the_middle()
{
  return THDVAR(table->in_use, bulk_load) ||
         THDVAR(table->in_use, commit_in_the_middle);
}

  /*
  If table was created without primary key, SQL layer represents the primary
  key number as MAX_INDEXES.  Hence, this function returns true if the table
  does not contain a primary key. (In which case we generate a hidden
  'auto-incremented' pk.)
*/
bool ha_rocksdb::has_hidden_pk(const TABLE* table)
{
  return RDBSE_KEYDEF::rocksdb_has_hidden_pk(table);
}

/*
  Returns true if given index number is a hidden_pk.
  - This is used when a table is created with no primary key.
*/
bool ha_rocksdb::is_hidden_pk(const uint index, const TABLE* table,
                              const RDBSE_TABLE_DEF* tbl_def)
{
  return (table->s->primary_key == MAX_INDEXES &&
          index == tbl_def->n_keys - 1);
}

/* Returns index of primary key */
int ha_rocksdb::pk_index(const TABLE* table, const RDBSE_TABLE_DEF* tbl_def)
{
  return table->s->primary_key == MAX_INDEXES ?
         tbl_def->n_keys - 1 :
         table->s->primary_key;
}

/* Returns true if given index number is a primary key */
bool ha_rocksdb::is_pk(const uint index, const TABLE* table,
                       const RDBSE_TABLE_DEF *tbl_def)
{
  return index == table->s->primary_key || is_hidden_pk(index, table, tbl_def);
}

int ha_rocksdb::write_row(uchar *buf)
{
  DBUG_ENTER("ha_rocksdb::write_row");
  DBUG_ASSERT(buf == table->record[0]);
  DBUG_ASSERT(lock_rows && lock_for_write);

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

  int rv= update_write_row(nullptr, buf, skip_unique_check());
  Rdb_transaction *trx= get_or_create_trx(table->in_use);

  if (rv == 0 && commit_in_the_middle() &&
      trx->get_n_writes() >= THDVAR(table->in_use, bulk_load_size))
  {
    /* Bulk insert mode: flush the write batch if it became too big */
    if (trx->flush_batch())
      rv= HA_ERR_INTERNAL_ERROR;
  }

  if (rv == 0)
  {
    stats.rows_inserted++;
    update_row_stats(ROWS_INSERTED);
  }
  io_perf_end_and_record();
  DBUG_RETURN(rv);
}


int ha_rocksdb::update_write_row(const uchar *old_data,
                                 uchar *new_data,
                                 const bool skip_unique_check)
{
  int pk_packed_size= 0;
  bool pk_key_changed= false;

  int new_packed_size;
  uchar *new_key_packed_tuple;
  int new_key_tail_size;
  longlong hidden_pk_id= 0;

  int old_packed_size;
  int old_key_tail_size;

  rocksdb::Slice old_pk_key_slice;
  rocksdb::Slice new_key_slice;
  rocksdb::Slice new_value_slice;
  rocksdb::Slice old_key_slice;

  DBUG_ENTER("ha_rocksdb::write_row_common");

  Rdb_transaction *trx= get_or_create_trx(table->in_use);

  /*
    Get new row key for any insert, and any update where the pk is not hidden.
    Row key for updates with hidden pk is handled below.
  */
  if (!has_hidden_pk(table))
  {
    pk_packed_size= pk_descr->pack_record(this, table, pack_buffer,
                                          new_data,
                                          pk_packed_tuple, nullptr,
                                          nullptr, 0, nullptr);
  }
  else if (!old_data)
  {
    hidden_pk_id= update_hidden_pk_val();
    pk_packed_size= pk_descr->pack_hidden_pk(table, hidden_pk_id,
                                             pk_packed_tuple);
  }

  if (old_data)
  {
    old_pk_key_slice= rocksdb::Slice(last_rowkey.ptr(), last_rowkey.length());
    /*
      If hidden primary key, rowkey for new record will always be the same as
      before
    */
    if (has_hidden_pk(table))
    {
      memcpy(pk_packed_tuple, old_pk_key_slice.data(), old_pk_key_slice.size());
      pk_packed_size= old_pk_key_slice.size();
      if (read_hidden_pk_id_from_rowkey(&hidden_pk_id))
        DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
    }

    /* Determine which indexes need updating. */
    calc_updated_indexes();
  }

  /*
    Go through each index and determine if the index has uniqueness
    requirements. If it does, then try to obtain a row lock on the new values.
    Once all locks have been obtained, then perform the changes needed to
    update/insert the row.
  */
  for (uint i= 0; i < tbl_def->n_keys; i++)
  {
    KEY* key_info= nullptr;
    uint n_null_fields= 0;
    uint user_defined_key_parts= 1;
    if (!is_hidden_pk(i, table, tbl_def))
    {
      key_info= &table->key_info[i];
      user_defined_key_parts= key_info->user_defined_key_parts;
    }

    /*
      If there are no uniqueness requirements, there's no need to obtain a
      lock for this key. The primary key should have this flag set.
    */
    if (skip_unique_check || (key_info && !(key_info->flags & HA_NOSAME)))
      continue;

    /*
      For UPDATEs, if the key has changed, we need to obtain a lock. INSERTs
      are always require locking.
    */
    if (old_data)
    {
      if (is_pk(i, table, tbl_def))
        old_key_slice= old_pk_key_slice;
      else
      {
        if (!updated_indexes.is_set(i))
          continue;

        old_packed_size= key_descr[i]->pack_record(this, table, pack_buffer,
                                                   old_data,
                                                   sec_key_packed_tuple_old,
                                                   nullptr, nullptr,
                                                   user_defined_key_parts,
                                                   nullptr, hidden_pk_id);
        old_key_slice= rocksdb::Slice((const char*)sec_key_packed_tuple_old,
                                      old_packed_size);
      }
    }

    /*
      Calculate the new key for obtaining the lock
    */
    if (is_pk(i, table, tbl_def))
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
      new_packed_size= key_descr[i]->pack_record(this, table, pack_buffer,
                                                 new_data,
                                                 sec_key_packed_tuple, nullptr,
                                                 nullptr,
                                                 user_defined_key_parts,
                                                 &n_null_fields, 0);
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
      Perform a read to determine if a duplicate entry exists. For primary
      keys, a point lookup will be sufficient. For secondary indexes, a
      range scan is needed.

      note: we intentionally don't set options.snapshot here. We want to read
      the latest committed data.
    */

    bool found;
    if (is_pk(i, table, tbl_def))
    {
      /* Primary key has changed, it should be deleted later. */
      if (old_data)
        pk_key_changed= true;

      rocksdb::Status s;

      /*
        To prevent race conditions like below, it is necessary to
        take a lock for a target row. GetForUpdate() holds a gap lock if
        target key does not exist, so below conditions should never
        happen.

        1) T1 Get(empty) -> T2 Get(empty) -> T1 Put(insert) -> T1 commit
           -> T2 Put(overwrite) -> T2 commit
        2) T1 Get(empty) -> T1 Put(insert, not committed yet) -> T2 Get(empty)
           -> T2 Put(insert, blocked) -> T1 commit -> T2 commit(overwrite)
      */
      s= trx->GetForUpdate(pk_descr->get_cf(), new_key_slice,
          &retrieved_record);

      if (!s.ok() && !s.IsNotFound())
        DBUG_RETURN(trx->set_status_error(table->in_use, s, key_descr[i],
                                          tbl_def));

      found= !s.IsNotFound();
    }
    else
    {
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
      bool total_order_seek=
        !can_use_bloom_filter(key_descr[i], new_key_slice, all_parts_used,
                              is_ascending(key_descr[i], HA_READ_KEY_EXACT));
      bool fill_cache= !THDVAR(ha_thd(), skip_fill_cache);

      /*
        psergey-todo: we just need to take lock, lookups not needed:
      */
      std::string dummy_value;
      rocksdb::Status s= trx->GetForUpdate(key_descr[i]->get_cf(),
                                           new_key_slice,
                                           &dummy_value);
      if (!s.ok() && !s.IsNotFound())
        DBUG_RETURN(trx->set_status_error(table->in_use, s, key_descr[i],
                                          tbl_def));

      rocksdb::Iterator *s_iter= trx->GetIterator(key_descr[i]->get_cf(),
                                                  total_order_seek,
                                                  fill_cache,
                                                  true /* read current data */,
                                                  false /* acquire snapshot */);
      /*
        Need to scan the transaction to see if there is a duplicate key.
        Also need to scan RocksDB and verify the key has not been deleted
        in the transaction.
      */
      s_iter->Seek(new_key_slice);
      found= !read_key_exact(key_descr[i], s_iter, all_parts_used,
                             new_key_slice);
      delete s_iter;
    }

    if (found)
    {
      /* There is a row with this rowid already, so error out. */
      errkey= i;
      dupp_errkey= errkey;
      DBUG_RETURN(HA_ERR_FOUND_DUPP_KEY);
    }
  }

  DEBUG_SYNC(ha_thd(), "rocksdb.update_write_row_after_unique_check");

  /*
    At this point, all locks have been obtained, and all checks for duplicate
    keys have been performed. No further errors can be allowed to occur from
    here because updates to the transaction will be made and those updates
    cannot be easily removed without rolling back the entire transaction.
  */
  for (uint i= 0; i < tbl_def->n_keys; i++)
  {
    if (is_hidden_pk(i, table, tbl_def))
      continue;

    /*
      Determine if the old key needs to be deleted.
    */
    if (is_pk(i, table, tbl_def))
    {
      if (pk_key_changed)
      {
        rocksdb::Status s= delete_or_singledelete(i, trx,
                                                  key_descr[i]->get_cf(),
                                                  old_pk_key_slice);
        if (!s.ok())
        {
          DBUG_RETURN(trx->set_status_error(table->in_use, s, key_descr[i],
                                            tbl_def));
        }
      }
      continue;
    }

    /*
      Can skip updating this key if none of the key fields have changed.
    */
    if (old_data && !updated_indexes.is_set(i))
      continue;

    new_packed_size= key_descr[i]->pack_record(this, table, pack_buffer,
                                               new_data,
                                               sec_key_packed_tuple,
                                               sec_key_tails,
                                               &new_key_tail_size,
                                               0, nullptr, hidden_pk_id);
    new_key_slice= rocksdb::Slice((const char*)sec_key_packed_tuple,
                                   new_packed_size);
    new_value_slice= rocksdb::Slice((const char*)sec_key_tails,
                                    new_key_tail_size);

    if (old_data)
    {
      // The old value (just used for delete)
      old_packed_size= key_descr[i]->pack_record(this, table, pack_buffer,
                                                 old_data,
                                                 sec_key_packed_tuple_old,
                                                 sec_key_tails_old,
                                                 &old_key_tail_size,
                                                 0, nullptr, hidden_pk_id);
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

      /*
        Deleting entries from secondary index should skip locking, but
        be visible to the transaction.
        (also note that DDL statements do not delete rows, so this is not a DDL
         statement)
      */
      trx->GetIndexedWriteBatch()->SingleDelete(key_descr[i]->get_cf(),
                                                old_key_slice);
    }

    /*
      We're writing a new entry for secondary key. We can skip locking; we
      should write to
      - WriteBatchWithIndex normally (so that transaction sees the new row)
      - non-indexed WriteBatch, when we don't need to see the new row:
         = when doing a DDL operation and writing to a non-unique index, or
         = when doing a bulk load
    */
    rocksdb::WriteBatchBase *write_batch;
    if ((trx->ddl_transaction && !(table->key_info[i].flags & HA_NOSAME)) ||
        skip_unique_check)
    {
      write_batch= trx->GetBlindWriteBatch();
    }
    else
      write_batch= trx->GetIndexedWriteBatch();

    write_batch->Put(key_descr[i]->get_cf(), new_key_slice, new_value_slice);
  }

  if (table->next_number_field)
    update_auto_incr_val();

  new_key_slice= rocksdb::Slice((const char*)pk_packed_tuple, pk_packed_size);
  rocksdb::Slice value_slice;

  convert_record_to_storage_format((const char*)pk_packed_tuple,
                                   pk_packed_size, &value_slice);
  if (skip_unique_check)
  {
    /*
      It is responsibility of the user to make sure that the data being
      inserted doesn't violate any unique keys.
    */
    trx->GetBlindWriteBatch()->Put(pk_descr->get_cf(), new_key_slice,
                                     value_slice);
  }
  else if (trx->ddl_transaction)
  {
    /*
      DDL statement must check for unique key conflicts. For example:
      ALTER TABLE tbl DROP PRIMARY KEY, ADD PRIMARY KEY(non_unique_column)
    */
    trx->GetIndexedWriteBatch()->Put(pk_descr->get_cf(), new_key_slice,
                                     value_slice);
  }
  else
  {
    auto s= trx->Put(pk_descr->get_cf(), new_key_slice, value_slice);
    if (!s.ok())
    {
      if (s.IsBusy())
      {
        errkey= table->s->primary_key;
        dupp_errkey= errkey;
        DBUG_RETURN(HA_ERR_FOUND_DUPP_KEY);
      }

      DBUG_RETURN(trx->set_status_error(table->in_use, s, pk_descr, tbl_def));
    }
  }

  DBUG_RETURN(0);
}


/*
  Open a cursor and position it at the passed record
*/

void ha_rocksdb::setup_scan_iterator(RDBSE_KEYDEF *keydef,
                                     rocksdb::Slice *slice,
                                     const bool use_all_keys,
                                     const bool is_ascending,
                                     const uint eq_cond_len)
{
  DBUG_ASSERT(slice->size() >= eq_cond_len);
  Rdb_transaction *trx= get_or_create_trx(table->in_use);

  bool skip_bloom= true;

  rocksdb::Slice eq_cond(slice->data(), eq_cond_len);
  if (can_use_bloom_filter(keydef, eq_cond, use_all_keys, is_ascending))
    skip_bloom= false;

  /*
    In some cases, setup_scan_iterator() is called multiple times from
    the same query but bloom filter can not always be used.
    Suppose the following query example. id2 is VARCHAR(30) and PRIMARY KEY (id1, id2).
     select count(*) from t2 WHERE id1=100 and id2 IN ('00000000000000000000', '100');
    In this case, setup_scan_iterator() is called twice, the first time is for
    (id1, id2)=(100, '00000000000000000000') and the second time is for (100, '100').
    If prefix bloom filter length is 24 bytes, prefix bloom filter can be used for the
    first condition but not for the second condition.
    If bloom filter condition is changed, currently it is necessary to destroy and
    re-create Iterator.
  */
  if (scan_it_skips_bloom != skip_bloom)
  {
    release_scan_iterator();
  }

  /*
    SQL layer can call rnd_init() multiple times in a row.
    In that case, re-use the iterator, but re-position it at the table start.
  */
  if (!scan_it)
  {
    bool fill_cache= !THDVAR(ha_thd(), skip_fill_cache);
    scan_it= trx->GetIterator(keydef->get_cf(), skip_bloom, fill_cache);
    scan_it_skips_bloom= skip_bloom;
  }
  /*
    Seek() will "Position at the first key in the source that at or past target".
    The operation cannot fail.
  */
  scan_it->Seek(*slice);
}

void ha_rocksdb::setup_iterator_for_rnd_scan()
{
  uint key_size;

  if (pk_descr->is_reverse_cf)
    pk_descr->get_supremum_key(pk_packed_tuple, &key_size);
  else
    pk_descr->get_infimum_key(pk_packed_tuple, &key_size);

  rocksdb::Slice table_key((const char*)pk_packed_tuple, key_size);

  setup_scan_iterator(pk_descr, &table_key);
  skip_scan_it_next_call= true;
}

int ha_rocksdb::rnd_init(bool scan)
{
  DBUG_ENTER("ha_rocksdb::rnd_init");

  io_perf_start();

  Rdb_transaction *trx= get_or_create_trx(table->in_use);
  if (scan)
  {
    rnd_scan_is_new_snapshot= !trx->has_snapshot();
    setup_iterator_for_rnd_scan();
  }
  else
  {
    /* We don't need any preparations for rnd_pos() calls. */
  }

  // If lock_rows is on then we will be doing a GetForUpdate when accessing
  // the index, so don't acquire the snapshot right away.  Otherwise acquire
  // the snapshot immediately.
  trx->acquire_snapshot(!lock_rows);

  io_perf_end_and_record();

  DBUG_RETURN(0);
}


int ha_rocksdb::rnd_next(uchar *buf)
{
  int rc;
  io_perf_start();
  for (;;)
  {
    rc= rnd_next_with_direction(buf, true);
    if (rc != HA_ERR_LOCK_DEADLOCK || !rnd_scan_is_new_snapshot)
      break;  /* exit the loop */

    // release the snapshot and iterator and then regenerate them
    Rdb_transaction *trx= get_or_create_trx(table->in_use);
    trx->release_snapshot();
    release_scan_iterator();
    setup_iterator_for_rnd_scan();
  }

  rnd_scan_is_new_snapshot= false;

  io_perf_end_and_record();
  if (rc == HA_ERR_KEY_NOT_FOUND)
    rc = HA_ERR_END_OF_FILE;
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

  if (!scan_it || !scan_it->Valid())
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
    if (!pk_descr->covers_key(key))
    {
      rc= HA_ERR_END_OF_FILE;
      table->status= STATUS_NOT_FOUND;
    }
    else
    {
      if (lock_rows)
      {
        /*
          Lock the row we've just read.

          Now we call GetForUpdate which will 1) Take a lock and 2) Will fail
          if the row was deleted since the snapshot was taken.
        */
        Rdb_transaction *trx= get_or_create_trx(table->in_use);
        DEBUG_SYNC(ha_thd(), "rocksdb_concurrent_delete");
        rocksdb::Status s= trx->GetForUpdate(pk_descr->get_cf(), key,
                                             &retrieved_record);
        if (!s.ok())
        {
          DBUG_RETURN(trx->set_status_error(table->in_use, s, pk_descr,
                                            tbl_def));
        }

        // If we called GetForUpdate() use the value from that call not from
        // the iterator as it may be stale since we don't have a snapshot
        // when lock_rows is true.
        last_rowkey.copy(key.data(), key.size(), &my_charset_bin);
        rc= convert_record_from_storage_format(&key, buf);
      }
      else
      {
        // Use the value from the iterator
        rocksdb::Slice value= scan_it->value();
        last_rowkey.copy(key.data(), key.size(), &my_charset_bin);
        rc= convert_record_from_storage_format(&key, &value, buf);
      }

      table->status= 0;
    }
  }
  else
    rc= HA_ERR_END_OF_FILE;

  if (!rc) {
    stats.rows_read++;
    stats.rows_index_next++;
    update_row_stats(ROWS_READ);
  }

  DBUG_RETURN(rc);
}


int ha_rocksdb::rnd_end()
{
  DBUG_ENTER("ha_rocksdb::rnd_end");

  release_scan_iterator();

  DBUG_RETURN(0);
}

int ha_rocksdb::index_init(uint idx, bool sorted)
{
  Rdb_transaction *trx= get_or_create_trx(table->in_use);

  // If lock_rows is on then we will be doing a GetForUpdate when accessing
  // the index, so don't acquire the snapshot right away.  Otherwise acquire
  // the snapshot immediately.
  trx->acquire_snapshot(!lock_rows);

  active_index= idx;

  return 0;
}

int ha_rocksdb::index_end()
{
  release_scan_iterator();

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
  tbl_def->auto_incr_val= 1;
  tbl_def->hidden_pk_val= 1;
  remove_rows(tbl_def);
  return 0;
}

/*
  Delete the row we've last read. The row is also passed as parameter.

  @detail
    The caller guarantees table buf points to the row that was just read.
    The row is either table->record[0] or table->record[1].
    (Check out InnoDB: row_update_for_mysql() has "UT_NOT_USED(mysql_rec)"
*/
int ha_rocksdb::delete_row(const uchar *buf)
{
  DBUG_ENTER("ha_rocksdb::delete_row");

  io_perf_start();

  rocksdb::Slice key_slice(last_rowkey.ptr(), last_rowkey.length());
  Rdb_transaction *trx= get_or_create_trx(table->in_use);

  uint index= pk_index(table, tbl_def);
  rocksdb::Status s= delete_or_singledelete(index, trx,
                                            pk_descr->get_cf(),
                                            key_slice);
  if (!s.ok())
  {
    DBUG_RETURN(trx->set_status_error(table->in_use, s, pk_descr, tbl_def));
  }

  longlong hidden_pk_id= 0;
  if (tbl_def->n_keys > 1 &&
      has_hidden_pk(table) && read_hidden_pk_id_from_rowkey(&hidden_pk_id))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  // Delete the record for every secondary index
  for (uint i= 0; i < tbl_def->n_keys; i++)
  {
    if (!is_pk(i, table, tbl_def))
    {
      int packed_size;
      packed_size= key_descr[i]->pack_record(this, table, pack_buffer,
                                             buf, sec_key_packed_tuple,
                                             nullptr, nullptr, 0, nullptr,
                                             hidden_pk_id);
      rocksdb::Slice secondary_key_slice((char*)sec_key_packed_tuple,
                                         packed_size);
      /* Deleting on secondary key doesn't need any locks: */
      trx->GetIndexedWriteBatch()->SingleDelete(key_descr[i]->get_cf(),
                                                secondary_key_slice);
    }
  }

  stats.rows_deleted++;
  update_row_stats(ROWS_DELETED);

  io_perf_end_and_record();

  DBUG_RETURN(0);
}

rocksdb::Status
ha_rocksdb::delete_or_singledelete(uint index,
                                   Rdb_transaction *trx,
                                   rocksdb::ColumnFamilyHandle* column_family,
                                   const rocksdb::Slice& key)
{
  if (can_use_single_delete(index))
    return trx->SingleDelete(column_family, key);
  return trx->Delete(column_family, key);
}


void ha_rocksdb::update_stats(void)
{
  DBUG_ENTER("ha_rocksdb::update_stats");

  stats.records= 0;
  stats.index_file_length= 0ul;
  stats.data_file_length= 0ul;
  stats.mean_rec_length= 0;
  for (uint i= 0; i < tbl_def->n_keys; i++)
  {
    if (is_pk(i, table, tbl_def))
    {
      stats.data_file_length= pk_descr->stats.actual_disk_size;
      stats.records = pk_descr->stats.rows;
    }
    else
    {
      stats.index_file_length+= key_descr[i]->stats.actual_disk_size;
    }
  }
  DBUG_VOID_RETURN;
}

int ha_rocksdb::info(uint flag)
{
  DBUG_ENTER("ha_rocksdb::info");

  if (!table)
    return 1;

  if (flag & HA_STATUS_VARIABLE)
  {
    /*
      Test only to simulate corrupted stats
    */
    DBUG_EXECUTE_IF("myrocks_simulate_negative_stats",
        pk_descr->stats.actual_disk_size = -pk_descr->stats.actual_disk_size;);

    update_stats();

    /*
      If any stats are negative due to bad cached stats, re-run analyze table
      and re-retrieve the stats.
    */
    if (static_cast<longlong>(stats.data_file_length) < 0 ||
        static_cast<longlong>(stats.index_file_length) < 0 ||
        static_cast<longlong>(stats.records) < 0)
    {
      analyze(nullptr, nullptr);
      update_stats();
    }

    if (stats.records == 0)
    {
      // most likely, the table is in memtable
      // try to deduce from GetApproximateSizes
      RDBSE_KEYDEF *kd= pk_descr;
      uchar buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*2];
      auto r = get_range(pk_index(table, tbl_def), buf);
      uint64_t sz = 0;
      rdb->GetApproximateSizes(
        kd->get_cf(),
        &r, 1,
        &sz, true);
      stats.records = sz/ROCKSDB_ASSUMED_KEY_VALUE_DISK_SIZE;
      stats.data_file_length = sz;

      if (rocksdb_debug_optimizer_n_rows > 0)
        stats.records = rocksdb_debug_optimizer_n_rows;
    }

    if (stats.records != 0)
      stats.mean_rec_length = stats.data_file_length / stats.records;
  }
  if (flag & HA_STATUS_CONST)
  {
    ref_length= pk_descr->max_storage_fmt_length();

    // TODO: Needs to reimplement after having real index statistics
    for (uint i= 0; i < tbl_def->n_keys; i++)
    {
      if (is_hidden_pk(i, table, tbl_def))
      {
        continue;
      }
      KEY* k= &table->key_info[i];
      for (uint j = 0; j < k->actual_key_parts; j++)
      {
        uint x = key_descr[i]->stats.distinct_keys_per_prefix.size() > j && key_descr[i]->stats.distinct_keys_per_prefix[j] > 0 ?
          key_descr[i]->stats.rows / key_descr[i]->stats.distinct_keys_per_prefix[j] :
          0;
        if (x > stats.records)
          x = stats.records;
        if ((x == 0 && rocksdb_debug_optimizer_no_zero_cardinality) ||
            rocksdb_debug_optimizer_n_rows > 0)
        {
          // Fake cardinality implementation. For example, (idx1, idx2, idx3) index
          // will have rec_per_key for (idx1)=4, (idx1,2)=2, and (idx1,2,3)=1.
          // rec_per_key for the whole index is 1, and multiplied by 2^n if
          // n suffix columns of the index are not used.
          x = 1 << (k->actual_key_parts-j-1);
        }
        k->rec_per_key[j]= x;
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

  longlong hidden_pk_id= 0;
  if (has_hidden_pk(table) && read_hidden_pk_id_from_rowkey(&hidden_pk_id))
    DBUG_ASSERT(false);  // should never reach here

  /*
    Get packed primary key value from the record.

    (Note: last_rowkey has the packed PK of last-read-row, which allows to
    handle most cases, but there is an exception to this: when slave applies
    RBR events, it fills the record and then calls position(); rnd_pos())

    Also note that we *can* rely on PK column values being available. This is
    because table_flags() includes HA_PRIMARY_KEY_REQUIRED_FOR_POSITION bit.
    When that is true, table->prepare_for_position() adds PK columns into the
    read set (this may potentially disable index-only access when PK column
    cannot be restored from its mem-comparable form in the secondary indexes).
  */
  uint packed_size= pk_descr->pack_record(this, table, pack_buffer, record,
                                          ref, nullptr, nullptr, 0, nullptr,
                                          hidden_pk_id);

  /*
    It could be that mem-comparable form of PK occupies less than ref_length
    bytes. Fill the remainder with zeros.
  */
  if (ref_length > packed_size)
    memset(ref + packed_size, 0, ref_length - packed_size);

  DBUG_VOID_RETURN;
}


int ha_rocksdb::rnd_pos(uchar *buf, uchar *pos)
{
  int rc;
  size_t len;
  DBUG_ENTER("ha_rocksdb::rnd_pos");

  stats.rows_requested++;
  len= pk_descr->key_length(table, rocksdb::Slice((const char*)pos,
                                                  ref_length));
  if (len == size_t(-1))
  {
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR); /* Data corruption? */
  }

  rc= get_row_by_rowid(buf, pos, len);

  if (!rc)
  {
    stats.rows_read++;
    update_row_stats(ROWS_READ);
  }

  DBUG_RETURN(rc);
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
      /* Walk over all key parts, including the "extended key" suffix */
      uint key_parts= key_descr[keynr]->get_m_key_parts();
      for (uint kp= 0; kp < key_parts; kp++)
      {
        if (has_hidden_pk(table) && kp + 1 == key_parts)
          break;

        Field *field= key_descr[keynr]->get_table_field_for_part_no(table, kp);
        if (bitmap_is_set(table->write_set, field->field_index))
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
  DBUG_ASSERT(lock_rows && lock_for_write);
  /*
    old_data points to record we're updating. It is the same as the record
    we've just read (for multi-table UPDATE, too, because SQL layer will make
    an rnd_pos() call to re-read the record before calling update_row())
  */
  DBUG_ASSERT(new_data == table->record[0]);

  io_perf_start();

  int rv= update_write_row(old_data, new_data, false);
  io_perf_end_and_record();

  if (rv == 0)
  {
    stats.rows_updated++;
    update_row_stats(ROWS_UPDATED);
  }
  DBUG_RETURN(rv);
}


/* The following function was copied from ha_blackhole::store_lock: */
THR_LOCK_DATA **ha_rocksdb::store_lock(THD *thd,
                                         THR_LOCK_DATA **to,
                                         enum thr_lock_type lock_type)
{
  DBUG_ENTER("ha_rocksdb::store_lock");

  /* First, make a decision about MyRocks's internal locking */
  if (lock_type >= TL_WRITE_ALLOW_WRITE)
  {
    lock_rows= true;
    lock_for_write=true;
  }
  else if (lock_type == TL_READ_WITH_SHARED_LOCKS)
  {
    lock_rows= true;
    lock_for_write= false;
  }
  else
  {
    lock_rows= false;
    lock_for_write= false;
  }

  /* Then, tell the SQL layer what kind of locking it should use: */

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

  int binlog_format= thd_binlog_format(thd);
  bool unsafe_for_binlog= THDVAR(ha_thd(), unsafe_for_binlog);
  if (lock_type == F_WRLCK && !thd->rli_slave &&
      !unsafe_for_binlog &&
      binlog_format != BINLOG_FORMAT_ROW &&
      binlog_format != BINLOG_FORMAT_UNSPEC &&
      thd_binlog_filter_ok(thd))
  {
    my_printf_error(ER_UNKNOWN_ERROR,
             "Can't execute updates on master with binlog_format != ROW.",
             MYF(0));
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  if (thd_tx_isolation(thd) < ISO_READ_COMMITTED ||
      thd_tx_isolation(thd) > ISO_REPEATABLE_READ)
  {
    my_printf_error(ER_UNKNOWN_ERROR,
                    "MyRocks supports only READ COMMITTED and REPEATABLE READ "
                    "isolation levels. Please change from current isolation "
                    "level %s",
                    MYF(0), tx_isolation_names[thd_tx_isolation(thd)]);
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  io_perf_start();

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
      res= trx->commit_or_rollback();
    }
  }
  else
  {
    /*
      It's nice to do the following on start of every statement. The problem
      is, handler->start_stmt() is not called for INSERTs.
      So, we put this code here.
    */
    Rdb_transaction* trx= get_or_create_trx(thd);

    THD *thd= table->in_use;
    trx->set_params(THDVAR(thd, lock_wait_timeout),
                    THDVAR(thd, max_row_locks));
    // TODO: get_or_create_trx above does the same

    store_checksums= THDVAR(thd, store_checksums);
    verify_checksums= THDVAR(thd, verify_checksums);
    checksums_pct= THDVAR(thd, checksums_pct);

    updated_indexes_is_valid= false;

    if (skip_unique_check())
    {
      if ((thd->lex->sql_command == SQLCOM_INSERT ||
          thd->lex->sql_command == SQLCOM_LOAD ||
          thd->lex->sql_command == SQLCOM_REPLACE) &&
          (thd->lex->duplicates == DUP_REPLACE ||
          thd->lex->duplicates == DUP_UPDATE))
      {
        my_printf_error(ER_UNKNOWN_ERROR,
                        "When unique checking is disabled in MyRocks, INSERT,"
                        "UPDATE,LOAD statements with clauses that update or "
                        "replace the key (i.e. "
                        "INSERT ON DUPLICATE KEY UPDATE, REPLACE) are not "
                        "allowed. Query: %s",
                        MYF(0), thd->query());
        DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
      }
    }

    if (lock_type == F_WRLCK)
    {
      if (trx->is_tx_read_only())
      {
        my_printf_error(ER_UNKNOWN_ERROR,
                        "Can't execute updates when you started a transaction "
                        "with START TRANSACTION WITH CONSISTENT [ROCKSDB] "
                        "SNAPSHOT.",
                        MYF(0));
        DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
      }

      /*
        SQL layer signals us to take a write lock. It does so when starting DML
        statement. We should put locks on the rows we're reading.

        Note: sometimes, external_lock() can be called without a prior
        ::store_lock call.  That's why we need to set lock_* members here, too.
      */
      lock_rows= true;
      lock_for_write= true;

      if (thd->lex->sql_command == SQLCOM_CREATE_INDEX ||
          thd->lex->sql_command == SQLCOM_DROP_INDEX   ||
          thd->lex->sql_command == SQLCOM_ALTER_TABLE)
      {
        trx->ddl_transaction= true;
      }
    }
    rocksdb_register_trx(rocksdb_hton, thd, trx);
  }

  io_perf_end_and_record();
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
   /* trx->set_params(THDVAR(table->in_use, lock_wait_timeout),
                    THDVAR(table->in_use, max_row_locks)); */
    store_checksums= THDVAR(thd, store_checksums);
    verify_checksums= THDVAR(thd, verify_checksums);

    rocksdb_register_trx(ht, thd, trx);
  }
  return 0;
}

rocksdb::Range get_range(
  uint32_t i, uchar buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*2], int offset1,
  int offset2)
{
  uchar* buf_begin = buf;
  uchar* buf_end = buf+RDBSE_KEYDEF::INDEX_NUMBER_SIZE;
  store_index_number(buf_begin, i + offset1);
  store_index_number(buf_end, i + offset2);

  return rocksdb::Range(
    rocksdb::Slice((const char*) buf_begin, RDBSE_KEYDEF::INDEX_NUMBER_SIZE),
    rocksdb::Slice((const char*) buf_end, RDBSE_KEYDEF::INDEX_NUMBER_SIZE));
}

static rocksdb::Range get_range(
  RDBSE_KEYDEF* keydef,
  uchar buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*2],
  int offset1, int offset2)
{
  return get_range(keydef->get_index_number(), buf, offset1, offset2);
}

rocksdb::Range get_range(
  RDBSE_KEYDEF* keydef, uchar buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*2])
{
  if (keydef->is_reverse_cf)
    return myrocks::get_range(keydef, buf, 1, 0);
  else
    return myrocks::get_range(keydef, buf, 0, 1);
}

rocksdb::Range ha_rocksdb::get_range(
  int i, uchar buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*2]) const
{
  return myrocks::get_range(key_descr[i], buf);
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
    // "stop_drop_index_thread = true" might be set by shutdown command
    // after drop_index_thread releases drop_index_interrupt_mutex
    // (i.e. while executing expensive Seek()). To prevent drop_index_thread
    // from entering long cond_timedwait, checking if stop_drop_index_thread
    // is true or not is needed, with drop_index_interrupt_mutex held.
    if (stop_drop_index_thread) {
      break;
    }

    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += dict_manager.is_drop_index_empty()
      ? 24*60*60 // no filtering
      : 60; // filtering

    auto ret MY_ATTRIBUTE((__unused__)) = mysql_cond_timedwait(
                                    &drop_index_interrupt_cond,
                                    &drop_index_interrupt_mutex, &ts);
    if (stop_drop_index_thread) {
      break;
    }
    // make sure, no program error is returned
    DBUG_ASSERT(ret == 0 || ret == ETIMEDOUT);
    mysql_mutex_unlock(&drop_index_interrupt_mutex);

    std::vector<GL_INDEX_ID> indices;
    dict_manager.get_drop_indexes_ongoing(&indices);
    if (!indices.empty()) {
      std::unordered_set<GL_INDEX_ID> finished;
      rocksdb::ReadOptions read_opts;
      read_opts.total_order_seek = true; // disable bloom filter

      for (auto d : indices) {
        uint32 cf_flags= 0;
        if (!dict_manager.get_cf_flags(d.cf_id, &cf_flags))
        {
          sql_print_error("RocksDB: Failed to get column family flags "
                          "from cf id %u. MyRocks data dictionary may "
                          "get corrupted.", d.cf_id);
          abort_with_stack_traces();
        }
        rocksdb::ColumnFamilyHandle* cfh= cf_manager.get_cf(d.cf_id);
        DBUG_ASSERT(cfh);
        bool is_reverse_cf= cf_flags & RDBSE_KEYDEF::REVERSE_CF_FLAG;

        bool index_removed= false;
        uchar key_buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE]= {0};
        store_big_uint4(key_buf, d.index_id);
        rocksdb::Slice key = rocksdb::Slice((char*)key_buf, sizeof(key_buf));
        uchar buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*2];
        rocksdb::Range range = get_range(d.index_id, buf, is_reverse_cf?1:0,
            is_reverse_cf?0:1);
        rocksdb::CompactRangeOptions compact_range_options;
        compact_range_options.bottommost_level_compaction =
                  rocksdb::BottommostLevelCompaction::kForce;
        compact_range_options.exclusive_manual_compaction = false;
        rocksdb::Status status = DeleteFilesInRange(rdb->GetBaseDB(), cfh,
            &range.start, &range.limit);
        if (!status.ok())
        {
          if (status.IsShutdownInProgress())
          {
            break;
          }
          rocksdb_handle_io_error(status, ROCKSDB_IO_ERROR_BG_THREAD);
        }
        status = rdb->CompactRange(
            compact_range_options, cfh, &range.start, &range.limit);
        if (!status.ok())
        {
          if (status.IsShutdownInProgress())
          {
            break;
          }
          rocksdb_handle_io_error(status, ROCKSDB_IO_ERROR_BG_THREAD);
        }
        std::unique_ptr<rocksdb::Iterator> it(
          rdb->NewIterator(read_opts, cfh));
        it->Seek(key);
        if (is_reverse_cf)
        {
          if (!it->Valid())
          {
            it->SeekToLast();
          }
          else
          {
            it->Prev();
          }
        }
        if (!it->Valid())
        {
          index_removed= true;
        }
        else
        {
          if (memcmp(it->key().data(), key_buf, RDBSE_KEYDEF::INDEX_NUMBER_SIZE))
          {
            // Key does not have same prefix
            index_removed= true;
          }
        }
        if (index_removed)
        {
          finished.insert(d);
        }
      }

      if (!finished.empty()) {
        dict_manager.done_drop_indexes(finished);
      }
    }
    mysql_mutex_lock(&drop_index_interrupt_mutex);
  }

  mysql_mutex_unlock(&drop_index_interrupt_mutex);
  mysql_mutex_unlock(&drop_index_mutex);

  return nullptr;
}

RDBSE_TABLE_DEF* ha_rocksdb::get_table_if_exists(const char* tablename)
{
  StringBuffer<256> strbuf;
  if (rocksdb_normalize_tablename(tablename, &strbuf))
  {
    // We were not passed table name?
    DBUG_ASSERT(0);
    return nullptr;
  }
  return ddl_manager.find(reinterpret_cast<uchar*>(strbuf.c_ptr()),
                          strbuf.length());
}

/*
  Note: the following function is called when the table is not open. That is,
  this->table==nullptr, pk_key_descr==nullptr, etc.

  tablename points to line in form "./dbname/tablename".
*/

int ha_rocksdb::delete_table(const char *tablename)
{
  DBUG_ENTER("ha_rocksdb::delete_table");
  std::unique_ptr<rocksdb::WriteBatch> wb= dict_manager.begin();
  rocksdb::WriteBatch *batch= wb.get();

  /* Find the table in the hash */
  RDBSE_TABLE_DEF *tbl = get_table_if_exists(tablename);
  if (!tbl)
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  dict_manager.add_drop_table(tbl->key_descr, tbl->n_keys, batch);

  /*
    Remove the table entry in data dictionary (this will also remove it from
    the persistent data dictionary).
  */
  ddl_manager.remove(tbl, batch, true);
  if (dict_manager.commit(batch))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  signal_drop_index_thread();

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
      if (!tbl->key_descr[i]->covers_key(key))
        break;
      if (can_use_single_delete(i))
        rdb->SingleDelete(wo, key);
      else
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
         been just created with (TABLE*)nullptr.

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

/*
  Given a starting key and an ending key, estimate the number of rows that
  will exist between the two keys.
*/
ha_rows ha_rocksdb::records_in_range(uint inx, key_range *min_key,
                                     key_range *max_key)
{
  DBUG_ENTER("ha_rocksdb::records_in_range");
  ha_rows ret = THDVAR(ha_thd(), records_in_range);
  if (ret) {
    DBUG_RETURN(ret);
  }
  if (table->force_index) {
    ha_rows force_rows = THDVAR(ha_thd(), force_index_records_in_range);
    if (force_rows) {
      DBUG_RETURN(force_rows);
    }
  }

  RDBSE_KEYDEF *kd= key_descr[inx];
  DBUG_ASSERT(kd);

  uint size1=0;
  if (min_key) {
    size1 = kd->pack_index_tuple(
      this,
      table,
      pack_buffer,
      sec_key_packed_tuple,
      min_key->key,
      min_key->keypart_map
    );
    if (min_key->flag == HA_READ_PREFIX_LAST_OR_PREV ||
        min_key->flag == HA_READ_PREFIX_LAST ||
        min_key->flag == HA_READ_AFTER_KEY)
    {
      kd->successor(sec_key_packed_tuple, size1);
    }
  } else {
    kd->get_infimum_key(sec_key_packed_tuple, &size1);
  }

  uint size2=0;
  if (max_key) {
    size2 = kd->pack_index_tuple(
      this,
      table,
      pack_buffer,
      sec_key_packed_tuple_old,
      max_key->key,
      max_key->keypart_map
    );
    if (max_key->flag == HA_READ_PREFIX_LAST_OR_PREV ||
        max_key->flag == HA_READ_PREFIX_LAST ||
        max_key->flag == HA_READ_AFTER_KEY)
    {
      kd->successor(sec_key_packed_tuple_old, size2);
    }
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

  // slice1 >= slice2 means no row will match
  if (slice1.compare(slice2) >= 0)
  {
    DBUG_RETURN(0);
  }

  rocksdb::Range r(
    kd->is_reverse_cf ? slice2 : slice1,
    kd->is_reverse_cf ? slice1 : slice2
  );

  uint64_t sz=0;
  auto disk_size = kd->stats.actual_disk_size;
  if (disk_size == 0)
    disk_size = kd->stats.data_size;
  auto rows = kd->stats.rows;
  if (rows == 0 || disk_size == 0)
  {
    // no stats. Most likely, this is memtable-only table.
    // Query for the range size including memtable.
    rows = 1;
    disk_size = ROCKSDB_ASSUMED_KEY_VALUE_DISK_SIZE;
    rdb->GetApproximateSizes(
      kd->get_cf(),
      &r, 1,
      &sz, true);
  }
  else
  {
    // Have stats, query for size which doesn't include memtable.
    rdb->GetApproximateSizes(
      kd->get_cf(),
      &r, 1,
      &sz);
  }

  ret = rows*sz/disk_size;
  if (rocksdb_debug_optimizer_n_rows > 0)
  {
    ret= rocksdb_debug_optimizer_n_rows;
  }
  else if (ret == 0)
  {
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
    if (!rdb->CompactRange(rocksdb::CompactRangeOptions(),
                           key_descr[i]->get_cf(),
                           &range.start, &range.limit).ok())
    {
      rc= 1;
      break;
    }
  }
  return rc;
}

int ha_rocksdb::analyze(THD* thd, HA_CHECK_OPT* check_opt)
{
  DBUG_ENTER("ha_rocksdb::analyze");

  if (!table)
    DBUG_RETURN(1);

  // find per column family key ranges which need to be queried
  std::unordered_map<rocksdb::ColumnFamilyHandle*, std::vector<rocksdb::Range>>
    ranges;
  std::unordered_set<GL_INDEX_ID> ids_to_check;
  std::vector<uchar> buf(table->s->keys * 2 * RDBSE_KEYDEF::INDEX_NUMBER_SIZE);
  for (uint i = 0; i < table->s->keys; i++)
  {
    auto bufp = &buf[i * 2 * RDBSE_KEYDEF::INDEX_NUMBER_SIZE];
    ranges[key_descr[i]->get_cf()].push_back(get_range(i, bufp));
    ids_to_check.insert(key_descr[i]->get_gl_index_id());
  }

  // for analyze statements, force flush on memtable to get accurate cardinality
  Column_family_manager& cf_manager = rocksdb_get_cf_manager();
  if (thd != nullptr && THDVAR(thd, flush_memtable_on_analyze) &&
      !rocksdb_pause_background_work)
  {
    for (auto it : ids_to_check)
    {
      rdb->Flush(rocksdb::FlushOptions(), cf_manager.get_cf(it.cf_id));
    }
  }

  // get RocksDB table properties for these ranges
  rocksdb::TablePropertiesCollection props;
  for (auto it : ranges)
  {
    auto old_size MY_ATTRIBUTE((__unused__)) = props.size();
    auto status = rdb->GetPropertiesOfTablesInRange(
      it.first, &it.second[0], it.second.size(), &props);
    DBUG_ASSERT(props.size() >= old_size);
    if (!status.ok())
      DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  int num_sst = 0;
  // group stats per index id
  std::unordered_map<GL_INDEX_ID, MyRocksTablePropertiesCollector::IndexStats>
    stats;
  for (auto it : props)
  {
    std::vector<MyRocksTablePropertiesCollector::IndexStats> sst_stats =
        MyRocksTablePropertiesCollector::GetStatsFromTableProperties(
          it.second);
    /*
      sst_stats is a list of index statistics for indexes that have entries
      in the current SST file.
    */
    for (auto it1 : sst_stats)
    {
      /*
        Only update statistics for indexes that belong to this SQL table.

        The reason is: We are walking through all SST files that have
        entries from this table (and so can compute good statistics). For
        other SQL tables, it can be that we're only seeing a small fraction
        of table's entries (and so we can't update statistics based on that).
      */
      if (ids_to_check.find(it1.gl_index_id) == ids_to_check.end())
        continue;

      RDBSE_KEYDEF* kd= ddl_manager.find(it1.gl_index_id);
      stats[it1.gl_index_id].merge(it1, true, kd->max_storage_fmt_length());
    }
    num_sst++;
  }

  // set and persist new stats
  ddl_manager.set_stats(stats);
  ddl_manager.persist_stats(true);

  DBUG_RETURN(0);
}

void ha_rocksdb::get_auto_increment(ulonglong offset, ulonglong increment,
                                    ulonglong nb_desired_values,
                                    ulonglong *first_value,
                                    ulonglong *nb_reserved_values)
{
  if (!nb_desired_values)
    nb_desired_values= 1;

  *first_value= (tbl_def->auto_incr_val += nb_desired_values) -
      nb_desired_values;
  *nb_reserved_values= nb_desired_values;
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
    return "(Item*)nullptr";
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
    Part of condition we couldn't check (always nullptr).
*/

class Item* ha_rocksdb::idx_cond_push(uint keyno, class Item* idx_cond)
{
  DBUG_ASSERT(keyno != MAX_KEY);
  DBUG_ASSERT(idx_cond != nullptr);

  pushed_idx_cond = idx_cond;
  pushed_idx_cond_keyno = keyno;
  in_range_check_pushed_down = TRUE;

  /* We will check the whole condition */
  return nullptr;
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
    var->type = SHOW_LONGLONG;                                           \
    var->value = (char *)&rocksdb_status_counters.name;                  \
    return 0;                                                            \
  }

#define DEF_STATUS_VAR(name) \
  {"rocksdb_" #name, (char*) &SHOW_FNAME(name), SHOW_FUNC}

#define DEF_STATUS_VAR_PTR(name, ptr, option) \
  {"rocksdb_" name, (char*) ptr, option}

#define DEF_STATUS_VAR_FUNC(name, ptr, option) \
  {name, reinterpret_cast<char*>(ptr), option}

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

static void myrocks_update_status() {
  export_stats.rows_deleted = global_stats.rows[ROWS_DELETED];
  export_stats.rows_inserted = global_stats.rows[ROWS_INSERTED];
  export_stats.rows_read = global_stats.rows[ROWS_READ];
  export_stats.rows_updated = global_stats.rows[ROWS_UPDATED];

  export_stats.system_rows_deleted = global_stats.system_rows[ROWS_DELETED];
  export_stats.system_rows_inserted = global_stats.system_rows[ROWS_INSERTED];
  export_stats.system_rows_read = global_stats.system_rows[ROWS_READ];
  export_stats.system_rows_updated = global_stats.system_rows[ROWS_UPDATED];
}

static SHOW_VAR myrocks_status_variables[]= {
  DEF_STATUS_VAR_FUNC("rows_deleted", &export_stats.rows_deleted,
                      SHOW_LONGLONG),
  DEF_STATUS_VAR_FUNC("rows_inserted", &export_stats.rows_inserted,
                      SHOW_LONGLONG),
  DEF_STATUS_VAR_FUNC("rows_read", &export_stats.rows_read, SHOW_LONGLONG),
  DEF_STATUS_VAR_FUNC("rows_updated", &export_stats.rows_updated,
                      SHOW_LONGLONG),
  DEF_STATUS_VAR_FUNC("system_rows_deleted", &export_stats.system_rows_deleted,
                      SHOW_LONGLONG),
  DEF_STATUS_VAR_FUNC("system_rows_inserted",
                      &export_stats.system_rows_inserted, SHOW_LONGLONG),
  DEF_STATUS_VAR_FUNC("system_rows_read", &export_stats.system_rows_read,
                      SHOW_LONGLONG),
  DEF_STATUS_VAR_FUNC("system_rows_updated", &export_stats.system_rows_updated,
                       SHOW_LONGLONG),

  {NullS, NullS, SHOW_LONG}
};

static void show_myrocks_vars(THD* thd, SHOW_VAR* var, char* buff) {
  myrocks_update_status();
  var->type = SHOW_ARRAY;
  var->value = reinterpret_cast<char*>(&myrocks_status_variables);
}

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
  DEF_STATUS_VAR_PTR("number_sst_entry_put", &rocksdb_num_sst_entry_put,
                     SHOW_LONGLONG),
  DEF_STATUS_VAR_PTR("number_sst_entry_delete", &rocksdb_num_sst_entry_delete,
                     SHOW_LONGLONG),
  DEF_STATUS_VAR_PTR("number_sst_entry_singledelete",
                     &rocksdb_num_sst_entry_singledelete, SHOW_LONGLONG),
  DEF_STATUS_VAR_PTR("number_sst_entry_merge", &rocksdb_num_sst_entry_merge,
                     SHOW_LONGLONG),
  DEF_STATUS_VAR_PTR("number_sst_entry_other", &rocksdb_num_sst_entry_other,
                     SHOW_LONGLONG),
  {"rocksdb", reinterpret_cast<char*>(&show_myrocks_vars), SHOW_FUNC},
  {NullS, NullS, SHOW_LONG}
};


/*
  Compute a hash number for a PK value in RowKeyFormat.

  @note
    RowKeyFormat is comparable with memcmp. This means, any hash function will
    work correctly. We use my_charset_bin's hash function.

    Note from Bar: could also use crc32 function.
*/

ulong Primary_key_comparator::get_hashnr(const char *key, const size_t key_len)
{
  ulong nr=1, nr2=4;
  my_charset_bin.coll->hash_sort(&my_charset_bin, (const uchar*)key, key_len,
                                 &nr, &nr2);
  return((ulong) nr);
}

void* background_thread(void*)
{
  mysql_mutex_lock(&background_mutex);

  timespec ts_next_sync;
  clock_gettime(CLOCK_REALTIME, &ts_next_sync);
  ts_next_sync.tv_sec++;

  for (;;)
  {
    // wait for 1 second or until we received a condition to stop the thread
    mysql_mutex_lock(&stop_cond_mutex);
    auto ret MY_ATTRIBUTE((__unused__)) = mysql_cond_timedwait(
      &stop_cond, &stop_cond_mutex, &ts_next_sync);
    // make sure that no program error is returned
    DBUG_ASSERT(ret == 0 || ret == ETIMEDOUT);
    auto local_bg_control = bg_control;
    bg_control = background_thread_control();
    mysql_mutex_unlock(&stop_cond_mutex);

    if (local_bg_control.stop)
    {
      break;
    }

    if (local_bg_control.save_stats)
    {
      ddl_manager.persist_stats();
    }

    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    if (ts.tv_sec-ts_next_sync.tv_sec >= 1)
    {
      if (rdb && rocksdb_background_sync)
      {
        DBUG_ASSERT(!db_options.allow_mmap_writes);
        rocksdb::Status s= rdb->SyncWAL();
        if (!s.ok())
          rocksdb_handle_io_error(s, ROCKSDB_IO_ERROR_BG_THREAD);
      }
      ts_next_sync.tv_sec = ts.tv_sec+1;
    }
  }

  // save remaining stats which might've left unsaved
  ddl_manager.persist_stats();

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
  if (!tablename) {
    rdb_perf_context_collect(global_perf_context, counters);
    return 0;
  }

  ROCKSDB_SHARE *share;
  share= get_share(tablename, nullptr);
  if (!share)
    return HA_ERR_INTERNAL_ERROR;

  rdb_perf_context_collect(share->table_perf_context, counters);
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
      sql_print_error("RocksDB: Failed to write to WAL - status %d, %s",
                      status.code(), status.ToString().c_str());
      sql_print_error("RocksDB: Aborting on WAL write error.");
      abort_with_stack_traces();
      break;
    }
    case ROCKSDB_IO_ERROR_BG_THREAD:
    {
      sql_print_warning("RocksDB: BG Thread failed to write to RocksDB "
                        "- status %d, %s", status.code(),
                        status.ToString().c_str());
      break;
    }
    default:
      DBUG_ASSERT(0);
      break;
    }
  }
  else if (status.IsCorruption())
  {
    /* NO_LINT_DEBUG */
    sql_print_error("RocksDB: Data Corruption detected! %d, %s",
                     status.code(), status.ToString().c_str());
    /* NO_LINT_DEBUG */
    sql_print_error("RocksDB: Aborting because of data corruption.");
    abort_with_stack_traces();
  }
  else if (!status.ok())
  {
    switch (type) {
    case ROCKSDB_IO_ERROR_DICT_COMMIT:
    {
      sql_print_error("RocksDB: Failed to write to WAL (dictionary) - "
                      "status %d, %s",
                      status.code(), status.ToString().c_str());
      sql_print_error("RocksDB: Aborting on WAL write error.");
      abort_with_stack_traces();
      break;
    }
    default:
      sql_print_warning("RocksDB: Failed to write to RocksDB "
                        "- status %d, %s", status.code(),
                        status.ToString().c_str());
      break;
    }
  }
}

Dict_manager *get_dict_manager(void)
{
  return &dict_manager;
}

Table_ddl_manager *get_ddl_manager(void)
{
  return &ddl_manager;
}

Binlog_info_manager *get_binlog_manager(void)
{
  return &binlog_manager;
}

void
set_compaction_options(THD* thd,
                       struct st_mysql_sys_var* var,
                       void* var_ptr,
                       const void* save)
{
  if (var_ptr && save) {
    *(uint64_t*)var_ptr = *(const uint64_t*) save;
  }
  CompactionParams params = {
    (uint64_t)rocksdb_compaction_sequential_deletes,
    (uint64_t)rocksdb_compaction_sequential_deletes_window,
    (uint64_t)rocksdb_compaction_sequential_deletes_file_size
  };
  if (properties_collector_factory) {
    properties_collector_factory->SetCompactionParams(params);
  }
}

void
myrocks_set_table_stats_sampling_pct(THD* thd,
                                     struct st_mysql_sys_var* var,
                                     void* var_ptr,
                                     const void* save)
{
  mysql_mutex_lock(&sysvar_mutex);

  uint32_t new_val = *static_cast<const uint32_t*>(save);

  if (new_val != rocksdb_table_stats_sampling_pct) {
    rocksdb_table_stats_sampling_pct = new_val;

    if (properties_collector_factory) {
      properties_collector_factory->SetTableStatsSamplingPct(
        rocksdb_table_stats_sampling_pct);
    }
  }

  mysql_mutex_unlock(&sysvar_mutex);
}

/*
  This function allows setting the rate limiter's bytes per second value
  but only if the rate limiter is turned on which has to be done at startup.
  If the rate is already 0 (turned off) or we are changing it to 0 (trying
  to turn it off) this function will push a warning to the client and do
  nothing.
  This is similar to the code in innodb_doublewrite_update (found in
  storage/innobase/handler/ha_innodb.cc).
*/
void
set_rate_limiter_bytes_per_sec(THD*                     thd,
                               struct st_mysql_sys_var* var,
                               void*                    var_ptr,
                               const void*              save)
{
  uint64_t new_val = *static_cast<const uint64_t*>(save);
  if (new_val == 0 || rocksdb_rate_limiter_bytes_per_sec == 0)
  {
    /*
      If a rate_limiter was not enabled at startup we can't change it nor
      can we disable it if one was created at startup
    */
    push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                        ER_WRONG_ARGUMENTS,
                        "RocksDB: rocksdb_rate_limiter_bytes_per_sec cannot "
                        "be dynamically changed to or from 0.  Do a clean "
                        "shutdown if you want to change it from or to 0.");
  }
  else if (new_val != rocksdb_rate_limiter_bytes_per_sec)
  {
    /* Apply the new value to the rate limiter and store it locally */
    DBUG_ASSERT(rate_limiter != nullptr);
    rocksdb_rate_limiter_bytes_per_sec = new_val;
    rate_limiter->SetBytesPerSecond(new_val);
  }
}

// Split a string based on a delimiter.  Two delimiters in a row will not
// add an empty string in the list.
static std::vector<std::string> split(const std::string& input,
                                      char               delimiter)
{
  size_t                   pos;
  size_t                   start = 0;
  std::vector<std::string> elems;

  // Find next delimiter
  while ((pos = input.find(delimiter, start)) != std::string::npos)
  {
    // If there is any data since the last delimiter add it to the list
    if (pos > start)
      elems.push_back(input.substr(start, pos - start));

    // Set our start position to the character after the delimiter
    start = pos + 1;
  }

  // Add a possible string since the last delimiter
  if (input.length() > start)
    elems.push_back(input.substr(start));

  // Return the resulting list back to the caller
  return elems;
}

void
rocksdb_set_collation_exception_list(THD*                     thd,
                                     struct st_mysql_sys_var* var,
                                     void*                    var_ptr,
                                     const void*              save)
{
  const char* val = *static_cast<const char*const*>(save);

  // Make sure we are not updating the list when it is being used elsewhere
  mysql_mutex_lock(&collation_exception_list_mutex);

  // Split the input string into a list of table names
  collation_exception_list = split(val, ',');

  // Release the mutex
  mysql_mutex_unlock(&collation_exception_list_mutex);

  *static_cast<const char**>(var_ptr) = val;
}

void request_save_stats()
{
  mysql_mutex_lock(&stop_cond_mutex);
  bg_control.save_stats= true;
  mysql_mutex_unlock(&stop_cond_mutex);
}

}  // namespace myrocks

/*
  Register the storage engine plugin outside of myrocks namespace
  so that mysql_declare_plugin does not get confused when it does
  its name generation.
*/

struct st_mysql_storage_engine rocksdb_storage_engine=
{ MYSQL_HANDLERTON_INTERFACE_VERSION };

mysql_declare_plugin(rocksdb_se)
{
  MYSQL_STORAGE_ENGINE_PLUGIN,                  /* Plugin Type */
  &rocksdb_storage_engine,                      /* Plugin Descriptor */
  "ROCKSDB",                                    /* Plugin Name */
  "Monty Program Ab",                           /* Plugin Author */
  "RocksDB storage engine",                     /* Plugin Description */
  PLUGIN_LICENSE_GPL,                           /* Plugin Licence */
  myrocks::rocksdb_init_func,                   /* Plugin Entry Point */
  myrocks::rocksdb_done_func,                   /* Plugin Deinitializer */
  0x0001,                                       /* version number (0.1) */
  myrocks::rocksdb_status_vars,                 /* status variables */
  myrocks::rocksdb_system_variables,            /* system variables */
  nullptr,                                      /* config options */
  0,                                            /* flags */
},
myrocks::i_s_rocksdb_cfstats,
myrocks::i_s_rocksdb_dbstats,
myrocks::i_s_rocksdb_perf_context,
myrocks::i_s_rocksdb_perf_context_global,
myrocks::i_s_rocksdb_cfoptions,
myrocks::i_s_rocksdb_global_info,
myrocks::i_s_rocksdb_ddl,
myrocks::i_s_rocksdb_index_file_map
mysql_declare_plugin_end;
