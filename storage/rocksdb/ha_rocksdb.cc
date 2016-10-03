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
#include "./rdb_threads.h"

#ifdef TARGET_OS_LINUX
extern my_bool cachedev_enabled;
#endif /* TARGET_OS_LINUX */

// Internal MySQL APIs not exposed in any header.
extern "C"
{
/**
  Mark transaction to rollback and mark error as fatal to a sub-statement.
  @param  thd   Thread handle
  @param  all   TRUE <=> rollback main transaction.
*/
void thd_mark_transaction_to_rollback(MYSQL_THD thd, bool all);

/**
 *   Get the user thread's binary logging format
 *   @param thd  user thread
 *   @return Value to be used as index into the binlog_format_names array
*/
int thd_binlog_format(const MYSQL_THD thd);

/**
 *   Check if binary logging is filtered for thread's current db.
 *   @param  thd   Thread handle
 *   @retval 1 the query is not filtered, 0 otherwise.
*/
bool thd_binlog_filter_ok(const MYSQL_THD thd);
}

namespace myrocks {

static st_global_stats global_stats;
static st_export_stats export_stats;

/**
  Updates row counters based on the table type and operation type.
*/
void ha_rocksdb::update_row_stats(operation_type type) {
  DBUG_ASSERT(type < ROWS_MAX);
  // Find if we are modifying system databases.
  if (table->s && m_tbl_def->m_is_mysql_system_table)
    global_stats.system_rows[type].inc();
  else
    global_stats.rows[type].inc();
}

void dbug_dump_database(rocksdb::DB *db);
static handler *rocksdb_create_handler(my_core::handlerton *hton,
                                       my_core::TABLE_SHARE *table_arg,
                                       my_core::MEM_ROOT *mem_root);

bool can_use_bloom_filter(THD *thd,
                          const std::shared_ptr<const Rdb_key_def>& kd,
                          const rocksdb::Slice &eq_cond,
                          const bool use_all_keys,
                          bool is_ascending);

///////////////////////////////////////////////////////////
// Parameters and settings
///////////////////////////////////////////////////////////
static char * rocksdb_default_cf_options;
static char * rocksdb_override_cf_options;
Rdb_cf_options rocksdb_cf_options_map;

///////////////////////////////////////////////////////////
// Globals
///////////////////////////////////////////////////////////
handlerton *rocksdb_hton;

rocksdb::TransactionDB *rdb= nullptr;

static std::shared_ptr<rocksdb::Statistics> rocksdb_stats;
static std::unique_ptr<rocksdb::Env> flashcache_aware_env;
static std::shared_ptr<Rdb_tbl_prop_coll_factory>
  properties_collector_factory;

Rdb_dict_manager dict_manager;
Rdb_cf_manager cf_manager;
Rdb_ddl_manager ddl_manager;
Rdb_binlog_manager binlog_manager;


/**
  MyRocks background thread control
  N.B. This is besides RocksDB's own background threads
       (@see rocksdb::CancelAllBackgroundWork())
*/

static Rdb_background_thread rdb_bg_thread;


// List of table names (using regex) that are exceptions to the strict
// collation check requirement.
Regex_list_handler *rdb_collation_exceptions;

static const char* const ERRSTR_ROLLBACK_ONLY
  = "This transaction was rolled back and cannot be "
    "committed. Only supported operation is to roll it back, "
    "so all pending changes will be discarded. "
    "Please restart another transaction.";


static void
rocksdb_flush_all_memtables()
{
  Rdb_cf_manager& cf_manager= rdb_get_cf_manager();
  for (auto cf_handle : cf_manager.get_all_cf()) {
    rdb->Flush(rocksdb::FlushOptions(), cf_handle);
  }
}

static void
rocksdb_compact_column_family_stub(THD* thd,
                                   struct st_mysql_sys_var* var,
                                   void* var_ptr,
                                   const void* save)
{
}

static int
rocksdb_compact_column_family(THD* thd,
                              struct st_mysql_sys_var* var,
                              void* var_ptr,
                              struct st_mysql_value* value)
{
  char buff[STRING_BUFFER_USUAL_SIZE];
  int  len = sizeof(buff);

  if (const char* cf = value->val_str(value, buff, &len)) {
    bool is_automatic;
    auto cfh = cf_manager.get_cf(cf, "", nullptr, &is_automatic);
    if (cfh != nullptr && rdb != nullptr) {
      sql_print_information("RocksDB: Manual compaction of column family: %s\n", cf);
      rdb->CompactRange(rocksdb::CompactRangeOptions(), cfh, nullptr, nullptr);
    }
  }
  return 0;
}

///////////////////////////////////////////////////////////
// Hash map: table name => open table handler
///////////////////////////////////////////////////////////

namespace  // anonymous namespace = not visible outside this source file
{

struct Rdb_open_tables_map
{
  /* Hash table used to track the handlers of open tables */
  my_core::HASH         m_hash;
  /* The mutex used to protect the hash table */
  mutable mysql_mutex_t m_mutex;

  void init_hash(void)
  {
    (void) my_hash_init(&m_hash, my_core::system_charset_info, 32, 0, 0,
                        (my_hash_get_key) Rdb_open_tables_map::get_hash_key,
                        0, 0);
  }

  void free_hash(void)
  {
    my_hash_free(&m_hash);
  }

  static uchar* get_hash_key(Rdb_table_handler *table_handler,
                             size_t *length,
                             my_bool not_used __attribute__((__unused__)));

  Rdb_table_handler* get_table_handler(const char *table_name);
  void release_table_handler(Rdb_table_handler *table_handler);

  std::vector<std::string> get_table_names(void) const;
};

}  // anonymous namespace

static Rdb_open_tables_map rdb_open_tables;


static std::string rdb_normalize_dir(std::string dir)
{
  while (dir.size() > 0 && dir.back() == '/')
  {
    dir.resize(dir.size() - 1);
  }
  return dir;
}


static int rocksdb_create_checkpoint(
    THD* thd __attribute__((__unused__)),
    struct st_mysql_sys_var* var __attribute__((__unused__)),
    void* save __attribute__((__unused__)),
    struct st_mysql_value* value)
{
  char buf[512];
  int len = sizeof(buf);
  const char* checkpoint_dir_raw= value->val_str(value, buf, &len);
  if (checkpoint_dir_raw) {
    if (rdb != nullptr) {
      std::string checkpoint_dir= rdb_normalize_dir(checkpoint_dir_raw);
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
rocksdb_force_flush_memtable_now_stub(THD* thd,
                                      struct st_mysql_sys_var* var,
                                      void* var_ptr,
                                      const void* save)
{
}

static int
rocksdb_force_flush_memtable_now(THD* thd,
                                 struct st_mysql_sys_var* var,
                                 void* var_ptr,
                                 struct st_mysql_value* value)
{
  sql_print_information("RocksDB: Manual memtable flush\n");
  rocksdb_flush_all_memtables();
  return 0;
}

static void rocksdb_drop_index_wakeup_thread(
    my_core::THD* thd __attribute__((__unused__)),
    struct st_mysql_sys_var* var __attribute__((__unused__)),
    void* var_ptr __attribute__((__unused__)),
    const void* save);

static my_bool rocksdb_pause_background_work= 0;
static mysql_mutex_t rdb_sysvars_mutex;

static void rocksdb_set_pause_background_work(
    my_core::THD* thd __attribute__((__unused__)),
    struct st_mysql_sys_var* var __attribute__((__unused__)),
    void* var_ptr __attribute__((__unused__)),
    const void* save)
{
  mysql_mutex_lock(&rdb_sysvars_mutex);
  bool pause_requested= *static_cast<const bool*>(save);
  if (rocksdb_pause_background_work != pause_requested) {
    if (pause_requested) {
      rdb->PauseBackgroundWork();
    } else {
      rdb->ContinueBackgroundWork();
    }
    rocksdb_pause_background_work= pause_requested;
  }
  mysql_mutex_unlock(&rdb_sysvars_mutex);
}

static void
rocksdb_set_compaction_options(THD* thd,
                               struct st_mysql_sys_var* var,
                               void* var_ptr,
                               const void* save);

static void
rocksdb_set_table_stats_sampling_pct(THD* thd,
                                     struct st_mysql_sys_var* var,
                                     void* var_ptr,
                                     const void* save);

static void
rocksdb_set_rate_limiter_bytes_per_sec(THD*                     thd,
                                       struct st_mysql_sys_var* var,
                                       void*                    var_ptr,
                                       const void*              save);

static void rdb_set_collation_exception_list(const char *exception_list);
static void
rocksdb_set_collation_exception_list(THD*                     thd,
                                     struct st_mysql_sys_var* var,
                                     void*                    var_ptr,
                                     const void*              save);

static void
rocksdb_set_bulk_load(THD*                     thd,
                      struct st_mysql_sys_var* var __attribute__((__unused__)),
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
static uint32_t rocksdb_wal_recovery_mode;
static uint32_t rocksdb_access_hint_on_compaction_start;
static char * rocksdb_compact_cf_name;
static char * rocksdb_checkpoint_name;
static my_bool rocksdb_signal_drop_index_thread;
static my_bool rocksdb_strict_collation_check= 1;
static my_bool rocksdb_disable_2pc= 0;
static char * rocksdb_strict_collation_exceptions;
static my_bool rocksdb_collect_sst_properties= 1;
static my_bool rocksdb_force_flush_memtable_now_var= 0;
static uint64_t rocksdb_number_stat_computes= 0;
static uint32_t rocksdb_seconds_between_stat_computes= 3600;
static long long rocksdb_compaction_sequential_deletes= 0l;
static long long rocksdb_compaction_sequential_deletes_window= 0l;
static long long rocksdb_compaction_sequential_deletes_file_size= 0l;
static uint32_t rocksdb_validate_tables = 1;
static char * rocksdb_datadir;
static uint32_t rocksdb_table_stats_sampling_pct;
static my_bool rocksdb_enable_bulk_load_api= 1;

std::atomic<uint64_t> rocksdb_snapshot_conflict_errors(0);

static rocksdb::DBOptions rdb_init_rocksdb_db_options(void)
{
  rocksdb::DBOptions o;

  o.create_if_missing= true;
  o.listeners.push_back(std::make_shared<Rdb_event_listener>(&ddl_manager));
  o.info_log_level= rocksdb::InfoLogLevel::INFO_LEVEL;
  o.max_subcompactions= DEFAULT_SUBCOMPACTIONS;

  return o;
}

static rocksdb::DBOptions rocksdb_db_options= rdb_init_rocksdb_db_options();
static rocksdb::BlockBasedTableOptions rocksdb_tbl_options;

static std::shared_ptr<rocksdb::RateLimiter> rocksdb_rate_limiter;

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
  mysql_mutex_lock(&rdb_sysvars_mutex);
  rocksdb_info_log_level = *static_cast<const uint64_t*>(save);
  rocksdb_db_options.info_log->SetInfoLogLevel(
      static_cast<const rocksdb::InfoLogLevel>(rocksdb_info_log_level));
  mysql_mutex_unlock(&rdb_sysvars_mutex);
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
  "rocksdb_skip_unique_check and rocksdb_commit_in_the_middle.",
  nullptr, rocksdb_set_bulk_load, FALSE);

static MYSQL_SYSVAR_BOOL(enable_bulk_load_api,
  rocksdb_enable_bulk_load_api,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "Enables using SstFileWriter for bulk loading",
  nullptr, nullptr, rocksdb_enable_bulk_load_api);

static MYSQL_THDVAR_STR(skip_unique_check,
  PLUGIN_VAR_RQCMDARG|PLUGIN_VAR_MEMALLOC,
  "Skip unique constraint checking for the specified tables", nullptr, nullptr,
  ".*");

static MYSQL_THDVAR_BOOL(debug_skip_unique_check, PLUGIN_VAR_RQCMDARG,
  "Skip unique constraint checking for all tables", nullptr, nullptr, FALSE);

static MYSQL_THDVAR_BOOL(commit_in_the_middle, PLUGIN_VAR_RQCMDARG,
  "Commit rows implicitly every rocksdb_bulk_load_size, on bulk load/insert, "
  "update and delete",
  nullptr, nullptr, FALSE);

static MYSQL_THDVAR_STR(read_free_rpl_tables,
  PLUGIN_VAR_RQCMDARG|PLUGIN_VAR_MEMALLOC,
  "List of tables that will use read-free replication on the slave "
  "(i.e. not lookup a row during replication)", nullptr, nullptr, "");

static MYSQL_THDVAR_BOOL(skip_bloom_filter_on_read, PLUGIN_VAR_RQCMDARG,
  "Skip using bloom filter for reads", nullptr, nullptr, FALSE);

static MYSQL_THDVAR_ULONG(max_row_locks, PLUGIN_VAR_RQCMDARG,
  "Maximum number of locks a transaction can have",
  nullptr, nullptr, /*default*/ 1024*1024*1024, /*min*/ 1,
  /*max*/ 1024*1024*1024, 0);

static MYSQL_THDVAR_BOOL(lock_scanned_rows, PLUGIN_VAR_RQCMDARG,
  "Take and hold locks on rows that are scanned but not updated",
  nullptr, nullptr, FALSE);

static MYSQL_THDVAR_ULONG(bulk_load_size, PLUGIN_VAR_RQCMDARG,
  "Max #records in a batch for bulk-load mode",
  nullptr, nullptr, /*default*/ 1000, /*min*/ 1, /*max*/ 1024*1024*1024, 0);

static MYSQL_SYSVAR_BOOL(create_if_missing,
  *reinterpret_cast<my_bool*>(&rocksdb_db_options.create_if_missing),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::create_if_missing for RocksDB",
  nullptr, nullptr, rocksdb_db_options.create_if_missing);

static MYSQL_SYSVAR_BOOL(create_missing_column_families,
  *reinterpret_cast<my_bool*>(
      &rocksdb_db_options.create_missing_column_families),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::create_missing_column_families for RocksDB",
  nullptr, nullptr, rocksdb_db_options.create_missing_column_families);

static MYSQL_SYSVAR_BOOL(error_if_exists,
  *reinterpret_cast<my_bool*>(&rocksdb_db_options.error_if_exists),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::error_if_exists for RocksDB",
  nullptr, nullptr, rocksdb_db_options.error_if_exists);

static MYSQL_SYSVAR_BOOL(paranoid_checks,
  *reinterpret_cast<my_bool*>(&rocksdb_db_options.paranoid_checks),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::paranoid_checks for RocksDB",
  nullptr, nullptr, rocksdb_db_options.paranoid_checks);

static MYSQL_SYSVAR_ULONGLONG(rate_limiter_bytes_per_sec,
  rocksdb_rate_limiter_bytes_per_sec,
  PLUGIN_VAR_RQCMDARG,
  "DBOptions::rate_limiter bytes_per_sec for RocksDB",
  nullptr, rocksdb_set_rate_limiter_bytes_per_sec, /* default */ 0L,
  /* min */ 0L, /* max */ MAX_RATE_LIMITER_BYTES_PER_SEC, 0);

static MYSQL_SYSVAR_ENUM(info_log_level,
  rocksdb_info_log_level,
  PLUGIN_VAR_RQCMDARG,
  "Filter level for info logs to be written mysqld error log. "
  "Valid values include 'debug_level', 'info_level', 'warn_level'"
  "'error_level' and 'fatal_level'.",
  nullptr, rocksdb_set_rocksdb_info_log_level,
  rocksdb::InfoLogLevel::ERROR_LEVEL, &info_log_level_typelib);

static MYSQL_THDVAR_INT(perf_context_level,
  PLUGIN_VAR_RQCMDARG,
  "Perf Context Level for rocksdb internal timer stat collection",
  nullptr, nullptr,
  /* default */ rocksdb::PerfLevel::kUninitialized,
  /* min */ rocksdb::PerfLevel::kUninitialized,
  /* max */ rocksdb::PerfLevel::kOutOfBounds - 1, 0);

static MYSQL_SYSVAR_UINT(wal_recovery_mode,
  rocksdb_wal_recovery_mode,
  PLUGIN_VAR_RQCMDARG,
  "DBOptions::wal_recovery_mode for RocksDB",
  nullptr, nullptr, 2,
  /* min */ 0L, /* max */ 3, 0);

static MYSQL_SYSVAR_ULONG(compaction_readahead_size,
  rocksdb_db_options.compaction_readahead_size,
  PLUGIN_VAR_RQCMDARG,
  "DBOptions::compaction_readahead_size for RocksDB",
  nullptr, nullptr, rocksdb_db_options.compaction_readahead_size,
  /* min */ 0L, /* max */ ULONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(new_table_reader_for_compaction_inputs,
  *reinterpret_cast<my_bool*>
    (&rocksdb_db_options.new_table_reader_for_compaction_inputs),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::new_table_reader_for_compaction_inputs for RocksDB",
  nullptr, nullptr, rocksdb_db_options.new_table_reader_for_compaction_inputs);

static MYSQL_SYSVAR_UINT(access_hint_on_compaction_start,
  rocksdb_access_hint_on_compaction_start,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::access_hint_on_compaction_start for RocksDB",
  nullptr, nullptr, 1,
  /* min */ 0L, /* max */ 3, 0);

static MYSQL_SYSVAR_BOOL(allow_concurrent_memtable_write,
  *reinterpret_cast<my_bool*>(
      &rocksdb_db_options.allow_concurrent_memtable_write),
  PLUGIN_VAR_RQCMDARG,
  "DBOptions::allow_concurrent_memtable_write for RocksDB",
  nullptr, nullptr, rocksdb_db_options.allow_concurrent_memtable_write);

static MYSQL_SYSVAR_BOOL(enable_write_thread_adaptive_yield,
  *reinterpret_cast<my_bool*>(
      &rocksdb_db_options.enable_write_thread_adaptive_yield),
  PLUGIN_VAR_RQCMDARG,
  "DBOptions::enable_write_thread_adaptive_yield for RocksDB",
  nullptr, nullptr, rocksdb_db_options.enable_write_thread_adaptive_yield);

static MYSQL_SYSVAR_INT(max_open_files,
  rocksdb_db_options.max_open_files,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_open_files for RocksDB",
  nullptr, nullptr, rocksdb_db_options.max_open_files,
  /* min */ -1, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_ULONG(max_total_wal_size,
  rocksdb_db_options.max_total_wal_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_total_wal_size for RocksDB",
  nullptr, nullptr, rocksdb_db_options.max_total_wal_size,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(disabledatasync,
  *reinterpret_cast<my_bool*>(&rocksdb_db_options.disableDataSync),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::disableDataSync for RocksDB",
  nullptr, nullptr, rocksdb_db_options.disableDataSync);

static MYSQL_SYSVAR_BOOL(use_fsync,
  *reinterpret_cast<my_bool*>(&rocksdb_db_options.use_fsync),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::use_fsync for RocksDB",
  nullptr, nullptr, rocksdb_db_options.use_fsync);

static MYSQL_SYSVAR_STR(wal_dir, rocksdb_wal_dir,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::wal_dir for RocksDB",
  nullptr, nullptr, rocksdb_db_options.wal_dir.c_str());

static MYSQL_SYSVAR_ULONG(delete_obsolete_files_period_micros,
  rocksdb_db_options.delete_obsolete_files_period_micros,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::delete_obsolete_files_period_micros for RocksDB",
  nullptr, nullptr, rocksdb_db_options.delete_obsolete_files_period_micros,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_INT(base_background_compactions,
  rocksdb_db_options.base_background_compactions,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::base_background_compactions for RocksDB",
  nullptr, nullptr, rocksdb_db_options.base_background_compactions,
  /* min */ -1, /* max */ MAX_BACKGROUND_COMPACTIONS, 0);

static MYSQL_SYSVAR_INT(max_background_compactions,
  rocksdb_db_options.max_background_compactions,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_background_compactions for RocksDB",
  nullptr, nullptr, rocksdb_db_options.max_background_compactions,
  /* min */ 1, /* max */ MAX_BACKGROUND_COMPACTIONS, 0);

static MYSQL_SYSVAR_INT(max_background_flushes,
  rocksdb_db_options.max_background_flushes,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_background_flushes for RocksDB",
  nullptr, nullptr, rocksdb_db_options.max_background_flushes,
  /* min */ 1, /* max */ MAX_BACKGROUND_FLUSHES, 0);

static MYSQL_SYSVAR_UINT(max_subcompactions,
  rocksdb_db_options.max_subcompactions,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_subcompactions for RocksDB",
  nullptr, nullptr, rocksdb_db_options.max_subcompactions,
  /* min */ 1, /* max */ MAX_SUBCOMPACTIONS, 0);

static MYSQL_SYSVAR_ULONG(max_log_file_size,
  rocksdb_db_options.max_log_file_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_log_file_size for RocksDB",
  nullptr, nullptr, rocksdb_db_options.max_log_file_size,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(log_file_time_to_roll,
  rocksdb_db_options.log_file_time_to_roll,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::log_file_time_to_roll for RocksDB",
  nullptr, nullptr, rocksdb_db_options.log_file_time_to_roll,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(keep_log_file_num,
  rocksdb_db_options.keep_log_file_num,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::keep_log_file_num for RocksDB",
  nullptr, nullptr, rocksdb_db_options.keep_log_file_num,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(max_manifest_file_size,
  rocksdb_db_options.max_manifest_file_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_manifest_file_size for RocksDB",
  nullptr, nullptr, rocksdb_db_options.max_manifest_file_size,
  /* min */ 0L, /* max */ ULONG_MAX, 0);

static MYSQL_SYSVAR_INT(table_cache_numshardbits,
  rocksdb_db_options.table_cache_numshardbits,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::table_cache_numshardbits for RocksDB",
  nullptr, nullptr, rocksdb_db_options.table_cache_numshardbits,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_ULONG(wal_ttl_seconds,
  rocksdb_db_options.WAL_ttl_seconds,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::WAL_ttl_seconds for RocksDB",
  nullptr, nullptr, rocksdb_db_options.WAL_ttl_seconds,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(wal_size_limit_mb,
  rocksdb_db_options.WAL_size_limit_MB,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::WAL_size_limit_MB for RocksDB",
  nullptr, nullptr, rocksdb_db_options.WAL_size_limit_MB,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(manifest_preallocation_size,
  rocksdb_db_options.manifest_preallocation_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::manifest_preallocation_size for RocksDB",
  nullptr, nullptr, rocksdb_db_options.manifest_preallocation_size,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(allow_os_buffer,
  *reinterpret_cast<my_bool*>(&rocksdb_db_options.allow_os_buffer),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::allow_os_buffer for RocksDB",
  nullptr, nullptr, rocksdb_db_options.allow_os_buffer);

static MYSQL_SYSVAR_BOOL(allow_mmap_reads,
  *reinterpret_cast<my_bool*>(&rocksdb_db_options.allow_mmap_reads),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::allow_mmap_reads for RocksDB",
  nullptr, nullptr, rocksdb_db_options.allow_mmap_reads);

static MYSQL_SYSVAR_BOOL(allow_mmap_writes,
  *reinterpret_cast<my_bool*>(&rocksdb_db_options.allow_mmap_writes),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::allow_mmap_writes for RocksDB",
  nullptr, nullptr, rocksdb_db_options.allow_mmap_writes);

static MYSQL_SYSVAR_BOOL(is_fd_close_on_exec,
  *reinterpret_cast<my_bool*>(&rocksdb_db_options.is_fd_close_on_exec),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::is_fd_close_on_exec for RocksDB",
  nullptr, nullptr, rocksdb_db_options.is_fd_close_on_exec);

static MYSQL_SYSVAR_UINT(stats_dump_period_sec,
  rocksdb_db_options.stats_dump_period_sec,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::stats_dump_period_sec for RocksDB",
  nullptr, nullptr, rocksdb_db_options.stats_dump_period_sec,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_BOOL(advise_random_on_open,
  *reinterpret_cast<my_bool*>(&rocksdb_db_options.advise_random_on_open),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::advise_random_on_open for RocksDB",
  nullptr, nullptr, rocksdb_db_options.advise_random_on_open);

static MYSQL_SYSVAR_ULONG(db_write_buffer_size,
  rocksdb_db_options.db_write_buffer_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::db_write_buffer_size for RocksDB",
  nullptr, nullptr, rocksdb_db_options.db_write_buffer_size,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(use_adaptive_mutex,
  *reinterpret_cast<my_bool*>(&rocksdb_db_options.use_adaptive_mutex),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::use_adaptive_mutex for RocksDB",
  nullptr, nullptr, rocksdb_db_options.use_adaptive_mutex);

static MYSQL_SYSVAR_ULONG(bytes_per_sync,
  rocksdb_db_options.bytes_per_sync,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::bytes_per_sync for RocksDB",
  nullptr, nullptr, rocksdb_db_options.bytes_per_sync,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(wal_bytes_per_sync,
  rocksdb_db_options.wal_bytes_per_sync,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::wal_bytes_per_sync for RocksDB",
  nullptr, nullptr, rocksdb_db_options.wal_bytes_per_sync,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(enable_thread_tracking,
  *reinterpret_cast<my_bool*>(&rocksdb_db_options.enable_thread_tracking),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::enable_thread_tracking for RocksDB",
  nullptr, nullptr, rocksdb_db_options.enable_thread_tracking);

static MYSQL_SYSVAR_LONGLONG(block_cache_size, rocksdb_block_cache_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "block_cache size for RocksDB",
  nullptr, nullptr, /* RocksDB's default is 8 MB: */ 8*1024*1024L,
  /* min */ 1024L, /* max */ LONGLONG_MAX, /* Block size */1024L);

static MYSQL_SYSVAR_BOOL(cache_index_and_filter_blocks,
  *reinterpret_cast<my_bool*>(
      &rocksdb_tbl_options.cache_index_and_filter_blocks),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::cache_index_and_filter_blocks for RocksDB",
  nullptr, nullptr, true);

// When pin_l0_filter_and_index_blocks_in_cache is true, RocksDB will  use the
// LRU cache, but will always keep the filter & idndex block's handle checked
// out (=won't call ShardedLRUCache::Release), plus the parsed out objects
// the LRU cache will never push flush them out, hence they're pinned.
//
// This fixes the mutex contention between :ShardedLRUCache::Lookup and
// ShardedLRUCache::Release which reduced the QPS ratio (QPS using secondary
// index / QPS using PK).
static MYSQL_SYSVAR_BOOL(pin_l0_filter_and_index_blocks_in_cache,
  *reinterpret_cast<my_bool*>(
    &rocksdb_tbl_options.pin_l0_filter_and_index_blocks_in_cache),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "pin_l0_filter_and_index_blocks_in_cache for RocksDB",
  nullptr, nullptr, true);

static MYSQL_SYSVAR_ENUM(index_type,
  rocksdb_index_type,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::index_type for RocksDB",
  nullptr, nullptr,
  (uint64_t)rocksdb_tbl_options.index_type, &index_type_typelib);

static MYSQL_SYSVAR_BOOL(hash_index_allow_collision,
  *reinterpret_cast<my_bool*>(&rocksdb_tbl_options.hash_index_allow_collision),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::hash_index_allow_collision for RocksDB",
  nullptr, nullptr, rocksdb_tbl_options.hash_index_allow_collision);

static MYSQL_SYSVAR_BOOL(no_block_cache,
  *reinterpret_cast<my_bool*>(&rocksdb_tbl_options.no_block_cache),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::no_block_cache for RocksDB",
  nullptr, nullptr, rocksdb_tbl_options.no_block_cache);

static MYSQL_SYSVAR_ULONG(block_size,
  rocksdb_tbl_options.block_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::block_size for RocksDB",
  nullptr, nullptr, rocksdb_tbl_options.block_size,
  /* min */ 1L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_INT(block_size_deviation,
  rocksdb_tbl_options.block_size_deviation,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::block_size_deviation for RocksDB",
  nullptr, nullptr, rocksdb_tbl_options.block_size_deviation,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_INT(block_restart_interval,
  rocksdb_tbl_options.block_restart_interval,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::block_restart_interval for RocksDB",
  nullptr, nullptr, rocksdb_tbl_options.block_restart_interval,
  /* min */ 1, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_BOOL(whole_key_filtering,
  *reinterpret_cast<my_bool*>(&rocksdb_tbl_options.whole_key_filtering),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::whole_key_filtering for RocksDB",
  nullptr, nullptr, rocksdb_tbl_options.whole_key_filtering);

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

static MYSQL_SYSVAR_STR(compact_cf, rocksdb_compact_cf_name,
  PLUGIN_VAR_RQCMDARG,
  "Compact column family",
  rocksdb_compact_column_family, rocksdb_compact_column_family_stub, "");

static MYSQL_SYSVAR_STR(create_checkpoint, rocksdb_checkpoint_name,
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

static MYSQL_SYSVAR_BOOL(disable_2pc,
  rocksdb_disable_2pc,
  PLUGIN_VAR_RQCMDARG,
  "Disable two phase commit for MyRocks",
  nullptr, nullptr, TRUE);

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
  rocksdb_force_flush_memtable_now,
  rocksdb_force_flush_memtable_now_stub, FALSE);

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
  nullptr, rocksdb_set_compaction_options,
  DEFAULT_COMPACTION_SEQUENTIAL_DELETES,
  /* min */ 0L, /* max */ MAX_COMPACTION_SEQUENTIAL_DELETES, 0);

static MYSQL_SYSVAR_LONGLONG(
  compaction_sequential_deletes_window,
  rocksdb_compaction_sequential_deletes_window,
  PLUGIN_VAR_RQCMDARG,
  "Size of the window for counting rocksdb_compaction_sequential_deletes",
  nullptr, rocksdb_set_compaction_options,
  DEFAULT_COMPACTION_SEQUENTIAL_DELETES_WINDOW,
  /* min */ 0L, /* max */ MAX_COMPACTION_SEQUENTIAL_DELETES_WINDOW, 0);

static MYSQL_SYSVAR_LONGLONG(
  compaction_sequential_deletes_file_size,
  rocksdb_compaction_sequential_deletes_file_size,
  PLUGIN_VAR_RQCMDARG,
  "Minimum file size required for compaction_sequential_deletes",
  nullptr, rocksdb_set_compaction_options, 0L,
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
  STRINGIFY_ARG(RDB_TBL_STATS_SAMPLE_PCT_MIN) ".."
  STRINGIFY_ARG(RDB_TBL_STATS_SAMPLE_PCT_MAX) "]. " "By default "
  STRINGIFY_ARG(RDB_DEFAULT_TBL_STATS_SAMPLE_PCT) "% of entries are "
  "sampled.",
  nullptr, rocksdb_set_table_stats_sampling_pct, /* default */
  RDB_DEFAULT_TBL_STATS_SAMPLE_PCT, /* everything */ 0,
  /* max */ RDB_TBL_STATS_SAMPLE_PCT_MAX, 0);

static const longlong ROCKSDB_WRITE_BUFFER_SIZE_DEFAULT= 4194304;
static const int ROCKSDB_ASSUMED_KEY_VALUE_DISK_SIZE= 100;

static struct st_mysql_sys_var* rocksdb_system_variables[]= {
  MYSQL_SYSVAR(lock_wait_timeout),
  MYSQL_SYSVAR(max_row_locks),
  MYSQL_SYSVAR(lock_scanned_rows),
  MYSQL_SYSVAR(bulk_load),
  MYSQL_SYSVAR(skip_unique_check),
  MYSQL_SYSVAR(debug_skip_unique_check),
  MYSQL_SYSVAR(commit_in_the_middle),
  MYSQL_SYSVAR(read_free_rpl_tables),
  MYSQL_SYSVAR(bulk_load_size),
  MYSQL_SYSVAR(enable_bulk_load_api),
  MYSQL_SYSVAR(skip_bloom_filter_on_read),

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
  MYSQL_SYSVAR(max_subcompactions),
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
  MYSQL_SYSVAR(pin_l0_filter_and_index_blocks_in_cache),
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
  MYSQL_SYSVAR(disable_2pc),
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


static rocksdb::WriteOptions rdb_get_rocksdb_write_options(my_core::THD* thd)
{
  rocksdb::WriteOptions opt;

  opt.sync= THDVAR(thd, write_sync);
  opt.disableWAL= THDVAR(thd, write_disable_wal);
  opt.ignore_missing_column_families=
      THDVAR(thd, write_ignore_missing_column_families);

  return opt;
}

///////////////////////////////////////////////////////////////////////////////////////////

/**
  @brief
  Function we use in the creation of our hash to get key.
*/

uchar* Rdb_open_tables_map::get_hash_key(
  Rdb_table_handler *table_handler, size_t *length,
  my_bool not_used __attribute__((__unused__)))
{
  *length= table_handler->m_table_name_length;
  return reinterpret_cast<uchar*>(table_handler->m_table_name);
}


/*
  The following is needed as an argument for mysql_stage_register,
  irrespectively of whether we're compiling with P_S or not.
*/
PSI_stage_info stage_waiting_on_row_lock= { 0, "Waiting for row lock", 0};

#ifdef HAVE_PSI_INTERFACE
static PSI_thread_key rdb_background_psi_thread_key;
static PSI_thread_key rdb_drop_idx_psi_thread_key;

static PSI_stage_info *all_rocksdb_stages[]=
{
  & stage_waiting_on_row_lock
};


static my_core::PSI_mutex_key rdb_psi_open_tbls_mutex_key,
  rdb_signal_bg_psi_mutex_key, rdb_signal_drop_idx_psi_mutex_key,
  rdb_collation_data_mutex_key,
  rdb_mem_cmp_space_mutex_key,
  key_mutex_tx_list, rdb_sysvars_psi_mutex_key;

static PSI_mutex_info all_rocksdb_mutexes[]=
{
  { &rdb_psi_open_tbls_mutex_key, "open tables", PSI_FLAG_GLOBAL},
  { &rdb_signal_bg_psi_mutex_key, "stop background", PSI_FLAG_GLOBAL},
  { &rdb_signal_drop_idx_psi_mutex_key, "signal drop index", PSI_FLAG_GLOBAL},
  { &rdb_collation_data_mutex_key, "collation data init", PSI_FLAG_GLOBAL},
  { &rdb_mem_cmp_space_mutex_key, "collation space char data init",
    PSI_FLAG_GLOBAL},
  { &key_mutex_tx_list, "tx_list", PSI_FLAG_GLOBAL},
  { &rdb_sysvars_psi_mutex_key, "setting sysvar", PSI_FLAG_GLOBAL},
};

static PSI_rwlock_key key_rwlock_collation_exception_list;
static PSI_rwlock_key key_rwlock_read_free_rpl_tables;
static PSI_rwlock_key key_rwlock_skip_unique_check;

static PSI_rwlock_info all_rocksdb_rwlocks[]=
{
  { &key_rwlock_collation_exception_list, "collation_exception_list",
      PSI_FLAG_GLOBAL},
  { &key_rwlock_read_free_rpl_tables, "read_free_rpl_tables", PSI_FLAG_GLOBAL},
  { &key_rwlock_skip_unique_check, "skip_unique_check", PSI_FLAG_GLOBAL},
};

PSI_cond_key rdb_signal_bg_psi_cond_key, rdb_signal_drop_idx_psi_cond_key;

static PSI_cond_info all_rocksdb_conds[]=
{
  { &rdb_signal_bg_psi_cond_key, "cond signal background", PSI_FLAG_GLOBAL},
  { &rdb_signal_drop_idx_psi_cond_key, "cond signal drop index",
      PSI_FLAG_GLOBAL},
};

static PSI_thread_info all_rocksdb_threads[]=
{
  { &rdb_background_psi_thread_key, "background", PSI_FLAG_GLOBAL},
  { &rdb_drop_idx_psi_thread_key, "drop index", PSI_FLAG_GLOBAL},
};

static void init_rocksdb_psi_keys()
{
  const char* category= "rocksdb";
  int count;

  if (PSI_server == nullptr)
    return;

  count= array_elements(all_rocksdb_mutexes);
  PSI_server->register_mutex(category, all_rocksdb_mutexes, count);

  count= array_elements(all_rocksdb_rwlocks);
  PSI_server->register_rwlock(category, all_rocksdb_rwlocks, count);

  count= array_elements(all_rocksdb_conds);
  // TODO Disabling PFS for conditions due to the bug https://github.com/MySQLOnRocksDB/mysql-5.6/issues/92
  // PSI_server->register_cond(category, all_rocksdb_conds, count);

  count= array_elements(all_rocksdb_stages);
  mysql_stage_register(category, all_rocksdb_stages, count);

  count= array_elements(all_rocksdb_threads);
  mysql_thread_register(category, all_rocksdb_threads, count);
}
#endif


/*
  Drop index thread's control
*/

static Rdb_drop_index_thread rdb_drop_idx_thread;

static void rocksdb_drop_index_wakeup_thread(
    my_core::THD* thd __attribute__((__unused__)),
    struct st_mysql_sys_var* var __attribute__((__unused__)),
    void* var_ptr __attribute__((__unused__)),
    const void* save)
{
  if (*static_cast<const bool*>(save)) {
    rdb_drop_idx_thread.signal();
  }
}

static inline uint32_t rocksdb_perf_context_level(THD* thd)
{
  DBUG_ASSERT(thd != nullptr);

  int session_perf_context_level= THDVAR(thd, perf_context_level);
  if (session_perf_context_level > rocksdb::PerfLevel::kUninitialized) {
    return session_perf_context_level;
  }

  /*
    Fallback to global thdvar, if session specific one was not set to a valid
    value.
  */

  int global_perf_context_level= THDVAR(nullptr, perf_context_level);
  if (global_perf_context_level > rocksdb::PerfLevel::kUninitialized) {
    return global_perf_context_level;
  }

  return rocksdb::PerfLevel::kDisable;
}

/*
  Very short (functor-like) interface to be passed to
  Rdb_transaction::walk_tx_list()
*/

interface Rdb_tx_list_walker
{
  virtual ~Rdb_tx_list_walker() {}
  virtual void process_tran(const Rdb_transaction*) = 0;
};

/*
  This is a helper class that is passed to RocksDB to get notifications when
  a snapshot gets created.
*/

class Rdb_snapshot_notifier : public rocksdb::TransactionNotifier
{
  Rdb_transaction* m_owning_tx;

  void SnapshotCreated(const rocksdb::Snapshot *snapshot) override;

 public:
  explicit Rdb_snapshot_notifier(Rdb_transaction* owning_tx)
    : m_owning_tx(owning_tx) {}

  // If the owning Rdb_transaction gets destructed we need to not reference
  // it anymore.
  void detach()
  {
    m_owning_tx = nullptr;
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
  ulonglong m_write_count= 0;
  ulonglong m_lock_count= 0;

  rocksdb::Transaction *m_rocksdb_tx= nullptr;
  rocksdb::Transaction *m_rocksdb_reuse_tx= nullptr;

  bool m_is_tx_failed= false;
  bool m_is_delayed_snapshot= false;
  bool m_tx_read_only= false;
  bool m_rollback_only= false;
  bool m_is_two_phase= false;

  THD* m_thd= nullptr;

  rocksdb::ReadOptions m_read_opts;

  static std::multiset<Rdb_transaction*> s_tx_list;
  static mysql_mutex_t s_tx_list_mutex;

  std::shared_ptr<Rdb_snapshot_notifier> m_notifier;

  Rdb_io_perf *m_tbl_io_perf;

public:
  const char* m_mysql_log_file_name;
  my_off_t m_mysql_log_offset;
  const char* m_mysql_gtid;
  String m_detailed_error;

  static void init_mutex()
  {
    mysql_mutex_init(key_mutex_tx_list, &s_tx_list_mutex, MY_MUTEX_INIT_FAST);
  }

  static void term_mutex()
  {
    DBUG_ASSERT(s_tx_list.size() == 0);
    mysql_mutex_destroy(&s_tx_list_mutex);
  }

  static void walk_tx_list(Rdb_tx_list_walker* walker)
  {
    mysql_mutex_lock(&s_tx_list_mutex);

    for (auto it = s_tx_list.begin(); it != s_tx_list.end(); it++)
      walker->process_tran(*it);

    mysql_mutex_unlock(&s_tx_list_mutex);
  }

  void set_tx_failed(bool failed_arg) { m_is_tx_failed= failed_arg; }

  int set_status_error(THD *thd, const rocksdb::Status &s,
                       const std::shared_ptr<const Rdb_key_def>& kd,
                       Rdb_tbl_def *tbl_def)
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
      my_core::thd_mark_transaction_to_rollback(thd, false /*just statement*/);
      m_detailed_error.copy(timeout_message("index",
                                            tbl_def->full_tablename().c_str(),
                                            kd->get_name().c_str()));

      return HA_ERR_LOCK_WAIT_TIMEOUT;
    }
    if (s.IsBusy())
    {
      rocksdb_snapshot_conflict_errors++;
      return HA_ERR_LOCK_DEADLOCK;
    }
    /* TODO: who returns HA_ERR_ROCKSDB_TOO_MANY_LOCKS now?? */

    my_error(ER_INTERNAL_ERROR, MYF(0), s.ToString().c_str());
    return HA_ERR_INTERNAL_ERROR;
  }

  int m_timeout_sec; /* Cached value of @@rocksdb_lock_wait_timeout */

  /* Maximum number of locks the transaction can have */
  ulonglong m_max_row_locks;

  bool m_ddl_transaction;

  THD* get_thd() const { return m_thd; }

  /* Used for tracking io_perf counters */
  void io_perf_start(Rdb_io_perf *io_perf)
  {
    /*
      Since perf_context is tracked per thread, it is difficult and expensive
      to maintain perf_context on a per table basis. Therefore, roll all
      perf_context data into the first table used in a query. This works well
      for single table queries and is probably good enough for queries that hit
      multiple tables.

      perf_context stats gathering is started when the table lock is acquired
      or when ha_rocksdb::start_stmt is called in case of LOCK TABLES. They
      are recorded when the table lock is released, or when commit/rollback
      is called on the transaction, whichever comes first. Table lock release
      and commit/rollback can happen in different orders. In the case where
      the lock is released before commit/rollback is called, an extra step to
      gather stats during commit/rollback is needed.
    */
    if (m_tbl_io_perf == nullptr &&
          io_perf->start(rocksdb_perf_context_level(m_thd)))
    {
      m_tbl_io_perf= io_perf;
    }
  }

  void io_perf_end_and_record(void)
  {
    if (m_tbl_io_perf != nullptr)
    {
      m_tbl_io_perf->end_and_record(rocksdb_perf_context_level(m_thd));
      m_tbl_io_perf= nullptr;
    }
  }

  void io_perf_end_and_record(Rdb_io_perf *io_perf)
  {
    if (m_tbl_io_perf == io_perf)
    {
      io_perf_end_and_record();
    }
  }

  void set_params(int timeout_sec_arg, int max_row_locks_arg)
  {
    m_timeout_sec= timeout_sec_arg;
    m_max_row_locks= max_row_locks_arg;
    if (m_rocksdb_tx)
      m_rocksdb_tx->SetLockTimeout(m_timeout_sec * 1000);
  }

  ulonglong get_write_count() const { return m_write_count; }

  ulonglong get_lock_count() const { return m_lock_count; }

  void set_sync(bool sync)
  {
    m_rocksdb_tx->GetWriteOptions()->sync= sync;
  }

  void release_lock(rocksdb::ColumnFamilyHandle* column_family,
                    const std::string &rowkey)
  {
    if (!THDVAR(m_thd, lock_scanned_rows))
    {
      m_rocksdb_tx->UndoGetForUpdate(column_family, rocksdb::Slice(rowkey));
    }
  }

  bool prepare(const rocksdb::TransactionName& name)
  {
    rocksdb::Status s;
    s= m_rocksdb_tx->SetName(name);
    if (!s.ok())
    {
      rdb_handle_io_error(s, RDB_IO_ERROR_TX_COMMIT);
      return false;
    }

    s= m_rocksdb_tx->Prepare();
    if (!s.ok())
    {
      rdb_handle_io_error(s, RDB_IO_ERROR_TX_COMMIT);
      return false;
    }
    return true;
  }

  bool commit_or_rollback()
  {
    bool res;
    if (m_is_tx_failed)
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
    if (get_write_count() == 0)
    {
      rollback();
      return false;
    }
    else if (m_rollback_only)
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
      my_core::thd_binlog_pos(m_thd, &m_mysql_log_file_name,
                              &m_mysql_log_offset, &m_mysql_gtid);
      rocksdb::WriteBatch *batch= nullptr;
      if (is_two_phase()) {
        batch= m_rocksdb_tx->GetCommitTimeWriteBatch();
      } else {
        batch= m_rocksdb_tx->GetWriteBatch()->GetWriteBatch();
      }
      binlog_manager.update(m_mysql_log_file_name,
                            m_mysql_log_offset,
                            m_mysql_gtid, batch);
      return commit_no_binlog();
    }
  }

 private:
  void release_tx(void)
  {
    // We are done with the current active transaction object.  Preserve it
    // for later reuse.
    DBUG_ASSERT(m_rocksdb_reuse_tx == nullptr);
    m_rocksdb_reuse_tx= m_rocksdb_tx;
    m_rocksdb_tx= nullptr;
  }

  bool commit_no_binlog()
  {
    bool res= false;
    release_snapshot();
    rocksdb::Status s= m_rocksdb_tx->Commit();
    if (!s.ok())
    {
      rdb_handle_io_error(s, RDB_IO_ERROR_TX_COMMIT);
      res= true;
    }

    /* Save the transaction object to be reused */
    release_tx();

    m_write_count= 0;
    m_lock_count= 0;
    set_tx_read_only(false);
    m_rollback_only= false;
    return res;
  }

 public:
  void rollback()
  {
    m_write_count= 0;
    m_lock_count= 0;
    m_ddl_transaction= false;
    if (m_rocksdb_tx)
    {
      release_snapshot();
      /* This will also release all of the locks: */
      m_rocksdb_tx->Rollback();

      /* Save the transaction object to be reused */
      release_tx();

      set_tx_read_only(false);
      m_rollback_only= false;
    }
  }

  int64_t m_snapshot_timestamp= 0;

  void snapshot_created(const rocksdb::Snapshot *snapshot)
  {
    m_read_opts.snapshot = snapshot;
    rdb->GetEnv()->GetCurrentTime(&m_snapshot_timestamp);
    m_is_delayed_snapshot = false;
  }

  void acquire_snapshot(bool acquire_now)
  {
    if (m_read_opts.snapshot == nullptr) {
      if (is_tx_read_only()) {
        snapshot_created(rdb->GetSnapshot());
      }
      else if (acquire_now) {
        m_rocksdb_tx->SetSnapshot();
        snapshot_created(m_rocksdb_tx->GetSnapshot());
      }
      else if (!m_is_delayed_snapshot) {
        m_rocksdb_tx->SetSnapshotOnNextOperation(m_notifier);
        m_is_delayed_snapshot = true;
      }
    }
  }

  void release_snapshot()
  {
    bool need_clear = m_is_delayed_snapshot;

    if (m_read_opts.snapshot != nullptr)
    {
      m_snapshot_timestamp = 0;
      if (is_tx_read_only())
      {
        rdb->ReleaseSnapshot(m_read_opts.snapshot);
        need_clear = false;
      }
      else
      {
        need_clear = true;
      }
      m_read_opts.snapshot = nullptr;
    }

    if (need_clear && m_rocksdb_tx != nullptr)
      m_rocksdb_tx->ClearSnapshot();
  }

  bool has_snapshot()
  {
    return m_read_opts.snapshot != nullptr;
  }

 private:
  // The tables we are currently loading.  In a partitioned table this can
  // have more than one entry
  std::vector<ha_rocksdb*> m_curr_bulk_load;

 public:
  int finish_bulk_load()
  {
    int rc= 0;

    std::vector<ha_rocksdb*>::iterator it;
    while ((it = m_curr_bulk_load.begin()) != m_curr_bulk_load.end())
    {
      int rc2= (*it)->finalize_bulk_load();
      if (rc2 != 0 && rc == 0)
      {
        rc= rc2;
      }
    }

    DBUG_ASSERT(m_curr_bulk_load.size() == 0);

    return rc;
  }

  void start_bulk_load(ha_rocksdb* bulk_load)
  {
    /*
     If we already have an open bulk load of a table and the name doesn't
     match the current one, close out the currently running one.  This allows
     multiple bulk loads to occur on a partitioned table, but then closes
     them all out when we switch to another table.
    */
    if (!m_curr_bulk_load.empty() &&
        !bulk_load->same_table(*m_curr_bulk_load[0]))
    {
      auto res= finish_bulk_load();
      SHIP_ASSERT(res == 0);
    }

    m_curr_bulk_load.push_back(bulk_load);
  }

  void end_bulk_load(ha_rocksdb* bulk_load)
  {
    for (auto it = m_curr_bulk_load.begin(); it != m_curr_bulk_load.end();
         it++)
    {
      if (*it == bulk_load)
      {
        m_curr_bulk_load.erase(it);
        return;
      }
    }

    // Should not reach here
    SHIP_ASSERT(0);
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
    if (get_write_count() == 0)
      return false;

    /* Commit the current transaction */
    if (commit_no_binlog())
    {
      return true;
    }

    /* Start another one */
    start_tx();
    return false;
  }

  const char *err_too_many_locks=
    "Number of locks held by the transaction exceeded @@rocksdb_max_row_locks";

  rocksdb::Status put(rocksdb::ColumnFamilyHandle* column_family,
                      const rocksdb::Slice& key, const rocksdb::Slice& value)
  {
    ++m_write_count;
    ++m_lock_count;
    if (m_write_count > m_max_row_locks || m_lock_count > m_max_row_locks)
      return rocksdb::Status::Aborted(rocksdb::Slice(err_too_many_locks));
    return m_rocksdb_tx->Put(column_family, key, value);
  }

  rocksdb::Status delete_key(rocksdb::ColumnFamilyHandle* column_family,
                             const rocksdb::Slice& key)
  {
    ++m_write_count;
    ++m_lock_count;
    if (m_write_count > m_max_row_locks || m_lock_count > m_max_row_locks)
      return rocksdb::Status::Aborted(rocksdb::Slice(err_too_many_locks));
    return m_rocksdb_tx->Delete(column_family, key);
  }

  rocksdb::Status single_delete(rocksdb::ColumnFamilyHandle* column_family,
                                const rocksdb::Slice& key)
  {
    ++m_write_count;
    ++m_lock_count;
    if (m_write_count > m_max_row_locks || m_lock_count > m_max_row_locks)
      return rocksdb::Status::Aborted(rocksdb::Slice(err_too_many_locks));
    return m_rocksdb_tx->SingleDelete(column_family, key);
  }

  bool has_modifications() const
  {
    return m_rocksdb_tx->GetWriteBatch() &&
           m_rocksdb_tx->GetWriteBatch()->GetWriteBatch() &&
           m_rocksdb_tx->GetWriteBatch()->GetWriteBatch()->Count() > 0;
  }

  /*
    Return a WriteBatch that one can write to. The writes will skip any
    transaction locking. The writes WILL be visible to the transaction.
  */
  rocksdb::WriteBatchBase* get_indexed_write_batch()
  {
    ++m_write_count;
    return m_rocksdb_tx->GetWriteBatch();
  }

  /*
    Return a WriteBatch that one can write to. The writes will skip any
    transaction locking. The writes will NOT be visible to the transaction.
  */
  rocksdb::WriteBatchBase* get_blind_write_batch()
  {
    ++m_write_count;
    return m_rocksdb_tx->GetWriteBatch()->GetWriteBatch();
  }

  rocksdb::Status get(rocksdb::ColumnFamilyHandle* column_family,
                      const rocksdb::Slice& key, std::string* value) const
  {
    return m_rocksdb_tx->Get(m_read_opts, column_family, key, value);
  }

  rocksdb::Status get_for_update(rocksdb::ColumnFamilyHandle* column_family,
                                 const rocksdb::Slice& key, std::string* value)
  {
    if (++m_lock_count > m_max_row_locks)
      return rocksdb::Status::Aborted(rocksdb::Slice(err_too_many_locks));
    return m_rocksdb_tx->GetForUpdate(m_read_opts, column_family, key, value);
  }

  rocksdb::Iterator *get_iterator(rocksdb::ColumnFamilyHandle* column_family,
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

    rocksdb::ReadOptions options= m_read_opts;

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
    {
      options.snapshot= nullptr;
    }
    rocksdb::Iterator* rocksdb_it= m_rocksdb_tx->GetIterator(options,
                                                             column_family);
    return rocksdb_it;
  }

  bool is_tx_started()
  {
    return (m_rocksdb_tx != nullptr);
  }

  void start_tx()
  {
    rocksdb::TransactionOptions tx_opts;
    rocksdb::WriteOptions write_opts;
    tx_opts.set_snapshot= false;
    tx_opts.lock_timeout= m_timeout_sec * 1000;

    write_opts.sync= THDVAR(m_thd, write_sync);
    write_opts.disableWAL= THDVAR(m_thd, write_disable_wal);
    write_opts.ignore_missing_column_families=
      THDVAR(m_thd, write_ignore_missing_column_families);
    m_is_two_phase= !rocksdb_disable_2pc;

    /*
      If m_rocksdb_reuse_tx is null this will create a new transaction object.
      Otherwise it will reuse the existing one.
    */
    m_rocksdb_tx= rdb->BeginTransaction(write_opts, tx_opts,
                                        m_rocksdb_reuse_tx);
    m_rocksdb_reuse_tx= nullptr;

    m_read_opts= rocksdb::ReadOptions();

    m_ddl_transaction= false;
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
    m_rocksdb_tx->SetSavePoint();
  }

  /*
    This must be called when last statement is rolled back, but the transaction
    continues
  */
  void rollback_stmt()
  {
    /* TODO: here we must release the locks taken since the start_stmt() call */
    if (m_rocksdb_tx)
    {
      const rocksdb::Snapshot *org_snapshot = m_rocksdb_tx->GetSnapshot();
      m_rocksdb_tx->RollbackToSavePoint();

      const rocksdb::Snapshot *cur_snapshot = m_rocksdb_tx->GetSnapshot();
      if (org_snapshot != cur_snapshot)
      {
        if (org_snapshot != nullptr)
          m_snapshot_timestamp = 0;

        m_read_opts.snapshot = cur_snapshot;
        if (cur_snapshot != nullptr)
          rdb->GetEnv()->GetCurrentTime(&m_snapshot_timestamp);
        else
          m_is_delayed_snapshot = true;
      }
    }
  }

  bool can_prepare()
  {
    if (m_rollback_only)
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
      m_rollback_only= true;
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
    return m_tx_read_only;
  }

  bool is_two_phase() const
  {
    return m_is_two_phase;
  }

  void set_tx_read_only(bool val)
  {
    m_tx_read_only= val;
  }

  explicit Rdb_transaction(THD *thd) : m_rocksdb_tx(nullptr), m_thd(thd),
                                       m_tbl_io_perf(nullptr)
  {
    // Create a notifier that can be called when a snapshot gets generated.
    m_notifier = std::make_shared<Rdb_snapshot_notifier>(this);
    mysql_mutex_lock(&s_tx_list_mutex);
    s_tx_list.insert(this);
    mysql_mutex_unlock(&s_tx_list_mutex);
  }

  virtual ~Rdb_transaction()
  {
    rollback();

    mysql_mutex_lock(&s_tx_list_mutex);
    s_tx_list.erase(this);
    mysql_mutex_unlock(&s_tx_list_mutex);

    // Theoretically the notifier could outlive the Rdb_transaction (because
    // of the shared_ptr), so let it know it can't reference the transaction
    // anymore.
    m_notifier->detach();

    // Free any transaction memory that is still hanging around.
    delete m_rocksdb_reuse_tx;
    DBUG_ASSERT(m_rocksdb_tx == nullptr);
  }
};

void Rdb_snapshot_notifier::SnapshotCreated(const rocksdb::Snapshot *snapshot)
{
  if (m_owning_tx != nullptr)
  {
    m_owning_tx->snapshot_created(snapshot);
  }
}

std::multiset<Rdb_transaction*> Rdb_transaction::s_tx_list;
mysql_mutex_t Rdb_transaction::s_tx_list_mutex;

static Rdb_transaction* &get_tx_from_thd(THD *thd)
{
  return *reinterpret_cast<Rdb_transaction**>(
    my_core::thd_ha_data(thd, rocksdb_hton));
}

namespace {

class Rdb_perf_context_guard
{
  Rdb_io_perf m_io_perf;
  THD *m_thd;

 public:
  explicit Rdb_perf_context_guard(THD *thd) : m_thd(thd)
  {
    Rdb_transaction*& tx= get_tx_from_thd(m_thd);
    /*
      if perf_context information is already being recorded, this becomes a
      no-op
    */
    if (tx != nullptr)
    {
      tx->io_perf_start(&m_io_perf);
    }
  }

  ~Rdb_perf_context_guard()
  {
    Rdb_transaction*& tx= get_tx_from_thd(m_thd);
    if (tx != nullptr)
    {
      tx->io_perf_end_and_record();
    }
  }
};

}  // anonymous namespace

/*
  TODO: maybe, call this in external_lock() and store in ha_rocksdb..
*/

static Rdb_transaction *get_or_create_tx(THD *thd)
{
  Rdb_transaction*& tx= get_tx_from_thd(thd);
  // TODO: this is called too many times.. O(#rows)
  if (tx == nullptr)
  {
    tx= new Rdb_transaction(thd);
    tx->set_params(THDVAR(thd, lock_wait_timeout), THDVAR(thd, max_row_locks));
    tx->start_tx();
  }
  else
  {
    tx->set_params(THDVAR(thd, lock_wait_timeout), THDVAR(thd, max_row_locks));
    if (!tx->is_tx_started())
    {
      tx->start_tx();
    }
  }

  return tx;
}


static int rocksdb_close_connection(handlerton* hton, THD* thd)
{
  Rdb_transaction*& tx= get_tx_from_thd(thd);
  if (tx != nullptr)
  {
    int rc= tx->finish_bulk_load();
    if (rc != 0)
    {
      // NO_LINT_DEBUG
      sql_print_error("RocksDB: Error %d finalizing last SST file while "
                      "disconnecting", rc);
      abort_with_stack_traces();
    }

    delete tx;
    tx= nullptr;
  }
  return 0;
}

/*
 * Serializes an xid to a string so that it can
 * be used as a rocksdb transaction name
 */
static std::string rdb_xid_to_string(const XID& src)
{
  DBUG_ASSERT(src.gtrid_length >= 0 && src.gtrid_length <= MAXGTRIDSIZE);
  DBUG_ASSERT(src.bqual_length >= 0 && src.bqual_length <= MAXBQUALSIZE);

  std::string buf;
  buf.reserve(RDB_XIDHDR_LEN + src.gtrid_length + src.bqual_length);

  /*
   * expand formatID to fill 8 bytes if it doesn't already
   * then reinterpret bit pattern as unsigned and store in network order
   */
  uchar fidbuf[RDB_FORMATID_SZ];
  int64 signed_fid8= src.formatID;
  uint64 raw_fid8= *reinterpret_cast<uint64*>(&signed_fid8);
  rdb_netbuf_store_uint64(fidbuf, raw_fid8);
  buf.append(reinterpret_cast<const char*>(fidbuf), RDB_FORMATID_SZ);

  buf.push_back(src.gtrid_length);
  buf.push_back(src.bqual_length);
  buf.append(src.data, (src.gtrid_length) + (src.bqual_length));
  return buf;
}


/**
  Called by hton->flush_logs after MySQL group commit prepares a set of
  transactions.
*/
static bool rocksdb_flush_wal(handlerton* hton __attribute__((__unused__)),
                              ulonglong target_lsn __attribute__((__unused__)))
{
  DBUG_ASSERT(rdb != nullptr);
  rocksdb::Status s= rdb->SyncWAL();
  if (!s.ok()) {
    return 1;
  }
  return 0;
}

/**
  For a slave, prepare() updates the slave_gtid_info table which tracks the
  replication progress.
*/
static int rocksdb_prepare(handlerton* hton, THD* thd, bool prepare_tx,
                           bool async)
{
  Rdb_transaction*& tx= get_tx_from_thd(thd);
  if (!tx->can_prepare())
  {
    return 1;
  }
  if (prepare_tx ||
      (!my_core::thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN))) {
    /* We were instructed to prepare the whole transaction, or
    this is an SQL statement end and autocommit is on */
    std::vector<st_slave_gtid_info> slave_gtid_info;
    my_core::thd_slave_gtid_info(thd, &slave_gtid_info);
    for (auto it : slave_gtid_info) {
      rocksdb::WriteBatchBase* write_batch = tx->get_blind_write_batch();
      binlog_manager.update_slave_gtid_info(it.id, it.db, it.gtid, write_batch);
    }

    if (tx->is_two_phase()) {
      if (thd->durability_property == HA_IGNORE_DURABILITY || async) {
        tx->set_sync(false);
      }
      XID xid;
      thd_get_xid(thd, reinterpret_cast<MYSQL_XID*>(&xid));
      if (!tx->prepare(rdb_xid_to_string(xid))) {
        return 1;
      }
      if (thd->durability_property == HA_IGNORE_DURABILITY) {
        /**
          we set the log sequence as '1' just to trigger hton->flush_logs
        */
        thd_store_lsn(thd, 1, DB_TYPE_ROCKSDB);
      }
    }

    DEBUG_SYNC(thd, "rocksdb.prepared");
  }

  return 0;
}

/**
 do nothing for prepare/commit by xid
 this is needed to avoid crashes in XA scenarios
*/
static int rocksdb_commit_by_xid(handlerton* hton,	XID* xid)
{
  auto name= rdb_xid_to_string(*xid);
  rocksdb::Transaction *trx= rdb->GetTransactionByName(name);
  if (trx == nullptr) {
    return 1;
  }
  rocksdb::Status s= trx->Commit();
  if (!s.ok()) {
    return 1;
  }
  delete trx;
  return 0;
}

static int rocksdb_rollback_by_xid(handlerton* hton __attribute__((__unused__)),
                                   XID* xid)
{
  auto name= rdb_xid_to_string(*xid);
  rocksdb::Transaction *trx= rdb->GetTransactionByName(name);
  if (trx == nullptr) {
    return 1;
  }
  rocksdb::Status s= trx->Rollback();
  if (!s.ok()) {
    return 1;
  }
  delete trx;
  return 0;
}

/**
  Rebuilds an XID from a serialized version stored in a string.
*/
static void rdb_xid_from_string(const std::string& src, XID *dst)
{
  DBUG_ASSERT(dst != nullptr);
  uint offset= 0;
  uint64 raw_fid8=
    rdb_netbuf_to_uint64(reinterpret_cast<const uchar*>(src.data()));
  int64 signed_fid8= *reinterpret_cast<int64*>(&raw_fid8);
  dst->formatID= signed_fid8;
  offset += RDB_FORMATID_SZ;
  dst->gtrid_length= src.at(offset);
  offset += RDB_GTRID_SZ;
  dst->bqual_length= src.at(offset);
  offset += RDB_BQUAL_SZ;

  DBUG_ASSERT(dst->gtrid_length >= 0 && dst->gtrid_length <= MAXGTRIDSIZE);
  DBUG_ASSERT(dst->bqual_length >= 0 && dst->bqual_length <= MAXBQUALSIZE);

  src.copy(dst->data, (dst->gtrid_length)+(dst->bqual_length), RDB_XIDHDR_LEN);
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
    if (binlog_manager.read(file_buf, &pos, gtid_buf))
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

  if (len == 0 || xid_list == nullptr)
  {
    return 0;
  }

  std::vector<rocksdb::Transaction*> trans_list;
  rdb->GetAllPreparedTransactions(&trans_list);

  uint count= 0;
  for (auto& trans : trans_list)
  {
    if (count >= len)
    {
      break;
    }
    auto name= trans->GetName();
    rdb_xid_from_string(name, &xid_list[count]);
    count++;
  }
  return count;
}

static int rocksdb_commit(handlerton* hton, THD* thd, bool commit_tx, bool)
{
  DBUG_ENTER("rocksdb_commit");

  /* this will trigger saving of perf_context information */
  Rdb_perf_context_guard guard(thd);

  /* note: h->external_lock(F_UNLCK) is called after this function is called) */
  Rdb_transaction*& tx= get_tx_from_thd(thd);

  if (tx != nullptr)
  {
    if (commit_tx ||
       (!my_core::thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN)))
    {
      /*
        We get here
         - For a COMMIT statement that finishes a multi-statement transaction
         - For a statement that has its own transaction
      */
      if (tx->commit())
        DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
    }
    else
    {
      /*
        We get here when committing a statement within a transaction.

        We don't need to do anything here. tx->start_stmt() will notify
        Rdb_transaction that another statement has started.
      */
      tx->set_tx_failed(false);
    }

    if (my_core::thd_tx_isolation(thd) <= ISO_READ_COMMITTED)
    {
      // For READ_COMMITTED, we release any existing snapshot so that we will
      // see any changes that occurred since the last statement.
      tx->release_snapshot();
    }
  }
  DBUG_RETURN(0);
}


static int rocksdb_rollback(handlerton* hton, THD* thd, bool rollback_tx)
{
  Rdb_perf_context_guard guard(thd);
  Rdb_transaction*& tx= get_tx_from_thd(thd);

  if (tx != nullptr)
  {
    if (rollback_tx)
    {
      /*
        We get here, when
        - ROLLBACK statement is issued.

        Discard the changes made by the transaction
      */
      tx->rollback();
    }
    else
    {
      /*
        We get here when
        - a statement with AUTOCOMMIT=1 is being rolled back (because of some
          error)
        - a statement inside a transaction is rolled back
      */

      tx->rollback_stmt();
      tx->set_tx_failed(true);
    }

    if (my_core::thd_tx_isolation(thd) <= ISO_READ_COMMITTED)
    {
      // For READ_COMMITTED, we release any existing snapshot so that we will
      // see any changes that occurred since the last statement.
      tx->release_snapshot();
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

class Rdb_snapshot_status : public Rdb_tx_list_walker
{
 private:
  std::string m_data;

  static std::string current_timestamp(void)
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

  static std::string get_header(void)
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

  static std::string get_footer(void)
  {
    return
        "-----------------------------------------\n"
        "END OF ROCKSDB TRANSACTION MONITOR OUTPUT\n"
        "=========================================\n";
  }

 public:
  Rdb_snapshot_status() : m_data(get_header()) {}

  std::string getResult() { return m_data + get_footer(); }

  /* Implement Rdb_transaction interface */
  /* Create one row in the snapshot status table */
  void process_tran(const Rdb_transaction *tx) override
  {
    /* Calculate the duration the snapshot has existed */
    int64_t snapshot_timestamp = tx->m_snapshot_timestamp;
    if (snapshot_timestamp != 0)
    {
      int64_t curr_time;
      rdb->GetEnv()->GetCurrentTime(&curr_time);

      THD* thd = tx->get_thd();

      m_data += format_string("---SNAPSHOT, ACTIVE %lld sec\n"
                              "MySQL thread id %lu, OS thread handle %p\n"
                              "lock count %llu, write count %llu\n",
                              curr_time - snapshot_timestamp,
                              my_core::thd_get_thread_id(thd), thd,
                              tx->get_lock_count(), tx->get_write_count());
    }
  }
};

/* Generate the snapshot status table */
static bool rocksdb_show_snapshot_status(handlerton*    hton,
                                         THD*           thd,
                                         stat_print_fn* stat_print)
{
  Rdb_snapshot_status showStatus;

  Rdb_transaction::walk_tx_list(&showStatus);

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
      cfh= cf_manager.get_cf(cf_name.c_str(), "", nullptr, &is_automatic);
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
    cache_set.insert(rocksdb_tbl_options.block_cache.get());
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

static inline void rocksdb_register_tx(handlerton *hton, THD *thd,
                                       Rdb_transaction *tx)
{
  trans_register_ha(thd, FALSE, rocksdb_hton);
  if (my_core::thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN))
  {
    tx->start_stmt();
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
static int rocksdb_start_tx_and_assign_read_view(
        handlerton*     hton,           /*!< in: RocksDB handlerton */
        THD*            thd,            /*!< in: MySQL thread handle of the
                                        user for whom the transaction should
                                        be committed */
        char*           binlog_file,    /* out: binlog file for last commit */
        ulonglong*      binlog_pos,     /* out: binlog pos for last commit */
        char**  gtid_executed,  /* out: Gtids logged until last commit */
        int*    gtid_executed_length)   /*out: Length of gtid_executed string */
{
  Rdb_perf_context_guard guard(thd);

  ulong const tx_isolation = my_core::thd_tx_isolation(thd);

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

  Rdb_transaction* tx= get_or_create_tx(thd);
  DBUG_ASSERT(!tx->has_snapshot());
  tx->set_tx_read_only(true);
  rocksdb_register_tx(hton, thd, tx);
  tx->acquire_snapshot(true);

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
  Rdb_transaction*& tx= get_tx_from_thd(thd);
  return tx->rollback_to_savepoint(savepoint);
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

  tablenames= rdb_open_tables.get_table_names();

  for (const auto& it : tablenames)
  {
    Rdb_table_handler *table_handler;
    std::string str, dbname, tablename, partname;
    char dbname_sys[NAME_LEN + 1];
    char tablename_sys[NAME_LEN + 1];
    bool is_partition;

    if (rdb_normalize_tablename(it, &str)) {
      /* Function needs to return void because of the interface and we've
       * detected an error which shouldn't happen. There's no way to let
       * caller know that something failed.
      */
      SHIP_ASSERT(false);
      return;
    }

    if (rdb_split_normalized_tablename(str, &dbname, &tablename, &partname))
    {
      continue;
    }

    is_partition= (partname.size() != 0);

    table_handler= rdb_open_tables.get_table_handler(it.c_str());
    if (table_handler == nullptr)
    {
      continue;
    }

    io_perf_read.bytes= table_handler->m_io_perf_read.bytes.load();
    io_perf_read.requests= table_handler->m_io_perf_read.requests.load();

    /*
      Convert from rocksdb timer to mysql timer. RocksDB values are
      in nanoseconds, but table statistics expect the value to be
      in my_timer format.
     */
     io_perf_read.svc_time= my_core::microseconds_to_my_timer(
         table_handler->m_io_perf_read.svc_time.load() / 1000);
     io_perf_read.svc_time_max= my_core::microseconds_to_my_timer(
         table_handler->m_io_perf_read.svc_time_max.load() / 1000);
     io_perf_read.wait_time= my_core::microseconds_to_my_timer(
         table_handler->m_io_perf_read.wait_time.load() / 1000);
     io_perf_read.wait_time_max= my_core::microseconds_to_my_timer(
         table_handler->m_io_perf_read.wait_time_max.load() / 1000);
     io_perf_read.slow_ios= table_handler->m_io_perf_read.slow_ios.load();
     rdb_open_tables.release_table_handler(table_handler);

    /*
      Table stats expects our database and table name to be in system encoding,
      not filename format. Convert before calling callback.
     */
    my_core::filename_to_tablename(dbname.c_str(), dbname_sys,
                                   sizeof(dbname_sys));
    my_core::filename_to_tablename(tablename.c_str(), tablename_sys,
                                   sizeof(tablename_sys));
    (*cb)(dbname_sys, tablename_sys, is_partition, &io_perf_read, &io_perf,
          &io_perf, &io_perf, &io_perf, &page_stats, &comp_stats, 0, 0,
          rocksdb_hton_name);
  }
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

  // Validate the assumption about the size of ROCKSDB_SIZEOF_HIDDEN_PK_COLUMN.
  static_assert(sizeof(longlong) == 8, "Assuming that longlong is 8 bytes.");

#ifdef HAVE_PSI_INTERFACE
  init_rocksdb_psi_keys();
#endif

  rocksdb_hton= (handlerton *)p;
  mysql_mutex_init(rdb_psi_open_tbls_mutex_key, &rdb_open_tables.m_mutex,
                   MY_MUTEX_INIT_FAST);
#ifdef HAVE_PSI_INTERFACE
  rdb_bg_thread.init(rdb_signal_bg_psi_mutex_key,
                     rdb_signal_bg_psi_cond_key);
  rdb_drop_idx_thread.init(rdb_signal_drop_idx_psi_mutex_key,
                           rdb_signal_drop_idx_psi_cond_key);
#else
  rdb_bg_thread.init();
  rdb_drop_idx_thread.init();
#endif
  mysql_mutex_init(rdb_collation_data_mutex_key, &rdb_collation_data_mutex,
                   MY_MUTEX_INIT_FAST);
  mysql_mutex_init(rdb_mem_cmp_space_mutex_key, &rdb_mem_cmp_space_mutex,
                   MY_MUTEX_INIT_FAST);

#if defined(HAVE_PSI_INTERFACE)
  rdb_collation_exceptions = new Regex_list_handler(
      key_rwlock_collation_exception_list);
#else
  rdb_collation_exceptions = new Regex_list_handler();
#endif

  mysql_mutex_init(rdb_sysvars_psi_mutex_key, &rdb_sysvars_mutex,
                   MY_MUTEX_INIT_FAST);
  rdb_open_tables.init_hash();
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
    rocksdb_start_tx_and_assign_read_view;
  rocksdb_hton->savepoint_set= rocksdb_savepoint;
  rocksdb_hton->savepoint_rollback= rocksdb_rollback_to_savepoint;
  rocksdb_hton->savepoint_rollback_can_release_mdl=
    rocksdb_rollback_to_savepoint_can_release_mdl;
  rocksdb_hton->update_table_stats = rocksdb_update_table_stats;
  rocksdb_hton->flush_logs= rocksdb_flush_wal;

  rocksdb_hton->flags= HTON_TEMPORARY_NOT_SUPPORTED |
                       HTON_SUPPORTS_EXTENDED_KEYS |
                       HTON_CAN_RECREATE;

  DBUG_ASSERT(!mysqld_embedded);

  rocksdb_stats= rocksdb::CreateDBStatistics();
  rocksdb_db_options.statistics= rocksdb_stats;

  if (rocksdb_rate_limiter_bytes_per_sec != 0) {
    rocksdb_rate_limiter.reset(rocksdb::NewGenericRateLimiter(
          rocksdb_rate_limiter_bytes_per_sec));
    rocksdb_db_options.rate_limiter= rocksdb_rate_limiter;
  }

  std::shared_ptr<Rdb_logger> myrocks_logger= std::make_shared<Rdb_logger>();
  rocksdb::Status s= rocksdb::CreateLoggerFromOptions(
      rocksdb_datadir, rocksdb_db_options, &rocksdb_db_options.info_log);
  if (s.ok()) {
    myrocks_logger->SetRocksDBLogger(rocksdb_db_options.info_log);
  }

  rocksdb_db_options.info_log= myrocks_logger;
  myrocks_logger->SetInfoLogLevel(
    static_cast<rocksdb::InfoLogLevel>(rocksdb_info_log_level));
  rocksdb_db_options.wal_dir= rocksdb_wal_dir;

  rocksdb_db_options.wal_recovery_mode=
    static_cast<rocksdb::WALRecoveryMode>(rocksdb_wal_recovery_mode);

  rocksdb_db_options.access_hint_on_compaction_start=
    static_cast<rocksdb::Options::AccessHint>
      (rocksdb_access_hint_on_compaction_start);

  if (rocksdb_db_options.allow_mmap_reads &&
     !rocksdb_db_options.allow_os_buffer)
  {
    // allow_mmap_reads implies allow_os_buffer and RocksDB will not open if
    // mmap_reads is on and os_buffer is off.   (NO_LINT_DEBUG)
    sql_print_error("RocksDB: Can't disable allow_os_buffer "
                    "if allow_mmap_reads is enabled\n");
    rdb_open_tables.free_hash();
    DBUG_RETURN(1);
  }

  std::vector<std::string> cf_names;
  rocksdb::Status status;
  status= rocksdb::DB::ListColumnFamilies(rocksdb_db_options, rocksdb_datadir,
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
      rdb_open_tables.free_hash();
      DBUG_RETURN(1);
    }
  }
  else
    sql_print_information("RocksDB: %ld column families found", cf_names.size());

  std::vector<rocksdb::ColumnFamilyDescriptor> cf_descr;
  std::vector<rocksdb::ColumnFamilyHandle*> cf_handles;

  rocksdb_tbl_options.index_type=
    (rocksdb::BlockBasedTableOptions::IndexType)rocksdb_index_type;

  if (!rocksdb_tbl_options.no_block_cache) {
    rocksdb_tbl_options.block_cache=
        rocksdb::NewLRUCache(rocksdb_block_cache_size);
  }
  // Using newer BlockBasedTable format version for better compression
  // and better memory allocation.
  // See: https://github.com/facebook/rocksdb/commit/9ab5adfc59a621d12357580c94451d9f7320c2dd
  rocksdb_tbl_options.format_version= 2;

  if (rocksdb_collect_sst_properties) {
    properties_collector_factory = std::make_shared
      <Rdb_tbl_prop_coll_factory>(
        &ddl_manager
      );

    rocksdb_set_compaction_options(nullptr, nullptr, nullptr, nullptr);

    mysql_mutex_lock(&rdb_sysvars_mutex);

    DBUG_ASSERT(rocksdb_table_stats_sampling_pct
                <= RDB_TBL_STATS_SAMPLE_PCT_MAX);
    properties_collector_factory->SetTableStatsSamplingPct(
      rocksdb_table_stats_sampling_pct);

    mysql_mutex_unlock(&rdb_sysvars_mutex);
  }

  if (!rocksdb_cf_options_map.init(ROCKSDB_WRITE_BUFFER_SIZE_DEFAULT,
                                   rocksdb_tbl_options,
                                   properties_collector_factory,
                                   rocksdb_default_cf_options,
                                   rocksdb_override_cf_options))
  {
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Failed to initialize CF options map.");
    rdb_open_tables.free_hash();
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
    rocksdb_cf_options_map.get_cf_options(cf_names[i], &opts);

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

  rocksdb::Options main_opts(rocksdb_db_options,
                             rocksdb_cf_options_map.get_defaults());

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
      // NO_LINT_DEBUG
      sql_print_error("RocksDB: Failed to open flashcache device at fd %d",
                      cachedev_fd);
      rdb_open_tables.free_hash();
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
  rocksdb::TransactionDBOptions tx_db_options;
  tx_db_options.transaction_lock_timeout= 2;  // 2 seconds
  tx_db_options.custom_mutex_factory= std::make_shared<Rdb_mutex_factory>();

  status= check_rocksdb_options_compatibility(rocksdb_datadir, main_opts,
                                              cf_descr);

  // We won't start if we'll determine that there's a chance of data corruption
  // because of incompatible options.
  if (!status.ok()) {
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: compatibility check against existing database " \
                    "options failed. %s", status.ToString().c_str());
    rdb_open_tables.free_hash();
    DBUG_RETURN(1);
  }

  status= rocksdb::TransactionDB::Open(main_opts, tx_db_options,
                                       rocksdb_datadir, cf_descr,
                                       &cf_handles, &rdb);

  if (!status.ok())
  {
    std::string err_text= status.ToString();
    sql_print_error("RocksDB: Error opening instance: %s", err_text.c_str());
    rdb_open_tables.free_hash();
    DBUG_RETURN(1);
  }
  cf_manager.init(&rocksdb_cf_options_map, &cf_handles);

  if (dict_manager.init(rdb->GetBaseDB(), &cf_manager))
  {
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Failed to initialize data dictionary.");
    rdb_open_tables.free_hash();
    DBUG_RETURN(1);
  }

  if (binlog_manager.init(&dict_manager))
  {
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Failed to initialize binlog manager.");
    rdb_open_tables.free_hash();
    DBUG_RETURN(1);
  }

  if (ddl_manager.init(&dict_manager, &cf_manager, rocksdb_validate_tables))
  {
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Failed to initialize DDL manager.");
    rdb_open_tables.free_hash();
    DBUG_RETURN(1);
  }

  Rdb_sst_info::init(rdb);

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
    rdb_open_tables.free_hash();
    DBUG_RETURN(1);
  }

  auto err= rdb_bg_thread.create_thread(
#ifdef HAVE_PSI_INTERFACE
    rdb_background_psi_thread_key
#endif
  );
  if (err != 0) {
    sql_print_error("RocksDB: Couldn't start the background thread: (errno=%d)",
                    err);
    rdb_open_tables.free_hash();
    DBUG_RETURN(1);
  }

  err= rdb_drop_idx_thread.create_thread(
#ifdef HAVE_PSI_INTERFACE
    rdb_drop_idx_psi_thread_key
#endif
  );
  if (err != 0) {
    sql_print_error("RocksDB: Couldn't start the drop index thread: (errno=%d)",
                    err);
    rdb_open_tables.free_hash();
    DBUG_RETURN(1);
  }

  rdb_set_collation_exception_list(rocksdb_strict_collation_exceptions);

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
  rdb_drop_idx_thread.signal(true);

  // Flush all memtables for not lose data, even if WAL is disabled.
  rocksdb_flush_all_memtables();

  // Stop all rocksdb background work
  CancelAllBackgroundWork(rdb->GetBaseDB(), true);

  // Signal the background thread to stop and to persist all stats collected
  // from background flushes and compactions. This will add more keys to a new
  // memtable, but since the memtables were just flushed, it should not trigger
  // a flush that can stall due to background threads being stopped. As long
  // as these keys are stored in a WAL file, they can be retrieved on restart.
  rdb_bg_thread.signal(true);

  // Wait for the background thread to finish.
  auto err= rdb_bg_thread.join();
  if (err != 0) {
    // We'll log the message and continue because we're shutting down and
    // continuation is the optimal strategy.
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Couldn't stop the background thread: (errno=%d)",
                    err);
  }

  // Wait for the drop index thread to finish.
  err= rdb_drop_idx_thread.join();
  if (err != 0) {
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Couldn't stop the index thread: (errno=%d)",
                    err);
  }

  if (rdb_open_tables.m_hash.records)
  {
    // Looks like we are getting unloaded and yet we have some open tables
    // left behind.
    error= 1;
  }

  rdb_open_tables.free_hash();
  mysql_mutex_destroy(&rdb_open_tables.m_mutex);
  mysql_mutex_destroy(&rdb_sysvars_mutex);

  delete rdb_collation_exceptions;
  mysql_mutex_destroy(&rdb_collation_data_mutex);
  mysql_mutex_destroy(&rdb_mem_cmp_space_mutex);

  Rdb_transaction::term_mutex();

  for (auto& it : rdb_collation_data)
  {
    delete it;
    it= nullptr;
  }

  ddl_manager.cleanup();
  binlog_manager.cleanup();
  dict_manager.cleanup();
  cf_manager.cleanup();

  delete rdb;
  rdb= nullptr;

  // Disown the cache data since we're shutting down.
  // This results in memory leaks but it improved the shutdown time.
  // Don't disown when running under valgrind
#ifndef HAVE_purify
  if (rocksdb_tbl_options.block_cache)
  {
    rocksdb_tbl_options.block_cache->DisownData();
  }
#endif /* HAVE_purify */

  DBUG_RETURN(error);
}


/**
  @brief
  Example of simple lock controls. The "table_handler" it creates is a
  structure we will pass to each ha_rocksdb handler. Do you have to have
  one of these? Well, you have pieces that are used for locking, and
  they are needed to function.
*/

Rdb_table_handler* Rdb_open_tables_map::get_table_handler(
    const char *table_name)
{
  Rdb_table_handler *table_handler;
  uint length;
  char *tmp_name;

  DBUG_ASSERT(table_name != nullptr);
  length=(uint) strlen(table_name);

  // First, look up the table in the hash map.
  mysql_mutex_lock(&m_mutex);
  if (!(table_handler= reinterpret_cast<Rdb_table_handler*>(
          my_hash_search(&m_hash, reinterpret_cast<const uchar*>(table_name),
                         length))))
  {
    // Since we did not find it in the hash map, attempt to create and add it
    // to the hash map.
    if (!(table_handler= reinterpret_cast<Rdb_table_handler*>(
            my_multi_malloc(MYF(MY_WME | MY_ZEROFILL),
                            &table_handler, sizeof(*table_handler),
                            &tmp_name, length+1,
                            NullS))))
    {
      // Allocating a new Rdb_table_handler and a new table name failed.
      mysql_mutex_unlock(&m_mutex);
      return nullptr;
    }

    table_handler->m_ref_count= 0;
    table_handler->m_table_name_length= length;
    table_handler->m_table_name= tmp_name;
    strmov(table_handler->m_table_name, table_name);

    if (my_hash_insert(&m_hash, reinterpret_cast<uchar*>(table_handler)))
    {
      // Inserting into the hash map failed.
      mysql_mutex_unlock(&m_mutex);
      my_free(table_handler);
      return nullptr;
    }

    thr_lock_init(&table_handler->m_thr_lock);
    my_io_perf_atomic_init(&table_handler->m_io_perf_read);
  }
  DBUG_ASSERT(table_handler->m_ref_count >= 0);
  table_handler->m_ref_count++;

  mysql_mutex_unlock(&m_mutex);

  return table_handler;
}


std::vector<std::string> rdb_get_open_table_names(void)
{
  return rdb_open_tables.get_table_names();
}


std::vector<std::string> Rdb_open_tables_map::get_table_names(void) const
{
  ulong i;
  const Rdb_table_handler *table_handler;
  std::vector<std::string> names;

  mysql_mutex_lock(&m_mutex);
  for (i = 0;
       (table_handler= reinterpret_cast<const Rdb_table_handler*>(
          my_hash_const_element(&m_hash, i)));
       i++)
  {
    DBUG_ASSERT(table_handler != nullptr);
    names.push_back(table_handler->m_table_name);
  }
  DBUG_ASSERT(i == m_hash.records);
  mysql_mutex_unlock(&m_mutex);

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
  Rdb_transaction *tx= get_or_create_tx(table->in_use);
  bool is_new_snapshot= !tx->has_snapshot();

  // Do a lookup. We only need index column, so it should be index-only.
  // (another reason to make it index-only is that table->read_set is
  //  not set appropriately and non-index-only lookup will not read the value)
  bool save_keyread_only= m_keyread_only;
  m_keyread_only= true;

  if (!index_last(table->record[0]))
    update_auto_incr_val();

  m_keyread_only= save_keyread_only;
  if (is_new_snapshot)
  {
    tx->release_snapshot();
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

  longlong auto_incr_val= m_tbl_def->m_auto_incr_val;
  while (auto_incr_val < new_val &&
         !m_tbl_def->m_auto_incr_val.compare_exchange_weak(
            auto_incr_val, new_val))
  {
    // Do nothing - just loop until auto_incr_val is >= new_val or
    // we successfully set it
  }
}

int ha_rocksdb::load_hidden_pk_value()
{
  int save_active_index= active_index;
  active_index= m_tbl_def->m_key_count - 1;
  uint8 save_table_status= table->status;

  Rdb_transaction *tx= get_or_create_tx(table->in_use);
  bool is_new_snapshot= !tx->has_snapshot();

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
        tx->release_snapshot();
      }
      return HA_ERR_INTERNAL_ERROR;
    }

    hidden_pk_id++;
    longlong old = m_tbl_def->m_hidden_pk_val;
    while (old < hidden_pk_id &&
           !m_tbl_def->m_hidden_pk_val.compare_exchange_weak(old, hidden_pk_id))
    {}
  }

  if (is_new_snapshot)
  {
    tx->release_snapshot();
  }

  table->status= save_table_status;
  active_index= save_active_index;

  release_scan_iterator();

  return 0;
}

/* Get PK value from m_tbl_def->m_hidden_pk_info. */
longlong ha_rocksdb::update_hidden_pk_val()
{
  DBUG_ASSERT(has_hidden_pk(table));
  longlong new_val;
  new_val= m_tbl_def->m_hidden_pk_val++;
  return new_val;
}

/* Get the id of the hidden pk id from m_last_rowkey */
int ha_rocksdb::read_hidden_pk_id_from_rowkey(longlong* hidden_pk_id)
{
  DBUG_ASSERT(hidden_pk_id != nullptr);
  DBUG_ASSERT(table != nullptr);
  DBUG_ASSERT(has_hidden_pk(table));

  rocksdb::Slice rowkey_slice(m_last_rowkey.ptr(), m_last_rowkey.length());

  // Get hidden primary key from old key slice
  Rdb_string_reader reader(&rowkey_slice);
  if ((!reader.read(Rdb_key_def::INDEX_NUMBER_SIZE)))
    return 1;

  const int length= Field_longlong::PACK_LENGTH;
  const uchar *from= reinterpret_cast<const uchar*>(reader.read(length));
  if (from == nullptr)
  {
    return 1; /* Mem-comparable image doesn't have enough bytes */
  }

  *hidden_pk_id= rdb_netbuf_read_uint64(&from);
  return 0;
}


/**
  @brief
  Free lock controls. We call this whenever we close a table. If the table had
  the last reference to the table_handler, then we free the memory associated
  with it.
*/

void Rdb_open_tables_map::release_table_handler(
    Rdb_table_handler *table_handler)
{
  mysql_mutex_lock(&m_mutex);

  DBUG_ASSERT(table_handler->m_ref_count > 0);
  if (!--table_handler->m_ref_count)
  {
    // Last rereference was released. Tear down the hash entry.
    auto ret __attribute__((__unused__)) =
        my_hash_delete(&m_hash, reinterpret_cast<uchar*>(table_handler));
    DBUG_ASSERT(!ret);  // the hash entry must actually be found and deleted
    my_core::thr_lock_delete(&table_handler->m_thr_lock);
    my_free(table_handler);
  }

  mysql_mutex_unlock(&m_mutex);
}


static handler* rocksdb_create_handler(my_core::handlerton *hton,
                                       my_core::TABLE_SHARE *table_arg,
                                       my_core::MEM_ROOT *mem_root)
{
  return new (mem_root) ha_rocksdb(hton, table_arg);
}


ha_rocksdb::ha_rocksdb(my_core::handlerton *hton,
                       my_core::TABLE_SHARE *table_arg)
  : handler(hton, table_arg), m_table_handler(nullptr), m_scan_it(nullptr),
    m_scan_it_skips_bloom(false), m_tbl_def(nullptr),
    m_pk_descr(nullptr), m_key_descr_arr(nullptr),
    m_pk_can_be_decoded(false),
    m_maybe_unpack_info(false),
    m_pk_tuple(nullptr), m_pk_packed_tuple(nullptr),
    m_sk_packed_tuple(nullptr),
    m_sk_match_prefix(nullptr), m_sk_match_prefix_buf(nullptr),
    m_sk_packed_tuple_old(nullptr),
    m_pack_buffer(nullptr), m_lock_rows(RDB_LOCK_NONE),
    m_keyread_only(FALSE), m_encoder_arr(nullptr),
    m_row_checksums_checked(0),
    m_in_rpl_delete_rows(false), m_in_rpl_update_rows(false),
    m_force_skip_unique_check(false)
{}


static const char *ha_rocksdb_exts[] = {
  NullS
};


const char **ha_rocksdb::bas_ext() const
{
  return ha_rocksdb_exts;
}

bool ha_rocksdb::same_table(const ha_rocksdb& other) const
{
  return m_tbl_def->base_tablename() == other.m_tbl_def->base_tablename();
}

bool ha_rocksdb::init_with_fields()
{
  uint pk= table_share->primary_key;
  if (pk != MAX_KEY)
  {
    uint key_parts= table_share->key_info[pk].user_defined_key_parts;
    check_keyread_allowed(pk /*PK*/, key_parts-1, true);
  }
  else
    m_pk_can_be_decoded= false;

  cached_table_flags= table_flags();
  return false; /* Ok */
}


/**
  Convert record from table->record[0] form into a form that can be written
  into rocksdb.

  @param pk_packed_slice      Packed PK tuple. We need it in order to compute
                              and store its CRC.
  @param packed_rec      OUT  Data slice with record data.
*/

void ha_rocksdb::convert_record_to_storage_format(
    const rocksdb::Slice& pk_packed_slice,
    Rdb_string_writer *pk_unpack_info,
    rocksdb::Slice *packed_rec)
{
  DBUG_ASSERT_IMP(m_maybe_unpack_info, pk_unpack_info);
  m_storage_record.length(0);

  /* All NULL bits are initially 0 */
  m_storage_record.fill(m_null_bytes_in_rec, 0);

  // If a primary key may have non-empty unpack_info for certain values,
  // (m_maybe_unpack_info=TRUE), we write the unpack_info block. The block
  // itself was prepared in Rdb_key_def::pack_record.
  if (m_maybe_unpack_info &&
      m_pk_descr->m_kv_format_version >=
      Rdb_key_def::PRIMARY_FORMAT_VERSION_UPDATE1)
  {
    m_storage_record.append(reinterpret_cast<char*>(pk_unpack_info->ptr()),
                            pk_unpack_info->get_current_pos());
  }

  for (uint i=0; i < table->s->fields; i++)
  {
    /* Don't pack decodable PK key parts */
    if (m_encoder_arr[i].m_storage_type != Rdb_field_encoder::STORE_ALL)
    {
      continue;
    }

    Field *field= table->field[i];
    if (m_encoder_arr[i].maybe_null())
    {
      char *data= (char*)m_storage_record.ptr();
      if (field->is_null())
      {
        data[m_encoder_arr[i].m_null_offset]|= m_encoder_arr[i].m_null_mask;
        /* Don't write anything for NULL values */
        continue;
      }
    }

    if (m_encoder_arr[i].m_field_type == MYSQL_TYPE_BLOB)
    {
      my_core::Field_blob *blob= (my_core::Field_blob*)field;
      /* Get the number of bytes needed to store length*/
      uint length_bytes= blob->pack_length() - portable_sizeof_char_ptr;

      /* Store the length of the value */
      m_storage_record.append(reinterpret_cast<char*>(blob->ptr), length_bytes);

      /* Store the blob value itself */
      char *data_ptr;
      memcpy(&data_ptr, blob->ptr + length_bytes, sizeof(uchar**));
      m_storage_record.append(data_ptr, blob->get_length());
    }
    else if (m_encoder_arr[i].m_field_type == MYSQL_TYPE_VARCHAR)
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
      m_storage_record.append(reinterpret_cast<char*>(field_var->ptr),
                              field_var->length_bytes + data_len);
    }
    else
    {
      /* Copy the field data */
      uint len= field->pack_length_in_rec();
      m_storage_record.append(reinterpret_cast<char*>(field->ptr), len);
    }
  }

  if (should_store_checksums())
  {
    uint32_t key_crc32= my_core::crc32(0,
                                       rdb_slice_to_uchar_ptr(&pk_packed_slice),
                                       pk_packed_slice.size());
    uint32_t val_crc32= my_core::crc32(
        0, rdb_mysql_str_to_uchar_str(&m_storage_record),
        m_storage_record.length());
    uchar key_crc_buf[RDB_CHECKSUM_SIZE];
    uchar val_crc_buf[RDB_CHECKSUM_SIZE];
    rdb_netbuf_store_uint32(key_crc_buf, key_crc32);
    rdb_netbuf_store_uint32(val_crc_buf, val_crc32);
    m_storage_record.append((const char*)&RDB_CHECKSUM_DATA_TAG, 1);
    m_storage_record.append((const char*)key_crc_buf, RDB_CHECKSUM_SIZE);
    m_storage_record.append((const char*)val_crc_buf, RDB_CHECKSUM_SIZE);
  }

  *packed_rec= rocksdb::Slice(m_storage_record.ptr(),
                              m_storage_record.length());
}


/*
  @brief
    Setup which fields will be unpacked when reading rows

  @detail
    Two special cases when we still unpack all fields:
    - When this table is being updated (m_lock_rows==RDB_LOCK_WRITE).
    - When @@rocksdb_verify_checksums is ON (In this mode, we need to read all
      fields to find whether there is a row checksum at the end. We could skip
      the fields instead of decoding them, but currently we do decoding.)

  @seealso
    ha_rocksdb::setup_field_converters()
    ha_rocksdb::convert_record_from_storage_format()
*/
void ha_rocksdb::setup_read_decoders()
{
  m_decoders_vect.clear();

  int last_useful= 0;
  int skip_size= 0;

  for (uint i= 0; i < table->s->fields; i++)
  {
    // We only need the decoder if the whole record is stored.
    if (m_encoder_arr[i].m_storage_type != Rdb_field_encoder::STORE_ALL)
    {
      continue;
    }

    if (m_lock_rows == RDB_LOCK_WRITE || m_verify_checksums ||
        bitmap_is_set(table->read_set, table->field[i]->field_index))
    {
      // We will need to decode this field
      m_decoders_vect.push_back({&m_encoder_arr[i], true, skip_size});
      last_useful= m_decoders_vect.size();
      skip_size= 0;
    }
    else
    {
      if (m_encoder_arr[i].uses_variable_len_encoding() ||
          m_encoder_arr[i].maybe_null())
      {
        // For variable-length field, we need to read the data and skip it
        m_decoders_vect.push_back({&m_encoder_arr[i], false, skip_size});
        skip_size= 0;
      }
      else
      {
        // Fixed-width field can be skipped without looking at it.
        // Add appropriate skip_size to the next field.
        skip_size += m_encoder_arr[i].m_pack_length_in_rec;
      }
    }
  }

  // It could be that the last few elements are varchars that just do
  // skipping. Remove them.
  m_decoders_vect.erase(m_decoders_vect.begin() + last_useful,
                        m_decoders_vect.end());
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
  res.append(on_disk_rec.ptr(), Rdb_key_def::INDEX_NUMBER_SIZE);

  // Then, a mem-comparable form of a varchar(8) value.
  res.append("ABCDE\0\0\0\xFC", 9);
  on_disk_rec.length(0);
  on_disk_rec.append(res.data(), res.size());
}

#endif


int ha_rocksdb::convert_record_from_storage_format(const rocksdb::Slice *key,
                                                   uchar * buf)
{
  DBUG_EXECUTE_IF("myrocks_simulate_bad_row_read1",
                  dbug_append_garbage_at_end(m_retrieved_record););
  DBUG_EXECUTE_IF("myrocks_simulate_bad_row_read2",
                  dbug_truncate_record(m_retrieved_record););
  DBUG_EXECUTE_IF("myrocks_simulate_bad_row_read3",
                  dbug_modify_rec_varchar12(m_retrieved_record););

  rocksdb::Slice retrieved_rec_slice(&m_retrieved_record.front(),
                                     m_retrieved_record.size());
  return convert_record_from_storage_format(key, &retrieved_rec_slice, buf);
}


/*
  @brief
  Unpack the record in this->m_retrieved_record and this->m_last_rowkey from
  storage format into buf (which can be table->record[0] or table->record[1]).

  @param  key   Table record's key in mem-comparable form.
  @param  buf   Store record in table->record[0] format here

  @detail
    If the table has blobs, the unpacked data in buf may keep pointers to the
    data in this->m_retrieved_record.

    The key is only needed to check its checksum value (the checksum is in
    m_retrieved_record).

  @seealso
    ha_rocksdb::setup_read_decoders()  Sets up data structures which tell which
    columns to decode.

  @return
    0      OK
    other  Error inpacking the data
*/

int ha_rocksdb::convert_record_from_storage_format(const rocksdb::Slice *key,
                                                   const rocksdb::Slice *value,
                                                   uchar * buf)
{
  DBUG_ASSERT(key != nullptr);
  DBUG_ASSERT(buf != nullptr);

  Rdb_string_reader reader(value);
  my_ptrdiff_t ptr_diff= buf - table->record[0];

  /*
    Decode PK fields from the key
  */
  DBUG_EXECUTE_IF("myrocks_simulate_bad_pk_read1",
                  dbug_modify_key_varchar8(m_last_rowkey););

  const rocksdb::Slice rowkey_slice(m_last_rowkey.ptr(),
                                    m_last_rowkey.length());
  const char *unpack_info= nullptr;
  uint16 unpack_info_len= 0;
  rocksdb::Slice unpack_slice;

  /* Other fields are decoded from the value */
  const char *null_bytes= nullptr;
  if (m_null_bytes_in_rec && !(null_bytes= reader.read(m_null_bytes_in_rec)))
  {
    return HA_ERR_INTERNAL_ERROR;
  }

  if (m_maybe_unpack_info && m_pk_descr->m_kv_format_version >=
      Rdb_key_def::PRIMARY_FORMAT_VERSION_UPDATE1)
  {
    unpack_info= reader.read(RDB_UNPACK_HEADER_SIZE);

    if (!unpack_info || unpack_info[0] != RDB_UNPACK_DATA_TAG)
    {
      return HA_ERR_INTERNAL_ERROR;
    }

    unpack_info_len= rdb_netbuf_to_uint16(
      reinterpret_cast<const uchar *>(unpack_info + 1));
    unpack_slice= rocksdb::Slice(unpack_info, unpack_info_len);

    reader.read(unpack_info_len - RDB_UNPACK_HEADER_SIZE);
  }

  if (m_pk_descr->unpack_record(table, buf, &rowkey_slice,
                                unpack_info ? &unpack_slice : nullptr,
                                false /* verify_checksum */))
  {
    return HA_ERR_INTERNAL_ERROR;
  }

  for (auto it= m_decoders_vect.begin(); it != m_decoders_vect.end(); it++)
  {
    const Rdb_field_encoder* const field_dec= it->m_field_enc;
    const bool decode= it->m_decode;
    bool isNull = field_dec->maybe_null() &&
      ((null_bytes[field_dec->m_null_offset] & field_dec->m_null_mask) != 0);

    Field *field= table->field[field_dec->m_field_index];

    /* Skip the bytes we need to skip */
    if (it->m_skip && !reader.read(it->m_skip))
      return HA_ERR_INTERNAL_ERROR;

    if (isNull)
    {
      if (decode)
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
      continue;
    }
    else
    {
      if (decode)
        field->set_notnull(ptr_diff);
    }

    if (field_dec->m_field_type == MYSQL_TYPE_BLOB)
    {
      my_core::Field_blob *blob= (my_core::Field_blob*)field;
      /* Get the number of bytes needed to store length*/
      uint length_bytes= blob->pack_length() - portable_sizeof_char_ptr;

      blob->move_field_offset(ptr_diff);

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

      if (decode)
      {
        // set 8-byte pointer to 0, like innodb does (relevant for 32-bit
        // platforms)
        memset(blob->ptr + length_bytes, 0, 8);
        memcpy(blob->ptr + length_bytes, &blob_ptr, sizeof(uchar**));
        blob->move_field_offset(-ptr_diff);
      }
    }
    else if (field_dec->m_field_type == MYSQL_TYPE_VARCHAR)
    {
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
        DBUG_ASSERT(field_var->length_bytes == 2);
        data_len= uint2korr(data_len_str);
      }
      if (data_len > field->field_length)
      {
        /* The data on disk is longer than table DDL allows? */
        return HA_ERR_INTERNAL_ERROR;
      }
      if (!reader.read(data_len))
        return HA_ERR_INTERNAL_ERROR;

      if (decode)
      {
        memcpy(field_var->ptr + ptr_diff, data_len_str,
               field_var->length_bytes + data_len);
      }
    }
    else
    {
      const char *data_bytes;
      uint len= field_dec->m_pack_length_in_rec;
      if (len > 0)
      {
        if ((data_bytes= reader.read(len)) == nullptr)
        {
          return HA_ERR_INTERNAL_ERROR;
        }
        if (decode)
          memcpy(field->ptr + ptr_diff, data_bytes, len);
      }
    }
  }

  if (m_verify_checksums)
  {
    if (reader.remaining_bytes() == RDB_CHECKSUM_CHUNK_SIZE &&
        reader.read(1)[0] == RDB_CHECKSUM_DATA_TAG)
    {
      uint32_t stored_key_chksum=
        rdb_netbuf_to_uint32((const uchar*)reader.read(RDB_CHECKSUM_SIZE));
      uint32_t stored_val_chksum=
        rdb_netbuf_to_uint32((const uchar*)reader.read(RDB_CHECKSUM_SIZE));

      uint32_t computed_key_chksum=
          my_core::crc32(0, rdb_slice_to_uchar_ptr(key), key->size());
      uint32_t computed_val_chksum=
          my_core::crc32(0, rdb_slice_to_uchar_ptr(value),
                         value->size() - RDB_CHECKSUM_CHUNK_SIZE);

      DBUG_EXECUTE_IF("myrocks_simulate_bad_pk_checksum1",
                      stored_key_chksum++;);

      if (stored_key_chksum != computed_key_chksum)
      {
        m_pk_descr->report_checksum_mismatch(true, key->data(), key->size());
        return HA_ERR_INTERNAL_ERROR;
      }

      DBUG_EXECUTE_IF("myrocks_simulate_bad_pk_checksum2",
                      stored_val_chksum++;);
      if (stored_val_chksum != computed_val_chksum)
      {
        m_pk_descr->report_checksum_mismatch(false, value->data(),
                                             value->size());
        return HA_ERR_INTERNAL_ERROR;
      }

      m_row_checksums_checked++;
    }
    if (reader.remaining_bytes())
      return HA_ERR_INTERNAL_ERROR;
  }

  return 0;
}


void ha_rocksdb::get_storage_type(Rdb_field_encoder *encoder, uint kp)
{
  Field *field= table->key_info[table->s->primary_key].key_part[kp].field;

  if (field->real_type() == MYSQL_TYPE_NEWDECIMAL)
  {
    // Index-only is supported for DECIMAL columns.
    // A DECIMAL value can be restored from its mem-comparable form.
    // This works for both the old data format and the new data format.
    if (m_pk_descr->m_kv_format_version >=
        Rdb_key_def::PRIMARY_FORMAT_VERSION_UPDATE1)
    {
      // New format, don't store Decimal value in the row.
      encoder->m_storage_type= Rdb_field_encoder::STORE_NONE;
    }
    else
    {
      // Old format. Pretend there's no way to unpack the decimal from
      // its mem-comparable form.
      encoder->m_storage_type= Rdb_field_encoder::STORE_ALL;
    }
    return;
  }

  if (m_pk_descr->m_kv_format_version >=
      Rdb_key_def::PRIMARY_FORMAT_VERSION_UPDATE1)
  {
    // STORE_SOME uses unpack_info, so our key must be of version
    // higher than PRIMARY_FORMAT_VERSION_UPDATE1 to use this
    // feature.
    if (m_pk_descr->has_unpack_info(kp))
    {
      DBUG_ASSERT(m_pk_descr->can_unpack(kp));
      encoder->m_storage_type= Rdb_field_encoder::STORE_SOME;
      m_maybe_unpack_info= true;
    }
    else if (m_pk_descr->can_unpack(kp))
    {
      encoder->m_storage_type= Rdb_field_encoder::STORE_NONE;
    }
  }
  else
  {
    // For old versions, we can only store none if there is no
    // unpack_info (and it's unpackable).
    if (m_pk_descr->can_unpack(kp) && !m_pk_descr->has_unpack_info(kp))
    {
      encoder->m_storage_type= Rdb_field_encoder::STORE_NONE;
    }
  }
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
  uchar cur_null_mask= 0x1;

  DBUG_ASSERT(m_encoder_arr == nullptr);
  m_encoder_arr= static_cast<Rdb_field_encoder*>(
      my_malloc(table->s->fields * sizeof(Rdb_field_encoder), MYF(0)));
  if (m_encoder_arr == nullptr)
  {
    return;
  }

  for (i= 0; i < table->s->fields; i++)
  {
    Field *field= table->field[i];
    m_encoder_arr[i].m_storage_type= Rdb_field_encoder::STORE_ALL;

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
          get_storage_type(&m_encoder_arr[i], kp);
          break;
        }
      }
    }

    m_encoder_arr[i].m_field_type= field->real_type();
    m_encoder_arr[i].m_field_index= i;
    m_encoder_arr[i].m_pack_length_in_rec= field->pack_length_in_rec();

    if (field->real_maybe_null())
    {
      m_encoder_arr[i].m_null_mask= cur_null_mask;
      m_encoder_arr[i].m_null_offset= null_bytes;
      if (cur_null_mask == 0x80)
      {
        cur_null_mask= 0x1;
        null_bytes++;
      }
      else
        cur_null_mask= cur_null_mask << 1;
    }
    else
    {
      m_encoder_arr[i].m_null_mask= 0;
    }
  }

  /* Count the last, unfinished NULL-bits byte */
  if (cur_null_mask != 0x1)
    null_bytes++;

  m_null_bytes_in_rec= null_bytes;
}

int ha_rocksdb::alloc_key_buffers(const TABLE* table_arg,
                                  const Rdb_tbl_def* tbl_def_arg)
{
  DBUG_ENTER("ha_rocksdb::alloc_key_buffers");
  DBUG_ASSERT(m_pk_tuple == nullptr);

  std::shared_ptr<Rdb_key_def>* kd_arr= tbl_def_arg->m_key_descr_arr;

  uint key_len= 0;
  m_pk_descr= kd_arr[pk_index(table_arg, tbl_def_arg)];
  if (has_hidden_pk(table_arg))
  {
    m_pk_key_parts= 1;
  }
  else
  {
    m_pk_key_parts=
        table->key_info[table->s->primary_key].user_defined_key_parts;
    key_len= table->key_info[table->s->primary_key].key_length;
  }

  // move this into get_table_handler() ??
  m_pk_descr->setup(table_arg, tbl_def_arg);

  m_pk_tuple= reinterpret_cast<uchar*>(my_malloc(key_len, MYF(0)));
  if (m_pk_tuple == nullptr)
  {
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  uint pack_key_len= m_pk_descr->max_storage_fmt_length();
  m_pk_packed_tuple= reinterpret_cast<uchar*>(my_malloc(pack_key_len, MYF(0)));
  if (m_pk_packed_tuple == nullptr)
  {
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  /* Sometimes, we may use m_sk_packed_tuple for storing packed PK */
  uint max_packed_sk_len= pack_key_len;
  for (uint i= 0; i < table_arg->s->keys; i++)
  {
    if (i == table_arg->s->primary_key) /* Primary key was processed above */
      continue;

    // TODO: move this into get_table_handler() ??
    kd_arr[i]->setup(table_arg, tbl_def_arg);

    uint packed_len= kd_arr[i]->max_storage_fmt_length();
    if (packed_len > max_packed_sk_len)
    {
      max_packed_sk_len= packed_len;
    }
  }

  if (!(m_sk_packed_tuple=
         reinterpret_cast<uchar*>(my_malloc(max_packed_sk_len, MYF(0)))) ||
      !(m_sk_match_prefix_buf=
         reinterpret_cast<uchar*>(my_malloc(max_packed_sk_len, MYF(0)))) ||
      !(m_sk_packed_tuple_old=
         reinterpret_cast<uchar*>(my_malloc(max_packed_sk_len, MYF(0)))) ||
      !(m_end_key_packed_tuple=
         reinterpret_cast<uchar*>(my_malloc(max_packed_sk_len, MYF(0)))) ||
      !((m_pack_buffer=
         reinterpret_cast<uchar*>(my_malloc(max_packed_sk_len, MYF(0))))))
  {
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  DBUG_RETURN(0);
}

void ha_rocksdb::free_key_buffers()
{
  my_free(m_pk_tuple);
  m_pk_tuple= nullptr;

  my_free(m_pk_packed_tuple);
  m_pk_packed_tuple= nullptr;

  my_free(m_sk_packed_tuple);
  m_sk_packed_tuple= nullptr;

  my_free(m_sk_match_prefix_buf);
  m_sk_match_prefix_buf= nullptr;

  my_free(m_sk_packed_tuple_old);
  m_sk_packed_tuple_old= nullptr;

  my_free(m_end_key_packed_tuple);
  m_end_key_packed_tuple= nullptr;

  my_free(m_pack_buffer);
  m_pack_buffer= nullptr;
}

void ha_rocksdb::set_use_read_free_rpl(const char* whitelist)
{
  DBUG_ASSERT(whitelist != nullptr);

#if defined(HAVE_PSI_INTERFACE)
  Regex_list_handler regex_handler(key_rwlock_read_free_rpl_tables);
#else
  Regex_list_handler regex_handler;
#endif

  if (!regex_handler.set_patterns(whitelist))
  {
    warn_about_bad_patterns(&regex_handler, "read_free_rpl_tables");
  }

  m_use_read_free_rpl = regex_handler.matches(m_tbl_def->base_tablename());
}

void ha_rocksdb::set_skip_unique_check(const char* whitelist)
{
  DBUG_ASSERT(whitelist != nullptr);

#if defined(HAVE_PSI_INTERFACE)
  Regex_list_handler regex_handler(key_rwlock_skip_unique_check);
#else
  Regex_list_handler regex_handler;
#endif

  if (!regex_handler.set_patterns(whitelist))
  {
    warn_about_bad_patterns(&regex_handler, "skip_unique_check");
  }

  m_skip_unique_check= regex_handler.matches(m_tbl_def->base_tablename());
}

int ha_rocksdb::open(const char *name, int mode, uint test_if_locked)
{
  DBUG_ENTER("ha_rocksdb::open");

  if (m_table_handler != nullptr)
  {
    rdb_open_tables.release_table_handler(m_table_handler);
  }
  m_table_handler= rdb_open_tables.get_table_handler(name);
  if (m_table_handler == nullptr)
  {
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  my_core::thr_lock_data_init(&m_table_handler->m_thr_lock, &m_db_lock,
                              nullptr);
  m_io_perf.init(&m_table_handler->m_table_perf_context,
                 &m_table_handler->m_io_perf_read, &stats);

  /*
    note: m_pk_descr may be non-NULL here, when using partitions. It seems,
    it's ok to discard it
  */
  std::string fullname;
  if (rdb_normalize_tablename(name, &fullname))
  {
    return HA_ERR_INTERNAL_ERROR;
  }

  m_tbl_def= ddl_manager.find(fullname);
  if (m_tbl_def == nullptr)
  {
    my_error(ER_INTERNAL_ERROR, MYF(0),
        "Attempt to open a table that is not present in RocksDB-SE data "
        "dictionary");
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }
  m_lock_rows= RDB_LOCK_NONE;

  m_key_descr_arr= m_tbl_def->m_key_descr_arr;

  /*
    Full table scan actually uses primary key
    (UPDATE needs to know this, otherwise it will go into infinite loop on
    queries like "UPDATE tbl SET pk=pk+100")
  */
  key_used_on_scan= table->s->primary_key;

  int err;
  if ((err = alloc_key_buffers(table, m_tbl_def)))
  {
    DBUG_RETURN(err);
  }

  setup_field_converters();

  info(HA_STATUS_NO_LOCK | HA_STATUS_VARIABLE | HA_STATUS_CONST);

  /*
    The following load_XXX code calls row decode functions, and they do
    that without having done ::external_lock() or index_init()/rnd_init().
    (Note: this also means we're doing a read when there was no
    setup_field_converters() call)

    Initialize the necessary variables for them:
  */
  m_verify_checksums= false;

  /* TODO: move the following to where TABLE_SHARE is opened: */
  if (table->found_next_number_field)
    load_auto_incr_value();

  if (has_hidden_pk(table) && load_hidden_pk_value())
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  /* Index block size in MyRocks: used by MySQL in query optimization */
  stats.block_size = rocksdb_tbl_options.block_size;

  /* Determine at open whether we can use Read Free Replication or not */
  set_use_read_free_rpl(THDVAR(ha_thd(), read_free_rpl_tables));

  /* Determine at open whether we should skip unique checks for this table */
  set_skip_unique_check(THDVAR(ha_thd(), skip_unique_check));

  DBUG_RETURN(0);
}


int ha_rocksdb::close(void)
{
  DBUG_ENTER("ha_rocksdb::close");

  m_pk_descr= nullptr;
  m_key_descr_arr= nullptr;

  free_key_buffers();

  my_free(m_encoder_arr);
  m_encoder_arr= nullptr;

  rdb_open_tables.release_table_handler(m_table_handler);
  m_table_handler= nullptr;

  DBUG_RETURN(0);
}

bool ha_rocksdb::get_error_message(const int error, String *buf)
{
  static_assert(HA_ERR_ROCKSDB_LAST > HA_ERR_FIRST,
    "HA_ERR_ROCKSDB_LAST > HA_ERR_FIRST");
  static_assert(HA_ERR_ROCKSDB_LAST > HA_ERR_LAST,
    "HA_ERR_ROCKSDB_LAST > HA_ERR_LAST");

  DBUG_ASSERT(error > 0);
  DBUG_ASSERT(error <= HA_ERR_ROCKSDB_LAST);
  DBUG_ASSERT(buf != nullptr);

  Rdb_transaction* tx = get_tx_from_thd(ha_thd());
  bool temp_error = false;

  switch (error) {
    case HA_ERR_ROCKSDB_PK_REQUIRED:
      buf->append("Table must have a PRIMARY KEY.");
      break;
    case HA_ERR_ROCKSDB_UNIQUE_NOT_SUPPORTED:
      buf->append("Unique indexes are not supported.");
      break;
    case HA_ERR_ROCKSDB_TOO_MANY_LOCKS:
      buf->append("Number of locks held reached @@rocksdb_max_row_locks.");
      break;
    case HA_ERR_LOCK_WAIT_TIMEOUT:
      DBUG_ASSERT(tx != nullptr);
      buf->append(tx->m_detailed_error);
      temp_error = true;
      break;
    default:
      // We can be called with the values which are < HA_ERR_FIRST because most
      // MySQL internal functions will just return 1 in case of an error.
      break;
  }

  return temp_error;
}

/* MyRocks supports only the following collations for indexed columns */
static const std::set<const my_core::CHARSET_INFO *> RDB_INDEX_COLLATIONS=
  {&my_charset_bin, &my_charset_utf8_bin, &my_charset_latin1_bin};


static bool rdb_is_index_collation_supported(const my_core::Field *field)
{
  my_core::enum_field_types type= field->real_type();
  /* Handle [VAR](CHAR|BINARY) or TEXT|BLOB */
  if (type == MYSQL_TYPE_VARCHAR || type == MYSQL_TYPE_STRING ||
      type == MYSQL_TYPE_BLOB)
  {
    return RDB_INDEX_COLLATIONS.find(field->charset()) !=
           RDB_INDEX_COLLATIONS.end();
  }
  return true;
}

/*
  Create structures needed for storing data in rocksdb. This is called when the
  table is created. The structures will be shared by all TABLE* objects.

  @param
    table_arg        Table with definition
    db_table         "dbname.tablename"
    len              strlen of the above
    tbl_def_arg      tbl_def whose key_descr is being created/populated
    old_tbl_def_arg  tbl_def from which keys are being copied over from
                     (for use during inplace alter)

  @return
    0      - Ok
    other  - error, either given table ddl is not supported by rocksdb or OOM.
*/
int ha_rocksdb::create_key_defs(const TABLE *table_arg,
                                Rdb_tbl_def *tbl_def_arg,
                                const TABLE *old_table_arg /* = nullptr */,
                                const Rdb_tbl_def *old_tbl_def_arg
                                /* = nullptr */)
{
  DBUG_ASSERT(table_arg != nullptr);
  DBUG_ASSERT(table_arg->s != nullptr);

  DBUG_ENTER("ha_rocksdb::create_key_defs");

  uint i;

  /*
    These need to be one greater than MAX_INDEXES since the user can create
    MAX_INDEXES secondary keys and no primary key which would cause us
    to generate a hidden one.
  */
  std::array<key_def_cf_info, MAX_INDEXES + 1> cfs;

  /*
    NOTE: All new column families must be created before new index numbers are
    allocated to each key definition. See below for more details.
    http://github.com/MySQLOnRocksDB/mysql-5.6/issues/86#issuecomment-138515501
  */
  if (create_cfs(table_arg, tbl_def_arg, &cfs))
  {
    DBUG_RETURN(1);
  };

  if (!old_tbl_def_arg)
  {
    /*
      old_tbl_def doesn't exist. this means we are in the process of creating
      a new table.

      Get the index numbers (this will update the next_index_number)
      and create Rdb_key_def structures.
    */
    for (i= 0; i < tbl_def_arg->m_key_count; i++)
    {
      if (create_key_def(table_arg, i, tbl_def_arg,
                         &m_key_descr_arr[i], cfs[i]))
      {
        DBUG_RETURN(1);
      }
    }
  }
  else
  {
    /*
      old_tbl_def exists.  This means we are creating a new tbl_def as part of
      in-place alter table.  Copy over existing keys from the old_tbl_def and
      generate the necessary new key definitions if any.
    */
    if (create_inplace_key_defs(table_arg, tbl_def_arg, old_table_arg,
                                old_tbl_def_arg, cfs))
    {
      DBUG_RETURN(1);
    }
  }

  DBUG_RETURN(0);
}

/*
  Checks index parameters and creates column families needed for storing data
  in rocksdb if necessary.

  @param in
    table_arg     Table with definition
    db_table      Table name
    tbl_def_arg   Table def structure being populated

  @param out
    cfs           CF info for each key definition in 'key_info' order

  @return
    0      - Ok
    other  - error
*/
int ha_rocksdb::create_cfs(const TABLE *table_arg, Rdb_tbl_def *tbl_def_arg,
                      std::array<struct key_def_cf_info, MAX_INDEXES + 1>* cfs)
{
  DBUG_ASSERT(table_arg != nullptr);
  DBUG_ASSERT(table_arg->s != nullptr);

  DBUG_ENTER("ha_rocksdb::create_cfs");

  char tablename_sys[NAME_LEN + 1];

  my_core::filename_to_tablename(tbl_def_arg->base_tablename().c_str(),
                                 tablename_sys, sizeof(tablename_sys));

  /*
    The first loop checks the index parameters and creates
    column families if necessary.
  */
  for (uint i= 0; i < tbl_def_arg->m_key_count; i++)
  {
    rocksdb::ColumnFamilyHandle* cf_handle;

    if (rocksdb_strict_collation_check &&
        !is_hidden_pk(i, table_arg, tbl_def_arg) &&
        tbl_def_arg->base_tablename().find(tmp_file_prefix) != 0)
    {
      for (uint part= 0; part < table_arg->key_info[i].actual_key_parts; part++)
      {
        if (!rdb_is_index_collation_supported(
            table_arg->key_info[i].key_part[part].field) &&
            !rdb_collation_exceptions->matches(tablename_sys))
        {
          std::string collation_err;
          for (auto coll : RDB_INDEX_COLLATIONS)
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
                          tbl_def_arg->full_tablename().c_str(),
                          table_arg->key_info[i].key_part[part].field->field_name,
                          collation_err.c_str());
          DBUG_RETURN(1);
        }
      }
    }

    /*
      index comment has Column Family name. If there was no comment, we get
      NULL, and it means use the default column family.
    */
    const char *comment = get_key_comment(i, table_arg, tbl_def_arg);
    const char *key_name = get_key_name(i, table_arg, tbl_def_arg);

    if (looks_like_per_index_cf_typo(comment))
    {
      my_error(ER_NOT_SUPPORTED_YET, MYF(0),
               "column family name looks like a typo of $per_index_cf");
      DBUG_RETURN(1);
    }
    /* Prevent create from using the system column family */
    if (comment && strcmp(DEFAULT_SYSTEM_CF_NAME, comment) == 0)
    {
      my_error(ER_WRONG_ARGUMENTS, MYF(0),
               "column family not valid for storing index data");
      DBUG_RETURN(1);
    }
    bool is_auto_cf_flag;
    cf_handle = cf_manager.get_or_create_cf(rdb, comment,
                                            tbl_def_arg->full_tablename(),
                                            key_name, &is_auto_cf_flag);
    if (!cf_handle)
      DBUG_RETURN(1);

    auto& cf = (*cfs)[i];
    cf.cf_handle = cf_handle;
    cf.is_reverse_cf = Rdb_cf_manager::is_cf_name_reverse(comment);
    cf.is_auto_cf = is_auto_cf_flag;
  }

  DBUG_RETURN(0);
}

/*
  Create key definition needed for storing data in rocksdb during ADD index
  inplace operations.

  @param in
    table_arg         Table with definition
    tbl_def_arg       New table def structure being populated
    old_tbl_def_arg   Old(current) table def structure
    cfs               Struct array which contains column family information

  @return
    0      - Ok
    other  - error, either given table ddl is not supported by rocksdb or OOM.
*/
int ha_rocksdb::create_inplace_key_defs(const TABLE *table_arg,
                       Rdb_tbl_def *tbl_def_arg,
                       const TABLE *old_table_arg,
                       const Rdb_tbl_def *old_tbl_def_arg,
                       const std::array<key_def_cf_info, MAX_INDEXES + 1>& cfs)
{
  DBUG_ASSERT(table_arg != nullptr);
  DBUG_ASSERT(tbl_def_arg != nullptr);
  DBUG_ASSERT(old_tbl_def_arg != nullptr);

  DBUG_ENTER("create_key_def");

  std::shared_ptr<Rdb_key_def>* old_key_descr=
      old_tbl_def_arg->m_key_descr_arr;
  std::shared_ptr<Rdb_key_def>* new_key_descr=
      tbl_def_arg->m_key_descr_arr;
  std::unordered_map<std::string, uint> old_key_pos =
    get_old_key_positions(table_arg, tbl_def_arg, old_table_arg,
                          old_tbl_def_arg);

  uint i;
  for (i= 0; i < tbl_def_arg->m_key_count; i++)
  {
    auto it = old_key_pos.find(get_key_name(i, table_arg, tbl_def_arg));
    if (it != old_key_pos.end())
    {
      /*
        Found matching index in old table definition, so copy it over to the
        new one created.
      */
      const std::shared_ptr<Rdb_key_def>& okd=
          old_key_descr[it->second];

      uint16 index_dict_version= 0;
      uchar index_type= 0;
      uint16 kv_version= 0;
      GL_INDEX_ID gl_index_id= okd->get_gl_index_id();
      if (!dict_manager.get_index_info(gl_index_id, &index_dict_version,
                           &index_type, &kv_version))
      {
        // NO_LINT_DEBUG
        sql_print_error("RocksDB: Could not get index information "
                        "for Index Number (%u,%u), table %s",
                        gl_index_id.cf_id, gl_index_id.index_id,
                        old_tbl_def_arg->full_tablename().c_str());
        DBUG_RETURN(1);
      }

      /*
        We can't use the copy constructor because we need to update the
        keynr within the pack_info for each field and the keyno of the keydef
        itself.
      */
      new_key_descr[i]= std::make_shared<Rdb_key_def>(
          okd->get_index_number(),
          i,
          okd->get_cf(),
          index_dict_version,
          index_type,
          kv_version,
          okd->m_is_reverse_cf,
          okd->m_is_auto_cf,
          okd->m_name.c_str(),
          dict_manager.get_stats(gl_index_id));
    }
    else if (create_key_def(table_arg, i, tbl_def_arg,
                            &new_key_descr[i], cfs[i]))
    {
      DBUG_RETURN(1);
    }

    DBUG_ASSERT(new_key_descr[i] != nullptr);
    new_key_descr[i]->setup(table_arg, tbl_def_arg);
  }

  DBUG_RETURN(0);
}

std::unordered_map<std::string, uint> ha_rocksdb::get_old_key_positions(
    const TABLE* table_arg,
    const Rdb_tbl_def* tbl_def_arg,
    const TABLE* old_table_arg,
    const Rdb_tbl_def* old_tbl_def_arg)
{
  DBUG_ASSERT(table_arg != nullptr);
  DBUG_ASSERT(old_table_arg != nullptr);
  DBUG_ASSERT(tbl_def_arg != nullptr);
  DBUG_ASSERT(old_tbl_def_arg != nullptr);

  DBUG_ENTER("get_old_key_positions");

  std::shared_ptr<Rdb_key_def>* old_key_descr=
      old_tbl_def_arg->m_key_descr_arr;
  std::unordered_map<std::string, uint> old_key_pos;
  std::unordered_map<std::string, uint> new_key_pos;
  uint i;

  for (i= 0; i < tbl_def_arg->m_key_count; i++)
  {
    new_key_pos[get_key_name(i, table_arg, tbl_def_arg)] = i;
  }

  for (i= 0; i < old_tbl_def_arg->m_key_count; i++)
  {
    if (is_hidden_pk(i, old_table_arg, old_tbl_def_arg))
    {
      old_key_pos[old_key_descr[i]->m_name] = i;
      continue;
    }

    /*
      In case of matching key name, need to check key parts of keys as well,
      in case a simultaneous drop + add is performed, where the key name is the
      same but the key parts are different.

      Example:
      CREATE TABLE t1 (a INT, b INT, KEY ka(a)) ENGINE=RocksDB;
      ALTER TABLE t1 DROP INDEX ka, ADD INDEX ka(b), ALGORITHM=INPLACE;
    */
    const KEY* old_key = &old_table_arg->key_info[i];
    auto it = new_key_pos.find(old_key->name);
    if (it == new_key_pos.end())
    {
      continue;
    }

    KEY* new_key = &table_arg->key_info[it->second];

    if (!compare_key_parts(old_key, new_key))
    {
      old_key_pos[old_key->name] = i;
    }
  }

  DBUG_RETURN(old_key_pos);
}

/* Check two keys to ensure that key parts within keys match */
int ha_rocksdb::compare_key_parts(const KEY* old_key, const KEY* new_key)
{
  DBUG_ASSERT(old_key != nullptr);
  DBUG_ASSERT(new_key != nullptr);

  DBUG_ENTER("compare_key_parts");

  /* Skip if key parts do not match, as it is a different key */
  if (new_key->user_defined_key_parts != old_key->user_defined_key_parts)
  {
    DBUG_RETURN(1);
  }

  /* Check to see that key parts themselves match */
  for (uint i= 0; i < old_key->user_defined_key_parts; i++)
  {
    if (strcmp(old_key->key_part[i].field->field_name,
               new_key->key_part[i].field->field_name) != 0)
    {
      DBUG_RETURN(1);
    }
  }

  DBUG_RETURN(0);
}

/*
  Create key definition needed for storing data in rocksdb.
  This can be called either during CREATE table or doing ADD index operations.

  @param in
    table_arg     Table with definition
    i             Position of index being created inside table_arg->key_info
    tbl_def_arg   Table def structure being populated
    cf_info       Struct which contains column family information

  @param out
    new_key_def  Newly created index definition.

  @return
    0      - Ok
    other  - error, either given table ddl is not supported by rocksdb or OOM.
*/
int ha_rocksdb::create_key_def(const TABLE *table_arg, uint i,
                               const Rdb_tbl_def* tbl_def_arg,
                               std::shared_ptr<Rdb_key_def>* new_key_def,
                               const struct key_def_cf_info& cf_info)
{
  DBUG_ENTER("create_key_def");
  DBUG_ASSERT(new_key_def != nullptr);
  DBUG_ASSERT(*new_key_def == nullptr);

  uint index_id= ddl_manager.get_and_update_next_number(&dict_manager);
  uint16_t index_dict_version= Rdb_key_def::INDEX_INFO_VERSION_LATEST;
  uchar index_type;
  uint16_t kv_version;

  if (is_hidden_pk(i, table_arg, tbl_def_arg))
  {
    index_type= Rdb_key_def::INDEX_TYPE_HIDDEN_PRIMARY;
    kv_version= Rdb_key_def::PRIMARY_FORMAT_VERSION_LATEST;
  }
  else if (i == table_arg->s->primary_key)
  {
    index_type= Rdb_key_def::INDEX_TYPE_PRIMARY;
    uint16 pk_latest_version= Rdb_key_def::PRIMARY_FORMAT_VERSION_LATEST;
    DBUG_EXECUTE_IF("MYROCKS_FORMAT_VERSION_INITIAL",
                    {pk_latest_version=
                       Rdb_key_def::PRIMARY_FORMAT_VERSION_INITIAL;
                    });
    kv_version= pk_latest_version;
  }
  else
  {
    index_type= Rdb_key_def::INDEX_TYPE_SECONDARY;
    uint16 sk_latest_version= Rdb_key_def::SECONDARY_FORMAT_VERSION_LATEST;
    DBUG_EXECUTE_IF("MYROCKS_FORMAT_VERSION_INITIAL",
                    {sk_latest_version=
                       Rdb_key_def::SECONDARY_FORMAT_VERSION_INITIAL;
                    });
    kv_version= sk_latest_version;
  }

  const char *key_name = get_key_name(i, table_arg, m_tbl_def);
  *new_key_def= std::make_shared<Rdb_key_def>(
      index_id, i, cf_info.cf_handle, index_dict_version, index_type,
      kv_version, cf_info.is_reverse_cf, cf_info.is_auto_cf, key_name);

  DBUG_RETURN(0);
}

int rdb_normalize_tablename(const std::string& tablename,
                            std::string* strbuf)
{
  DBUG_ASSERT(strbuf != nullptr);

  if (tablename.size() < 2 || tablename[0] != '.' || tablename[1] != '/')
  {
    DBUG_ASSERT(0);  // We were not passed table name?
    return HA_ERR_INTERNAL_ERROR;
  }

  size_t pos = tablename.find_first_of('/', 2);
  if (pos == std::string::npos)
  {
    DBUG_ASSERT(0);  // We were not passed table name?
    return HA_ERR_INTERNAL_ERROR;
  }

  *strbuf = tablename.substr(2, pos - 2) + "." + tablename.substr(pos + 1);

  return 0;
}

/*
  Check to see if the user's original statement includes foreign key
  references
*/
bool ha_rocksdb::contains_foreign_key(THD* thd)
{
  bool success;
  const char* str = thd_query_string(thd)->str;

  DBUG_ASSERT(str != nullptr);

  while (*str != '\0')
  {
    // Scan from our current pos looking for 'FOREIGN'
    str = rdb_find_in_string(str, "FOREIGN", &success);
    if (!success)
    {
      return false;
    }

    // Skip past the found "FOREIGN'
    str = rdb_check_next_token(&my_charset_bin, str, "FOREIGN", &success);
    DBUG_ASSERT(success);

    if (!my_isspace(&my_charset_bin, *str))
    {
      return false;
    }

    // See if the next token is 'KEY'
    str = rdb_check_next_token(&my_charset_bin, str, "KEY", &success);
    if (!success)
    {
      continue;
    }

    // See if the next token is '('
    str = rdb_check_next_token(&my_charset_bin, str, "(", &success);
    if (!success)
    {
      // There is an optional index id after 'FOREIGN KEY', skip it
      str = rdb_skip_id(&my_charset_bin, str);

      // Now check for '(' again
      str = rdb_check_next_token(&my_charset_bin, str, "(", &success);
    }

    // If we have found 'FOREIGN KEY [<word>] (' we can be confident we have
    // a foreign key clause.
    return success;
  }

  // We never found a valid foreign key clause
  return false;
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
int rdb_split_normalized_tablename(const std::string& fullname,
                                   std::string* db,
                                   std::string* table,
                                   std::string* partition)
{
  DBUG_ASSERT(!fullname.empty());

#define RDB_PARTITION_STR "#P#"

  /* Normalize returns dbname.tablename. */
  size_t dotpos = fullname.find('.');

  /* Invalid table name? */
  if (dotpos == std::string::npos)
  {
    return HA_ERR_INTERNAL_ERROR;
  }

  // Table must have a database name associated with it.
  DBUG_ASSERT(dotpos > 0);

  if (db != nullptr)
  {
    *db = fullname.substr(0, dotpos);
  }

  dotpos++;

  size_t partpos = fullname.find(RDB_PARTITION_STR, dotpos,
    strlen(RDB_PARTITION_STR));

  if (partpos != std::string::npos)
  {
    DBUG_ASSERT(partpos >= dotpos);

    if (table != nullptr)
    {
      *table = fullname.substr(dotpos, partpos - dotpos);
    }

    if (partition != nullptr)
    {
      *partition = fullname.substr(partpos + strlen(RDB_PARTITION_STR));
    }
  }
  else if (table != nullptr)
  {
    *table = fullname.substr(dotpos);
  }

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
  std::string str;
  DBUG_ENTER("ha_rocksdb::create");

  THD *thd = my_core::thd_get_current_thd();

  if (get_table_if_exists(name)) {
    if (thd->lex->sql_command == SQLCOM_TRUNCATE) {
      if (delete_table(name)) {
        DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
      }
    } else {
      if (rdb_normalize_tablename(name, &str)) {
        DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
      }
      my_printf_error(ER_UNKNOWN_ERROR,
        "Table '%s' does not exist, but metadata information exists inside "
        "MyRocks. This is a sign of data inconsistency. Please check if "
        "'%s.frm' exists, and try to restore it if it does not exist.",
        MYF(0), str.c_str(), name);
      DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
    }
  }

  /*
    Construct dbname.tablename ourselves, because parititioning
    passes strings like "./test/t14#P#p0" for individual partitions,
    while table_arg->s->table_name has none of that.
  */
  if (rdb_normalize_tablename(name, &str))
  {
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  if (contains_foreign_key(thd))
  {
    my_printf_error(ER_NOT_SUPPORTED_YET, "MyRocks does not currently support "
        "foreign key constraints", MYF(0));
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

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

  bool write_err= false;
  std::unique_ptr<rocksdb::WriteBatch> wb= dict_manager.begin();
  rocksdb::WriteBatch *batch= wb.get();

  /* Create table/key descriptions and put them into the data dictionary */
  m_tbl_def= new Rdb_tbl_def(str);

  uint n_keys= table_arg->s->keys;

  /*
    If no primary key found, create a hidden PK and place it inside table
    definition
  */
  if (has_hidden_pk(table_arg))
  {
    n_keys += 1;
  }

  m_key_descr_arr= new std::shared_ptr<Rdb_key_def>[n_keys];
  m_tbl_def->m_key_count= n_keys;
  m_tbl_def->m_key_descr_arr= m_key_descr_arr;

  if ((res= create_key_defs(table_arg, m_tbl_def)))
  {
    goto error;
  }

  m_pk_descr= m_key_descr_arr[pk_index(table_arg, m_tbl_def)];

  dict_manager.lock();
  write_err= ddl_manager.put_and_write(m_tbl_def, batch)
             || dict_manager.commit(batch);
  dict_manager.unlock();

  if (write_err)
  {
    goto error;
  }

  if (create_info->auto_increment_value)
     m_tbl_def->m_auto_incr_val= create_info->auto_increment_value;

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

error:
  /* Delete what we have allocated so far */
  delete m_tbl_def;
  m_tbl_def= nullptr;
  m_key_descr_arr= nullptr;

  DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
}


/**
  @note
  This function is used only when the table has not yet been opened, and
  keyread_allowed bitmap doesn't have the correct values yet.

  See comment in ha_rocksdb::index_flags() for details.
*/

bool ha_rocksdb::check_keyread_allowed(uint inx, uint part, bool all_parts) const
{
  bool res= true;
  KEY *key_info= &table_share->key_info[inx];

  Rdb_field_packing dummy1;
  res= dummy1.setup(nullptr, key_info->key_part[part].field, inx, part,
                    key_info->key_part[part].length);

  if (res && all_parts)
  {
    for (uint i= 0; i < part; i++)
    {
      Field *field;
      if ((field= key_info->key_part[i].field))
      {
        Rdb_field_packing dummy;
        if (!dummy.setup(nullptr, field, inx, i, key_info->key_part[i].length))
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
      part + 1 == table_share->key_info[pk].user_defined_key_parts)
  {
    m_pk_can_be_decoded= res;
  }

  return res;
}

int ha_rocksdb::read_key_exact(const std::shared_ptr<const Rdb_key_def>& kd,
                               rocksdb::Iterator *iter,
                               bool full_key_match,
                               const rocksdb::Slice& key_slice) const
{
  DBUG_ASSERT(kd != nullptr);
  DBUG_ASSERT(iter != nullptr);

  /*
    We are looking for the first record such that
      index_tuple= lookup_tuple.
    lookup_tuple may be a prefix of the index.
  */
  if (kd->m_is_reverse_cf)
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

int ha_rocksdb::read_before_key(const std::shared_ptr<const Rdb_key_def>& kd,
                                bool full_key_match,
                                const rocksdb::Slice& key_slice)
{
  DBUG_ASSERT(kd != nullptr);

  /*
    We are looking for record with the biggest t.key such that
    t.key < lookup_tuple.
  */
  if (kd->m_is_reverse_cf)
  {
    if (m_scan_it->Valid() && full_key_match &&
        kd->value_matches_prefix(m_scan_it->key(), key_slice))
    {
      /* We are using full key and we've hit an exact match */
      m_scan_it->Next();
    }
  }
  else
  {
    if (m_scan_it->Valid())
      m_scan_it->Prev();
    else
      m_scan_it->SeekToLast();
  }

  return m_scan_it->Valid() ? 0 : HA_ERR_KEY_NOT_FOUND;
}

int ha_rocksdb::read_after_key(const std::shared_ptr<const Rdb_key_def>& kd,
                               bool full_key_match,
                               const rocksdb::Slice& key_slice)
{
  DBUG_ASSERT(kd != nullptr);

  /*
    We are looking for the first record such that

      index_tuple $GT lookup_tuple

    with HA_READ_AFTER_KEY, $GT = '>',
    with HA_READ_KEY_OR_NEXT, $GT = '>='
  */
  if (kd->m_is_reverse_cf)
  {
    if (!m_scan_it->Valid())
    {
      m_scan_it->SeekToLast();
    }
    else
    {
      /*
        We should step back
         - when not using full extended key
         - when using full extended key and when we've got an exact match
      */
      if (!full_key_match ||
          !kd->value_matches_prefix(m_scan_it->key(), key_slice))
      {
        m_scan_it->Prev();
      }
    }
  }

  return m_scan_it->Valid() ? 0 : HA_ERR_KEY_NOT_FOUND;
}

int ha_rocksdb::position_to_correct_key(
    const std::shared_ptr<const Rdb_key_def>& kd,
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
    rc= read_key_exact(kd, m_scan_it, full_key_match, key_slice);
    break;
  case HA_READ_BEFORE_KEY:
    *move_forward= false;
    rc= read_before_key(kd, full_key_match, key_slice);
    if (rc == 0 && !kd->covers_key(m_scan_it->key()))
    {
      /* The record we've got is not from this index */
      rc= HA_ERR_KEY_NOT_FOUND;
    }
    break;
  case HA_READ_AFTER_KEY:
  case HA_READ_KEY_OR_NEXT:
    rc= read_after_key(kd, full_key_match, key_slice);
    if (rc == 0 && !kd->covers_key(m_scan_it->key()))
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
      const rocksdb::Slice& rkey= m_scan_it->key();
      if (!kd->covers_key(rkey))
      {
        /* The record we've got is not from this index */
        rc= HA_ERR_KEY_NOT_FOUND;
      }
      else if (find_flag == HA_READ_PREFIX_LAST)
      {
        uint size = kd->pack_index_tuple(table, m_pack_buffer,
                                         m_sk_packed_tuple, key,
                                         keypart_map);
        rocksdb::Slice lookup_tuple(
            reinterpret_cast<char*>(m_sk_packed_tuple), size);

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

int ha_rocksdb::calc_eq_cond_len(const std::shared_ptr<const Rdb_key_def>& kd,
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
      We have made the kd->successor(m_sk_packed_tuple) call above.

      The slice is at least Rdb_key_def::INDEX_NUMBER_SIZE bytes long.
    */
    return slice.size() - bytes_changed_by_succ;
  }

  if (end_key)
  {
    *end_key_packed_size= kd->pack_index_tuple(table, m_pack_buffer,
                                               m_end_key_packed_tuple,
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
    rocksdb::Slice end_slice(reinterpret_cast<char*>(m_end_key_packed_tuple),
                             *end_key_packed_size);
    return slice.difference_offset(end_slice);
  }

  /*
    On range scan without any end key condition, there is no
    eq cond, and eq cond length is the same as index_id size (4 bytes).
    Example1: id1 BIGINT, id2 INT, id3 BIGINT, PRIMARY KEY (id1, id2, id3)
     WHERE id1>=1 AND id2 >= 2 and id2 <= 5 => eq_cond_len= 4
  */
  return Rdb_key_def::INDEX_NUMBER_SIZE;
}

int ha_rocksdb::read_row_from_primary_key(uchar *buf)
{
  DBUG_ASSERT(buf != nullptr);

  int rc;
  const rocksdb::Slice& rkey= m_scan_it->key();
  uint pk_size= rkey.size();
  const char *pk_data= rkey.data();

  memcpy(m_pk_packed_tuple, pk_data, pk_size);
  m_last_rowkey.copy(pk_data, pk_size, &my_charset_bin);

  if (m_lock_rows != RDB_LOCK_NONE)
  {
    /* We need to put a lock and re-read */
    rc= get_row_by_rowid(buf, m_pk_packed_tuple, pk_size);
  }
  else
  {
    /* Unpack from the row we've read */
    const rocksdb::Slice& value = m_scan_it->value();
    rc= convert_record_from_storage_format(&rkey, &value, buf);
  }

  return rc;
}

int ha_rocksdb::read_row_from_secondary_key(uchar *buf,
    const std::shared_ptr<const Rdb_key_def>& kd, bool move_forward)
{
  DBUG_ASSERT(buf != nullptr);
  DBUG_ASSERT(kd != nullptr);

  int rc= 0;
  uint pk_size;

  if (m_keyread_only && m_lock_rows == RDB_LOCK_NONE && !has_hidden_pk(table))
  {
    /* Get the key columns and primary key value */
    const rocksdb::Slice& rkey= m_scan_it->key();
    pk_size= kd->get_primary_key_tuple(table, m_pk_descr, &rkey,
                                       m_pk_packed_tuple);
    const rocksdb::Slice& value= m_scan_it->value();
    if (pk_size == RDB_INVALID_KEY_LEN)
    {
      rc= HA_ERR_INTERNAL_ERROR;
    }
    else
    {
      rc= try_keyonly_read_from_sk(buf, kd, rkey, value, pk_size);
    }
  }
  else
  {
    if (kd->m_is_reverse_cf)
      move_forward= !move_forward;

    rc= find_icp_matching_index_rec(move_forward, buf);
    if (!rc)
    {
      const rocksdb::Slice& rkey= m_scan_it->key();
      pk_size= kd->get_primary_key_tuple(table, m_pk_descr, &rkey,
                                         m_pk_packed_tuple);
      if (pk_size == RDB_INVALID_KEY_LEN)
      {
        rc= HA_ERR_INTERNAL_ERROR;
      }
      else
      {
        rc= get_row_by_rowid(buf, m_pk_packed_tuple, pk_size);
      }
    }
  }

  if (!rc)
  {
    m_last_rowkey.copy((const char*)m_pk_packed_tuple, pk_size,
                       &my_charset_bin);
  }

  return rc;
}

/**
  @note
    Some data types require unpack_info in order to do index-only reads.

    However, support for this may have been added after the database was built.
    This means that we need to support the case where unpack_info is not
    available. In this case, we should just do a primary key lookup.
*/
int ha_rocksdb::try_keyonly_read_from_sk(uchar* buf,
    const std::shared_ptr<const Rdb_key_def>& kd,
    const rocksdb::Slice& key,
    const rocksdb::Slice& value,
    const uint rowid_size)
{
  int res= kd->unpack_record(table, buf, &key, &value, m_verify_checksums);
  uint size= rowid_size;
  if (res == UNPACK_INFO_MISSING)
  {
    // If we can't unpack, then do a PK lookup.
    if (rowid_size == RDB_INVALID_KEY_LEN)
    {
      size= kd->get_primary_key_tuple(
              table, m_pk_descr, &key, m_pk_packed_tuple);
    }
    return get_row_by_rowid(buf, m_pk_packed_tuple, size);
  }
  else if (res == UNPACK_FAILURE)
  {
    return HA_ERR_INTERNAL_ERROR;
  }
  DBUG_ASSERT(res == UNPACK_SUCCESS);
  return res;
}


/**
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


/**
  @brief
  Read next index tuple through the secondary index.

  @details
  m_scan_it points at the index key-value pair that we should read the (pk,row)
  pair for.
*/
int ha_rocksdb::secondary_index_read(const int keyno, uchar *buf)
{
  DBUG_ASSERT(buf != nullptr);
  DBUG_ASSERT(table != nullptr);

  stats.rows_requested++;

  /* Use STATUS_NOT_FOUND when record not found or some error occurred */
  table->status= STATUS_NOT_FOUND;

  if (m_scan_it->Valid())
  {
    rocksdb::Slice key= m_scan_it->key();

    /* Check if we've ran out of records of this index */
    if (m_key_descr_arr[keyno]->covers_key(key))
    {
      int rc;

      //TODO: We could here check if we have ran out of range we're scanning
      uint size= m_key_descr_arr[keyno]->get_primary_key_tuple(
          table, m_pk_descr, &key, m_pk_packed_tuple);
      if (size == RDB_INVALID_KEY_LEN)
      {
        return HA_ERR_INTERNAL_ERROR;
      }

      m_last_rowkey.copy((const char*)m_pk_packed_tuple, size, &my_charset_bin);

      if (m_keyread_only && m_lock_rows == RDB_LOCK_NONE &&
          !has_hidden_pk(table))
      {
        rc= try_keyonly_read_from_sk(buf, m_key_descr_arr[keyno],
                                     key, m_scan_it->value(), size);
      }
      else
      {
        rc= get_row_by_rowid(buf, m_pk_packed_tuple, size);
      }

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
    if (is_using_prohibited_gap_locks(table,
                                      is_using_full_unique_key(
                                        active_index,
                                        start_key->keypart_map,
                                        start_key->flag)))
    {
      DBUG_RETURN(HA_ERR_LOCK_DEADLOCK);
    }

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

  ha_statistic_increment(&SSV::ha_read_key_count);
  const std::shared_ptr<const Rdb_key_def>& kd= m_key_descr_arr[active_index];
  uint actual_key_parts= kd->get_key_parts();
  bool using_full_key= is_using_full_key(keypart_map, actual_key_parts);

  if (!end_key)
    end_key= end_range;

  /* By default, we don't need the retrieved records to match the prefix */
  m_sk_match_prefix= nullptr;
  stats.rows_requested++;

  if (active_index == table->s->primary_key &&
      find_flag == HA_READ_KEY_EXACT && using_full_key)
  {
    /*
      Equality lookup over primary key, using full tuple.
      This is a special case, use DB::Get.
    */
    uint size= kd->pack_index_tuple(table, m_pack_buffer, m_pk_packed_tuple,
                                    key, keypart_map);
    rc= get_row_by_rowid(buf, m_pk_packed_tuple, size);
    if (!rc)
    {
      stats.rows_read++;
      stats.rows_index_first++;
      update_row_stats(ROWS_READ);
    }
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
    packed_size= kd->pack_index_tuple(table, m_pack_buffer, m_sk_packed_tuple,
                                      key, tmp_map);
    if (table->key_info[active_index].user_defined_key_parts !=
        kd->get_key_parts())
      using_full_key= false;
  }
  else
  {
    packed_size= kd->pack_index_tuple(table, m_pack_buffer, m_sk_packed_tuple,
                                      key, keypart_map);
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
    m_sk_match_prefix= m_sk_match_prefix_buf;
    m_sk_match_length= packed_size;
    memcpy(m_sk_match_prefix, m_sk_packed_tuple, packed_size);
  }

  int bytes_changed_by_succ= 0;
  if (find_flag == HA_READ_PREFIX_LAST_OR_PREV ||
      find_flag == HA_READ_PREFIX_LAST ||
      find_flag == HA_READ_AFTER_KEY)
  {
    /* See below */
    bytes_changed_by_succ= kd->successor(m_sk_packed_tuple, packed_size);
  }

  rocksdb::Slice slice(
      reinterpret_cast<const char*>(m_sk_packed_tuple), packed_size);

  uint end_key_packed_size= 0;
  uint eq_cond_len= calc_eq_cond_len(kd, find_flag, slice,
                                     bytes_changed_by_succ, end_key,
                                     &end_key_packed_size);

  bool use_all_keys= false;
  if (find_flag == HA_READ_KEY_EXACT &&
      my_count_bits(keypart_map) == kd->get_key_parts())
    use_all_keys= true;

  Rdb_transaction *tx= get_or_create_tx(table->in_use);
  bool is_new_snapshot= !tx->has_snapshot();
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
      DBUG_RETURN(rc);
    }

    m_skip_scan_it_next_call= false;

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
    tx->release_snapshot();
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
  DBUG_RETURN(rc);
}


/*
  @brief
  Scan the secondary index until we find an index record that satisfies ICP

  @param move_forward   TRUE  <=> move m_scan_it forward
                        FALSE <=> move m_scan_it backward
  @param buf            Record buffer (must be the same buffer that
                        pushed index condition points to, in practice
                        it is table->record[0])

  @detail
  Move the current iterator m_scan_it until we get an index tuple that
  satisfies the pushed Index Condition.
  (if there is no pushed index condition, return right away)

  @return
    0     - Index tuple satisfies ICP, can do index read.
    other - error code
*/

int ha_rocksdb::find_icp_matching_index_rec(bool move_forward, uchar *buf)
{
  DBUG_ASSERT(buf != nullptr);

  if (pushed_idx_cond && pushed_idx_cond_keyno == active_index)
  {
    const std::shared_ptr<const Rdb_key_def>& kd= m_key_descr_arr[active_index];
    DBUG_ASSERT(kd != nullptr);

    while (1)
    {
      if (!m_scan_it->Valid())
      {
        table->status= STATUS_NOT_FOUND;
        return HA_ERR_END_OF_FILE;
      }
      rocksdb::Slice rkey= m_scan_it->key();

      if (!kd->covers_key(rkey))
      {
        table->status= STATUS_NOT_FOUND;
        return HA_ERR_END_OF_FILE;
      }

      if (m_sk_match_prefix)
      {
        rocksdb::Slice prefix((const char*)m_sk_match_prefix,
                              m_sk_match_length);
        if (!kd->value_matches_prefix(rkey, prefix))
        {
          table->status= STATUS_NOT_FOUND;
          return HA_ERR_END_OF_FILE;
        }
      }

      rocksdb::Slice value= m_scan_it->value();
      if (try_keyonly_read_from_sk(buf, kd, rkey, value, RDB_INVALID_KEY_LEN))
      {
        return HA_ERR_INTERNAL_ERROR;
      }

      enum icp_result icp_status= check_index_cond();
      if (icp_status == ICP_NO_MATCH)
      {
        if (move_forward)
          m_scan_it->Next();
        else
          m_scan_it->Prev();
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
        if (my_core::thd_killed(current_thd))
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


int ha_rocksdb::check(THD* thd, HA_CHECK_OPT* check_opt)
{
  uint pk= pk_index(table, m_tbl_def);
  String rowkey_copy;
  String sec_key_copy;
  const char* table_name = table->s->table_name.str;

  // Only when debugging: don't use snapshot when reading
  // Rdb_transaction *tx= get_or_create_tx(table->in_use);
  // tx->snapshot= nullptr;

  bool save_verify_checksums= m_verify_checksums;
  m_verify_checksums= true;

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
        row_checksums_at_start= m_row_checksums_checked;
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
        rocksdb::Slice key= m_scan_it->key();
        sec_key_copy.copy(key.data(), key.size(), &my_charset_bin);
        rowkey_copy.copy(m_last_rowkey.ptr(), m_last_rowkey.length(),
                         &my_charset_bin);

        if (m_key_descr_arr[keyno]->
              unpack_info_has_checksum(m_scan_it->value()))
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
        uint packed_size= m_pk_descr->pack_record(
            table, m_pack_buffer, table->record[0], m_pk_packed_tuple, nullptr,
            false, hidden_pk_id);
        if (packed_size != rowkey_copy.length() ||
            memcmp(m_pk_packed_tuple, rowkey_copy.ptr(), packed_size))
        {
          // NO_LINT_DEBUG
          sql_print_error("CHECKTABLE %s:   .. row %lld: PK value mismatch",
                          table_name, rows);
          goto print_and_error;
        }

        /* Check if we get the same secondary key value */
        packed_size= m_key_descr_arr[keyno]->pack_record(
            table, m_pack_buffer, table->record[0], m_sk_packed_tuple,
            &m_sk_tails, false, hidden_pk_id);
        if (packed_size != sec_key_copy.length() ||
            memcmp(m_sk_packed_tuple, sec_key_copy.ptr(), packed_size))
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
          std::string buf;
          buf = rdb_hexdump(rowkey_copy.ptr(), rowkey_copy.length(), 1000);
          // NO_LINT_DEBUG
          sql_print_error("CHECKTABLE %s:   rowkey: %s", table_name,
                          buf.c_str());

          buf= rdb_hexdump(m_retrieved_record.data(), m_retrieved_record.size(),
                           1000);
          // NO_LINT_DEBUG
          sql_print_error("CHECKTABLE %s:   record: %s", table_name,
                          buf.c_str());

          buf = rdb_hexdump(sec_key_copy.ptr(), sec_key_copy.length(), 1000);
          // NO_LINT_DEBUG
          sql_print_error("CHECKTABLE %s:   index: %s", table_name,
                          buf.c_str());

          goto error;
        }
      }
      // NO_LINT_DEBUG
      sql_print_information("CHECKTABLE %s:   ... %lld index entries checked "
                            "(%lld had checksums)", table_name, rows,
                            checksums);

      if (first_index)
      {
        row_checksums= m_row_checksums_checked - row_checksums_at_start;
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

  m_verify_checksums= save_verify_checksums;
  /*
    TODO: we should check also for PK records that are missing in the secondary
    indexes.
    For that, need to walk through the PK and check that every PK record has a
    proper counterpart in each secondary index.
  */
  return HA_ADMIN_OK;
error:
  m_verify_checksums= save_verify_checksums;
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

int ha_rocksdb::get_row_by_rowid(uchar *buf, const char *rowid,
                                 const uint rowid_size)
{
  DBUG_ASSERT(buf != nullptr);
  DBUG_ASSERT(rowid != nullptr);
  DBUG_ASSERT(table != nullptr);

  int rc;
  DBUG_ENTER("ha_rocksdb::get_row_by_rowid");

  rocksdb::Slice key_slice(rowid, rowid_size);

  Rdb_transaction *tx= get_or_create_tx(table->in_use);
  DBUG_ASSERT(tx != nullptr);

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

  if (m_lock_rows == RDB_LOCK_NONE)
  {
    tx->acquire_snapshot(true);
    s= tx->get(m_pk_descr->get_cf(), key_slice, &m_retrieved_record);
  }
  else
    s= tx->get_for_update(m_pk_descr->get_cf(), key_slice, &m_retrieved_record);

  if (!s.IsNotFound() && !s.ok())
  {
    DBUG_RETURN(tx->set_status_error(table->in_use, s, m_pk_descr, m_tbl_def));
  }
  found= !s.IsNotFound();

  table->status= STATUS_NOT_FOUND;
  if (found)
  {
    m_last_rowkey.copy((const char*)rowid, rowid_size, &my_charset_bin);
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
  ha_statistic_increment(&SSV::ha_read_next_count);
  if (m_key_descr_arr[active_index]->m_is_reverse_cf)
  {
    moves_forward= false;
  }

  int rc= index_next_with_direction(buf, moves_forward);
  if (rc == HA_ERR_KEY_NOT_FOUND)
    rc = HA_ERR_END_OF_FILE;
  return rc;
}


int ha_rocksdb::index_prev(uchar *buf)
{
  bool moves_forward= false;
  ha_statistic_increment(&SSV::ha_read_prev_count);
  if (m_key_descr_arr[active_index]->m_is_reverse_cf)
  {
    moves_forward= true;
  }

  int rc= index_next_with_direction(buf, moves_forward);
  if (rc == HA_ERR_KEY_NOT_FOUND)
    rc = HA_ERR_END_OF_FILE;

  return rc;
}


int ha_rocksdb::index_next_with_direction(uchar *buf, bool move_forward)
{
  int rc;
  DBUG_ENTER("ha_rocksdb::index_next");

  if (active_index == pk_index(table, m_tbl_def))
  {
    rc= rnd_next_with_direction(buf, move_forward);
  }
  else
  {
    if (m_skip_scan_it_next_call)
    {
      m_skip_scan_it_next_call= false;
    }
    else
    {
      if (move_forward)
        m_scan_it->Next();  /* this call cannot fail */
      else
        m_scan_it->Prev();
    }
    rc= find_icp_matching_index_rec(move_forward, buf);
    if (!rc)
      rc= secondary_index_read(active_index, buf);
  }

  DBUG_RETURN(rc);
}


int ha_rocksdb::index_first(uchar *buf)
{
  m_sk_match_prefix= nullptr;
  ha_statistic_increment(&SSV::ha_read_first_count);
  int rc= m_key_descr_arr[active_index]->m_is_reverse_cf ?
      index_last_intern(buf) : index_first_intern(buf);
  if (rc == HA_ERR_KEY_NOT_FOUND)
    rc = HA_ERR_END_OF_FILE;
  return rc;
}


int ha_rocksdb::index_last(uchar *buf)
{
  m_sk_match_prefix= nullptr;
  ha_statistic_increment(&SSV::ha_read_last_count);
  int rc= m_key_descr_arr[active_index]->m_is_reverse_cf ?
      index_first_intern(buf) : index_last_intern(buf);
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
  DBUG_ASSERT(buf != nullptr);

  uchar *key;
  uint key_size;
  int rc;
  DBUG_ENTER("ha_rocksdb::index_first");

  if (is_pk(active_index, table, m_tbl_def))
  {
    key= m_pk_packed_tuple;
  }
  else
  {
    key= m_sk_packed_tuple;
  }

  DBUG_ASSERT(key != nullptr);

  const std::shared_ptr<const Rdb_key_def>& kd= m_key_descr_arr[active_index];
  if (kd->m_is_reverse_cf)
  {
    kd->get_supremum_key(key, &key_size);
  }
  else
  {
    kd->get_infimum_key(key, &key_size);
  }

  rocksdb::Slice index_key((const char*)key, key_size);

  Rdb_transaction *tx= get_or_create_tx(table->in_use);
  DBUG_ASSERT(tx != nullptr);

  bool is_new_snapshot= !tx->has_snapshot();
  // Loop as long as we get a deadlock error AND we end up creating the
  // snapshot here (i.e. it did not exist prior to this)
  for (;;)
  {
    setup_scan_iterator(kd, &index_key, false, !kd->m_is_reverse_cf,
                        Rdb_key_def::INDEX_NUMBER_SIZE);
    m_skip_scan_it_next_call= true;

    rc= index_next_with_direction(buf, true);
    if (rc != HA_ERR_LOCK_DEADLOCK || !is_new_snapshot)
      break;  // exit the loop

    // release the snapshot and iterator so they will be regenerated
    tx->release_snapshot();
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


/**
  @details
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
  DBUG_ASSERT(buf != nullptr);

  uchar *key;
  uint key_size;
  int rc;
  DBUG_ENTER("ha_rocksdb::index_last");

  if (is_pk(active_index, table, m_tbl_def))
  {
    key= m_pk_packed_tuple;
  }
  else
  {
    key= m_sk_packed_tuple;
  }

  DBUG_ASSERT(key != nullptr);

  const std::shared_ptr<const Rdb_key_def>& kd= m_key_descr_arr[active_index];
  if (kd->m_is_reverse_cf)
  {
    kd->get_infimum_key(key, &key_size);
  }
  else
  {
    kd->get_supremum_key(key, &key_size);
  }

  rocksdb::Slice index_key((const char*)key, key_size);

  Rdb_transaction *tx= get_or_create_tx(table->in_use);
  DBUG_ASSERT(tx != nullptr);

  bool is_new_snapshot= !tx->has_snapshot();
  // Loop as long as we get a deadlock error AND we end up creating the
  // snapshot here (i.e. it did not exist prior to this)
  for (;;)
  {
    setup_scan_iterator(kd, &index_key, false, kd->m_is_reverse_cf,
                        Rdb_key_def::INDEX_NUMBER_SIZE);

    if (!m_scan_it->Valid())
    {
      /*
        RocksDB keyspace has no records that follow our index. It could be, our
        index is the last in the keyspace. Navigate to last record in the
        keyspace.
      */
      m_scan_it->SeekToLast();
    }
    else
    {
      /*
        Iterator is pointing at some record right after the end of the index.
        Move one record back.
      */
      m_scan_it->Prev();
    }
    m_skip_scan_it_next_call= false;

    if (is_pk(active_index, table, m_tbl_def))
    {
      m_skip_scan_it_next_call= true;
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
    tx->release_snapshot();
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
  if (m_lock_rows != RDB_LOCK_NONE)
  {
    Rdb_transaction *tx= get_or_create_tx(table->in_use);
    tx->release_lock(m_pk_descr->get_cf(), std::string(m_last_rowkey.ptr(),
                                                     m_last_rowkey.length()));
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
  return (index != pk_index(table, m_tbl_def) ||
          (!has_hidden_pk(table) &&
          table->key_info[index].actual_key_parts == table->s->fields));
}

bool ha_rocksdb::skip_unique_check()
{
  /*
    We want to skip unique checks if:
      1) bulk_load is on
      2) this table is in the whitelist of tables to skip and the replication
         lag has reached a large enough value (see unique_check_lag_threshold
         and unique_check_lage_reset_threshold)
      3) the user set rocksdb_debug_skip_unique_check
  */
  return THDVAR(table->in_use, bulk_load) ||
         (m_force_skip_unique_check && m_skip_unique_check) ||
         THDVAR(table->in_use, debug_skip_unique_check);
}

void ha_rocksdb::set_force_skip_unique_check(bool skip)
{
  m_force_skip_unique_check= skip;
}

bool ha_rocksdb::commit_in_the_middle()
{
  return THDVAR(table->in_use, bulk_load) ||
         THDVAR(table->in_use, commit_in_the_middle);
}

/*
  Executing bulk commit if it should.
  @retval true if bulk commit failed
  @retval false if bulk commit was skipped or succeeded
*/
bool ha_rocksdb::do_bulk_commit(Rdb_transaction *tx)
{
  DBUG_ASSERT(tx != nullptr);
  return commit_in_the_middle() &&
         tx->get_write_count() >= THDVAR(table->in_use, bulk_load_size) &&
         tx->flush_batch();
}

/*
  If table was created without primary key, SQL layer represents the primary
  key number as MAX_INDEXES.  Hence, this function returns true if the table
  does not contain a primary key. (In which case we generate a hidden
  'auto-incremented' pk.)
*/
bool ha_rocksdb::has_hidden_pk(const TABLE* table)
{
  DBUG_ASSERT(table != nullptr);
  return Rdb_key_def::table_has_hidden_pk(table);
}

/*
  Returns true if given index number is a hidden_pk.
  - This is used when a table is created with no primary key.
*/
bool ha_rocksdb::is_hidden_pk(const uint index, const TABLE* table_arg,
                              const Rdb_tbl_def* tbl_def_arg)
{
  DBUG_ASSERT(table_arg != nullptr);
  DBUG_ASSERT(table_arg->s != nullptr);
  DBUG_ASSERT(tbl_def_arg != nullptr);

  return (table_arg->s->primary_key == MAX_INDEXES &&
          index == tbl_def_arg->m_key_count - 1);
}

/* Returns index of primary key */
uint ha_rocksdb::pk_index(const TABLE* table_arg,
                          const Rdb_tbl_def* tbl_def_arg)
{
  DBUG_ASSERT(table_arg != nullptr);
  DBUG_ASSERT(table_arg->s != nullptr);
  DBUG_ASSERT(tbl_def_arg != nullptr);

  return table_arg->s->primary_key == MAX_INDEXES ?
            tbl_def_arg->m_key_count - 1 :
            table_arg->s->primary_key;
}

/* Returns true if given index number is a primary key */
bool ha_rocksdb::is_pk(const uint index, const TABLE* table_arg,
                       const Rdb_tbl_def *tbl_def_arg)
{
  DBUG_ASSERT(table_arg != nullptr);
  DBUG_ASSERT(table_arg->s != nullptr);
  DBUG_ASSERT(tbl_def_arg != nullptr);

  return index == table_arg->s->primary_key ||
         is_hidden_pk(index, table_arg, tbl_def_arg);
}

const char* ha_rocksdb::get_key_name(const uint index, const TABLE* table_arg,
                                     const Rdb_tbl_def* tbl_def_arg)
{
  DBUG_ASSERT(table_arg != nullptr);

  if (is_hidden_pk(index, table_arg, tbl_def_arg))
  {
    return HIDDEN_PK_NAME;
  }

  return table_arg->key_info[index].name;
}

const char* ha_rocksdb::get_key_comment(const uint index,
                                        const TABLE* table_arg,
                                        const Rdb_tbl_def* tbl_def_arg)
{
  DBUG_ASSERT(table_arg != nullptr);

  if (is_hidden_pk(index, table_arg, tbl_def_arg))
  {
    return nullptr;
  }

  return table_arg->key_info[index].comment.str;
}

int ha_rocksdb::write_row(uchar *buf)
{
  DBUG_ENTER("ha_rocksdb::write_row");
  DBUG_ASSERT(buf == table->record[0]);
  DBUG_ASSERT(m_lock_rows == RDB_LOCK_WRITE);
  ha_statistic_increment(&SSV::ha_write_count);
  /*
    Note: "buf == table->record[0]" is copied from innodb. I am not aware of
    any use cases where this condition is not true.
  */
  if (table->next_number_field && buf == table->record[0])
  {
    int err;
    if ((err= update_auto_increment()))
    {
      DBUG_RETURN(err);
    }
  }

  int rv= update_write_row(nullptr, buf, skip_unique_check());

  if (rv == 0)
  {
    stats.rows_inserted++;
    update_row_stats(ROWS_INSERTED);
  }
  DBUG_RETURN(rv);
}


/**
  Constructing m_last_rowkey (MyRocks key expression) from
  before_update|delete image (MySQL row expression).
  m_last_rowkey is normally set during lookup phase, such as
  rnd_next_with_direction() and rnd_pos(). With Read Free Replication,
  these read functions are skipped and update_rows(), delete_rows() are
  called without setting m_last_rowkey. This function sets m_last_rowkey
  for Read Free Replication.
*/
void ha_rocksdb::set_last_rowkey(const uchar *old_data)
{
  if (old_data && use_read_free_rpl())
  {
    int old_pk_size= m_pk_descr->pack_record(
        table, m_pack_buffer, old_data, m_pk_packed_tuple, nullptr, false);
    m_last_rowkey.copy((const char*)m_pk_packed_tuple, old_pk_size,
                       &my_charset_bin);
  }
}

int ha_rocksdb::get_pk_for_update(struct update_row_info* row_info)
{
  int size;

  /*
    Get new row key for any insert, and any update where the pk is not hidden.
    Row key for updates with hidden pk is handled below.
  */
  if (!has_hidden_pk(table))
  {
    row_info->hidden_pk_id= 0;

    row_info->new_pk_unpack_info= &m_pk_unpack_info;

    size= m_pk_descr->pack_record(table, m_pack_buffer, row_info->new_data,
                                  m_pk_packed_tuple,
                                  row_info->new_pk_unpack_info,
                                  false);
  }
  else if (row_info->old_data == nullptr)
  {
    row_info->hidden_pk_id= update_hidden_pk_val();
    size= m_pk_descr->pack_hidden_pk(row_info->hidden_pk_id, m_pk_packed_tuple);
  }
  else
  {
    /*
      If hidden primary key, rowkey for new record will always be the same as
      before
    */
    size= row_info->old_pk_slice.size();
    memcpy(m_pk_packed_tuple, row_info->old_pk_slice.data(), size);
    if (read_hidden_pk_id_from_rowkey(&row_info->hidden_pk_id))
    {
      return HA_ERR_INTERNAL_ERROR;
    }
  }

  row_info->new_pk_slice= rocksdb::Slice((const char*)m_pk_packed_tuple, size);

  return 0;
}

int ha_rocksdb::check_and_lock_unique_pk(uint key_id,
                                         const struct update_row_info& row_info,
                                         bool* found, bool* pk_changed)
{
  DBUG_ASSERT(found != nullptr);
  DBUG_ASSERT(pk_changed != nullptr);

  *pk_changed= false;

  /*
    For UPDATEs, if the key has changed, we need to obtain a lock. INSERTs
    always require locking.
  */
  if (row_info.old_pk_slice.size() > 0)
  {
    /*
      If the keys are the same, then no lock is needed
    */
    if (!Rdb_pk_comparator::bytewise_compare(row_info.new_pk_slice,
                                             row_info.old_pk_slice))
    {
      *found= false;
      return 0;
    }

    *pk_changed= true;
  }

  /*
    Perform a read to determine if a duplicate entry exists. For primary
    keys, a point lookup will be sufficient.

    note: we intentionally don't set options.snapshot here. We want to read
    the latest committed data.
  */

  /*
    To prevent race conditions like below, it is necessary to
    take a lock for a target row. get_for_update() holds a gap lock if
    target key does not exist, so below conditions should never
    happen.

    1) T1 Get(empty) -> T2 Get(empty) -> T1 Put(insert) -> T1 commit
       -> T2 Put(overwrite) -> T2 commit
    2) T1 Get(empty) -> T1 Put(insert, not committed yet) -> T2 Get(empty)
       -> T2 Put(insert, blocked) -> T1 commit -> T2 commit(overwrite)
  */
  rocksdb::Status s= row_info.tx->get_for_update(m_pk_descr->get_cf(),
                                                 row_info.new_pk_slice,
                                                 &m_retrieved_record);
  if (!s.ok() && !s.IsNotFound())
  {
    return row_info.tx->set_status_error(table->in_use, s,
                                         m_key_descr_arr[key_id], m_tbl_def);
  }

  *found= !s.IsNotFound();
  return 0;
}

int ha_rocksdb::check_and_lock_sk(uint key_id,
                                 const struct update_row_info& row_info,
                                 bool* found) const
{
  DBUG_ASSERT(found != nullptr);
  *found= false;

  /*
    Can skip checking this key if none of the key fields have changed.
  */
  if (row_info.old_data != nullptr && !m_update_scope.is_set(key_id))
  {
    return 0;
  }

  KEY* key_info= nullptr;
  uint n_null_fields= 0;
  uint user_defined_key_parts= 1;

  key_info= &table->key_info[key_id];
  user_defined_key_parts= key_info->user_defined_key_parts;
  /*
    If there are no uniqueness requirements, there's no need to obtain a
    lock for this key.
  */
  if (!(key_info->flags & HA_NOSAME))
  {
    return 0;
  }

  const std::shared_ptr<const Rdb_key_def>& kd= m_key_descr_arr[key_id];

  /*
    Calculate the new key for obtaining the lock

    For unique secondary indexes, the key used for locking does not
    include the extended fields.
  */
  int size= kd->pack_record(table, m_pack_buffer, row_info.new_data,
                            m_sk_packed_tuple, nullptr, false, 0,
                            user_defined_key_parts, &n_null_fields);
  if (n_null_fields > 0)
  {
    /*
      If any fields are marked as NULL this will never match another row as
      to NULL never matches anything else including another NULL.
     */
    return 0;
  }

  rocksdb::Slice new_slice= rocksdb::Slice((const char*)m_sk_packed_tuple,
                                           size);

  /*
    For UPDATEs, if the key has changed, we need to obtain a lock. INSERTs
    always require locking.
  */
  if (row_info.old_data != nullptr)
  {
    size= kd->pack_record(table, m_pack_buffer, row_info.old_data,
                          m_sk_packed_tuple_old, nullptr, false,
                          row_info.hidden_pk_id,
                          user_defined_key_parts);
    rocksdb::Slice old_slice= rocksdb::Slice(
        (const char*)m_sk_packed_tuple_old, size);

    /*
      For updates, if the keys are the same, then no lock is needed

      Also check to see if the key has any fields set to NULL. If it does, then
      this key is unique since NULL is not equal to each other, so no lock is
      needed.
    */
    if (!Rdb_pk_comparator::bytewise_compare(new_slice, old_slice))
    {
      return 0;
    }
  }

  /*
    Perform a read to determine if a duplicate entry exists - since this is
    a secondary indexes a range scan is needed.

    note: we intentionally don't set options.snapshot here. We want to read
    the latest committed data.
  */

  bool all_parts_used= (user_defined_key_parts == kd->get_key_parts());

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
    !can_use_bloom_filter(ha_thd(), kd, new_slice, all_parts_used,
                          is_ascending(m_key_descr_arr[key_id],
                                       HA_READ_KEY_EXACT));
  bool fill_cache= !THDVAR(ha_thd(), skip_fill_cache);

  /*
    psergey-todo: we just need to take lock, lookups not needed:
  */
  std::string dummy_value;
  rocksdb::Status s= row_info.tx->get_for_update(kd->get_cf(), new_slice,
                                                 &dummy_value);
  if (!s.ok() && !s.IsNotFound())
  {
    return row_info.tx->set_status_error(table->in_use, s, kd, m_tbl_def);
  }

  rocksdb::Iterator *iter= row_info.tx->get_iterator(kd->get_cf(),
      total_order_seek,
      fill_cache,
      true /* read current data */,
      false /* acquire snapshot */);
  /*
    Need to scan the transaction to see if there is a duplicate key.
    Also need to scan RocksDB and verify the key has not been deleted
    in the transaction.
  */
  iter->Seek(new_slice);
  *found= !read_key_exact(kd, iter, all_parts_used, new_slice);
  delete iter;

  return 0;
}

int ha_rocksdb::check_uniqueness_and_lock(
      const struct update_row_info& row_info,
      bool* pk_changed)
{
  /*
    Go through each index and determine if the index has uniqueness
    requirements. If it does, then try to obtain a row lock on the new values.
    Once all locks have been obtained, then perform the changes needed to
    update/insert the row.
  */
  for (uint key_id= 0; key_id < m_tbl_def->m_key_count; key_id++)
  {
    bool found;
    int  rc;

    if (is_pk(key_id, table, m_tbl_def))
    {
      rc= check_and_lock_unique_pk(key_id, row_info, &found, pk_changed);
    }
    else
    {
      rc= check_and_lock_sk(key_id, row_info, &found);
    }

    if (rc != 0)
    {
      return rc;
    }

    if (found)
    {
      /* There is a row with this key already, so error out. */
      errkey= key_id;
      m_dupp_errkey= errkey;
      return HA_ERR_FOUND_DUPP_KEY;
    }
  }

  return 0;
}

int ha_rocksdb::bulk_load_pk(Rdb_transaction* tx, uint key_id,
                             rocksdb::ColumnFamilyHandle* cf,
                             const rocksdb::Slice& key,
                             const rocksdb::Slice& value)
{
  DBUG_ASSERT(cf != nullptr);

  if (m_sst_info == nullptr)
  {
    const std::shared_ptr<const Rdb_key_def>& kd= m_key_descr_arr[key_id];

    m_sst_info=
        std::make_shared<Rdb_sst_info>(rdb, m_table_handler->m_table_name,
                                       kd->get_name(), cf, rocksdb_db_options);
    tx->start_bulk_load(this);
  }

  DBUG_ASSERT(m_sst_info != nullptr);

  int rc= m_sst_info->put(key, value);
  if (rc != 0)
  {
    my_printf_error(ER_UNKNOWN_ERROR,
                    "Failed to add a key to sst file writer(%s)", MYF(0),
                    m_sst_info->error_message().c_str());
    rc= HA_ERR_INTERNAL_ERROR;
  }

  return rc;
}

int ha_rocksdb::finalize_bulk_load()
{
  int rc= 0;

  if (m_sst_info != nullptr)
  {
    rc= m_sst_info->commit();
    if (rc != 0)
    {
      my_printf_error(ER_UNKNOWN_ERROR,
                      "Failed to commit bulk loaded sst file to the "
                      "data store (%s)",
                      MYF(0), m_sst_info->error_message().c_str());
      rc= HA_ERR_INTERNAL_ERROR;
    }

    m_sst_info= nullptr;

    Rdb_transaction *tx= get_or_create_tx(ha_thd());
    if (tx != nullptr)
    {
      tx->end_bulk_load(this);
    }
  }

  return rc;
}

int ha_rocksdb::update_pk(uint key_id, const struct update_row_info& row_info,
                          bool pk_changed)
{
  bool hidden_pk= is_hidden_pk(key_id, table, m_tbl_def);
  if (!hidden_pk && pk_changed)
  {
    const std::shared_ptr<const Rdb_key_def>& kd= m_key_descr_arr[key_id];

    /*
      The old key needs to be deleted.
    */
    rocksdb::Status s= delete_or_singledelete(key_id, row_info.tx, kd->get_cf(),
                                              row_info.old_pk_slice);
    if (!s.ok())
    {
      return row_info.tx->set_status_error(table->in_use, s, kd, m_tbl_def);
    }
  }

  if (table->next_number_field)
  {
    update_auto_incr_val();
  }

  rocksdb::Slice value_slice;
  convert_record_to_storage_format(row_info.new_pk_slice,
                                   row_info.new_pk_unpack_info,
                                   &value_slice);

  int rc= 0;
  auto cf= m_pk_descr->get_cf();
  if (rocksdb_enable_bulk_load_api && THDVAR(table->in_use, bulk_load) &&
      !hidden_pk)
  {
    /*
      Write the primary key directly to an SST file using an SstFileWriter
     */
    rc= bulk_load_pk(row_info.tx, key_id, cf, row_info.new_pk_slice,
                     value_slice);
  }
  else if (row_info.skip_unique_check)
  {
    /*
      It is responsibility of the user to make sure that the data being
      inserted doesn't violate any unique keys.
    */
    row_info.tx->get_blind_write_batch()->Put(cf, row_info.new_pk_slice,
                                              value_slice);
  }
  else if (row_info.tx->m_ddl_transaction)
  {
    /*
      DDL statement must check for unique key conflicts. For example:
      ALTER TABLE tbl DROP PRIMARY KEY, ADD PRIMARY KEY(non_unique_column)
    */
    row_info.tx->get_indexed_write_batch()->Put(cf, row_info.new_pk_slice,
                                                value_slice);
  }
  else
  {
    auto s= row_info.tx->put(cf, row_info.new_pk_slice, value_slice);
    if (!s.ok())
    {
      if (s.IsBusy())
      {
        errkey= table->s->primary_key;
        m_dupp_errkey= errkey;
        rc = HA_ERR_FOUND_DUPP_KEY;
      }
      else
      {
        rc = row_info.tx->set_status_error(table->in_use, s, m_pk_descr,
                                           m_tbl_def);
      }
    }
  }

  return rc;
}

int ha_rocksdb::update_sk(const TABLE* table_arg,
                          const std::shared_ptr<const Rdb_key_def>& kd,
                          const struct update_row_info& row_info)
{
  int new_packed_size;
  int old_packed_size;

  rocksdb::Slice new_key_slice;
  rocksdb::Slice new_value_slice;
  rocksdb::Slice old_key_slice;

  uint key_id = kd->get_keyno();
  /*
    Can skip updating this key if none of the key fields have changed.
  */
  if (row_info.old_data != nullptr && !m_update_scope.is_set(key_id))
  {
    return 0;
  }

  bool store_checksums= should_store_checksums();

  new_packed_size= kd->pack_record(table_arg, m_pack_buffer, row_info.new_data,
                                   m_sk_packed_tuple, &m_sk_tails,
                                   store_checksums,
                                   row_info.hidden_pk_id);

  if (row_info.old_data != nullptr)
  {
    // The old value
    old_packed_size= kd->pack_record(table_arg, m_pack_buffer,
                                     row_info.old_data,
                                     m_sk_packed_tuple_old, &m_sk_tails_old,
                                     store_checksums,
                                     row_info.hidden_pk_id);

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
        m_sk_tails_old.get_current_pos() == m_sk_tails.get_current_pos() &&
        memcmp(m_sk_packed_tuple_old, m_sk_packed_tuple,
               old_packed_size) == 0 &&
        memcmp(m_sk_tails_old.ptr(), m_sk_tails.ptr(),
               m_sk_tails.get_current_pos()) == 0)
    {
      return 0;
    }

    /*
      Deleting entries from secondary index should skip locking, but
      be visible to the transaction.
      (also note that DDL statements do not delete rows, so this is not a DDL
       statement)
    */
    old_key_slice= rocksdb::Slice(
        reinterpret_cast<const char*>(m_sk_packed_tuple_old),
        old_packed_size);

    row_info.tx->get_indexed_write_batch()->SingleDelete(kd->get_cf(),
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
  if ((row_info.tx->m_ddl_transaction &&
       !(table_arg->key_info[key_id].flags & HA_NOSAME)) ||
      row_info.skip_unique_check)
  {
    write_batch= row_info.tx->get_blind_write_batch();
  }
  else
  {
    write_batch= row_info.tx->get_indexed_write_batch();
  }

  new_key_slice= rocksdb::Slice(
      reinterpret_cast<const char*>(m_sk_packed_tuple), new_packed_size);
  new_value_slice= rocksdb::Slice(
      reinterpret_cast<const char*>(m_sk_tails.ptr()),
      m_sk_tails.get_current_pos());

  write_batch->Put(kd->get_cf(), new_key_slice, new_value_slice);

  return 0;
}

int ha_rocksdb::update_indexes(const struct update_row_info& row_info,
                               bool pk_changed)
{
  int rc;

  for (uint key_id= 0; key_id < m_tbl_def->m_key_count; key_id++)
  {
    const std::shared_ptr<const Rdb_key_def>& kd= m_key_descr_arr[key_id];
    if (is_pk(key_id, table, m_tbl_def))
    {
      rc= update_pk(key_id, row_info, pk_changed);
    }
    else
    {
      rc= update_sk(table, kd, row_info);
    }

    if (rc != 0)
    {
      return rc;
    }
  }

  return 0;
}

int ha_rocksdb::update_write_row(const uchar *old_data,
                                 const uchar *new_data,
                                 const bool skip_unique_check)
{
  bool                   pk_changed= false;
  struct update_row_info row_info;

  row_info.old_data= old_data;
  row_info.new_data= new_data;
  row_info.skip_unique_check= skip_unique_check;
  row_info.new_pk_unpack_info= nullptr;

  DBUG_ENTER("ha_rocksdb::write_row_common");

  set_last_rowkey(old_data);

  row_info.tx= get_or_create_tx(table->in_use);

  if (old_data != nullptr)
  {
    row_info.old_pk_slice= rocksdb::Slice(m_last_rowkey.ptr(),
                                          m_last_rowkey.length());

    /* Determine which indexes need updating. */
    calc_updated_indexes();
  }

  /*
    Get the new row key into row_info.new_pk_slice
   */
  int rc= get_pk_for_update(&row_info);
  if (rc != 0)
  {
    DBUG_RETURN(rc);
  }

  if (!skip_unique_check)
  {
    /*
      Check to see if we are going to have failures because of unique
      keys.  Also lock the appropriate key values.
    */
    rc= check_uniqueness_and_lock(row_info, &pk_changed);
    if (rc != 0)
    {
      DBUG_RETURN(rc);
    }
  }

  DEBUG_SYNC(ha_thd(), "rocksdb.update_write_row_after_unique_check");

  /*
    At this point, all locks have been obtained, and all checks for duplicate
    keys have been performed. No further errors can be allowed to occur from
    here because updates to the transaction will be made and those updates
    cannot be easily removed without rolling back the entire transaction.
  */
  rc= update_indexes(row_info, pk_changed);
  if (rc != 0)
  {
    DBUG_RETURN(rc);
  }

  if (do_bulk_commit(row_info.tx))
  {
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  DBUG_RETURN(0);
}


/*
  Open a cursor and position it at the passed record
*/

void ha_rocksdb::setup_scan_iterator(
    const std::shared_ptr<const Rdb_key_def>& kd,
    rocksdb::Slice *slice,
    const bool use_all_keys,
    const bool is_ascending,
    const uint eq_cond_len)
{
  DBUG_ASSERT(kd != nullptr);
  DBUG_ASSERT(slice != nullptr);
  DBUG_ASSERT(slice->size() >= eq_cond_len);

  Rdb_transaction *tx= get_or_create_tx(table->in_use);

  bool skip_bloom= true;

  rocksdb::Slice eq_cond(slice->data(), eq_cond_len);
  if (can_use_bloom_filter(ha_thd(), kd, eq_cond, use_all_keys, is_ascending))
  {
    skip_bloom= false;
  }

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
  if (m_scan_it_skips_bloom != skip_bloom)
  {
    release_scan_iterator();
  }

  /*
    SQL layer can call rnd_init() multiple times in a row.
    In that case, re-use the iterator, but re-position it at the table start.
  */
  if (!m_scan_it)
  {
    bool fill_cache= !THDVAR(ha_thd(), skip_fill_cache);
    m_scan_it= tx->get_iterator(kd->get_cf(), skip_bloom, fill_cache);
    m_scan_it_skips_bloom= skip_bloom;
  }
  /*
    Seek() will "Position at the first key in the source that at or past target".
    The operation cannot fail.
  */
  m_scan_it->Seek(*slice);
}


void ha_rocksdb::setup_iterator_for_rnd_scan()
{
  uint key_size;

  if (m_pk_descr->m_is_reverse_cf)
    m_pk_descr->get_supremum_key(m_pk_packed_tuple, &key_size);
  else
    m_pk_descr->get_infimum_key(m_pk_packed_tuple, &key_size);

  rocksdb::Slice table_key((const char*)m_pk_packed_tuple, key_size);

  setup_scan_iterator(m_pk_descr, &table_key);
  m_skip_scan_it_next_call= true;
}


int ha_rocksdb::rnd_init(bool scan)
{
  DBUG_ENTER("ha_rocksdb::rnd_init");

  Rdb_transaction *tx= get_or_create_tx(table->in_use);
  setup_read_decoders();

  if (scan)
  {
    m_rnd_scan_is_new_snapshot= !tx->has_snapshot();
    setup_iterator_for_rnd_scan();
  }
  else
  {
    /* We don't need any preparations for rnd_pos() calls. */
  }

  // If m_lock_rows is on then we will be doing a get_for_update when accessing
  // the index, so don't acquire the snapshot right away.  Otherwise acquire
  // the snapshot immediately.
  tx->acquire_snapshot(m_lock_rows == RDB_LOCK_NONE);

  DBUG_RETURN(0);
}


int ha_rocksdb::rnd_next(uchar *buf)
{
  int rc;
  ha_statistic_increment(&SSV::ha_read_rnd_next_count);
  for (;;)
  {
    rc= rnd_next_with_direction(buf, true);
    if (rc != HA_ERR_LOCK_DEADLOCK || !m_rnd_scan_is_new_snapshot)
      break;  /* exit the loop */

    // release the snapshot and iterator and then regenerate them
    Rdb_transaction *tx= get_or_create_tx(table->in_use);
    tx->release_snapshot();
    release_scan_iterator();
    setup_iterator_for_rnd_scan();
  }

  m_rnd_scan_is_new_snapshot= false;

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

  table->status= STATUS_NOT_FOUND;
  stats.rows_requested++;

  if (!m_scan_it || !m_scan_it->Valid())
  {
    /*
      We can get here when SQL layer has called

        h->index_init(PRIMARY);
        h->index_read_map(full index tuple, HA_READ_KEY_EXACT);

      In this case, we should return EOF.
    */
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }

  if (m_skip_scan_it_next_call)
  {
    m_skip_scan_it_next_call= false;
  }
  else
  {
    if (move_forward)
      m_scan_it->Next();  /* this call cannot fail */
    else
      m_scan_it->Prev();  /* this call cannot fail */
  }

  if (m_scan_it->Valid())
  {
    /* check if we're out of this table */
    rocksdb::Slice key= m_scan_it->key();
    if (!m_pk_descr->covers_key(key))
    {
      rc= HA_ERR_END_OF_FILE;
    }
    else
    {
      if (m_lock_rows != RDB_LOCK_NONE)
      {
        /*
          Lock the row we've just read.

          Now we call get_for_update which will 1) Take a lock and 2) Will fail
          if the row was deleted since the snapshot was taken.
        */
        Rdb_transaction *tx= get_or_create_tx(table->in_use);
        DEBUG_SYNC(ha_thd(), "rocksdb_concurrent_delete");
        rocksdb::Status s= tx->get_for_update(m_pk_descr->get_cf(), key,
                                              &m_retrieved_record);
        if (!s.ok())
        {
          DBUG_RETURN(tx->set_status_error(table->in_use, s, m_pk_descr,
                                           m_tbl_def));
        }

        // If we called get_for_update() use the value from that call not from
        // the iterator as it may be stale since we don't have a snapshot
        // when m_lock_rows is not RDB_LOCK_NONE.
        m_last_rowkey.copy(key.data(), key.size(), &my_charset_bin);
        rc= convert_record_from_storage_format(&key, buf);
      }
      else
      {
        // Use the value from the iterator
        rocksdb::Slice value= m_scan_it->value();
        m_last_rowkey.copy(key.data(), key.size(), &my_charset_bin);
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
  Rdb_transaction *tx= get_or_create_tx(table->in_use);
  DBUG_ASSERT(tx != nullptr);

  setup_read_decoders();

  // If m_lock_rows is not RDB_LOCK_NONE then we will be doing a get_for_update
  // when accessing the index, so don't acquire the snapshot right away.
  // Otherwise acquire the snapshot immediately.
  tx->acquire_snapshot(m_lock_rows == RDB_LOCK_NONE);

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
  m_tbl_def->m_auto_incr_val= 1;
  m_tbl_def->m_hidden_pk_val= 1;
  remove_rows(m_tbl_def);
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

  ha_statistic_increment(&SSV::ha_delete_count);
  set_last_rowkey(buf);

  rocksdb::Slice key_slice(m_last_rowkey.ptr(), m_last_rowkey.length());
  Rdb_transaction *tx= get_or_create_tx(table->in_use);

  uint index= pk_index(table, m_tbl_def);
  rocksdb::Status s= delete_or_singledelete(index, tx,
                                            m_pk_descr->get_cf(),
                                            key_slice);
  if (!s.ok())
  {
    DBUG_RETURN(tx->set_status_error(table->in_use, s, m_pk_descr, m_tbl_def));
  }

  longlong hidden_pk_id= 0;
  if (m_tbl_def->m_key_count > 1 &&
      has_hidden_pk(table) && read_hidden_pk_id_from_rowkey(&hidden_pk_id))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  // Delete the record for every secondary index
  for (uint i= 0; i < m_tbl_def->m_key_count; i++)
  {
    if (!is_pk(i, table, m_tbl_def))
    {
      int packed_size;
      const std::shared_ptr<const Rdb_key_def>& kd= m_key_descr_arr[i];
      packed_size= kd->pack_record(table, m_pack_buffer, buf, m_sk_packed_tuple,
                                   nullptr, false, hidden_pk_id);
      rocksdb::Slice secondary_key_slice(
         reinterpret_cast<const char*>(m_sk_packed_tuple), packed_size);
      /* Deleting on secondary key doesn't need any locks: */
      tx->get_indexed_write_batch()->SingleDelete(kd->get_cf(),
                                                  secondary_key_slice);
    }
  }

  if (do_bulk_commit(tx))
  {
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }
  stats.rows_deleted++;
  update_row_stats(ROWS_DELETED);

  DBUG_RETURN(0);
}


rocksdb::Status
ha_rocksdb::delete_or_singledelete(uint index,
                                   Rdb_transaction *tx,
                                   rocksdb::ColumnFamilyHandle* column_family,
                                   const rocksdb::Slice& key)
{
  if (can_use_single_delete(index))
    return tx->single_delete(column_family, key);
  return tx->delete_key(column_family, key);
}


void ha_rocksdb::update_stats(void)
{
  DBUG_ENTER("ha_rocksdb::update_stats");

  stats.records= 0;
  stats.index_file_length= 0ul;
  stats.data_file_length= 0ul;
  stats.mean_rec_length= 0;
  for (uint i= 0; i < m_tbl_def->m_key_count; i++)
  {
    if (is_pk(i, table, m_tbl_def))
    {
      stats.data_file_length= m_pk_descr->m_stats.m_actual_disk_size;
      stats.records = m_pk_descr->m_stats.m_rows;
    }
    else
    {
      stats.index_file_length+= m_key_descr_arr[i]->m_stats.m_actual_disk_size;
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
        m_pk_descr->m_stats.m_actual_disk_size =
          -m_pk_descr->m_stats.m_actual_disk_size;);

    update_stats();

    /*
      If any stats are negative due to bad cached stats, re-run analyze table
      and re-retrieve the stats.
    */
    if (static_cast<longlong>(stats.data_file_length) < 0 ||
        static_cast<longlong>(stats.index_file_length) < 0 ||
        static_cast<longlong>(stats.records) < 0)
    {
      if (analyze(nullptr, nullptr)) {
        DBUG_RETURN(1);
      }

      update_stats();
    }

    if (stats.records == 0)
    {
      // most likely, the table is in memtable
      // try to deduce from GetApproximateSizes
      uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE*2];
      auto r= get_range(pk_index(table, m_tbl_def), buf);
      uint64_t sz= 0;
      rdb->GetApproximateSizes(
        m_pk_descr->get_cf(),
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
    ref_length= m_pk_descr->max_storage_fmt_length();

    // TODO: Needs to reimplement after having real index statistics
    for (uint i= 0; i < m_tbl_def->m_key_count; i++)
    {
      if (is_hidden_pk(i, table, m_tbl_def))
      {
        continue;
      }
      KEY* k= &table->key_info[i];
      for (uint j = 0; j < k->actual_key_parts; j++)
      {
        const Rdb_index_stats& k_stats= m_key_descr_arr[i]->m_stats;
        uint x = k_stats.m_distinct_keys_per_prefix.size() > j &&
                 k_stats.m_distinct_keys_per_prefix[j] > 0 ?
          k_stats.m_rows / k_stats.m_distinct_keys_per_prefix[j] :
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
    errkey= m_dupp_errkey;
    dup_ref= m_pk_tuple;  // TODO(?): this should store packed PK.
  }

  if (flag & HA_STATUS_AUTO)
  {
    stats.auto_increment_value= m_tbl_def->m_auto_incr_val;
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

    (Note: m_last_rowkey has the packed PK of last-read-row, which allows to
    handle most cases, but there is an exception to this: when slave applies
    RBR events, it fills the record and then calls position(); rnd_pos())

    Also note that we *can* rely on PK column values being available. This is
    because table_flags() includes HA_PRIMARY_KEY_REQUIRED_FOR_POSITION bit.
    When that is true, table->prepare_for_position() adds PK columns into the
    read set (this may potentially disable index-only access when PK column
    cannot be restored from its mem-comparable form in the secondary indexes).
  */
  uint packed_size= m_pk_descr->pack_record(table, m_pack_buffer, record,
                                            ref, nullptr, false, hidden_pk_id);

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

  ha_statistic_increment(&SSV::ha_read_rnd_count);
  stats.rows_requested++;
  len= m_pk_descr->key_length(table, rocksdb::Slice((const char*)pos,
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
  if (!m_update_scope_is_valid)
  {
    m_update_scope_is_valid= true;
    m_update_scope.clear_all();

    for (uint keynr= 0; keynr < table->s->keys; keynr++)
    {
      const std::shared_ptr<const Rdb_key_def>& kd= m_key_descr_arr[keynr];
      /* Walk over all key parts, including the "extended key" suffix */
      uint key_parts= kd->get_key_parts();
      for (uint kp= 0; kp < key_parts; kp++)
      {
        if (has_hidden_pk(table) && kp + 1 == key_parts)
          break;

        Field *field= kd->get_table_field_for_part_no(table, kp);
        if (bitmap_is_set(table->write_set, field->field_index))
        {
          m_update_scope.set_bit(keynr);
          break;
        }
      }
    }
  }
}


int ha_rocksdb::update_row(const uchar *old_data, uchar *new_data)
{
  DBUG_ENTER("ha_rocksdb::update_row");
  DBUG_ASSERT(m_lock_rows == RDB_LOCK_WRITE);
  /*
    old_data points to record we're updating. It is the same as the record
    we've just read (for multi-table UPDATE, too, because SQL layer will make
    an rnd_pos() call to re-read the record before calling update_row())
  */
  DBUG_ASSERT(new_data == table->record[0]);

  ha_statistic_increment(&SSV::ha_update_count);
  int rv;
  rv= update_write_row(old_data, new_data, false);

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

  bool in_lock_tables = my_core::thd_in_lock_tables(thd);

  /* First, make a decision about MyRocks's internal locking */
  if (lock_type >= TL_WRITE_ALLOW_WRITE)
  {
    m_lock_rows = RDB_LOCK_WRITE;
  }
  else if (lock_type == TL_READ_WITH_SHARED_LOCKS)
  {
    m_lock_rows = RDB_LOCK_READ;
  }
  else
  {
    m_lock_rows = RDB_LOCK_NONE;
    if (THDVAR(thd, lock_scanned_rows))
    {
      /*
        The following logic was copied directly from
        ha_innobase::store_lock_with_x_type() in
        storage/innobase/handler/ha_innodb.cc and causes MyRocks to leave
        locks in place on rows that are in a table that is not beint updated.
      */
      uint sql_command = my_core::thd_sql_command(thd);
      if ((lock_type == TL_READ && in_lock_tables) ||
          (lock_type == TL_READ_HIGH_PRIORITY && in_lock_tables) ||
          can_hold_read_locks_on_select(thd, lock_type))
      {
        ulong tx_isolation = my_core::thd_tx_isolation(thd);
        if (sql_command != SQLCOM_CHECKSUM &&
            ((my_core::thd_test_options(thd, OPTION_BIN_LOG) &&
              tx_isolation > ISO_READ_COMMITTED) ||
             tx_isolation == ISO_SERIALIZABLE ||
             (lock_type != TL_READ && lock_type != TL_READ_NO_INSERT) ||
             (sql_command != SQLCOM_INSERT_SELECT &&
              sql_command != SQLCOM_REPLACE_SELECT &&
              sql_command != SQLCOM_UPDATE &&
              sql_command != SQLCOM_DELETE &&
              sql_command != SQLCOM_CREATE_TABLE)))
        {
          m_lock_rows = RDB_LOCK_READ;
        }
      }
    }
  }

  /* Then, tell the SQL layer what kind of locking it should use: */
  if (lock_type != TL_IGNORE && m_db_lock.type == TL_UNLOCK)
  {
    /*
      Here is where we get into the guts of a row level lock.
      If TL_UNLOCK is set
      If we are not doing a LOCK TABLE or DISCARD/IMPORT
      TABLESPACE, then allow multiple writers
    */

    if ((lock_type >= TL_WRITE_CONCURRENT_INSERT && lock_type <= TL_WRITE) &&
        !in_lock_tables && !my_core::thd_tablespace_op(thd))
    {
      lock_type = TL_WRITE_ALLOW_WRITE;
    }

    /*
      In queries of type INSERT INTO t1 SELECT ... FROM t2 ...
      MySQL would use the lock TL_READ_NO_INSERT on t2, and that
      would conflict with TL_WRITE_ALLOW_WRITE, blocking all inserts
      to t2. Convert the lock to a normal read lock to allow
      concurrent inserts to t2.
    */

    if (lock_type == TL_READ_NO_INSERT && !in_lock_tables)
    {
      lock_type = TL_READ;
    }

    m_db_lock.type= lock_type;
  }
  *to++= &m_db_lock;
  DBUG_RETURN(to);
}

void ha_rocksdb::read_thd_vars(THD *thd)
{
  m_store_checksums= THDVAR(thd, store_checksums);
  m_verify_checksums= THDVAR(thd, verify_checksums);
  m_checksums_pct= THDVAR(thd, checksums_pct);
}

int ha_rocksdb::external_lock(THD *thd, int lock_type)
{
  int res= 0;
  DBUG_ENTER("ha_rocksdb::external_lock");

  int binlog_format= my_core::thd_binlog_format(thd);
  bool unsafe_for_binlog= THDVAR(ha_thd(), unsafe_for_binlog);
  if (lock_type == F_WRLCK && !thd->rli_slave &&
      !unsafe_for_binlog &&
      binlog_format != BINLOG_FORMAT_ROW &&
      binlog_format != BINLOG_FORMAT_UNSPEC &&
      my_core::thd_binlog_filter_ok(thd))
  {
    my_printf_error(ER_UNKNOWN_ERROR,
             "Can't execute updates on master with binlog_format != ROW.",
             MYF(0));
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  if (lock_type == F_UNLCK)
  {
    Rdb_transaction* tx= get_or_create_tx(thd);

    tx->io_perf_end_and_record(&m_io_perf);
    if (!my_core::thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN))
    {
      /*
        Do like InnoDB: when we get here, it's time to commit a
        single-statement transaction.

        If the statement involved multiple tables, this code will be executed
        for each of them, but that's ok because non-first tx->commit() calls
        will be no-ops.
      */
      res= tx->commit_or_rollback();
    }
  }
  else
  {
    if (my_core::thd_tx_isolation(thd) < ISO_READ_COMMITTED ||
        my_core::thd_tx_isolation(thd) > ISO_REPEATABLE_READ)
    {
      my_printf_error(ER_UNKNOWN_ERROR,
                      "MyRocks supports only READ COMMITTED and "
                      "REPEATABLE READ isolation levels. "
                      "Please change from current isolation "
                      "level %s",
                      MYF(0),
                      tx_isolation_names[my_core::thd_tx_isolation(thd)]);
      DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
    }
    /*
      It's nice to do the following on start of every statement. The problem
      is, handler->start_stmt() is not called for INSERTs.
      So, we put this code here.
    */
    Rdb_transaction* tx= get_or_create_tx(thd);
    read_thd_vars(thd);

    m_update_scope_is_valid= false;

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
      if (tx->is_tx_read_only())
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
      m_lock_rows= RDB_LOCK_WRITE;

      if (thd->lex->sql_command == SQLCOM_CREATE_INDEX ||
          thd->lex->sql_command == SQLCOM_DROP_INDEX   ||
          thd->lex->sql_command == SQLCOM_ALTER_TABLE)
      {
        tx->m_ddl_transaction= true;
      }
    }
    rocksdb_register_tx(rocksdb_hton, thd, tx);
    tx->io_perf_start(&m_io_perf);
  }

  DBUG_RETURN(res);
}


/**
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
  Rdb_transaction* tx= get_or_create_tx(thd);
  read_thd_vars(thd);
  rocksdb_register_tx(ht, thd, tx);
  tx->io_perf_start(&m_io_perf);
  return 0;
}

rocksdb::Range get_range(
  uint32_t i, uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE*2], int offset1,
  int offset2)
{
  uchar* buf_begin= buf;
  uchar* buf_end= buf + Rdb_key_def::INDEX_NUMBER_SIZE;
  rdb_netbuf_store_index(buf_begin, i + offset1);
  rdb_netbuf_store_index(buf_end, i + offset2);

  return rocksdb::Range(
    rocksdb::Slice((const char*) buf_begin, Rdb_key_def::INDEX_NUMBER_SIZE),
    rocksdb::Slice((const char*) buf_end, Rdb_key_def::INDEX_NUMBER_SIZE));
}

static rocksdb::Range get_range(
  const std::shared_ptr<const Rdb_key_def>& kd,
  uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE*2],
  int offset1, int offset2)
{
  return get_range(kd->get_index_number(), buf, offset1, offset2);
}

rocksdb::Range get_range(const std::shared_ptr<const Rdb_key_def>& kd,
                         uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE*2])
{
  if (kd->m_is_reverse_cf)
  {
    return myrocks::get_range(kd, buf, 1, 0);
  }
  else
  {
    return myrocks::get_range(kd, buf, 0, 1);
  }
}

rocksdb::Range ha_rocksdb::get_range(
  int i, uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE*2]) const
{
  return myrocks::get_range(m_key_descr_arr[i], buf);
}


/*
  Drop index thread's main logic
*/

void Rdb_drop_index_thread::run()
{
  mysql_mutex_lock(&m_signal_mutex);

  for (;;) {
    // The stop flag might be set by shutdown command
    // after drop_index_thread releases signal_mutex
    // (i.e. while executing expensive Seek()). To prevent drop_index_thread
    // from entering long cond_timedwait, checking if stop flag
    // is true or not is needed, with drop_index_interrupt_mutex held.
    if (m_stop) {
      break;
    }

    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += dict_manager.is_drop_index_empty()
      ? 24*60*60 // no filtering
      : 60; // filtering

    auto ret __attribute__((__unused__)) = mysql_cond_timedwait(
        &m_signal_cond, &m_signal_mutex, &ts);
    if (m_stop) {
      break;
    }
    // make sure, no program error is returned
    DBUG_ASSERT(ret == 0 || ret == ETIMEDOUT);
    mysql_mutex_unlock(&m_signal_mutex);

    std::vector<GL_INDEX_ID> indices;
    dict_manager.get_ongoing_drop_indexes(&indices);
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
        bool is_reverse_cf= cf_flags & Rdb_key_def::REVERSE_CF_FLAG;

        bool index_removed= false;
        uchar key_buf[Rdb_key_def::INDEX_NUMBER_SIZE]= {0};
        rdb_netbuf_store_uint32(key_buf, d.index_id);
        rocksdb::Slice key = rocksdb::Slice((char*)key_buf, sizeof(key_buf));
        uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE*2];
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
          rdb_handle_io_error(status, RDB_IO_ERROR_BG_THREAD);
        }
        status = rdb->CompactRange(
            compact_range_options, cfh, &range.start, &range.limit);
        if (!status.ok())
        {
          if (status.IsShutdownInProgress())
          {
            break;
          }
          rdb_handle_io_error(status, RDB_IO_ERROR_BG_THREAD);
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
          if (memcmp(it->key().data(), key_buf, Rdb_key_def::INDEX_NUMBER_SIZE))
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
        dict_manager.finish_drop_indexes(finished);
      }
    }
    mysql_mutex_lock(&m_signal_mutex);
  }

  mysql_mutex_unlock(&m_signal_mutex);
}


Rdb_tbl_def* ha_rocksdb::get_table_if_exists(const char* tablename)
{
  DBUG_ASSERT(tablename != nullptr);

  std::string str;
  if (rdb_normalize_tablename(tablename, &str))
  {
    // We were not passed table name?
    DBUG_ASSERT(0);
    return nullptr;
  }

  return ddl_manager.find(str);
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
  Rdb_tbl_def *tbl = get_table_if_exists(tablename);
  if (!tbl)
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  dict_manager.add_drop_table(tbl->m_key_descr_arr, tbl->m_key_count, batch);

  /*
    Remove the table entry in data dictionary (this will also remove it from
    the persistent data dictionary).
  */
  ddl_manager.remove(tbl, batch, true);
  if (dict_manager.commit(batch))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  rdb_drop_idx_thread.signal();

  DBUG_RETURN(0);
}


void ha_rocksdb::remove_rows(Rdb_tbl_def *tbl)
{
  rocksdb::WriteOptions wo= rdb_get_rocksdb_write_options(handler::ha_thd());

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
  for (uint i= 0; i < tbl->m_key_count ; i++)
  {
    const std::shared_ptr<const Rdb_key_def>& kd= tbl->m_key_descr_arr[i];
    kd->get_infimum_key(reinterpret_cast<uchar*>(key_buf), &key_len);

    rocksdb::Slice table_key(key_buf, key_len);
    it->Seek(table_key);
    while (it->Valid())
    {
      rocksdb::Slice key= it->key();
      if (!kd->covers_key(key))
      {
        break;
      }

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
  DBUG_ASSERT(from != nullptr);
  DBUG_ASSERT(to != nullptr);

  bool write_err= false;
  std::string from_str;
  std::string to_str;
  std::string from_db;
  std::string to_db;

  if (rdb_normalize_tablename(from, &from_str) ||
      rdb_split_normalized_tablename(from_str, &from_db))
  {
    return HA_ERR_INTERNAL_ERROR;
  }

  if (rdb_normalize_tablename(to, &to_str) ||
      rdb_split_normalized_tablename(to_str, &to_db))
  {
    return HA_ERR_INTERNAL_ERROR;
  }

  // If the user changed the database part of the name then validate that the
  // 'to' database exists.
  if (from_db != to_db && !rdb_database_exists(to_db))
  {
    return HA_ERR_INTERNAL_ERROR;
  }

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
    Rdb_tbl_def *tdef;
    if (!(tdef= ddl_manager.find(from_str)))
      return HA_ERR_INTERNAL_ERROR;

    for (uint i= 0; i < tdef->m_key_count; i++)
    {
      DBUG_ASSERT(tdef->m_key_descr_arr != nullptr);

      if (tdef->m_key_descr_arr[i]->m_is_auto_cf)
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
  write_err= ddl_manager.rename(from_str, to_str, batch)
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
    m_keyread_only= true;
    break;
  case HA_EXTRA_NO_KEYREAD:
    m_keyread_only= false;
    break;
  case HA_EXTRA_FLUSH:
    /*
      If the table has blobs, then they are part of m_retrieved_record.
      This call invalidates them.
    */
    m_retrieved_record.clear();
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

  const std::shared_ptr<const Rdb_key_def>& kd= m_key_descr_arr[inx];
  DBUG_ASSERT(kd);

  uint size1=0;
  if (min_key) {
    size1 = kd->pack_index_tuple(table, m_pack_buffer, m_sk_packed_tuple,
                                 min_key->key, min_key->keypart_map);
    if (min_key->flag == HA_READ_PREFIX_LAST_OR_PREV ||
        min_key->flag == HA_READ_PREFIX_LAST ||
        min_key->flag == HA_READ_AFTER_KEY)
    {
      kd->successor(m_sk_packed_tuple, size1);
    }
  } else {
    kd->get_infimum_key(m_sk_packed_tuple, &size1);
  }

  uint size2=0;
  if (max_key) {
    size2 = kd->pack_index_tuple(table, m_pack_buffer, m_sk_packed_tuple_old,
                                 max_key->key, max_key->keypart_map);
    if (max_key->flag == HA_READ_PREFIX_LAST_OR_PREV ||
        max_key->flag == HA_READ_PREFIX_LAST ||
        max_key->flag == HA_READ_AFTER_KEY)
    {
      kd->successor(m_sk_packed_tuple_old, size2);
    }
    // pad the upper key with FFFFs to make sure it is more than the lower
    if (size1 > size2) {
      memset(m_sk_packed_tuple_old+size2, 0xff, size1-size2);
      size2 = size1;
    }
  } else {
    kd->get_supremum_key(m_sk_packed_tuple_old, &size2);
  }

  rocksdb::Slice slice1((const char*) m_sk_packed_tuple, size1);
  rocksdb::Slice slice2((const char*) m_sk_packed_tuple_old, size2);

  // slice1 >= slice2 means no row will match
  if (slice1.compare(slice2) >= 0)
  {
    DBUG_RETURN(0);
  }

  rocksdb::Range r(
    kd->m_is_reverse_cf ? slice2 : slice1,
    kd->m_is_reverse_cf ? slice1 : slice2
  );

  uint64_t sz=0;
  auto disk_size = kd->m_stats.m_actual_disk_size;
  if (disk_size == 0)
    disk_size = kd->m_stats.m_data_size;
  auto rows = kd->m_stats.m_rows;
  if (rows == 0 || disk_size == 0)
  {
    rows= 1;
    disk_size= ROCKSDB_ASSUMED_KEY_VALUE_DISK_SIZE;
  }
  // Getting statistics, including from Memtables
  rdb->GetApproximateSizes(
    kd->get_cf(),
    &r, 1,
    &sz, true);
  ret = rows*sz/disk_size;

  /*
    GetApproximateSizes() gives estimates so ret might exceed stats.records.
    MySQL then decides to use full index scan rather than range scan, which
    is not efficient for most cases.
    To prevent this, changing estimated records slightly smaller than
    stats.records.
  */
  if (ret >= stats.records)
  {
    ret = stats.records * 0.99;
  }

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
    create_info->auto_increment_value= m_tbl_def->m_auto_incr_val;
  }
}

/**
  @brief
  Doing manual compaction on OPTIMIZE TABLE in RocksDB.
  Compaction itself is executed by background thread in RocksDB, but
  CompactRange() waits until compaction completes so this function
  may take a long time.
  Since RocksDB dataset is allocated per index id, OPTIMIZE TABLE
  triggers manual compaction for all indexes of the table.
  @details
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
    uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE*2];
    auto range = get_range(i, buf);
    if (!rdb->CompactRange(rocksdb::CompactRangeOptions(),
                           m_key_descr_arr[i]->get_cf(),
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
  std::vector<uchar> buf(table->s->keys * 2 * Rdb_key_def::INDEX_NUMBER_SIZE);
  for (uint i = 0; i < table->s->keys; i++)
  {
    auto bufp = &buf[i * 2 * Rdb_key_def::INDEX_NUMBER_SIZE];
    const std::shared_ptr<const Rdb_key_def>& kd= m_key_descr_arr[i];
    ranges[kd->get_cf()].push_back(get_range(i, bufp));
    ids_to_check.insert(kd->get_gl_index_id());
  }

  // for analyze statements, force flush on memtable to get accurate cardinality
  Rdb_cf_manager& cf_manager= rdb_get_cf_manager();
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
    auto old_size __attribute__((__unused__)) = props.size();
    auto status = rdb->GetPropertiesOfTablesInRange(
      it.first, &it.second[0], it.second.size(), &props);
    DBUG_ASSERT(props.size() >= old_size);
    if (!status.ok())
      DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  int num_sst= 0;
  // group stats per index id
  std::unordered_map<GL_INDEX_ID, Rdb_index_stats> stats;
  for (auto it : props)
  {
    std::vector<Rdb_index_stats> sst_stats;
    Rdb_tbl_prop_coll::read_stats_from_tbl_props(it.second, &sst_stats);
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
      if (ids_to_check.find(it1.m_gl_index_id) == ids_to_check.end())
        continue;

      const std::shared_ptr<const Rdb_key_def>& kd=
          ddl_manager.find(it1.m_gl_index_id);
      stats[it1.m_gl_index_id].merge(it1, true, kd->max_storage_fmt_length());
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

  *first_value= (m_tbl_def->m_auto_incr_val += nb_desired_values) -
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


/**
  SQL layer calls this function to push an index condition.

  @details
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


/**
  Checking if an index is used for ascending scan or not

  @detail
  Currently RocksDB does not support bloom filter for
  prefix lookup + descending scan, but supports bloom filter for
  prefix lookup + ascending scan. This function returns true if
  the scan pattern is absolutely ascending.
  @param kd
  @param find_flag
*/
bool ha_rocksdb::is_ascending(const std::shared_ptr<const Rdb_key_def>& kd,
                              enum ha_rkey_function find_flag) const
{
  DBUG_ASSERT(kd != nullptr);

  bool is_ascending;
  switch (find_flag) {
  case HA_READ_KEY_EXACT:
  {
    is_ascending = !kd->m_is_reverse_cf;
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
    is_ascending = !kd->m_is_reverse_cf;
    break;
  }
  case HA_READ_KEY_OR_PREV:
  case HA_READ_BEFORE_KEY:
  case HA_READ_PREFIX_LAST:
  case HA_READ_PREFIX_LAST_OR_PREV:
  {
    is_ascending = kd->m_is_reverse_cf;
    break;
  }
  default:
    is_ascending= false;
  }
  return is_ascending;
}

/*
  Checks if inplace alter is supported for a given operation.
*/

my_core::enum_alter_inplace_result ha_rocksdb::check_if_supported_inplace_alter(
  TABLE* /* altered_table */,
  my_core::Alter_inplace_info *ha_alter_info)
{
  DBUG_ENTER("check_if_supported_alter");

  if (ha_alter_info->handler_flags &
      ~(my_core::Alter_inplace_info::DROP_INDEX |
        my_core::Alter_inplace_info::DROP_UNIQUE_INDEX |
        my_core::Alter_inplace_info::ADD_INDEX))
  {
      DBUG_RETURN(my_core::HA_ALTER_INPLACE_NOT_SUPPORTED);
  }

  DBUG_RETURN(my_core::HA_ALTER_INPLACE_SHARED_LOCK_AFTER_PREPARE);
}

/**
  Allows the storage engine to update internal structures with concurrent
  writes blocked. If check_if_supported_inplace_alter() returns
  HA_ALTER_INPLACE_NO_LOCK_AFTER_PREPARE or
  HA_ALTER_INPLACE_SHARED_AFTER_PREPARE, this function is called with
  exclusive lock otherwise the same level of locking as for
  inplace_alter_table() will be used.

  @note Storage engines are responsible for reporting any errors by
  calling my_error()/print_error()

  @note If this function reports error, commit_inplace_alter_table()
  will be called with commit= false.

  @note For partitioning, failing to prepare one partition, means that
  commit_inplace_alter_table() will be called to roll back changes for
  all partitions. This means that commit_inplace_alter_table() might be
  called without prepare_inplace_alter_table() having been called first
  for a given partition.

  @param    altered_table     TABLE object for new version of table.
  @param    ha_alter_info     Structure describing changes to be done
                              by ALTER TABLE and holding data used
                              during in-place alter.

  @retval   true              Error
  @retval   false             Success
*/
bool ha_rocksdb::prepare_inplace_alter_table(
    TABLE *altered_table,
    my_core::Alter_inplace_info *ha_alter_info)
{
  DBUG_ENTER("prepare_inplace_alter_table");
  uint n_dropped_keys= ha_alter_info->index_drop_count;
  uint n_added_keys= ha_alter_info->index_add_count;
  uint old_n_keys= m_tbl_def->m_key_count;
  uint new_n_keys= altered_table->s->keys;
  if (has_hidden_pk(altered_table))
  {
    new_n_keys += 1;
  }

  std::shared_ptr<Rdb_key_def>* old_key_descr= m_tbl_def->m_key_descr_arr;
  std::shared_ptr<Rdb_key_def>* new_key_descr=
      new std::shared_ptr<Rdb_key_def>[new_n_keys];

  Rdb_tbl_def* new_tdef= new Rdb_tbl_def(m_tbl_def->full_tablename());
  new_tdef->m_key_descr_arr= new_key_descr;
  new_tdef->m_key_count= new_n_keys;
  new_tdef->m_auto_incr_val=
    m_tbl_def->m_auto_incr_val.load(std::memory_order_relaxed);
  new_tdef->m_hidden_pk_val=
    m_tbl_def->m_hidden_pk_val.load(std::memory_order_relaxed);

  if (ha_alter_info->handler_flags &
     (my_core::Alter_inplace_info::DROP_INDEX |
      my_core::Alter_inplace_info::DROP_UNIQUE_INDEX |
      my_core::Alter_inplace_info::ADD_INDEX)
     && create_key_defs(altered_table, new_tdef, table, m_tbl_def))
  {
    /* Delete the new key descriptors */
    delete[] new_key_descr;

    /*
      Explicitly mark as nullptr so we don't accidentally remove entries
      from data dictionary on cleanup (or cause double delete[]).
    */
    new_tdef->m_key_descr_arr= nullptr;
    delete new_tdef;
    DBUG_RETURN(1);
  }

  std::unordered_set<std::shared_ptr<Rdb_key_def>> added_indexes;
  std::unordered_set<GL_INDEX_ID> dropped_index_ids;

  uint i;
  uint j;
  const KEY* key;

  /* Determine which(if any) key definition(s) need to be dropped */
  for (i = 0; i < ha_alter_info->index_drop_count; i++)
  {
    const KEY* key = ha_alter_info->index_drop_buffer[i];
    for (j = 0; j < old_n_keys; j++)
    {
      if (!old_key_descr[j]->m_name.compare(key->name))
      {
        dropped_index_ids.insert(old_key_descr[j]->get_gl_index_id());
        break;
      }
    }
  }

  DBUG_ASSERT(dropped_index_ids.size() == ha_alter_info->index_drop_count);

  /* Determine which(if any) key definitions(s) need to be added */
  for (i = 0; i < ha_alter_info->index_add_count; i++)
  {
    key = &ha_alter_info->key_info_buffer[
          ha_alter_info->index_add_buffer[i]];
    for (j = 0; j < new_n_keys; j++)
    {
      if (!new_key_descr[j]->m_name.compare(key->name))
      {
        added_indexes.insert(new_key_descr[j]);
        break;
      }
    }
  }

  DBUG_ASSERT(added_indexes.size() == ha_alter_info->index_add_count);

  ha_alter_info->handler_ctx= new Rdb_inplace_alter_ctx(
    new_tdef, old_key_descr, new_key_descr, old_n_keys, new_n_keys,
    added_indexes, dropped_index_ids, n_added_keys, n_dropped_keys);

  DBUG_RETURN(0);
}

/**
  Alter the table structure in-place with operations specified using
  HA_ALTER_FLAGS and Alter_inplace_info. The level of concurrency allowed
  during this operation depends on the return value from
  check_if_supported_inplace_alter().

  @note Storage engines are responsible for reporting any errors by
  calling my_error()/print_error()

  @note If this function reports error, commit_inplace_alter_table()
  will be called with commit= false.

  @param    altered_table     TABLE object for new version of table.
  @param    ha_alter_info     Structure describing changes to be done
                              by ALTER TABLE and holding data used
                              during in-place alter.

  @retval   true              Error
  @retval   false             Success
*/
bool ha_rocksdb::inplace_alter_table(TABLE* altered_table,
                                  my_core::Alter_inplace_info *ha_alter_info)
{
  /* Do nothing for now */
  DBUG_ENTER("inplace_alter_table");
  DBUG_ASSERT(ha_alter_info->handler_ctx);

  Rdb_inplace_alter_ctx* ctx=
    static_cast<Rdb_inplace_alter_ctx*> (ha_alter_info->handler_ctx);

  if (ha_alter_info->handler_flags & my_core::Alter_inplace_info::ADD_INDEX)
  {
    /*
      Buffers need to be set up again to account for new, possibly longer
      secondary keys.
    */
    free_key_buffers();
    if (alloc_key_buffers(altered_table, ctx->m_new_tdef))
    {
      DBUG_RETURN(1);
    }

    /* Populate all new secondary keys by scanning primary key */
    if (inplace_populate_sk(altered_table, ctx->m_added_indexes))
    {
      DBUG_RETURN(1);
    }
  }
  DBUG_EXECUTE_IF("myrocks_simulate_index_create_rollback", DBUG_RETURN(1););

  DBUG_RETURN(0);
}

/**
 Scan the Primary Key index entries and populate the new secondary keys.
*/
int ha_rocksdb::inplace_populate_sk(const TABLE* new_table_arg,
      const std::unordered_set<std::shared_ptr<Rdb_key_def>>& indexes)
{
  DBUG_ENTER("ha_rocksdb::inplace_populate_sk");
  std::unique_ptr<rocksdb::WriteBatch> wb= dict_manager.begin();
  rocksdb::WriteBatch *batch= wb.get();

  /* Update the data dictionary */
  std::unordered_set<GL_INDEX_ID> create_index_ids;
  for (auto& index : indexes)
  {
    create_index_ids.insert(index->get_gl_index_id());
  }
  dict_manager.add_create_index(create_index_ids, batch);
  dict_manager.commit(batch);

  /*
    Note: We pass in the currently existing table + tbl_def object here, as the
    pk index position may have changed in the case of hidden primary keys.
  */
  uint pk= pk_index(table, m_tbl_def);
  ha_index_init(pk, true);

  const bool hidden_pk_exists = has_hidden_pk(table);
  struct update_row_info row_info;

  row_info.tx = get_or_create_tx(table->in_use);
  row_info.new_data = table->record[0];
  row_info.old_data = nullptr;
  /* TODO(alexyang): no support for unique secondary keys yet */
  row_info.skip_unique_check = skip_unique_check();

  int res;
  for (res = index_first(table->record[0]); res == 0;
       res = index_next(table->record[0]))
  {
    for (auto& index : indexes)
    {
      if (hidden_pk_exists &&
          read_hidden_pk_id_from_rowkey(&row_info.hidden_pk_id))
      {
        // NO_LINT_DEBUG
        sql_print_error("Error retrieving hidden pk id.");
        ha_index_end();
        DBUG_RETURN(1);
      }

      res = update_sk(new_table_arg, index, row_info);
      if (res != 0)
      {
        // NO_LINT_DEBUG
        sql_print_error("Failed to create new secondary key entry.");
        ha_index_end();
        DBUG_RETURN(res);
      }

      if (do_bulk_commit(row_info.tx))
      {
        // NO_LINT_DEBUG
        sql_print_error("Bulk commit failed during index creation.");
        ha_index_end();
        DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
      }
    }
  }

  if (res != 0 && res != HA_ERR_END_OF_FILE)
  {
    // NO_LINT_DEBUG
    sql_print_error("Error retrieving index entry from primary key.");
    ha_index_end();
    DBUG_RETURN(1);
  }

  DBUG_EXECUTE_IF("crash_during_online_index_creation", DBUG_SUICIDE(););

  ha_index_end();

  DBUG_RETURN(0);
}

/**
  Commit or rollback the changes made during prepare_inplace_alter_table()
  and inplace_alter_table() inside the storage engine.
  Note that in case of rollback the allowed level of concurrency during
  this operation will be the same as for inplace_alter_table() and thus
  might be higher than during prepare_inplace_alter_table(). (For example,
  concurrent writes were blocked during prepare, but might not be during
  rollback).

  @note Storage engines are responsible for reporting any errors by
  calling my_error()/print_error()

  @note If this function with commit= true reports error, it will be called
  again with commit= false.

  @note In case of partitioning, this function might be called for rollback
  without prepare_inplace_alter_table() having been called first.
  Also partitioned tables sets ha_alter_info->group_commit_ctx to a NULL
  terminated array of the partitions handlers and if all of them are
  committed as one, then group_commit_ctx should be set to NULL to indicate
  to the partitioning handler that all partitions handlers are committed.
  @see prepare_inplace_alter_table().

  @param    altered_table     TABLE object for new version of table.
  @param    ha_alter_info     Structure describing changes to be done
                              by ALTER TABLE and holding data used
                              during in-place alter.
  @param    commit            True => Commit, False => Rollback.

  @retval   true              Error
  @retval   false             Success
*/
bool ha_rocksdb::commit_inplace_alter_table(
    my_core::TABLE *altered_table,
    my_core::Alter_inplace_info *ha_alter_info,
    bool commit)
{
  Rdb_inplace_alter_ctx* ctx0=
    static_cast<Rdb_inplace_alter_ctx*> (ha_alter_info->handler_ctx);

  DBUG_ENTER("commit_inplace_alter_table");

  /*
    IMPORTANT: When rollback is requested, mysql will abort with
    an assertion failure. That means every failed commit during inplace alter
    table will result in a fatal error on the server. Indexes ongoing creation
    will be detected when the server restarts, and dropped.

    For partitioned tables, a rollback call to this function (commit == false)
    is done for each partition.  A successful commit call only executes once
    for all partitions.
  */
  if (!commit)
  {
    /* If ctx has not been created yet, nothing to do here */
    if (!ctx0)
    {
      DBUG_RETURN(0);
    }

    /*
      Cannot call destructor for Rdb_tbl_def directly because we don't want to
      erase the mappings inside the ddl_manager, as the old_key_descr is still
      using them.
    */
    if (ctx0->m_new_key_descr)
    {
      /* Delete the new key descriptors */
      for (uint i = 0; i < ctx0->m_new_tdef->m_key_count; i++)
      {
        ctx0->m_new_key_descr[i]= nullptr;
      }

      delete[] ctx0->m_new_key_descr;
      ctx0->m_new_key_descr = nullptr;
      ctx0->m_new_tdef->m_key_descr_arr = nullptr;

      delete ctx0->m_new_tdef;
    }

    DBUG_RETURN(0);
  }

  DBUG_ASSERT(ctx0);

  /*
    For partitioned tables, we need to commit all changes to all tables at
    once, unlike in the other inplace alter API methods.
  */
  inplace_alter_handler_ctx** ctx_array;
  inplace_alter_handler_ctx*  ctx_single[2];

  if (ha_alter_info->group_commit_ctx)
  {
    DBUG_EXECUTE_IF("crash_during_index_creation_partition",
                    DBUG_SUICIDE(););
    ctx_array = ha_alter_info->group_commit_ctx;
  }
  else
  {
    ctx_single[0] = ctx0;
    ctx_single[1] = nullptr;
    ctx_array = ctx_single;
  }

  DBUG_ASSERT(ctx0 == ctx_array[0]);
  ha_alter_info->group_commit_ctx = nullptr;

  if (ha_alter_info->handler_flags &
      (my_core::Alter_inplace_info::DROP_INDEX |
       my_core::Alter_inplace_info::DROP_UNIQUE_INDEX |
       my_core::Alter_inplace_info::ADD_INDEX))
  {
    std::unique_ptr<rocksdb::WriteBatch> wb= dict_manager.begin();
    rocksdb::WriteBatch *batch= wb.get();
    std::unordered_set<GL_INDEX_ID> create_index_ids;

    m_tbl_def= ctx0->m_new_tdef;
    m_key_descr_arr= m_tbl_def->m_key_descr_arr;
    m_pk_descr= m_key_descr_arr[pk_index(altered_table, m_tbl_def)];

    dict_manager.lock();
    for (inplace_alter_handler_ctx** pctx = ctx_array; *pctx; pctx++)
    {
      Rdb_inplace_alter_ctx* ctx= static_cast<Rdb_inplace_alter_ctx*> (*pctx);

      /* Mark indexes to be dropped */
      dict_manager.add_drop_index(ctx->m_dropped_index_ids, batch);

      for (auto& index : ctx->m_added_indexes)
      {
        create_index_ids.insert(index->get_gl_index_id());
      }

      if (ddl_manager.put_and_write(ctx->m_new_tdef, batch))
      {
        /*
          Failed to write new entry into data dictionary, this should never
          happen.
        */
        DBUG_ASSERT(0);
      }
    }

    if (dict_manager.commit(batch))
    {
      /*
        Should never reach here. We assume MyRocks will abort if commit fails.
      */
      DBUG_ASSERT(0);
    }

    dict_manager.unlock();

    /* Mark ongoing create indexes as finished/remove from data dictionary */
    dict_manager.finish_indexes_operation(create_index_ids,
        Rdb_key_def::DDL_CREATE_INDEX_ONGOING);
    rdb_drop_idx_thread.signal();
  }

  DBUG_RETURN(0);
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
  DEF_STATUS_VAR_PTR("snapshot_conflict_errors",
                     &rocksdb_snapshot_conflict_errors,
                     SHOW_LONGLONG),
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
  Background thread's main logic
*/

void Rdb_background_thread::run()
{
  timespec ts_next_sync;
  clock_gettime(CLOCK_REALTIME, &ts_next_sync);
  ts_next_sync.tv_sec++;

  for (;;)
  {
    // wait for 1 second or until we received a condition to stop the thread
    mysql_mutex_lock(&m_signal_mutex);
    auto ret __attribute__((__unused__)) = mysql_cond_timedwait(
        &m_signal_cond, &m_signal_mutex, &ts_next_sync);
    // make sure that no program error is returned
    DBUG_ASSERT(ret == 0 || ret == ETIMEDOUT);
    bool local_stop= m_stop;
    bool local_save_stats= m_save_stats;
    reset();
    mysql_mutex_unlock(&m_signal_mutex);

    if (local_stop)
    {
      break;
    }

    if (local_save_stats)
    {
      ddl_manager.persist_stats();
    }

    // Flush the WAL if need be but don't do it more frequent
    // than once per second
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    if (ts.tv_sec - ts_next_sync.tv_sec >= 1)
    {
      if (rdb && rocksdb_background_sync)
      {
        DBUG_ASSERT(!rocksdb_db_options.allow_mmap_writes);
        rocksdb::Status s= rdb->SyncWAL();
        if (!s.ok())
          rdb_handle_io_error(s, RDB_IO_ERROR_BG_THREAD);
      }
      ts_next_sync.tv_sec= ts.tv_sec + 1;
    }
  }

  // save remaining stats which might've left unsaved
  ddl_manager.persist_stats();
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

  @param kd
  @param eq_cond      Equal condition part of the key. This always includes
                      system index id (4 bytes).
  @param use_all_keys True if all key parts are set with equal conditions.
                      This is aware of extended keys.
*/
bool can_use_bloom_filter(THD *thd,
                          const std::shared_ptr<const Rdb_key_def>& kd,
                          const rocksdb::Slice &eq_cond,
                          const bool use_all_keys,
                          bool is_ascending)
{
  bool can_use= false;

  if (THDVAR(thd, skip_bloom_filter_on_read))
  {
    return can_use;
  }

  rocksdb::Options opt = rdb->GetOptions(kd->get_cf());
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
rocksdb::DB *rdb_get_rocksdb_db()
{
  return rdb;
}

Rdb_cf_manager& rdb_get_cf_manager()
{
  return cf_manager;
}

rocksdb::BlockBasedTableOptions& rdb_get_table_options()
{
  return rocksdb_tbl_options;
}


int rdb_get_table_perf_counters(const char *tablename,
                                Rdb_perf_counters *counters)
{
  DBUG_ASSERT(counters != nullptr);
  DBUG_ASSERT(tablename != nullptr);

  Rdb_table_handler *table_handler;
  table_handler= rdb_open_tables.get_table_handler(tablename);
  if (table_handler == nullptr)
  {
    return HA_ERR_INTERNAL_ERROR;
  }

  counters->load(table_handler->m_table_perf_context);

  rdb_open_tables.release_table_handler(table_handler);
  return 0;
}


void rdb_handle_io_error(rocksdb::Status status, RDB_IO_ERROR_TYPE err_type)
{
  if (status.IsIOError())
  {
    switch (err_type) {
    case RDB_IO_ERROR_TX_COMMIT:
    case RDB_IO_ERROR_DICT_COMMIT:
    {
      sql_print_error("RocksDB: Failed to write to WAL - status %d, %s",
                      status.code(), status.ToString().c_str());
      sql_print_error("RocksDB: Aborting on WAL write error.");
      abort_with_stack_traces();
      break;
    }
    case RDB_IO_ERROR_BG_THREAD:
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
    switch (err_type) {
    case RDB_IO_ERROR_DICT_COMMIT:
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

Rdb_dict_manager *rdb_get_dict_manager(void)
{
  return &dict_manager;
}

Rdb_ddl_manager *rdb_get_ddl_manager(void)
{
  return &ddl_manager;
}

Rdb_binlog_manager *rdb_get_binlog_manager(void)
{
  return &binlog_manager;
}


void
rocksdb_set_compaction_options(
    my_core::THD*               thd     __attribute__((__unused__)),
    my_core::st_mysql_sys_var*  var     __attribute__((__unused__)),
    void*                       var_ptr,
    const void*                 save)
{
  if (var_ptr && save) {
    *(uint64_t*)var_ptr = *(const uint64_t*) save;
  }
  Rdb_compact_params params = {
    (uint64_t)rocksdb_compaction_sequential_deletes,
    (uint64_t)rocksdb_compaction_sequential_deletes_window,
    (uint64_t)rocksdb_compaction_sequential_deletes_file_size
  };
  if (properties_collector_factory) {
    properties_collector_factory->SetCompactionParams(params);
  }
}

void rocksdb_set_table_stats_sampling_pct(
    my_core::THD*               thd     __attribute__((__unused__)),
    my_core::st_mysql_sys_var*  var     __attribute__((__unused__)),
    void*                       var_ptr __attribute__((__unused__)),
    const void*                 save)
{
  mysql_mutex_lock(&rdb_sysvars_mutex);

  uint32_t new_val= *static_cast<const uint32_t*>(save);

  if (new_val != rocksdb_table_stats_sampling_pct) {
    rocksdb_table_stats_sampling_pct = new_val;

    if (properties_collector_factory) {
      properties_collector_factory->SetTableStatsSamplingPct(
        rocksdb_table_stats_sampling_pct);
    }
  }

  mysql_mutex_unlock(&rdb_sysvars_mutex);
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
rocksdb_set_rate_limiter_bytes_per_sec(
    my_core::THD*               thd,
    my_core::st_mysql_sys_var*  var     __attribute__((__unused__)),
    void*                       var_ptr __attribute__((__unused__)),
    const void*                 save)
{
  uint64_t new_val= *static_cast<const uint64_t*>(save);
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
    DBUG_ASSERT(rocksdb_rate_limiter != nullptr);
    rocksdb_rate_limiter_bytes_per_sec= new_val;
    rocksdb_rate_limiter->SetBytesPerSecond(new_val);
  }
}

void rdb_set_collation_exception_list(const char *exception_list)
{
  DBUG_ASSERT(rdb_collation_exceptions != nullptr);

  if (!rdb_collation_exceptions->set_patterns(exception_list))
  {
    my_core::warn_about_bad_patterns(rdb_collation_exceptions,
                                     "strict_collation_exceptions");
  }
}

void
rocksdb_set_collation_exception_list(THD*                     thd,
                                     struct st_mysql_sys_var* var,
                                     void*                    var_ptr,
                                     const void*              save)
{
  const char* val = *static_cast<const char*const*>(save);

  rdb_set_collation_exception_list(val);

  *static_cast<const char**>(var_ptr) = val;
}

void
rocksdb_set_bulk_load(THD*                     thd,
                      struct st_mysql_sys_var* var __attribute__((__unused__)),
                      void*                    var_ptr,
                      const void*              save)
{
  Rdb_transaction*& tx= get_tx_from_thd(thd);

  if (tx != nullptr)
  {
    int rc= tx->finish_bulk_load();
    if (rc != 0)
    {
      // NO_LINT_DEBUG
      sql_print_error("RocksDB: Error %d finalizing last SST file while "
                      "setting bulk loading variable", rc);
      abort_with_stack_traces();
    }
  }

  *static_cast<bool*>(var_ptr) = *static_cast<const bool*>(save);
}

void rdb_queue_save_stats_request()
{
  rdb_bg_thread.request_save_stats();
}

void ha_rocksdb::rpl_before_delete_rows()
{
  m_in_rpl_delete_rows= true;
}

void ha_rocksdb::rpl_after_delete_rows()
{
  m_in_rpl_delete_rows= false;
}

void ha_rocksdb::rpl_before_update_rows()
{
  m_in_rpl_update_rows= true;
}

void ha_rocksdb::rpl_after_update_rows()
{
  m_in_rpl_update_rows= false;
}

/**
  @brief
  Read Free Replication can be used or not. Returning False means
  Read Free Replication can be used. Read Free Replication can be used
  on UPDATE or DELETE row events, and table must have user defined
  primary key.
*/
bool ha_rocksdb::use_read_free_rpl()
{
  return ((m_in_rpl_delete_rows || m_in_rpl_update_rows) &&
      !has_hidden_pk(table) && m_use_read_free_rpl);
}

double ha_rocksdb::read_time(uint index, uint ranges, ha_rows rows)
{
  if (index != table->s->primary_key) {
    /* Non covering index range scan */
    return(handler::read_time(index, ranges, rows));
  }
  return (rows/20.0) + 1;
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
myrocks::rdb_i_s_cfstats,
myrocks::rdb_i_s_dbstats,
myrocks::rdb_i_s_perf_context,
myrocks::rdb_i_s_perf_context_global,
myrocks::rdb_i_s_cfoptions,
myrocks::rdb_i_s_global_info,
myrocks::rdb_i_s_ddl,
myrocks::rdb_i_s_index_file_map
mysql_declare_plugin_end;
