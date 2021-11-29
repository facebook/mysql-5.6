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
#pragma implementation  // gcc: Class implementation
#endif

#define MYSQL_SERVER 1

/* The C++ file's header */
#include "./ha_rocksdb.h"

#ifndef _WIN32
#include <errno.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#endif

/* C++ standard header files */
#include <inttypes.h>
#include <algorithm>
#include <deque>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

/* MySQL includes */
#include <mysql/components/services/log_builtins.h>
#include <mysql/psi/mysql_file.h>
#include <mysql/psi/mysql_table.h>
#include <mysql/thread_pool_priv.h>
#include <mysys_err.h>
#include "my_bit.h"
#include "my_stacktrace.h"
#include "my_sys.h"
#include "scope_guard.h"
#include "sql/binlog.h"
#include "sql/debug_sync.h"
#include "sql/json_dom.h"
#include "sql/sql_audit.h"
#include "sql/sql_class.h"
#include "sql/sql_lex.h"
#include "sql/sql_partition.h"
#include "sql/sql_table.h"
#include "sql/sql_thd_internal_api.h"

/* RocksDB includes */
#include "monitoring/histogram.h"
#include "rocksdb/compaction_filter.h"
#include "rocksdb/compaction_job_stats.h"
#include "rocksdb/env.h"
#include "rocksdb/env/composite_env_wrapper.h"
#include "rocksdb/memory_allocator.h"
#include "rocksdb/persistent_cache.h"
#include "rocksdb/rate_limiter.h"
#include "rocksdb/slice_transform.h"
#include "rocksdb/thread_status.h"
#include "rocksdb/trace_reader_writer.h"
#include "rocksdb/utilities/checkpoint.h"
#include "rocksdb/utilities/convenience.h"
#include "rocksdb/utilities/fault_injection_fs.h"
#include "rocksdb/utilities/memory_util.h"
#include "rocksdb/utilities/sim_cache.h"
#include "rocksdb/utilities/write_batch_with_index.h"
#include "util/stop_watch.h"

/* MyRocks includes */
#include "./event_listener.h"
#include "./ha_rocksdb_proto.h"
#include "./ha_rockspart.h"
#include "./logger.h"
#include "./nosql_access.h"
#include "./rdb_cf_manager.h"
#include "./rdb_cf_options.h"
#include "./rdb_converter.h"
#include "./rdb_datadic.h"
#include "./rdb_i_s.h"
#include "./rdb_index_merge.h"
#include "./rdb_iterator.h"
#include "./rdb_mutex_wrapper.h"
#include "./rdb_psi.h"
#include "./rdb_threads.h"

#include <rapidjson/document.h>

#ifdef FB_HAVE_WSENV
#include "./ObjectFactory.h"
#endif

#include "./rdb_locking_iter.h"


// Internal MySQL APIs not exposed in any header.
extern "C" {
/**
  Mark transaction to rollback and mark error as fatal to a sub-statement.
  @param  thd   Thread handle
  @param  all   true <=> rollback main transaction.
*/
void thd_mark_transaction_to_rollback(MYSQL_THD thd, int all);
}

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

namespace myrocks {

static st_global_stats global_stats;
static st_export_stats export_stats;
static st_memory_stats memory_stats;
static st_io_stall_stats io_stall_stats;
Rdb_compaction_stats compaction_stats;

const std::string DEFAULT_CF_NAME("default");
const std::string DEFAULT_SYSTEM_CF_NAME("__system__");
const std::string PER_INDEX_CF_NAME("$per_index_cf");
const std::string DEFAULT_SK_CF_NAME("default_sk");
const std::string TRUNCATE_TABLE_PREFIX("#truncate_tmp#");

static std::vector<std::string> rdb_tables_to_recalc;

static Rdb_exec_time st_rdb_exec_time;

int mysql_value_to_bool(struct st_mysql_value *value, bool *return_value);

class Rdb_explicit_snapshot : public explicit_snapshot {
 public:
  static std::shared_ptr<Rdb_explicit_snapshot> create(
      snapshot_info_st *ssinfo, rocksdb::DB *db,
      const rocksdb::Snapshot *snapshot) {
    std::lock_guard<std::mutex> lock(explicit_snapshot_mutex);
    auto s = std::unique_ptr<rocksdb::ManagedSnapshot>(
        new rocksdb::ManagedSnapshot(db, snapshot));
    if (!s) {
      return nullptr;
    }
    ssinfo->snapshot_id = ++explicit_snapshot_counter;
    auto ret = std::make_shared<Rdb_explicit_snapshot>(*ssinfo, std::move(s));
    if (!ret) {
      return nullptr;
    }
    explicit_snapshots[ssinfo->snapshot_id] = ret;
    return ret;
  }

  static std::string dump_snapshots() {
    std::string str;
    std::lock_guard<std::mutex> lock(explicit_snapshot_mutex);
    for (const auto &elem : explicit_snapshots) {
      const auto &ss = elem.second.lock();
      DBUG_ASSERT(ss != nullptr);
      const auto &info = ss->ss_info;
      str += "\nSnapshot ID: " + std::to_string(info.snapshot_id) +
             "\nBinlog File: " + info.binlog_file +
             "\nBinlog Pos: " + std::to_string(info.binlog_pos) +
             "\nGtid Executed: " + info.gtid_executed + "\n";
    }

    return str;
  }

  static std::shared_ptr<Rdb_explicit_snapshot> get(
      const ulonglong snapshot_id) {
    std::lock_guard<std::mutex> lock(explicit_snapshot_mutex);
    auto elem = explicit_snapshots.find(snapshot_id);
    if (elem == explicit_snapshots.end()) {
      return nullptr;
    }
    return elem->second.lock();
  }

  rocksdb::ManagedSnapshot *get_snapshot() { return snapshot.get(); }

  Rdb_explicit_snapshot(snapshot_info_st ssinfo,
                        std::unique_ptr<rocksdb::ManagedSnapshot> &&snapshot)
      : explicit_snapshot(ssinfo), snapshot(std::move(snapshot)) {}

  virtual ~Rdb_explicit_snapshot() {
    std::lock_guard<std::mutex> lock(explicit_snapshot_mutex);
    explicit_snapshots.erase(ss_info.snapshot_id);
  }

 private:
  std::unique_ptr<rocksdb::ManagedSnapshot> snapshot;

  static std::mutex explicit_snapshot_mutex;
  static ulonglong explicit_snapshot_counter;
  static std::unordered_map<ulonglong, std::weak_ptr<Rdb_explicit_snapshot>>
      explicit_snapshots;
};

std::mutex Rdb_explicit_snapshot::explicit_snapshot_mutex;
ulonglong Rdb_explicit_snapshot::explicit_snapshot_counter = 0;
std::unordered_map<ulonglong, std::weak_ptr<Rdb_explicit_snapshot>>
    Rdb_explicit_snapshot::explicit_snapshots;

/**
  Updates row counters based on the table type and operation type.
*/
void ha_rocksdb::update_row_stats(const operation_type &type, ulonglong count) {
  DBUG_ASSERT(type < ROWS_MAX);
  // Find if we are modifying system databases.
  if (table->s && m_tbl_def->m_is_mysql_system_table) {
    global_stats.system_rows[type].add(count);
  } else {
    global_stats.rows[type].add(count);
  }
}

void ha_rocksdb::update_row_read(ulonglong count) {
  update_row_stats(ROWS_READ, count);
}

void ha_rocksdb::inc_covered_sk_lookup() {
  global_stats.covered_secondary_key_lookups.inc();
}

void dbug_dump_database(rocksdb::DB *db);
static handler *rocksdb_create_handler(my_core::handlerton *hton,
                                       my_core::TABLE_SHARE *table_arg,
                                       bool partitioned,
                                       my_core::MEM_ROOT *mem_root);

static rocksdb::CompactRangeOptions getCompactRangeOptions(
    int concurrency = 0,
    rocksdb::BottommostLevelCompaction bottommost_level_compaction =
        rocksdb::BottommostLevelCompaction::kForceOptimized) {
  rocksdb::CompactRangeOptions compact_range_options;
  compact_range_options.bottommost_level_compaction =
      bottommost_level_compaction;
  compact_range_options.exclusive_manual_compaction = false;
  if (concurrency > 0) {
    compact_range_options.max_subcompactions = concurrency;
  }
  return compact_range_options;
}

///////////////////////////////////////////////////////////
// Parameters and settings
///////////////////////////////////////////////////////////
static char *rocksdb_default_cf_options = nullptr;
static char *rocksdb_override_cf_options = nullptr;
static char *rocksdb_update_cf_options = nullptr;
static bool rocksdb_use_default_sk_cf = false;

///////////////////////////////////////////////////////////
// Globals
///////////////////////////////////////////////////////////
handlerton *rocksdb_hton;

rocksdb::TransactionDB *rdb = nullptr;
rocksdb::HistogramImpl *commit_latency_stats = nullptr;

static std::shared_ptr<rocksdb::Statistics> rocksdb_stats;
static std::unique_ptr<rocksdb::Env> flashcache_aware_env;
static std::shared_ptr<Rdb_tbl_prop_coll_factory> properties_collector_factory;

Rdb_dict_manager dict_manager;
Rdb_cf_manager cf_manager;
Rdb_ddl_manager ddl_manager;
Rdb_binlog_manager binlog_manager;
Rdb_io_watchdog *io_watchdog = nullptr;

/**
  MyRocks background thread control
  N.B. This is besides RocksDB's own background threads
       (@see rocksdb::CancelAllBackgroundWork())
*/

static Rdb_background_thread rdb_bg_thread;

static Rdb_index_stats_thread rdb_is_thread;

static Rdb_manual_compaction_thread rdb_mc_thread;

static Rdb_drop_index_thread rdb_drop_idx_thread;
// List of table names (using regex) that are exceptions to the strict
// collation check requirement.
Regex_list_handler *rdb_collation_exceptions;

static const char *rdb_get_error_message(int nr);

static void rocksdb_flush_all_memtables() {
  const Rdb_cf_manager &cf_manager = rdb_get_cf_manager();

  if (!cf_manager.is_initialized()) return;

  // RocksDB will fail the flush if the CF is deleted,
  // but here we don't handle return status
  for (const auto &cf_handle : cf_manager.get_all_cf()) {
    rdb->Flush(rocksdb::FlushOptions(), cf_handle.get());
  }
}

static void rocksdb_delete_column_family_stub(THD *const /* thd */,
                                              struct SYS_VAR *const /* var */,
                                              void *const /* var_ptr */,
                                              const void *const /* save */) {}

static int rocksdb_delete_column_family(THD *const /* thd */,
                                        struct SYS_VAR *const /* var */,
                                        void *const /* var_ptr */,
                                        struct st_mysql_value *const value) {
  char buff[STRING_BUFFER_USUAL_SIZE];
  int len = sizeof(buff);

  const char *const cf = value->val_str(value, buff, &len);
  if (cf == nullptr) return HA_EXIT_SUCCESS;

  std::string cf_name = std::string(cf);
  // Forbid to remove these built-in CFs
  if (cf_name == DEFAULT_SYSTEM_CF_NAME || cf_name == DEFAULT_CF_NAME ||
      cf_name.empty() ||
      (cf_name == DEFAULT_SK_CF_NAME && rocksdb_use_default_sk_cf)) {
    my_error(ER_CANT_DROP_CF, MYF(0), cf);
    return HA_EXIT_FAILURE;
  }

  auto &cf_manager = rdb_get_cf_manager();
  int ret = 0;

  {
    std::lock_guard<Rdb_dict_manager> dm_lock(dict_manager);
    ret = cf_manager.drop_cf(&ddl_manager, &dict_manager, cf_name);
  }

  if (ret == HA_EXIT_SUCCESS) {
    rdb_drop_idx_thread.signal();
  } else {
    my_error(ER_CANT_DROP_CF, MYF(0), cf);
  }

  return ret;
}

///////////////////////////////////////////////////////////
// Hash map: table name => open table handler
///////////////////////////////////////////////////////////

namespace  // anonymous namespace = not visible outside this source file
{

class Rdb_open_tables_map {
 private:
  /* Hash table used to track the handlers of open tables */
  std::unordered_map<std::string, Rdb_table_handler *> m_table_map;

  /* The mutex used to protect the hash table */
  mutable Rds_mysql_mutex m_mutex;

 public:
  void init() {
    m_table_map.clear();
    m_mutex.init(rdb_psi_open_tbls_mutex_key, MY_MUTEX_INIT_FAST);
  }

  void free() {
    m_table_map.clear();
    m_mutex.destroy();
  }

  size_t count() { return m_table_map.size(); }

  Rdb_table_handler *get_table_handler(const char *const table_name);
  void release_table_handler(Rdb_table_handler *const table_handler);

  std::vector<std::string> get_table_names(void) const;
};

}  // anonymous namespace

static Rdb_open_tables_map rdb_open_tables;

static std::string rdb_normalize_dir(std::string dir) {
  while (dir.size() > 0 && dir.back() == '/') {
    dir.resize(dir.size() - 1);
  }
  return dir;
}

static int rocksdb_create_checkpoint(const char *checkpoint_dir_raw) {
  DBUG_ASSERT(checkpoint_dir_raw);

  const auto checkpoint_dir = rdb_normalize_dir(checkpoint_dir_raw);
  sql_print_information("creating checkpoint in directory: %s\n",
                        checkpoint_dir.c_str());
  rocksdb::Checkpoint *checkpoint;
  auto status = rocksdb::Checkpoint::Create(rdb, &checkpoint);
  if (status.ok()) {
    status = checkpoint->CreateCheckpoint(checkpoint_dir.c_str());
    delete checkpoint;
    if (status.ok()) {
      sql_print_information("created checkpoint in directory: %s\n",
                            checkpoint_dir.c_str());
      return HA_EXIT_SUCCESS;
    } else {
      my_error(ER_GET_ERRMSG, MYF(0), status.code(), status.ToString().c_str(),
               rocksdb_hton_name);
    }
  } else {
    my_error(ER_GET_ERRMSG, MYF(0), status.code(), status.ToString().c_str(),
             rocksdb_hton_name);
  }

  return HA_EXIT_FAILURE;
}

static int rocksdb_remove_checkpoint(const char *checkpoint_dir_raw) {
  const auto checkpoint_dir = rdb_normalize_dir(checkpoint_dir_raw);
  sql_print_information("deleting temporary checkpoint in directory : %s\n",
                        checkpoint_dir.c_str());
  const auto status = rocksdb::DestroyDB(checkpoint_dir, rocksdb::Options());
  if (status.ok()) {
    return HA_EXIT_SUCCESS;
  }
  my_error(ER_GET_ERRMSG, MYF(0), status.code(), status.ToString().c_str(),
           rocksdb_hton_name);
  return HA_EXIT_FAILURE;
}

static int rocksdb_create_checkpoint_validate(
    THD *const thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const save MY_ATTRIBUTE((__unused__)),
    struct st_mysql_value *const value) {
  char buf[FN_REFLEN];
  int len = sizeof(buf);
  const char *const checkpoint_dir_raw = value->val_str(value, buf, &len);
  if (checkpoint_dir_raw) {
    return rocksdb_create_checkpoint(checkpoint_dir_raw);
  }
  return HA_EXIT_FAILURE;
}

/* This method is needed to indicate that the
   ROCKSDB_CREATE_CHECKPOINT command is not read-only */
static void rocksdb_create_checkpoint_update(
    THD *const thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)),
    const void *const save MY_ATTRIBUTE((__unused__))) {}

static int rocksdb_create_temporary_checkpoint_validate(
    THD *const thd, struct SYS_VAR *const var, void *const save,
    struct st_mysql_value *const value);

static void rocksdb_disable_file_deletions_update(
    my_core::THD *const thd, my_core::SYS_VAR *const /* unused */,
    void *const var_ptr, const void *const save);

static void rocksdb_max_compaction_history_update(
    my_core::THD *const thd, my_core::SYS_VAR *const /* unused */,
    void *const var_ptr, const void *const save);

static void rocksdb_select_bypass_rejected_query_history_size_update(
    my_core::THD *const thd, my_core::SYS_VAR *const /* unused */,
    void *const var_ptr, const void *const save);

static bool parse_fault_injection_params(bool *retryable,
                                         uint32_t *failure_ratio,
                                         std::vector<rocksdb::FileType> *types);

static void rocksdb_force_flush_memtable_now_stub(
    THD *const thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)),
    const void *const save MY_ATTRIBUTE((__unused__))) {}

static int rocksdb_force_flush_memtable_now(
    THD *const thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)),
    struct st_mysql_value *const value MY_ATTRIBUTE((__unused__))) {
  bool parsed_value = false;
  if (mysql_value_to_bool(value, &parsed_value) != 0 || !parsed_value) {
    return 1;
  }

  // NO_LINT_DEBUG
  sql_print_information("RocksDB: Manual memtable flush.");
  rocksdb_flush_all_memtables();
  return HA_EXIT_SUCCESS;
}

static void rocksdb_force_flush_memtable_and_lzero_now_stub(
    THD *const thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)),
    const void *const save MY_ATTRIBUTE((__unused__))) {}

static int rocksdb_force_flush_memtable_and_lzero_now(
    THD *const thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)),
    struct st_mysql_value *const value MY_ATTRIBUTE((__unused__))) {
  // NO_LINT_DEBUG
  sql_print_information("RocksDB: Manual memtable and L0 flush.");
  rocksdb_flush_all_memtables();

  const Rdb_cf_manager &cf_manager = rdb_get_cf_manager();
  rocksdb::CompactionOptions c_options = rocksdb::CompactionOptions();
  rocksdb::ColumnFamilyMetaData metadata;
  rocksdb::ColumnFamilyDescriptor cf_descr;

  int i, max_attempts = 3, num_errors = 0;

  for (const auto &cf_handle : cf_manager.get_all_cf()) {
    for (i = 0; i < max_attempts; i++) {
      rdb->GetColumnFamilyMetaData(cf_handle.get(), &metadata);
      cf_handle->GetDescriptor(&cf_descr);
      c_options.output_file_size_limit = cf_descr.options.target_file_size_base;

      DBUG_ASSERT(metadata.levels[0].level == 0);
      std::vector<std::string> file_names;
      for (auto &file : metadata.levels[0].files) {
        file_names.emplace_back(file.db_path + file.name);
      }

      if (file_names.empty()) {
        break;
      }

      rocksdb::Status s;
      s = rdb->CompactFiles(c_options, cf_handle.get(), file_names, 1);

      if (!s.ok()) {
        std::shared_ptr<rocksdb::ColumnFamilyHandle> cfh =
            cf_manager.get_cf(cf_handle->GetID());

        // If the CF handle has been removed from cf_manager, it is not an
        // error. We are done with this CF and proceed to the next CF.
        if (!cfh) {
          // NO_LINT_DEBUG
          sql_print_information("cf %s has been dropped during CompactFiles.",
                                cf_handle->GetName().c_str());
          break;
        }

        // Due to a race, it's possible for CompactFiles to collide
        // with auto compaction, causing an error to return
        // regarding file not found. In that case, retry.
        if (s.IsInvalidArgument()) {
          continue;
        }

        if (!s.ok() && !s.IsAborted()) {
          rdb_handle_io_error(s, RDB_IO_ERROR_GENERAL);
          return HA_EXIT_FAILURE;
        }
        break;
      }
    }
    if (i == max_attempts) {
      num_errors++;
    }
  }

  return num_errors == 0 ? HA_EXIT_SUCCESS : HA_EXIT_FAILURE;
}

static void rocksdb_cancel_manual_compactions_stub(
    THD *const thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)),
    const void *const save MY_ATTRIBUTE((__unused__))) {}

static int rocksdb_cancel_manual_compactions(
    THD *const thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)),
    struct st_mysql_value *const value MY_ATTRIBUTE((__unused__))) {
  rdb_mc_thread.cancel_all_pending_manual_compaction_requests();
  // NO_LINT_DEBUG
  sql_print_information("RocksDB: Stopping all Manual Compactions.");
  rdb->GetBaseDB()->DisableManualCompaction();
  // NO_LINT_DEBUG
  sql_print_information("RocksDB: Enabling Manual Compactions.");
  rdb->GetBaseDB()->EnableManualCompaction();
  return HA_EXIT_SUCCESS;
}

static void rocksdb_drop_index_wakeup_thread(
    my_core::THD *const thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)), const void *const save);

static bool rocksdb_pause_background_work = 0;
static Rds_mysql_mutex rdb_sysvars_mutex;
static Rds_mysql_mutex rdb_block_cache_resize_mutex;
static Rds_mysql_mutex rdb_bottom_pri_background_compactions_resize_mutex;

static void rocksdb_set_pause_background_work(
    my_core::THD *const thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)), const void *const save) {
  RDB_MUTEX_LOCK_CHECK(rdb_sysvars_mutex);
  const bool pause_requested = *static_cast<const bool *>(save);
  if (rocksdb_pause_background_work != pause_requested) {
    if (pause_requested) {
      rdb->PauseBackgroundWork();
    } else {
      rdb->ContinueBackgroundWork();
    }
    rocksdb_pause_background_work = pause_requested;
  }
  RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
}

static void rocksdb_set_compaction_options(THD *thd, struct SYS_VAR *var,
                                           void *var_ptr, const void *save);

static void rocksdb_set_table_stats_sampling_pct(THD *thd, struct SYS_VAR *var,
                                                 void *var_ptr,
                                                 const void *save);

static void rocksdb_update_table_stats_use_table_scan(
    THD *const /* thd */, struct SYS_VAR *const /* var */, void *const var_ptr,
    const void *const save);

static int rocksdb_index_stats_thread_renice(
    THD *const /* thd */, struct SYS_VAR *const /* var */, void *const save,
    struct st_mysql_value *const value);

static void rocksdb_set_rate_limiter_bytes_per_sec(THD *thd,
                                                   struct SYS_VAR *var,
                                                   void *var_ptr,
                                                   const void *save);

static void rocksdb_set_sst_mgr_rate_bytes_per_sec(THD *thd,
                                                   struct SYS_VAR *var,
                                                   void *var_ptr,
                                                   const void *save);

static void rocksdb_set_delayed_write_rate(THD *thd, struct SYS_VAR *var,
                                           void *var_ptr, const void *save);

static void rocksdb_set_max_latest_deadlocks(THD *thd, struct SYS_VAR *var,
                                             void *var_ptr, const void *save);

static void rocksdb_set_max_lock_memory(THD *thd,
                                        struct SYS_VAR *var,
                                        void *var_ptr, const void *save);

static void rdb_set_collation_exception_list(const char *exception_list);
static void rocksdb_set_collation_exception_list(THD *thd, struct SYS_VAR *var,
                                                 void *var_ptr,
                                                 const void *save);

static int rocksdb_validate_update_cf_options(THD *thd, struct SYS_VAR *var,
                                              void *save,
                                              st_mysql_value *value);

static void rocksdb_set_update_cf_options(THD *thd, struct SYS_VAR *var,
                                          void *var_ptr, const void *save);

static int rocksdb_check_bulk_load(
    THD *const thd, struct SYS_VAR *var MY_ATTRIBUTE((__unused__)), void *save,
    struct st_mysql_value *value);

static int rocksdb_check_bulk_load_allow_unsorted(
    THD *const thd, struct SYS_VAR *var MY_ATTRIBUTE((__unused__)), void *save,
    struct st_mysql_value *value);

static void rocksdb_set_max_background_jobs(THD *thd, struct SYS_VAR *const var,
                                            void *const var_ptr,
                                            const void *const save);
static void rocksdb_set_max_background_compactions(THD *thd,
                                                   struct SYS_VAR *const var,
                                                   void *const var_ptr,
                                                   const void *const save);
static void rocksdb_set_bytes_per_sync(THD *thd, struct SYS_VAR *const var,
                                       void *const var_ptr,
                                       const void *const save);
static void rocksdb_set_wal_bytes_per_sync(THD *thd, struct SYS_VAR *const var,
                                           void *const var_ptr,
                                           const void *const save);
static int rocksdb_validate_set_block_cache_size(THD *thd,
                                                 struct SYS_VAR *const var,
                                                 void *var_ptr,
                                                 struct st_mysql_value *value);
static int rocksdb_tracing(THD *const thd MY_ATTRIBUTE((__unused__)),
                           struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
                           void *const save, struct st_mysql_value *const value,
                           bool trace_block_cache_access = true);
static int rocksdb_validate_max_bottom_pri_background_compactions(
    THD *thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)), void *var_ptr,
    struct st_mysql_value *value);
//////////////////////////////////////////////////////////////////////////////
// Options definitions
//////////////////////////////////////////////////////////////////////////////
static long long rocksdb_block_cache_size;
static long long rocksdb_sim_cache_size;
static bool rocksdb_use_clock_cache;
static double rocksdb_cache_high_pri_pool_ratio;
static bool rocksdb_cache_dump;
/* Use unsigned long long instead of uint64_t because of MySQL compatibility */
static unsigned long long  // NOLINT(runtime/int)
    rocksdb_rate_limiter_bytes_per_sec;
static unsigned long long  // NOLINT(runtime/int)
    rocksdb_sst_mgr_rate_bytes_per_sec;
static unsigned long long rocksdb_delayed_write_rate;
static uint32_t rocksdb_max_latest_deadlocks;
static unsigned long  // NOLINT(runtime/int)
    rocksdb_persistent_cache_size_mb;
static uint64_t rocksdb_info_log_level;
static char *rocksdb_wal_dir;
static char *rocksdb_persistent_cache_path;
static char *rocksdb_wsenv_path;
static char *rocksdb_wsenv_tenant;
static uint64_t rocksdb_index_type;
static uint32_t rocksdb_flush_log_at_trx_commit;
static uint32_t rocksdb_debug_optimizer_n_rows;
static bool rocksdb_force_compute_memtable_stats;
static uint32_t rocksdb_force_compute_memtable_stats_cachetime;
static bool rocksdb_debug_optimizer_no_zero_cardinality;
static uint32_t rocksdb_debug_cardinality_multiplier;
static uint32_t rocksdb_wal_recovery_mode;
static bool rocksdb_track_and_verify_wals_in_manifest;
static uint32_t rocksdb_stats_level;
static uint32_t rocksdb_access_hint_on_compaction_start;
static char *rocksdb_compact_cf_name;
static char *rocksdb_delete_cf_name;
static char *rocksdb_checkpoint_name;
static char *rocksdb_block_cache_trace_options_str;
static char *rocksdb_trace_options_str;
static bool rocksdb_signal_drop_index_thread;
static bool rocksdb_strict_collation_check = 1;
static bool rocksdb_ignore_unknown_options = 1;
static bool rocksdb_enable_2pc = 0;
static char *rocksdb_strict_collation_exceptions;
static bool rocksdb_collect_sst_properties = 1;
static bool rocksdb_force_flush_memtable_now_var = 1;
static bool rocksdb_force_flush_memtable_and_lzero_now_var = 0;
static bool rocksdb_cancel_manual_compactions_var = 0;
static bool rocksdb_enable_ttl = 1;
static bool rocksdb_enable_ttl_read_filtering = 1;
static int rocksdb_debug_ttl_rec_ts = 0;
static int rocksdb_debug_ttl_snapshot_ts = 0;
static int rocksdb_debug_ttl_read_filter_ts = 0;
static bool rocksdb_debug_ttl_ignore_pk = 0;
static bool rocksdb_reset_stats = 0;
static uint32_t rocksdb_io_write_timeout_secs = 0;
static uint32_t rocksdb_seconds_between_stat_computes = 3600;
static long long rocksdb_compaction_sequential_deletes = 0l;
static long long rocksdb_compaction_sequential_deletes_window = 0l;
static long long rocksdb_compaction_sequential_deletes_file_size = 0l;
static uint32_t rocksdb_validate_tables = 1;
static char *rocksdb_datadir;
static uint32_t rocksdb_max_bottom_pri_background_compactions = 0;
static uint32_t rocksdb_table_stats_sampling_pct;
static uint32_t rocksdb_table_stats_recalc_threshold_pct = 10;
static unsigned long long rocksdb_table_stats_recalc_threshold_count = 100ul;
static bool rocksdb_table_stats_use_table_scan = 0;
static char *opt_rocksdb_fault_injection_options = nullptr;
static int32_t rocksdb_table_stats_background_thread_nice_value =
    THREAD_PRIO_MAX;
static unsigned long long rocksdb_table_stats_max_num_rows_scanned = 0ul;
static bool rocksdb_enable_bulk_load_api = 1;
static bool rocksdb_enable_remove_orphaned_dropped_cfs = 1;
static bool rocksdb_print_snapshot_conflict_queries = 0;
static bool rocksdb_large_prefix = 0;
static bool rocksdb_allow_to_start_after_corruption = 0;
static uint64_t rocksdb_write_policy =
    rocksdb::TxnDBWritePolicy::WRITE_COMMITTED;
char *rocksdb_read_free_rpl_tables;
std::mutex rocksdb_read_free_rpl_tables_mutex;
#if defined(HAVE_PSI_INTERFACE)
Regex_list_handler rdb_read_free_regex_handler(key_rwlock_read_free_rpl_tables);
#else
Regex_list_handler rdb_read_free_regex_handler;
#endif
enum read_free_rpl_type { OFF = 0, PK_ONLY, PK_SK };
static uint64_t rocksdb_read_free_rpl = read_free_rpl_type::OFF;
static bool rocksdb_error_on_suboptimal_collation = 1;
static uint32_t rocksdb_stats_recalc_rate = 0;
static uint32_t rocksdb_debug_manual_compaction_delay = 0;
static uint32_t rocksdb_max_manual_compactions = 0;
static bool rocksdb_rollback_on_timeout = false;
static bool rocksdb_enable_insert_with_update_caching = true;
/* Use unsigned long long instead of uint64_t because of MySQL compatibility */
static unsigned long long  // NOLINT(runtime/int)
    rocksdb_max_compaction_history = 0;
static uint64_t rocksdb_select_bypass_policy =
    select_bypass_policy_type::default_value;
static bool rocksdb_select_bypass_fail_unsupported = true;
static bool rocksdb_select_bypass_log_rejected = true;
static bool rocksdb_select_bypass_log_failed = false;
static bool rocksdb_select_bypass_allow_filters = true;
static uint32_t rocksdb_select_bypass_rejected_query_history_size = 0;
static uint32_t rocksdb_select_bypass_debug_row_delay = 0;
static unsigned long long  // NOLINT(runtime/int)
    rocksdb_select_bypass_multiget_min = 0;
static bool rocksdb_skip_locks_if_skip_unique_check = false;
static bool rocksdb_alter_column_default_inplace = false;
static bool rocksdb_instant_ddl = false;

// Range Locking: how much memory can be used used for the lock data structure
//  (which hold the locks acquired by all clients).
static ulonglong rocksdb_max_lock_memory;

bool rocksdb_use_range_locking = 0;
static bool rocksdb_use_range_lock_manager_as_point = 0;
std::shared_ptr<rocksdb::RangeLockManagerHandle> range_lock_mgr;

std::shared_ptr<rocksdb::RangeLockManagerHandle> range_lock_mgr_used_as_point;

std::atomic<uint64_t> rocksdb_row_lock_deadlocks(0);
std::atomic<uint64_t> rocksdb_row_lock_wait_timeouts(0);
std::atomic<uint64_t> rocksdb_snapshot_conflict_errors(0);
std::atomic<uint64_t> rocksdb_wal_group_syncs(0);
std::atomic<uint64_t> rocksdb_manual_compactions_processed(0);
std::atomic<uint64_t> rocksdb_manual_compactions_cancelled(0);
std::atomic<uint64_t> rocksdb_manual_compactions_running(0);
std::atomic<uint64_t> rocksdb_manual_compactions_pending(0);
#ifndef DBUG_OFF
std::atomic<uint64_t> rocksdb_num_get_for_update_calls(0);
#endif
std::atomic<uint64_t> rocksdb_select_bypass_executed(0);
std::atomic<uint64_t> rocksdb_select_bypass_rejected(0);
std::atomic<uint64_t> rocksdb_select_bypass_failed(0);

std::atomic<uint64_t> rocksdb_partial_index_groups_sorted(0);
std::atomic<uint64_t> rocksdb_partial_index_groups_materialized(0);
std::atomic<uint64_t> rocksdb_partial_index_rows_sorted(0);
std::atomic<uint64_t> rocksdb_partial_index_rows_materialized(0);

static int rocksdb_trace_block_cache_access(
    THD *const thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)), void *const save,
    struct st_mysql_value *const value) {
  return rocksdb_tracing(thd, var, save, value,
                         /* trace_block_cache_accecss = */ true);
}

static int rocksdb_trace_queries(
    THD *const thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)), void *const save,
    struct st_mysql_value *const value) {
  return rocksdb_tracing(thd, var, save, value,
                         /* trace_block_cache_accecss = */ false);
}

/* This method is needed to indicate that the
  ROCKSDB_TRACE_BLOCK_CACHE_ACCESS or ROCKSDB_TRACE_QUERIES command is not
  read-only */
static void rocksdb_trace_stub(THD *const thd MY_ATTRIBUTE((__unused__)),
                               struct SYS_VAR *const var
                                   MY_ATTRIBUTE((__unused__)),
                               void *const var_ptr, const void *const save) {
  const auto trace_opt_str_raw = *static_cast<const char *const *>(save);
  DBUG_ASSERT(trace_opt_str_raw != nullptr);
  *static_cast<const char **>(var_ptr) = trace_opt_str_raw;
}

static int rocksdb_tracing(THD *const thd MY_ATTRIBUTE((__unused__)),
                           struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
                           void *const save, struct st_mysql_value *const value,
                           bool trace_block_cache_access) {
  std::string trace_folder =
      trace_block_cache_access ? "/block_cache_traces" : "/queries_traces";
  int len = 0;
  const char *const trace_opt_str_raw = value->val_str(value, nullptr, &len);
  rocksdb::Status s;
  if (trace_opt_str_raw == nullptr || rdb == nullptr) {
    return HA_EXIT_FAILURE;
  }
  int rc __attribute__((__unused__));
  std::string trace_opt_str(trace_opt_str_raw);
  if (trace_opt_str.empty()) {
    // End tracing block cache accesses or queries.
    // NO_LINT_DEBUG
    sql_print_information(
        "RocksDB: Stop tracing block cache accesses or queries.\n");
    s = trace_block_cache_access ? rdb->EndBlockCacheTrace() : rdb->EndTrace();

    if (!s.ok()) {
      rc = ha_rocksdb::rdb_error_to_mysql(s);
      return HA_EXIT_FAILURE;
    }
    *static_cast<const char **>(save) = "";
    return HA_EXIT_SUCCESS;
  }

  // Start tracing block cache accesses or queries.
  std::stringstream ss(trace_opt_str);
  std::vector<std::string> trace_opts_strs;
  while (ss.good()) {
    std::string substr;
    getline(ss, substr, ':');
    trace_opts_strs.push_back(substr);
  }
  rocksdb::TraceOptions trace_opt;
  try {
    if (trace_opts_strs.size() != 3) {
      throw std::invalid_argument("Incorrect number of arguments.");
    }
    trace_opt.sampling_frequency = std::stoull(trace_opts_strs[0]);
    trace_opt.max_trace_file_size = std::stoull(trace_opts_strs[1]);
  } catch (const std::exception &e) {
    // NO_LINT_DEBUG
    sql_print_information(
        "RocksDB: Failed to parse trace option string: %s. The correct "
        "format is sampling_frequency:max_trace_file_size:trace_file_name. "
        "sampling_frequency and max_trace_file_size are positive integers. "
        "The block accesses or quries are saved to the "
        "rocksdb_datadir%s/trace_file_name.\n",
        trace_opt_str.c_str(), trace_folder.c_str());
    return HA_EXIT_FAILURE;
  }
  const std::string &trace_file_name = trace_opts_strs[2];
  if (trace_file_name.find("/") != std::string::npos) {
    // NO_LINT_DEBUG
    sql_print_information(
        "RocksDB: Start tracing failed (trace option string: %s). The file "
        "name contains directory separator.\n",
        trace_opt_str.c_str());
    return HA_EXIT_FAILURE;
  }
  const std::string trace_dir = std::string(rocksdb_datadir) + trace_folder;
  s = rdb->GetEnv()->CreateDirIfMissing(trace_dir);
  if (!s.ok()) {
    // NO_LINT_DEBUG
    sql_print_information(
        "RocksDB: Start tracing failed (trace option string: %s). Failed to "
        "create the trace directory %s: %s\n",
        trace_opt_str.c_str(), trace_dir.c_str(), s.ToString().c_str());
    return HA_EXIT_FAILURE;
  }
  const std::string trace_file_path = trace_dir + "/" + trace_file_name;
  s = rdb->GetEnv()->FileExists(trace_file_path);
  if (s.ok() || !s.IsNotFound()) {
    // NO_LINT_DEBUG
    sql_print_information(
        "RocksDB: Start tracing failed (trace option string: %s). The trace "
        "file either already exists or we encountered an error "
        "when calling rdb->GetEnv()->FileExists. The returned status string "
        "is: %s\n",
        trace_opt_str.c_str(), s.ToString().c_str());
    return HA_EXIT_FAILURE;
  }
  std::unique_ptr<rocksdb::TraceWriter> trace_writer;
  const rocksdb::EnvOptions env_option(rdb->GetDBOptions());
  s = rocksdb::NewFileTraceWriter(rdb->GetEnv(), env_option, trace_file_path,
                                  &trace_writer);
  if (!s.ok()) {
    rc = ha_rocksdb::rdb_error_to_mysql(s);
    return HA_EXIT_FAILURE;
  }
  if (trace_block_cache_access) {
    s = rdb->StartBlockCacheTrace(trace_opt, std::move(trace_writer));
  } else {
    s = rdb->StartTrace(trace_opt, std::move(trace_writer));
  }
  if (!s.ok()) {
    rc = ha_rocksdb::rdb_error_to_mysql(s);
    return HA_EXIT_FAILURE;
  }
  // NO_LINT_DEBUG
  sql_print_information(
      "RocksDB: Start tracing block cache accesses or queries. Sampling "
      "frequency: %lu, "
      "Maximum trace file size: %lu, Trace file path %s.\n",
      trace_opt.sampling_frequency, trace_opt.max_trace_file_size,
      trace_file_path.c_str());
  // Save the trace option.
  *static_cast<const char **>(save) = trace_opt_str_raw;
  return HA_EXIT_SUCCESS;
}

static std::unique_ptr<rocksdb::DBOptions> rdb_init_rocksdb_db_options(void) {
  auto o = std::unique_ptr<rocksdb::DBOptions>(new rocksdb::DBOptions());

  o->create_if_missing = true;
  o->listeners.push_back(std::make_shared<Rdb_event_listener>(&ddl_manager));
  o->info_log_level = rocksdb::InfoLogLevel::INFO_LEVEL;
  o->max_subcompactions = DEFAULT_SUBCOMPACTIONS;
  o->max_open_files = -2;  // auto-tune to 50% open_files_limit

  o->two_write_queues = true;
  o->manual_wal_flush = true;
  return o;
}

/* DBOptions contains Statistics and needs to be destructed last */
static std::unique_ptr<rocksdb::BlockBasedTableOptions> rocksdb_tbl_options =
    std::unique_ptr<rocksdb::BlockBasedTableOptions>(
        new rocksdb::BlockBasedTableOptions());
static std::unique_ptr<rocksdb::DBOptions> rocksdb_db_options =
    rdb_init_rocksdb_db_options();

static std::shared_ptr<rocksdb::RateLimiter> rocksdb_rate_limiter;

/* This enum needs to be kept up to date with rocksdb::TxnDBWritePolicy */
static const char *write_policy_names[] = {"write_committed", "write_prepared",
                                           "write_unprepared", NullS};

static TYPELIB write_policy_typelib = {array_elements(write_policy_names) - 1,
                                       "write_policy_typelib",
                                       write_policy_names, nullptr};

/* This array needs to be kept up to date with myrocks::read_free_rpl_type */
static const char *read_free_rpl_names[] = {"OFF", "PK_ONLY", "PK_SK", NullS};

static TYPELIB read_free_rpl_typelib = {array_elements(read_free_rpl_names) - 1,
                                        "read_free_rpl_typelib",
                                        read_free_rpl_names, nullptr};

/* This enum needs to be kept up to date with myrocks::select_bypass_policy_type
 */
static const char *select_bypass_policy_names[] = {"always_off", "always_on",
                                                   "opt_in", "opt_out", NullS};

static TYPELIB select_bypass_policy_typelib = {
    array_elements(select_bypass_policy_names) - 1,
    "select_bypass_policy_typelib", select_bypass_policy_names, nullptr};

/* This enum needs to be kept up to date with rocksdb::InfoLogLevel */
static const char *info_log_level_names[] = {"debug_level", "info_level",
                                             "warn_level",  "error_level",
                                             "fatal_level", NullS};

static TYPELIB info_log_level_typelib = {
    array_elements(info_log_level_names) - 1, "info_log_level_typelib",
    info_log_level_names, nullptr};

/* This enum needs to be kept up to date with rocksdb::BottommostLevelCompaction
 */
static const char *bottommost_level_compaction_names[] = {
    "kSkip", "kIfHaveCompactionFilter", "kForce", "kForceOptimized", NullS};

static TYPELIB bottommost_level_compaction_typelib = {
    array_elements(bottommost_level_compaction_names) - 1,
    "bottommost_level_compaction_typelib", bottommost_level_compaction_names,
    nullptr};

static void rocksdb_set_rocksdb_info_log_level(
    THD *const thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)), const void *const save) {
  DBUG_ASSERT(save != nullptr);

  RDB_MUTEX_LOCK_CHECK(rdb_sysvars_mutex);
  rocksdb_info_log_level = *static_cast<const uint64_t *>(save);
  rocksdb_db_options->info_log->SetInfoLogLevel(
      static_cast<rocksdb::InfoLogLevel>(rocksdb_info_log_level));
  RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
}

static void rocksdb_set_rocksdb_stats_level(
    THD *const thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)), const void *const save) {
  DBUG_ASSERT(save != nullptr);

  RDB_MUTEX_LOCK_CHECK(rdb_sysvars_mutex);
  rocksdb_db_options->statistics->set_stats_level(
      static_cast<rocksdb::StatsLevel>(*static_cast<const uint64_t *>(save)));
  // Actual stats level is defined at rocksdb dbopt::statistics::stats_level_
  // so adjusting rocksdb_stats_level here to make sure it points to
  // the correct stats level.
  rocksdb_stats_level = rocksdb_db_options->statistics->get_stats_level();
  RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
}

static void rocksdb_set_reset_stats(
    my_core::THD *const /* unused */,
    my_core::SYS_VAR *const var MY_ATTRIBUTE((__unused__)), void *const var_ptr,
    const void *const save) {
  DBUG_ASSERT(save != nullptr);
  DBUG_ASSERT(rdb != nullptr);
  DBUG_ASSERT(rocksdb_stats != nullptr);

  RDB_MUTEX_LOCK_CHECK(rdb_sysvars_mutex);

  *static_cast<bool *>(var_ptr) = *static_cast<const bool *>(save);

  if (rocksdb_reset_stats) {
    rocksdb::Status s = rdb->ResetStats();

    // RocksDB will always return success. Let's document this assumption here
    // as well so that we'll get immediately notified when contract changes.
    DBUG_ASSERT(s == rocksdb::Status::OK());

    s = rocksdb_stats->Reset();
    DBUG_ASSERT(s == rocksdb::Status::OK());
  }

  RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
}

static void rocksdb_set_io_write_timeout(
    my_core::THD *const thd MY_ATTRIBUTE((__unused__)),
    my_core::SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)), const void *const save) {
  DBUG_ASSERT(save != nullptr);
  DBUG_ASSERT(rdb != nullptr);
  DBUG_ASSERT(io_watchdog != nullptr);

  RDB_MUTEX_LOCK_CHECK(rdb_sysvars_mutex);

  const uint32_t new_val = *static_cast<const uint32_t *>(save);

  rocksdb_io_write_timeout_secs = new_val;
  io_watchdog->reset_timeout(rocksdb_io_write_timeout_secs);

  RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
}

enum rocksdb_flush_log_at_trx_commit_type : unsigned int {
  FLUSH_LOG_NEVER = 0,
  FLUSH_LOG_SYNC,
  FLUSH_LOG_BACKGROUND,
  FLUSH_LOG_MAX /* must be last */
};

static int rocksdb_validate_flush_log_at_trx_commit(
    THD *const thd MY_ATTRIBUTE((__unused__)),
    /* in: pointer to system variable */
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    /* out: immediate result for update function */
    void *var_ptr,
    /* in: incoming value */
    struct st_mysql_value *const value) {
  long long new_value;

  /* value is NULL */
  if (value->val_int(value, &new_value)) {
    return HA_EXIT_FAILURE;
  }

  if (rocksdb_db_options->allow_mmap_writes && new_value != FLUSH_LOG_NEVER) {
    return HA_EXIT_FAILURE;
  }

  *static_cast<uint32_t *>(var_ptr) = static_cast<uint32_t>(new_value);
  return HA_EXIT_SUCCESS;
}
static void rocksdb_compact_column_family_stub(
    THD *const thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)),
    const void *const save MY_ATTRIBUTE((__unused__))) {}

static int rocksdb_compact_column_family(THD *const thd,
                                         struct SYS_VAR *const var,
                                         void *const var_ptr,
                                         struct st_mysql_value *const value);

static const char *index_type_names[] = {"kBinarySearch", "kHashSearch", NullS};

static TYPELIB index_type_typelib = {array_elements(index_type_names) - 1,
                                     "index_type_typelib", index_type_names,
                                     nullptr};

const ulong RDB_MAX_LOCK_WAIT_SECONDS = 1024 * 1024 * 1024;
const ulong RDB_DEFAULT_MAX_ROW_LOCKS = 1024 * 1024;
const ulong RDB_MAX_ROW_LOCKS = 1024 * 1024 * 1024;
const ulong RDB_DEFAULT_BULK_LOAD_SIZE = 1000;
const ulong RDB_MAX_BULK_LOAD_SIZE = 1024 * 1024 * 1024;
const size_t RDB_DEFAULT_MERGE_BUF_SIZE = 64 * 1024 * 1024;
const size_t RDB_MIN_MERGE_BUF_SIZE = 100;
const size_t RDB_DEFAULT_MERGE_COMBINE_READ_SIZE = 1024 * 1024 * 1024;
const size_t RDB_MIN_MERGE_COMBINE_READ_SIZE = 100;
const size_t RDB_DEFAULT_MERGE_TMP_FILE_REMOVAL_DELAY = 0;
const size_t RDB_MIN_MERGE_TMP_FILE_REMOVAL_DELAY = 0;
const int64 RDB_DEFAULT_BLOCK_CACHE_SIZE = 512 * 1024 * 1024;
const int64 RDB_MIN_BLOCK_CACHE_SIZE = 1024;
const int RDB_MAX_CHECKSUMS_PCT = 100;
const ulong RDB_DEADLOCK_DETECT_DEPTH = 50;
const ulonglong RDB_DEFAULT_MAX_COMPACTION_HISTORY = 64;
const ulong ROCKSDB_MAX_MRR_BATCH_SIZE = 1000;
const uint ROCKSDB_MAX_BOTTOM_PRI_BACKGROUND_COMPACTIONS = 64;

// TODO: 0 means don't wait at all, and we don't support it yet?
static MYSQL_THDVAR_ULONG(lock_wait_timeout, PLUGIN_VAR_RQCMDARG,
                          "Number of seconds to wait for lock", nullptr,
                          nullptr, /*default*/ 1, /*min*/ 1,
                          /*max*/ RDB_MAX_LOCK_WAIT_SECONDS, 0);

static MYSQL_THDVAR_BOOL(deadlock_detect, PLUGIN_VAR_RQCMDARG,
                         "Enables deadlock detection", nullptr, nullptr, false);

static MYSQL_THDVAR_ULONG(deadlock_detect_depth, PLUGIN_VAR_RQCMDARG,
                          "Number of transactions deadlock detection will "
                          "traverse through before assuming deadlock",
                          nullptr, nullptr,
                          /*default*/ RDB_DEADLOCK_DETECT_DEPTH,
                          /*min*/ 2,
                          /*max*/ ULONG_MAX, 0);

static MYSQL_THDVAR_BOOL(
    commit_time_batch_for_recovery, PLUGIN_VAR_RQCMDARG,
    "TransactionOptions::commit_time_batch_for_recovery for RocksDB", nullptr,
    nullptr, true);

static MYSQL_THDVAR_BOOL(
    trace_sst_api, PLUGIN_VAR_RQCMDARG,
    "Generate trace output in the log for each call to the SstFileWriter",
    nullptr, nullptr, false);

static MYSQL_THDVAR_BOOL(
    bulk_load, PLUGIN_VAR_RQCMDARG,
    "Use bulk-load mode for inserts. This disables "
    "unique_checks and enables rocksdb_commit_in_the_middle.",
    rocksdb_check_bulk_load, nullptr, false);

static MYSQL_THDVAR_BOOL(bulk_load_allow_sk, PLUGIN_VAR_RQCMDARG,
                         "Allow bulk loading of sk keys during bulk-load. "
                         "Can be changed only when bulk load is disabled.",
                         /* Intentionally reuse unsorted's check function */
                         rocksdb_check_bulk_load_allow_unsorted, nullptr,
                         false);

static MYSQL_THDVAR_BOOL(bulk_load_allow_unsorted, PLUGIN_VAR_RQCMDARG,
                         "Allow unsorted input during bulk-load. "
                         "Can be changed only when bulk load is disabled.",
                         rocksdb_check_bulk_load_allow_unsorted, nullptr,
                         false);

static MYSQL_SYSVAR_BOOL(enable_bulk_load_api, rocksdb_enable_bulk_load_api,
                         PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                         "Enables using SstFileWriter for bulk loading",
                         nullptr, nullptr, rocksdb_enable_bulk_load_api);

static MYSQL_SYSVAR_BOOL(enable_remove_orphaned_dropped_cfs,
                         rocksdb_enable_remove_orphaned_dropped_cfs,
                         PLUGIN_VAR_RQCMDARG,
                         "Enables removing dropped cfs from metadata if it "
                         "doesn't exist in cf manager",
                         nullptr, nullptr,
                         rocksdb_enable_remove_orphaned_dropped_cfs);

static MYSQL_THDVAR_STR(tmpdir, PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_MEMALLOC,
                        "Directory for temporary files during DDL operations.",
                        nullptr, nullptr, "");

static MYSQL_THDVAR_BOOL(
    commit_in_the_middle, PLUGIN_VAR_RQCMDARG,
    "Commit rows implicitly every rocksdb_bulk_load_size, on bulk load/insert, "
    "update and delete",
    nullptr, nullptr, false);

static MYSQL_THDVAR_BOOL(
    blind_delete_primary_key, PLUGIN_VAR_RQCMDARG,
    "Deleting rows by primary key lookup, without reading rows (Blind Deletes)."
    " Blind delete is disabled if the table has secondary key",
    nullptr, nullptr, false);

static MYSQL_THDVAR_BOOL(
    enable_iterate_bounds, PLUGIN_VAR_OPCMDARG,
    "Enable rocksdb iterator upper/lower bounds in read options.", nullptr,
    nullptr, true);

static const char *DEFAULT_READ_FREE_RPL_TABLES = ".*";

static int rocksdb_validate_read_free_rpl_tables(
    THD *thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *var MY_ATTRIBUTE((__unused__)), void *save,
    struct st_mysql_value *value) {
  int length = 0;
  const char *wlist_buf = value->val_str(value, nullptr, &length);
  const auto wlist = wlist_buf ? wlist_buf : DEFAULT_READ_FREE_RPL_TABLES;

#if defined(HAVE_PSI_INTERFACE)
  Regex_list_handler regex_handler(key_rwlock_read_free_rpl_tables);
#else
  Regex_list_handler regex_handler;
#endif

  if (!regex_handler.set_patterns(wlist)) {
    warn_about_bad_patterns(&regex_handler, "rocksdb_read_free_rpl_tables");
    return HA_EXIT_FAILURE;
  }

  *static_cast<const char **>(save) = wlist;
  return HA_EXIT_SUCCESS;
}

static void rocksdb_update_read_free_rpl_tables(
    THD *thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *var MY_ATTRIBUTE((__unused__)), void *var_ptr,
    const void *save) {
  const auto wlist = *static_cast<const char *const *>(save);
  DBUG_ASSERT(wlist != nullptr);

  // This is bound to succeed since we've already checked for bad patterns in
  // rocksdb_validate_read_free_rpl_tables
  rdb_read_free_regex_handler.set_patterns(wlist);

  // update all table defs
  struct Rdb_read_free_rpl_updater : public Rdb_tables_scanner {
    int add_table(Rdb_tbl_def *tdef) override {
      tdef->check_and_set_read_free_rpl_table();
      return HA_EXIT_SUCCESS;
    }
  } updater;
  ddl_manager.scan_for_tables(&updater);

  if (wlist == DEFAULT_READ_FREE_RPL_TABLES) {
    // If running SET var = DEFAULT, then rocksdb_validate_read_free_rpl_tables
    // isn't called, and memory is never allocated for the value. Allocate it
    // here.
    *static_cast<const char **>(var_ptr) =
        my_strdup(PSI_NOT_INSTRUMENTED, wlist, MYF(MY_WME));
  } else {
    // Otherwise, we just reuse the value allocated from
    // rocksdb_validate_read_free_rpl_tables.
    *static_cast<const char **>(var_ptr) = wlist;
  }
}

static void rocksdb_set_max_bottom_pri_background_compactions_internal(
    uint val) {
  // Set lower priority for compactions
  if (val > 0) {
    // This creates background threads in rocksdb with BOTTOM priority pool.
    // Compactions for bottommost level use threads in the BOTTOM pool, and
    // the threads in the BOTTOM pool run with lower OS priority (19 in Linux).
    rdb->GetEnv()->SetBackgroundThreads(val, rocksdb::Env::Priority::BOTTOM);
    rdb->GetEnv()->LowerThreadPoolCPUPriority(rocksdb::Env::Priority::BOTTOM);
    sql_print_information(
        "Set %d compaction thread(s) with "
        "lower scheduling priority.",
        val);
  }
}

static MYSQL_SYSVAR_STR(
    read_free_rpl_tables, rocksdb_read_free_rpl_tables,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
    "List of tables that will use read-free replication on the slave "
    "(i.e. not lookup a row during replication)",
    rocksdb_validate_read_free_rpl_tables, rocksdb_update_read_free_rpl_tables,
    DEFAULT_READ_FREE_RPL_TABLES);

static MYSQL_SYSVAR_ENUM(
    read_free_rpl, rocksdb_read_free_rpl,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
    "Use read-free replication on the slave (i.e. no row lookup during "
    "replication). Default is OFF, PK_SK will enable it on all tables with "
    "primary key. PK_ONLY will enable it on tables where the only key is the "
    "primary key (i.e. no secondary keys).",
    nullptr, nullptr, read_free_rpl_type::OFF, &read_free_rpl_typelib);

static MYSQL_THDVAR_BOOL(skip_bloom_filter_on_read, PLUGIN_VAR_RQCMDARG,
                         "Skip using bloom filter for reads", nullptr, nullptr,
                         false);

static MYSQL_THDVAR_ULONG(max_row_locks, PLUGIN_VAR_RQCMDARG,
                          "Maximum number of locks a transaction can have",
                          nullptr, nullptr,
                          /*default*/ RDB_DEFAULT_MAX_ROW_LOCKS,
                          /*min*/ 1,
                          /*max*/ RDB_MAX_ROW_LOCKS, 0);

static MYSQL_THDVAR_ULONGLONG(
    write_batch_max_bytes, PLUGIN_VAR_RQCMDARG,
    "Maximum size of write batch in bytes. 0 means no limit.", nullptr, nullptr,
    /* default */ 0, /* min */ 0, /* max */ SIZE_T_MAX, 1);

static MYSQL_THDVAR_ULONGLONG(
    write_batch_flush_threshold, PLUGIN_VAR_RQCMDARG,
    "Maximum size of write batch in bytes before flushing. Only valid if "
    "rocksdb_write_policy is WRITE_UNPREPARED. 0 means no limit.",
    nullptr, nullptr, /* default */ 0, /* min */ 0, /* max */ SIZE_T_MAX, 1);

static MYSQL_THDVAR_BOOL(
    lock_scanned_rows, PLUGIN_VAR_RQCMDARG,
    "Take and hold locks on rows that are scanned but not updated", nullptr,
    nullptr, false);

static MYSQL_THDVAR_ULONG(bulk_load_size, PLUGIN_VAR_RQCMDARG,
                          "Max #records in a batch for bulk-load mode", nullptr,
                          nullptr,
                          /*default*/ RDB_DEFAULT_BULK_LOAD_SIZE,
                          /*min*/ 1,
                          /*max*/ RDB_MAX_BULK_LOAD_SIZE, 0);

static MYSQL_THDVAR_BOOL(
    bulk_load_partial_index, PLUGIN_VAR_RQCMDARG,
    "Materialize partial index during bulk load, instead of leaving it empty.",
    nullptr, nullptr, true);

static MYSQL_THDVAR_ULONGLONG(
    merge_buf_size, PLUGIN_VAR_RQCMDARG,
    "Size to allocate for merge sort buffers written out to disk "
    "during inplace index creation.",
    nullptr, nullptr,
    /* default (64MB) */ RDB_DEFAULT_MERGE_BUF_SIZE,
    /* min (100B) */ RDB_MIN_MERGE_BUF_SIZE,
    /* max */ SIZE_T_MAX, 1);

static MYSQL_THDVAR_ULONGLONG(
    merge_combine_read_size, PLUGIN_VAR_RQCMDARG,
    "Size that we have to work with during combine (reading from disk) phase "
    "of "
    "external sort during fast index creation.",
    nullptr, nullptr,
    /* default (1GB) */ RDB_DEFAULT_MERGE_COMBINE_READ_SIZE,
    /* min (100B) */ RDB_MIN_MERGE_COMBINE_READ_SIZE,
    /* max */ SIZE_T_MAX, 1);

static MYSQL_THDVAR_ULONGLONG(
    merge_tmp_file_removal_delay_ms, PLUGIN_VAR_RQCMDARG,
    "Fast index creation creates a large tmp file on disk during index "
    "creation.  Removing this large file all at once when index creation is "
    "complete can cause trim stalls on Flash.  This variable specifies a "
    "duration to sleep (in milliseconds) between calling chsize() to truncate "
    "the file in chunks.  The chunk size is  the same as merge_buf_size.",
    nullptr, nullptr,
    /* default (0ms) */ RDB_DEFAULT_MERGE_TMP_FILE_REMOVAL_DELAY,
    /* min (0ms) */ RDB_MIN_MERGE_TMP_FILE_REMOVAL_DELAY,
    /* max */ SIZE_T_MAX, 1);

static MYSQL_THDVAR_INT(
    manual_compaction_threads, PLUGIN_VAR_RQCMDARG,
    "How many rocksdb threads to run for manual compactions", nullptr, nullptr,
    /* default rocksdb.dboption max_subcompactions */ 0,
    /* min */ 0, /* max */ 128, 0);

static MYSQL_THDVAR_ENUM(
    manual_compaction_bottommost_level, PLUGIN_VAR_RQCMDARG,
    "Option for bottommost level compaction during manual "
    "compaction",
    nullptr, nullptr,
    /* default */
    (ulong)rocksdb::BottommostLevelCompaction::kForceOptimized,
    &bottommost_level_compaction_typelib);

static MYSQL_SYSVAR_BOOL(
    create_if_missing,
    *reinterpret_cast<bool *>(&rocksdb_db_options->create_if_missing),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::create_if_missing for RocksDB", nullptr, nullptr,
    rocksdb_db_options->create_if_missing);

static MYSQL_SYSVAR_BOOL(
    two_write_queues,
    *reinterpret_cast<bool *>(&rocksdb_db_options->two_write_queues),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::two_write_queues for RocksDB", nullptr, nullptr,
    rocksdb_db_options->two_write_queues);

static MYSQL_SYSVAR_BOOL(
    manual_wal_flush,
    *reinterpret_cast<bool *>(&rocksdb_db_options->manual_wal_flush),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::manual_wal_flush for RocksDB", nullptr, nullptr,
    rocksdb_db_options->manual_wal_flush);

static MYSQL_SYSVAR_ENUM(write_policy, rocksdb_write_policy,
                         PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                         "DBOptions::write_policy for RocksDB", nullptr,
                         nullptr, rocksdb::TxnDBWritePolicy::WRITE_COMMITTED,
                         &write_policy_typelib);

static MYSQL_SYSVAR_BOOL(
    create_missing_column_families,
    *reinterpret_cast<bool *>(
        &rocksdb_db_options->create_missing_column_families),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::create_missing_column_families for RocksDB", nullptr, nullptr,
    rocksdb_db_options->create_missing_column_families);

static MYSQL_SYSVAR_BOOL(
    error_if_exists,
    *reinterpret_cast<bool *>(&rocksdb_db_options->error_if_exists),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::error_if_exists for RocksDB", nullptr, nullptr,
    rocksdb_db_options->error_if_exists);

static MYSQL_SYSVAR_BOOL(
    paranoid_checks,
    *reinterpret_cast<bool *>(&rocksdb_db_options->paranoid_checks),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::paranoid_checks for RocksDB", nullptr, nullptr,
    rocksdb_db_options->paranoid_checks);

static MYSQL_SYSVAR_ULONGLONG(
    rate_limiter_bytes_per_sec, rocksdb_rate_limiter_bytes_per_sec,
    PLUGIN_VAR_RQCMDARG, "DBOptions::rate_limiter bytes_per_sec for RocksDB",
    nullptr, rocksdb_set_rate_limiter_bytes_per_sec, /* default */ 0L,
    /* min */ 0L, /* max */ MAX_RATE_LIMITER_BYTES_PER_SEC, 0);

static MYSQL_SYSVAR_ULONGLONG(
    sst_mgr_rate_bytes_per_sec, rocksdb_sst_mgr_rate_bytes_per_sec,
    PLUGIN_VAR_RQCMDARG,
    "DBOptions::sst_file_manager rate_bytes_per_sec for RocksDB", nullptr,
    rocksdb_set_sst_mgr_rate_bytes_per_sec,
    /* default */ DEFAULT_SST_MGR_RATE_BYTES_PER_SEC,
    /* min */ 0L, /* max */ UINT64_MAX, 0);

static MYSQL_SYSVAR_ULONGLONG(delayed_write_rate, rocksdb_delayed_write_rate,
                              PLUGIN_VAR_RQCMDARG,
                              "DBOptions::delayed_write_rate", nullptr,
                              rocksdb_set_delayed_write_rate,
                              rocksdb_db_options->delayed_write_rate, 0,
                              UINT64_MAX, 0);

static MYSQL_SYSVAR_UINT(max_latest_deadlocks, rocksdb_max_latest_deadlocks,
                         PLUGIN_VAR_RQCMDARG,
                         "Maximum number of recent "
                         "deadlocks to store",
                         nullptr, rocksdb_set_max_latest_deadlocks,
                         rocksdb::kInitialMaxDeadlocks, 0, UINT32_MAX, 0);

static MYSQL_SYSVAR_ULONGLONG(max_lock_memory, rocksdb_max_lock_memory,
                              PLUGIN_VAR_RQCMDARG,
                              "Range-locking mode: Maximum amount of memory "
                              "that locks from all transactions can use at a time",
                              nullptr, rocksdb_set_max_lock_memory,
                              /*initial*/1073741824, 0, UINT64_MAX, 0);

static MYSQL_SYSVAR_ENUM(
    info_log_level, rocksdb_info_log_level, PLUGIN_VAR_RQCMDARG,
    "Filter level for info logs to be written mysqld error log. "
    "Valid values include 'debug_level', 'info_level', 'warn_level'"
    "'error_level' and 'fatal_level'.",
    nullptr, rocksdb_set_rocksdb_info_log_level,
    rocksdb::InfoLogLevel::ERROR_LEVEL, &info_log_level_typelib);

static MYSQL_THDVAR_INT(
    perf_context_level, PLUGIN_VAR_RQCMDARG,
    "Perf Context Level for rocksdb internal timer stat collection", nullptr,
    nullptr,
    /* default */ rocksdb::PerfLevel::kUninitialized,
    /* min */ rocksdb::PerfLevel::kUninitialized,
    /* max */ rocksdb::PerfLevel::kOutOfBounds - 1, 0);

static MYSQL_SYSVAR_UINT(
    wal_recovery_mode, rocksdb_wal_recovery_mode, PLUGIN_VAR_RQCMDARG,
    "DBOptions::wal_recovery_mode for RocksDB. Default is kPointInTimeRecovery",
    nullptr, nullptr,
    /* default */ (uint)rocksdb::WALRecoveryMode::kPointInTimeRecovery,
    /* min */ (uint)rocksdb::WALRecoveryMode::kTolerateCorruptedTailRecords,
    /* max */ (uint)rocksdb::WALRecoveryMode::kSkipAnyCorruptedRecords, 0);

static MYSQL_SYSVAR_BOOL(
    track_and_verify_wals_in_manifest,
    *reinterpret_cast<bool *>(&rocksdb_track_and_verify_wals_in_manifest),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::track_and_verify_wals_in_manifest for RocksDB", nullptr,
    nullptr, true);

static MYSQL_SYSVAR_UINT(
    stats_level, rocksdb_stats_level, PLUGIN_VAR_RQCMDARG,
    "Statistics Level for RocksDB. Default is 1 (kExceptHistogramOrTimers)",
    nullptr, rocksdb_set_rocksdb_stats_level,
    /* default */ (uint)rocksdb::StatsLevel::kExceptHistogramOrTimers,
    /* min */ (uint)rocksdb::StatsLevel::kExceptTickers,
    /* max */ (uint)rocksdb::StatsLevel::kAll, 0);

static MYSQL_SYSVAR_ULONG(compaction_readahead_size,
                          rocksdb_db_options->compaction_readahead_size,
                          PLUGIN_VAR_RQCMDARG,
                          "DBOptions::compaction_readahead_size for RocksDB",
                          nullptr, nullptr,
                          rocksdb_db_options->compaction_readahead_size,
                          /* min */ 0L, /* max */ ULONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(
    new_table_reader_for_compaction_inputs,
    *reinterpret_cast<bool *>(
        &rocksdb_db_options->new_table_reader_for_compaction_inputs),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::new_table_reader_for_compaction_inputs for RocksDB", nullptr,
    nullptr, rocksdb_db_options->new_table_reader_for_compaction_inputs);

static MYSQL_SYSVAR_UINT(
    access_hint_on_compaction_start, rocksdb_access_hint_on_compaction_start,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::access_hint_on_compaction_start for RocksDB", nullptr, nullptr,
    /* default */ (uint)rocksdb::Options::AccessHint::NORMAL,
    /* min */ (uint)rocksdb::Options::AccessHint::NONE,
    /* max */ (uint)rocksdb::Options::AccessHint::WILLNEED, 0);

static MYSQL_SYSVAR_BOOL(
    allow_concurrent_memtable_write,
    *reinterpret_cast<bool *>(
        &rocksdb_db_options->allow_concurrent_memtable_write),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::allow_concurrent_memtable_write for RocksDB", nullptr, nullptr,
    false);

static MYSQL_SYSVAR_BOOL(
    enable_write_thread_adaptive_yield,
    *reinterpret_cast<bool *>(
        &rocksdb_db_options->enable_write_thread_adaptive_yield),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::enable_write_thread_adaptive_yield for RocksDB", nullptr,
    nullptr, false);

static MYSQL_SYSVAR_INT(max_open_files, rocksdb_db_options->max_open_files,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                        "DBOptions::max_open_files for RocksDB", nullptr,
                        nullptr, rocksdb_db_options->max_open_files,
                        /* min */ -2, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_ULONG(max_total_wal_size,
                          rocksdb_db_options->max_total_wal_size,
                          PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                          "DBOptions::max_total_wal_size for RocksDB", nullptr,
                          nullptr, rocksdb_db_options->max_total_wal_size,
                          /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(
    use_fsync, *reinterpret_cast<bool *>(&rocksdb_db_options->use_fsync),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::use_fsync for RocksDB", nullptr, nullptr,
    rocksdb_db_options->use_fsync);

static MYSQL_SYSVAR_STR(wal_dir, rocksdb_wal_dir,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                        "DBOptions::wal_dir for RocksDB", nullptr, nullptr,
                        rocksdb_db_options->wal_dir.c_str());

static MYSQL_SYSVAR_STR(
    persistent_cache_path, rocksdb_persistent_cache_path,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "Path for BlockBasedTableOptions::persistent_cache for RocksDB", nullptr,
    nullptr, "");

static MYSQL_SYSVAR_ULONG(
    persistent_cache_size_mb, rocksdb_persistent_cache_size_mb,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "Size of cache in MB for BlockBasedTableOptions::persistent_cache "
    "for RocksDB",
    nullptr, nullptr, rocksdb_persistent_cache_size_mb,
    /* min */ 0L, /* max */ ULONG_MAX, 0);

static MYSQL_SYSVAR_STR(wsenv_path, rocksdb_wsenv_path,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                        "Path for RocksDB WSEnv", nullptr, nullptr, "");

static MYSQL_SYSVAR_STR(wsenv_tenant, rocksdb_wsenv_tenant,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                        "Tenant for RocksDB WSEnv", nullptr, nullptr, "");

static MYSQL_SYSVAR_STR(fault_injection_options,
                        opt_rocksdb_fault_injection_options,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                        "Fault injection options for running rocksdb tests",
                        nullptr, nullptr, nullptr);

static MYSQL_SYSVAR_ULONG(
    delete_obsolete_files_period_micros,
    rocksdb_db_options->delete_obsolete_files_period_micros,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::delete_obsolete_files_period_micros for RocksDB", nullptr,
    nullptr, rocksdb_db_options->delete_obsolete_files_period_micros,
    /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_INT(max_background_jobs,
                        rocksdb_db_options->max_background_jobs,
                        PLUGIN_VAR_RQCMDARG,
                        "DBOptions::max_background_jobs for RocksDB", nullptr,
                        rocksdb_set_max_background_jobs,
                        rocksdb_db_options->max_background_jobs,
                        /* min */ -1, /* max */ MAX_BACKGROUND_JOBS, 0);

static MYSQL_SYSVAR_INT(max_background_flushes,
                        rocksdb_db_options->max_background_flushes,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                        "DBOptions::max_background_flushes for RocksDB",
                        nullptr, nullptr,
                        rocksdb_db_options->max_background_flushes,
                        /* min */ -1, /* max */ 64, 0);

static MYSQL_SYSVAR_INT(max_background_compactions,
                        rocksdb_db_options->max_background_compactions,
                        PLUGIN_VAR_RQCMDARG,
                        "DBOptions::max_background_compactions for RocksDB",
                        nullptr, rocksdb_set_max_background_compactions,
                        rocksdb_db_options->max_background_compactions,
                        /* min */ -1, /* max */ 64, 0);

static MYSQL_SYSVAR_UINT(
    max_bottom_pri_background_compactions,
    rocksdb_max_bottom_pri_background_compactions, PLUGIN_VAR_RQCMDARG,
    "Creating specified number of threads, setting lower "
    "CPU priority, and letting Lmax compactions use them. "
    "Maximum total compaction concurrency continues to be capped to "
    "rocksdb_max_background_compactions or "
    "rocksdb_max_background_jobs. In addition to that, Lmax "
    "compaction concurrency is capped to "
    "rocksdb_max_bottom_pri_background_compactions. Default value is 0, "
    "which means all compactions are under concurrency of "
    "rocksdb_max_background_compactions|jobs. If you set very low "
    "rocksdb_max_bottom_pri_background_compactions (e.g. 1 or 2), compactions "
    "may not be able to keep up. Since Lmax normally has "
    "90 percent of data, it is recommended to set closer number to "
    "rocksdb_max_background_compactions|jobs. This option is helpful to "
    "give more CPU resources to other threads (e.g. query processing).",
    rocksdb_validate_max_bottom_pri_background_compactions, nullptr, 0,
    /* min */ 0, /* max */ ROCKSDB_MAX_BOTTOM_PRI_BACKGROUND_COMPACTIONS, 0);

static MYSQL_SYSVAR_UINT(max_subcompactions,
                         rocksdb_db_options->max_subcompactions,
                         PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                         "DBOptions::max_subcompactions for RocksDB", nullptr,
                         nullptr, rocksdb_db_options->max_subcompactions,
                         /* min */ 1, /* max */ MAX_SUBCOMPACTIONS, 0);

static MYSQL_SYSVAR_ULONG(max_log_file_size,
                          rocksdb_db_options->max_log_file_size,
                          PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                          "DBOptions::max_log_file_size for RocksDB", nullptr,
                          nullptr, rocksdb_db_options->max_log_file_size,
                          /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(log_file_time_to_roll,
                          rocksdb_db_options->log_file_time_to_roll,
                          PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                          "DBOptions::log_file_time_to_roll for RocksDB",
                          nullptr, nullptr,
                          rocksdb_db_options->log_file_time_to_roll,
                          /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(keep_log_file_num,
                          rocksdb_db_options->keep_log_file_num,
                          PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                          "DBOptions::keep_log_file_num for RocksDB", nullptr,
                          nullptr, rocksdb_db_options->keep_log_file_num,
                          /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(max_manifest_file_size,
                          rocksdb_db_options->max_manifest_file_size,
                          PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                          "DBOptions::max_manifest_file_size for RocksDB",
                          nullptr, nullptr,
                          rocksdb_db_options->max_manifest_file_size,
                          /* min */ 0L, /* max */ ULONG_MAX, 0);

static MYSQL_SYSVAR_INT(table_cache_numshardbits,
                        rocksdb_db_options->table_cache_numshardbits,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                        "DBOptions::table_cache_numshardbits for RocksDB",
                        nullptr, nullptr,
                        rocksdb_db_options->table_cache_numshardbits,
                        // LRUCache limits this to 19 bits, anything greater
                        // fails to create a cache and returns a nullptr
                        /* min */ 0, /* max */ 19, 0);

static MYSQL_SYSVAR_ULONG(wal_ttl_seconds, rocksdb_db_options->WAL_ttl_seconds,
                          PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                          "DBOptions::WAL_ttl_seconds for RocksDB", nullptr,
                          nullptr, rocksdb_db_options->WAL_ttl_seconds,
                          /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(wal_size_limit_mb,
                          rocksdb_db_options->WAL_size_limit_MB,
                          PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                          "DBOptions::WAL_size_limit_MB for RocksDB", nullptr,
                          nullptr, rocksdb_db_options->WAL_size_limit_MB,
                          /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(manifest_preallocation_size,
                          rocksdb_db_options->manifest_preallocation_size,
                          PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                          "DBOptions::manifest_preallocation_size for RocksDB",
                          nullptr, nullptr,
                          rocksdb_db_options->manifest_preallocation_size,
                          /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(
    use_direct_reads,
    *reinterpret_cast<bool *>(&rocksdb_db_options->use_direct_reads),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::use_direct_reads for RocksDB", nullptr, nullptr,
    rocksdb_db_options->use_direct_reads);

static MYSQL_SYSVAR_BOOL(
    use_direct_io_for_flush_and_compaction,
    *reinterpret_cast<bool *>(
        &rocksdb_db_options->use_direct_io_for_flush_and_compaction),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::use_direct_io_for_flush_and_compaction for RocksDB", nullptr,
    nullptr, rocksdb_db_options->use_direct_io_for_flush_and_compaction);

static MYSQL_SYSVAR_BOOL(
    allow_mmap_reads,
    *reinterpret_cast<bool *>(&rocksdb_db_options->allow_mmap_reads),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::allow_mmap_reads for RocksDB", nullptr, nullptr,
    rocksdb_db_options->allow_mmap_reads);

static MYSQL_SYSVAR_BOOL(
    allow_mmap_writes,
    *reinterpret_cast<bool *>(&rocksdb_db_options->allow_mmap_writes),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::allow_mmap_writes for RocksDB", nullptr, nullptr,
    rocksdb_db_options->allow_mmap_writes);

static MYSQL_SYSVAR_BOOL(
    is_fd_close_on_exec,
    *reinterpret_cast<bool *>(&rocksdb_db_options->is_fd_close_on_exec),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::is_fd_close_on_exec for RocksDB", nullptr, nullptr,
    rocksdb_db_options->is_fd_close_on_exec);

static MYSQL_SYSVAR_UINT(stats_dump_period_sec,
                         rocksdb_db_options->stats_dump_period_sec,
                         PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                         "DBOptions::stats_dump_period_sec for RocksDB",
                         nullptr, nullptr,
                         rocksdb_db_options->stats_dump_period_sec,
                         /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_BOOL(
    advise_random_on_open,
    *reinterpret_cast<bool *>(&rocksdb_db_options->advise_random_on_open),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::advise_random_on_open for RocksDB", nullptr, nullptr,
    rocksdb_db_options->advise_random_on_open);

static MYSQL_SYSVAR_ULONG(db_write_buffer_size,
                          rocksdb_db_options->db_write_buffer_size,
                          PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                          "DBOptions::db_write_buffer_size for RocksDB",
                          nullptr, nullptr,
                          rocksdb_db_options->db_write_buffer_size,
                          /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(
    use_adaptive_mutex,
    *reinterpret_cast<bool *>(&rocksdb_db_options->use_adaptive_mutex),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::use_adaptive_mutex for RocksDB", nullptr, nullptr,
    rocksdb_db_options->use_adaptive_mutex);

static MYSQL_SYSVAR_ULONG(bytes_per_sync, rocksdb_db_options->bytes_per_sync,
                          PLUGIN_VAR_RQCMDARG,
                          "DBOptions::bytes_per_sync for RocksDB", nullptr,
                          rocksdb_set_bytes_per_sync,
                          rocksdb_db_options->bytes_per_sync,
                          /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(wal_bytes_per_sync,
                          rocksdb_db_options->wal_bytes_per_sync,
                          PLUGIN_VAR_RQCMDARG,
                          "DBOptions::wal_bytes_per_sync for RocksDB", nullptr,
                          rocksdb_set_wal_bytes_per_sync,
                          rocksdb_db_options->wal_bytes_per_sync,
                          /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(
    enable_thread_tracking,
    *reinterpret_cast<bool *>(&rocksdb_db_options->enable_thread_tracking),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::enable_thread_tracking for RocksDB", nullptr, nullptr, true);

static MYSQL_SYSVAR_LONGLONG(block_cache_size, rocksdb_block_cache_size,
                             PLUGIN_VAR_RQCMDARG,
                             "block_cache size for RocksDB",
                             rocksdb_validate_set_block_cache_size, nullptr,
                             /* default */ RDB_DEFAULT_BLOCK_CACHE_SIZE,
                             /* min */ RDB_MIN_BLOCK_CACHE_SIZE,
                             /* max */ LLONG_MAX,
                             /* Block size */ RDB_MIN_BLOCK_CACHE_SIZE);

static MYSQL_SYSVAR_LONGLONG(sim_cache_size, rocksdb_sim_cache_size,
                             PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                             "Simulated cache size for RocksDB", nullptr,
                             nullptr,
                             /* default */ 0,
                             /* min */ 0,
                             /* max */ LLONG_MAX,
                             /* Block size */ 0);

static MYSQL_SYSVAR_BOOL(
    use_clock_cache, rocksdb_use_clock_cache,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "Use ClockCache instead of default LRUCache for RocksDB", nullptr, nullptr,
    false);

static MYSQL_SYSVAR_BOOL(cache_dump, rocksdb_cache_dump,
                         PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                         "Include RocksDB block cache content in core dump.",
                         nullptr, nullptr, true);

static MYSQL_SYSVAR_DOUBLE(cache_high_pri_pool_ratio,
                           rocksdb_cache_high_pri_pool_ratio,
                           PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                           "Specify the size of block cache high-pri pool",
                           nullptr, nullptr, /* default */ 0.0, /* min */ 0.0,
                           /* max */ 1.0, 0);

static MYSQL_SYSVAR_BOOL(
    cache_index_and_filter_blocks,
    *reinterpret_cast<bool *>(
        &rocksdb_tbl_options->cache_index_and_filter_blocks),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "BlockBasedTableOptions::cache_index_and_filter_blocks for RocksDB",
    nullptr, nullptr, true);

static MYSQL_SYSVAR_BOOL(
    cache_index_and_filter_with_high_priority,
    *reinterpret_cast<bool *>(
        &rocksdb_tbl_options->cache_index_and_filter_blocks_with_high_priority),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "cache_index_and_filter_blocks_with_high_priority for RocksDB", nullptr,
    nullptr, true);

// When pin_l0_filter_and_index_blocks_in_cache is true, RocksDB will  use the
// LRU cache, but will always keep the filter & idndex block's handle checked
// out (=won't call ShardedLRUCache::Release), plus the parsed out objects
// the LRU cache will never push flush them out, hence they're pinned.
//
// This fixes the mutex contention between :ShardedLRUCache::Lookup and
// ShardedLRUCache::Release which reduced the QPS ratio (QPS using secondary
// index / QPS using PK).
static MYSQL_SYSVAR_BOOL(
    pin_l0_filter_and_index_blocks_in_cache,
    *reinterpret_cast<bool *>(
        &rocksdb_tbl_options->pin_l0_filter_and_index_blocks_in_cache),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "pin_l0_filter_and_index_blocks_in_cache for RocksDB", nullptr, nullptr,
    true);

static MYSQL_SYSVAR_ENUM(index_type, rocksdb_index_type,
                         PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                         "BlockBasedTableOptions::index_type for RocksDB",
                         nullptr, nullptr,
                         (uint64_t)rocksdb_tbl_options->index_type,
                         &index_type_typelib);

static MYSQL_SYSVAR_BOOL(
    hash_index_allow_collision,
    *reinterpret_cast<bool *>(&rocksdb_tbl_options->hash_index_allow_collision),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "BlockBasedTableOptions::hash_index_allow_collision for RocksDB", nullptr,
    nullptr, rocksdb_tbl_options->hash_index_allow_collision);

static MYSQL_SYSVAR_BOOL(
    no_block_cache,
    *reinterpret_cast<bool *>(&rocksdb_tbl_options->no_block_cache),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "BlockBasedTableOptions::no_block_cache for RocksDB", nullptr, nullptr,
    rocksdb_tbl_options->no_block_cache);

static MYSQL_SYSVAR_ULONG(block_size, rocksdb_tbl_options->block_size,
                          PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                          "BlockBasedTableOptions::block_size for RocksDB",
                          nullptr, nullptr, rocksdb_tbl_options->block_size,
                          /* min */ 1L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_INT(
    block_size_deviation, rocksdb_tbl_options->block_size_deviation,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "BlockBasedTableOptions::block_size_deviation for RocksDB", nullptr,
    nullptr, rocksdb_tbl_options->block_size_deviation,
    /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_INT(
    block_restart_interval, rocksdb_tbl_options->block_restart_interval,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "BlockBasedTableOptions::block_restart_interval for RocksDB", nullptr,
    nullptr, rocksdb_tbl_options->block_restart_interval,
    /* min */ 1, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_BOOL(
    whole_key_filtering,
    *reinterpret_cast<bool *>(&rocksdb_tbl_options->whole_key_filtering),
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "BlockBasedTableOptions::whole_key_filtering for RocksDB", nullptr, nullptr,
    rocksdb_tbl_options->whole_key_filtering);

static MYSQL_SYSVAR_STR(default_cf_options, rocksdb_default_cf_options,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                        "default cf options for RocksDB", nullptr, nullptr, "");

static MYSQL_SYSVAR_STR(override_cf_options, rocksdb_override_cf_options,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                        "option overrides per cf for RocksDB", nullptr, nullptr,
                        "");

static MYSQL_SYSVAR_STR(update_cf_options, rocksdb_update_cf_options,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
                        "Option updates per column family for RocksDB",
                        rocksdb_validate_update_cf_options,
                        rocksdb_set_update_cf_options, nullptr);

static MYSQL_SYSVAR_BOOL(use_default_sk_cf, rocksdb_use_default_sk_cf,
                         PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                         "Use default_sk for secondary keys", nullptr, nullptr,
                         false);

static MYSQL_SYSVAR_UINT(flush_log_at_trx_commit,
                         rocksdb_flush_log_at_trx_commit, PLUGIN_VAR_RQCMDARG,
                         "Sync on transaction commit. Similar to "
                         "innodb_flush_log_at_trx_commit. 1: sync on commit, "
                         "0,2: not sync on commit",
                         rocksdb_validate_flush_log_at_trx_commit, nullptr,
                         /* default */ FLUSH_LOG_SYNC,
                         /* min */ FLUSH_LOG_NEVER,
                         /* max */ FLUSH_LOG_BACKGROUND, 0);

static MYSQL_THDVAR_BOOL(write_disable_wal, PLUGIN_VAR_RQCMDARG,
                         "WriteOptions::disableWAL for RocksDB", nullptr,
                         nullptr, rocksdb::WriteOptions().disableWAL);

static MYSQL_THDVAR_BOOL(
    write_ignore_missing_column_families, PLUGIN_VAR_RQCMDARG,
    "WriteOptions::ignore_missing_column_families for RocksDB", nullptr,
    nullptr, rocksdb::WriteOptions().ignore_missing_column_families);

static MYSQL_THDVAR_BOOL(skip_fill_cache, PLUGIN_VAR_RQCMDARG,
                         "Skip filling block cache on read requests", nullptr,
                         nullptr, false);

static MYSQL_THDVAR_BOOL(
    unsafe_for_binlog, PLUGIN_VAR_RQCMDARG,
    "Allowing statement based binary logging which may break consistency",
    nullptr, nullptr, false);

static MYSQL_THDVAR_UINT(records_in_range, PLUGIN_VAR_RQCMDARG,
                         "Used to override the result of records_in_range(). "
                         "Set to a positive number to override",
                         nullptr, nullptr, 0,
                         /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_THDVAR_UINT(force_index_records_in_range, PLUGIN_VAR_RQCMDARG,
                         "Used to override the result of records_in_range() "
                         "when FORCE INDEX is used.",
                         nullptr, nullptr, 0,
                         /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_UINT(
    debug_optimizer_n_rows, rocksdb_debug_optimizer_n_rows,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY | PLUGIN_VAR_NOSYSVAR,
    "Test only to override rocksdb estimates of table size in a memtable",
    nullptr, nullptr, 0, /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_BOOL(force_compute_memtable_stats,
                         rocksdb_force_compute_memtable_stats,
                         PLUGIN_VAR_RQCMDARG,
                         "Force to always compute memtable stats", nullptr,
                         nullptr, true);

static MYSQL_SYSVAR_UINT(force_compute_memtable_stats_cachetime,
                         rocksdb_force_compute_memtable_stats_cachetime,
                         PLUGIN_VAR_RQCMDARG,
                         "Time in usecs to cache memtable estimates", nullptr,
                         nullptr, /* default */ 60 * 1000 * 1000,
                         /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_BOOL(
    debug_optimizer_no_zero_cardinality,
    rocksdb_debug_optimizer_no_zero_cardinality, PLUGIN_VAR_RQCMDARG,
    "In case if cardinality is zero, overrides it with some value", nullptr,
    nullptr, true);

static MYSQL_SYSVAR_UINT(debug_cardinality_multiplier,
                         rocksdb_debug_cardinality_multiplier,
                         PLUGIN_VAR_RQCMDARG,
                         "Cardinality multiplier used in tests", nullptr,
                         nullptr, /* default */ 2,
                         /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_STR(compact_cf, rocksdb_compact_cf_name,
                        PLUGIN_VAR_RQCMDARG, "Compact column family",
                        rocksdb_compact_column_family,
                        rocksdb_compact_column_family_stub, "");

static MYSQL_SYSVAR_STR(delete_cf, rocksdb_delete_cf_name, PLUGIN_VAR_RQCMDARG,
                        "Delete column family", rocksdb_delete_column_family,
                        rocksdb_delete_column_family_stub, "");

static MYSQL_SYSVAR_STR(create_checkpoint, rocksdb_checkpoint_name,
                        PLUGIN_VAR_RQCMDARG, "Checkpoint directory",
                        rocksdb_create_checkpoint_validate,
                        rocksdb_create_checkpoint_update, "");

static MYSQL_THDVAR_STR(create_temporary_checkpoint,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC |
                            PLUGIN_VAR_NOCMDOPT,
                        "Temporary checkpoint directory",
                        rocksdb_create_temporary_checkpoint_validate, nullptr,
                        nullptr);

static MYSQL_SYSVAR_BOOL(signal_drop_index_thread,
                         rocksdb_signal_drop_index_thread, PLUGIN_VAR_RQCMDARG,
                         "Wake up drop index thread", nullptr,
                         rocksdb_drop_index_wakeup_thread, false);

static MYSQL_SYSVAR_BOOL(pause_background_work, rocksdb_pause_background_work,
                         PLUGIN_VAR_RQCMDARG,
                         "Disable all rocksdb background operations", nullptr,
                         rocksdb_set_pause_background_work, false);

static MYSQL_SYSVAR_BOOL(
    enable_ttl, rocksdb_enable_ttl, PLUGIN_VAR_RQCMDARG,
    "Enable expired TTL records to be dropped during compaction.", nullptr,
    nullptr, true);

static MYSQL_SYSVAR_BOOL(
    enable_ttl_read_filtering, rocksdb_enable_ttl_read_filtering,
    PLUGIN_VAR_RQCMDARG,
    "For tables with TTL, expired records are skipped/filtered out during "
    "processing and in query results. Disabling this will allow these records "
    "to be seen, but as a result rows may disappear in the middle of "
    "transactions as they are dropped during compaction. Use with caution.",
    nullptr, nullptr, true);

static MYSQL_SYSVAR_INT(
    debug_ttl_rec_ts, rocksdb_debug_ttl_rec_ts, PLUGIN_VAR_RQCMDARG,
    "For debugging purposes only.  Overrides the TTL of records to "
    "now() + debug_ttl_rec_ts.  The value can be +/- to simulate "
    "a record inserted in the past vs a record inserted in the 'future'. "
    "A value of 0 denotes that the variable is not set. This variable is a "
    "no-op in non-debug builds.",
    nullptr, nullptr, 0, /* min */ -3600, /* max */ 3600, 0);

static MYSQL_SYSVAR_INT(
    debug_ttl_snapshot_ts, rocksdb_debug_ttl_snapshot_ts, PLUGIN_VAR_RQCMDARG,
    "For debugging purposes only.  Sets the snapshot during compaction to "
    "now() + debug_set_ttl_snapshot_ts.  The value can be +/- to simulate "
    "a snapshot in the past vs a snapshot created in the 'future'. "
    "A value of 0 denotes that the variable is not set. This variable is a "
    "no-op in non-debug builds.",
    nullptr, nullptr, 0, /* min */ -3600, /* max */ 3600, 0);

static MYSQL_SYSVAR_INT(
    debug_ttl_read_filter_ts, rocksdb_debug_ttl_read_filter_ts,
    PLUGIN_VAR_RQCMDARG,
    "For debugging purposes only.  Overrides the TTL read filtering time to "
    "time + debug_ttl_read_filter_ts. A value of 0 denotes that the variable "
    "is not set. This variable is a no-op in non-debug builds.",
    nullptr, nullptr, 0, /* min */ -3600, /* max */ 3600, 0);

static MYSQL_SYSVAR_BOOL(
    debug_ttl_ignore_pk, rocksdb_debug_ttl_ignore_pk, PLUGIN_VAR_RQCMDARG,
    "For debugging purposes only. If true, compaction filtering will not occur "
    "on PK TTL data. This variable is a no-op in non-debug builds.",
    nullptr, nullptr, false);

static MYSQL_SYSVAR_UINT(
    max_manual_compactions, rocksdb_max_manual_compactions, PLUGIN_VAR_RQCMDARG,
    "Maximum number of pending + ongoing number of manual compactions.",
    nullptr, nullptr, /* default */ 10, /* min */ 0, /* max */ UINT_MAX, 0);

static MYSQL_SYSVAR_BOOL(
    rollback_on_timeout, rocksdb_rollback_on_timeout, PLUGIN_VAR_OPCMDARG,
    "Whether to roll back the complete transaction or a single statement on "
    "lock wait timeout (a single statement by default)",
    NULL, NULL, false);

static MYSQL_SYSVAR_UINT(
    debug_manual_compaction_delay, rocksdb_debug_manual_compaction_delay,
    PLUGIN_VAR_RQCMDARG,
    "For debugging purposes only. Sleeping specified seconds "
    "for simulating long running compactions.",
    nullptr, nullptr, 0, /* min */ 0, /* max */ UINT_MAX, 0);

static MYSQL_SYSVAR_BOOL(
    reset_stats, rocksdb_reset_stats, PLUGIN_VAR_RQCMDARG,
    "Reset the RocksDB internal statistics without restarting the DB.", nullptr,
    rocksdb_set_reset_stats, false);

static MYSQL_SYSVAR_UINT(io_write_timeout, rocksdb_io_write_timeout_secs,
                         PLUGIN_VAR_RQCMDARG,
                         "Timeout for experimental I/O watchdog.", nullptr,
                         rocksdb_set_io_write_timeout, /* default */ 0,
                         /* min */ 0L,
                         /* max */ UINT_MAX, 0);

static MYSQL_SYSVAR_BOOL(enable_2pc, rocksdb_enable_2pc, PLUGIN_VAR_RQCMDARG,
                         "Enable two phase commit for MyRocks", nullptr,
                         nullptr, true);

static MYSQL_SYSVAR_BOOL(ignore_unknown_options, rocksdb_ignore_unknown_options,
                         PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_READONLY,
                         "Enable ignoring unknown options passed to RocksDB",
                         nullptr, nullptr, true);

/*
  TODO(herman) - Both strict_collation_check and strict_collation_exceptions can
  be deprecated now that SKs support index lookups for all collations.
*/
static MYSQL_SYSVAR_BOOL(strict_collation_check, rocksdb_strict_collation_check,
                         PLUGIN_VAR_RQCMDARG,
                         "Enforce case sensitive collation for MyRocks indexes",
                         nullptr, nullptr, true);

static MYSQL_SYSVAR_STR(strict_collation_exceptions,
                        rocksdb_strict_collation_exceptions,
                        PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
                        "List of tables (using regex) that are excluded "
                        "from the case sensitive collation enforcement",
                        nullptr, rocksdb_set_collation_exception_list, "");

static MYSQL_SYSVAR_BOOL(collect_sst_properties, rocksdb_collect_sst_properties,
                         PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                         "Enables collecting SST file properties on each flush",
                         nullptr, nullptr, rocksdb_collect_sst_properties);

static MYSQL_SYSVAR_BOOL(
    force_flush_memtable_now, rocksdb_force_flush_memtable_now_var,
    PLUGIN_VAR_RQCMDARG,
    "Forces memstore flush which may block all write requests so be careful",
    rocksdb_force_flush_memtable_now, rocksdb_force_flush_memtable_now_stub,
    true);

static MYSQL_SYSVAR_BOOL(
    force_flush_memtable_and_lzero_now,
    rocksdb_force_flush_memtable_and_lzero_now_var, PLUGIN_VAR_RQCMDARG,
    "Acts similar to force_flush_memtable_now, but also compacts all L0 files.",
    rocksdb_force_flush_memtable_and_lzero_now,
    rocksdb_force_flush_memtable_and_lzero_now_stub, false);

static MYSQL_SYSVAR_BOOL(cancel_manual_compactions,
                         rocksdb_cancel_manual_compactions_var,
                         PLUGIN_VAR_RQCMDARG,
                         "Cancelling all ongoing manual compactions.",
                         rocksdb_cancel_manual_compactions,
                         rocksdb_cancel_manual_compactions_stub, false);

static MYSQL_SYSVAR_UINT(
    seconds_between_stat_computes, rocksdb_seconds_between_stat_computes,
    PLUGIN_VAR_RQCMDARG,
    "Sets a number of seconds to wait between optimizer stats recomputation. "
    "Only changed indexes will be refreshed.",
    nullptr, nullptr, rocksdb_seconds_between_stat_computes,
    /* min */ 0L, /* max */ UINT_MAX, 0);

static MYSQL_SYSVAR_LONGLONG(compaction_sequential_deletes,
                             rocksdb_compaction_sequential_deletes,
                             PLUGIN_VAR_RQCMDARG,
                             "RocksDB will trigger compaction for the file if "
                             "it has more than this number sequential deletes "
                             "per window",
                             nullptr, rocksdb_set_compaction_options,
                             DEFAULT_COMPACTION_SEQUENTIAL_DELETES,
                             /* min */ 0L,
                             /* max */ MAX_COMPACTION_SEQUENTIAL_DELETES, 0);

static MYSQL_SYSVAR_LONGLONG(
    compaction_sequential_deletes_window,
    rocksdb_compaction_sequential_deletes_window, PLUGIN_VAR_RQCMDARG,
    "Size of the window for counting rocksdb_compaction_sequential_deletes",
    nullptr, rocksdb_set_compaction_options,
    DEFAULT_COMPACTION_SEQUENTIAL_DELETES_WINDOW,
    /* min */ 0L, /* max */ MAX_COMPACTION_SEQUENTIAL_DELETES_WINDOW, 0);

static MYSQL_SYSVAR_LONGLONG(
    compaction_sequential_deletes_file_size,
    rocksdb_compaction_sequential_deletes_file_size, PLUGIN_VAR_RQCMDARG,
    "Minimum file size required for compaction_sequential_deletes", nullptr,
    rocksdb_set_compaction_options, 0L,
    /* min */ -1L, /* max */ LLONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(
    compaction_sequential_deletes_count_sd,
    rocksdb_compaction_sequential_deletes_count_sd, PLUGIN_VAR_RQCMDARG,
    "Counting SingleDelete as rocksdb_compaction_sequential_deletes", nullptr,
    nullptr, rocksdb_compaction_sequential_deletes_count_sd);

static MYSQL_SYSVAR_BOOL(
    print_snapshot_conflict_queries, rocksdb_print_snapshot_conflict_queries,
    PLUGIN_VAR_RQCMDARG,
    "Logging queries that got snapshot conflict errors into *.err log", nullptr,
    nullptr, rocksdb_print_snapshot_conflict_queries);

static MYSQL_THDVAR_INT(checksums_pct, PLUGIN_VAR_RQCMDARG,
                        "How many percentages of rows to be checksummed",
                        nullptr, nullptr, RDB_MAX_CHECKSUMS_PCT,
                        /* min */ 0, /* max */ RDB_MAX_CHECKSUMS_PCT, 0);

static MYSQL_THDVAR_BOOL(store_row_debug_checksums, PLUGIN_VAR_RQCMDARG,
                         "Include checksums when writing index/table records",
                         nullptr, nullptr, false /* default value */);

static MYSQL_THDVAR_BOOL(verify_row_debug_checksums, PLUGIN_VAR_RQCMDARG,
                         "Verify checksums when reading index/table records",
                         nullptr, nullptr, false /* default value */);

static MYSQL_THDVAR_BOOL(master_skip_tx_api, PLUGIN_VAR_RQCMDARG,
                         "Skipping holding any lock on row access. "
                         "Not effective on slave.",
                         nullptr, nullptr, false);

static MYSQL_SYSVAR_UINT(
    validate_tables, rocksdb_validate_tables,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "Verify all DD tables match all RocksDB tables (0 means no verification, "
    "1 means verify and fail on error, and 2 means verify but continue",
    nullptr, nullptr, 1 /* default value */, 0 /* min value */,
    2 /* max value */, 0);

static MYSQL_SYSVAR_STR(datadir, rocksdb_datadir,
                        PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_READONLY,
                        "RocksDB data directory", nullptr, nullptr,
                        "./.rocksdb");

static MYSQL_SYSVAR_UINT(
    table_stats_sampling_pct, rocksdb_table_stats_sampling_pct,
    PLUGIN_VAR_RQCMDARG,
    "Percentage of entries to sample when collecting statistics about table "
    "properties. Specify either 0 to sample everything or percentage "
    "[" STRINGIFY_ARG(RDB_TBL_STATS_SAMPLE_PCT_MIN) ".." STRINGIFY_ARG(
        RDB_TBL_STATS_SAMPLE_PCT_MAX) "]. "
                                      "By default " STRINGIFY_ARG(
                                          RDB_DEFAULT_TBL_STATS_SAMPLE_PCT) "% "
                                                                            "of"
                                                                            " e"
                                                                            "nt"
                                                                            "ri"
                                                                            "es"
                                                                            " a"
                                                                            "re"
                                                                            " "
                                                                            "sa"
                                                                            "mp"
                                                                            "le"
                                                                            "d"
                                                                            ".",
    nullptr, rocksdb_set_table_stats_sampling_pct, /* default */
    RDB_DEFAULT_TBL_STATS_SAMPLE_PCT, /* everything */ 0,
    /* max */ RDB_TBL_STATS_SAMPLE_PCT_MAX, 0);

static MYSQL_SYSVAR_UINT(table_stats_recalc_threshold_pct,
                         rocksdb_table_stats_recalc_threshold_pct,
                         PLUGIN_VAR_RQCMDARG,
                         "Percentage of number of modified rows over total "
                         "number of rows to trigger stats recalculation",
                         nullptr, nullptr, /* default */
                         rocksdb_table_stats_recalc_threshold_pct,
                         /* everything */ 0,
                         /* max */ RDB_TBL_STATS_RECALC_THRESHOLD_PCT_MAX, 0);

static MYSQL_SYSVAR_ULONGLONG(
    table_stats_recalc_threshold_count,
    rocksdb_table_stats_recalc_threshold_count, PLUGIN_VAR_RQCMDARG,
    "Number of modified rows to trigger stats recalculation", nullptr,
    nullptr, /* default */
    rocksdb_table_stats_recalc_threshold_count,
    /* everything */ 0,
    /* max */ UINT64_MAX, 0);

static MYSQL_SYSVAR_INT(
    table_stats_background_thread_nice_value,
    rocksdb_table_stats_background_thread_nice_value, PLUGIN_VAR_RQCMDARG,
    "nice value for index stats", rocksdb_index_stats_thread_renice, nullptr,
    /* default */ rocksdb_table_stats_background_thread_nice_value,
    /* min */ THREAD_PRIO_MIN, /* max */ THREAD_PRIO_MAX, 0);

static MYSQL_SYSVAR_ULONGLONG(
    table_stats_max_num_rows_scanned, rocksdb_table_stats_max_num_rows_scanned,
    PLUGIN_VAR_RQCMDARG,
    "The maximum number of rows to scan in table scan based "
    "cardinality calculation",
    nullptr, nullptr, /* default */
    0, /* everything */ 0,
    /* max */ UINT64_MAX, 0);

static MYSQL_SYSVAR_UINT(
    stats_recalc_rate, rocksdb_stats_recalc_rate, PLUGIN_VAR_RQCMDARG,
    "The number of indexes per second to recalculate statistics for. 0 to "
    "disable background recalculation.",
    nullptr, nullptr, 0 /* default value */, 0 /* min value */,
    UINT_MAX /* max value */, 0);

static MYSQL_SYSVAR_BOOL(table_stats_use_table_scan,
                         rocksdb_table_stats_use_table_scan,
                         PLUGIN_VAR_RQCMDARG,
                         "Enable table scan based index calculation.", nullptr,
                         rocksdb_update_table_stats_use_table_scan,
                         rocksdb_table_stats_use_table_scan);

static MYSQL_SYSVAR_BOOL(use_range_locking, rocksdb_use_range_locking,
                         PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_READONLY,
                         "Use Range Locking",
                         nullptr, nullptr,
                         rocksdb_use_range_locking);

static MYSQL_SYSVAR_BOOL(use_range_lock_manager_as_point,
                         rocksdb_use_range_lock_manager_as_point,
                         PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_READONLY,
                         "Use Range Lock Manager as point",
                         nullptr, nullptr,
                         rocksdb_use_range_lock_manager_as_point);

static MYSQL_SYSVAR_BOOL(
    large_prefix, rocksdb_large_prefix, PLUGIN_VAR_RQCMDARG,
    "Support large index prefix length of 3072 bytes. If off, the maximum "
    "index prefix length is 767.",
    nullptr, nullptr, false);

static MYSQL_SYSVAR_BOOL(
    allow_to_start_after_corruption, rocksdb_allow_to_start_after_corruption,
    PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_READONLY,
    "Allow server still to start successfully even if RocksDB corruption is "
    "detected.",
    nullptr, nullptr, false);

static MYSQL_SYSVAR_BOOL(error_on_suboptimal_collation,
                         rocksdb_error_on_suboptimal_collation,
                         PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_READONLY,
                         "Raise an error instead of warning if a sub-optimal "
                         "collation is used",
                         nullptr, nullptr, true);

static MYSQL_SYSVAR_BOOL(
    enable_insert_with_update_caching,
    rocksdb_enable_insert_with_update_caching, PLUGIN_VAR_OPCMDARG,
    "Whether to enable optimization where we cache the read from a failed "
    "insertion attempt in INSERT ON DUPLICATE KEY UPDATE",
    nullptr, nullptr, true);

static MYSQL_SYSVAR_STR(
    trace_block_cache_access, rocksdb_block_cache_trace_options_str,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
    "Block cache trace option string. The format is "
    "sampling_frequency:max_trace_file_size:trace_file_name. "
    "sampling_frequency and max_trace_file_size are positive integers. The "
    "block accesses are saved to the "
    "rocksdb_datadir/block_cache_traces/trace_file_name.",
    rocksdb_trace_block_cache_access, rocksdb_trace_stub, "");

static MYSQL_SYSVAR_STR(
    trace_queries, rocksdb_trace_options_str,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
    "Trace option string. The format is "
    "sampling_frequency:max_trace_file_size:trace_file_name. "
    "sampling_frequency and max_trace_file_size are positive integers. The "
    "queries are saved to the "
    "rocksdb_datadir/queries_traces/trace_file_name.",
    rocksdb_trace_queries, rocksdb_trace_stub, "");

static MYSQL_SYSVAR_ULONGLONG(
    max_compaction_history, rocksdb_max_compaction_history, PLUGIN_VAR_OPCMDARG,
    "Track history for at most this many completed compactions. "
    "The history is in the INFORMATION_SCHEMA.ROCKSDB_COMPACTION_HISTORY "
    "table.",
    nullptr, rocksdb_max_compaction_history_update,
    RDB_DEFAULT_MAX_COMPACTION_HISTORY, 0ULL /* min */, UINT64_MAX /* max */,
    0 /* blk */);

static MYSQL_THDVAR_BOOL(disable_file_deletions,
                         PLUGIN_VAR_NOCMDARG | PLUGIN_VAR_RQCMDARG,
                         "Prevent file deletions", nullptr,
                         rocksdb_disable_file_deletions_update, false);
static MYSQL_SYSVAR_ENUM(
    select_bypass_policy, rocksdb_select_bypass_policy, PLUGIN_VAR_RQCMDARG,
    "Change bypass SELECT related policy and allow directly talk to RocksDB. "
    "Valid values include 'always_off', 'always_on', 'opt_in', 'opt_out'. ",
    nullptr, nullptr, select_bypass_policy_type::default_value,
    &select_bypass_policy_typelib);

static MYSQL_SYSVAR_BOOL(
    select_bypass_fail_unsupported, rocksdb_select_bypass_fail_unsupported,
    PLUGIN_VAR_RQCMDARG,
    "Select bypass would fail for unsupported SELECT commands", nullptr,
    nullptr, true);

static MYSQL_SYSVAR_BOOL(select_bypass_log_rejected,
                         rocksdb_select_bypass_log_rejected,
                         PLUGIN_VAR_RQCMDARG,
                         "Log rejected SELECT bypass queries", nullptr, nullptr,
                         true);

static MYSQL_SYSVAR_BOOL(select_bypass_log_failed,
                         rocksdb_select_bypass_log_failed, PLUGIN_VAR_RQCMDARG,
                         "Log failed SELECT bypass queries", nullptr, nullptr,
                         false);

static MYSQL_SYSVAR_BOOL(select_bypass_allow_filters,
                         rocksdb_select_bypass_allow_filters,
                         PLUGIN_VAR_RQCMDARG,
                         "Allow non-optimal filters in SELECT bypass queries",
                         nullptr, nullptr, true);

static MYSQL_SYSVAR_UINT(
    select_bypass_rejected_query_history_size,
    rocksdb_select_bypass_rejected_query_history_size, PLUGIN_VAR_RQCMDARG,
    "History size of rejected bypass queries in "
    "information_schema.bypass_rejected_query_history. "
    "Set to 0 to turn off",
    nullptr, rocksdb_select_bypass_rejected_query_history_size_update, 0,
    /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_UINT(
    select_bypass_debug_row_delay, rocksdb_select_bypass_debug_row_delay,
    PLUGIN_VAR_RQCMDARG,
    "Test only to inject delays in bypass select to simulate long queries "
    "for each row sent",
    nullptr, nullptr, 0, /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_ULONGLONG(
    select_bypass_multiget_min, rocksdb_select_bypass_multiget_min,
    PLUGIN_VAR_RQCMDARG,
    "Minimum number of items to use RocksDB MultiGet API. Default is "
    "SIZE_T_MAX meaning it is turned off. Set to 0 to enable always using "
    "MultiGet",
    nullptr, nullptr, SIZE_T_MAX, /* min */ 0, /* max */ SIZE_T_MAX, 0);

static MYSQL_THDVAR_ULONG(mrr_batch_size, PLUGIN_VAR_RQCMDARG,
                          "maximum number of keys to fetch during each MRR",
                          nullptr, nullptr, /* default */ 100, /* min */ 0,
                          /* max */ ROCKSDB_MAX_MRR_BATCH_SIZE, 0);

static MYSQL_SYSVAR_BOOL(skip_locks_if_skip_unique_check,
                         rocksdb_skip_locks_if_skip_unique_check,
                         PLUGIN_VAR_RQCMDARG,
                         "Skip row locking when unique checks are disabled.",
                         nullptr, nullptr, false);

static MYSQL_SYSVAR_BOOL(
    alter_column_default_inplace, rocksdb_alter_column_default_inplace,
    PLUGIN_VAR_RQCMDARG,
    "Allow inplace alter for alter column default operation", nullptr, nullptr,
    true);

static MYSQL_THDVAR_ULONGLONG(
    partial_index_sort_max_mem, PLUGIN_VAR_RQCMDARG,
    "Maximum memory to use when sorting an unmaterialized group for partial "
    "indexes. 0 means no limit.",
    nullptr, nullptr,
    /* default */ 0, /* min */ 0, /* max */ SIZE_T_MAX, 1);

static MYSQL_SYSVAR_BOOL(instant_ddl, rocksdb_instant_ddl, PLUGIN_VAR_RQCMDARG,
                         "Allow instant ddl during alter table", nullptr,
                         nullptr, false);

static const int ROCKSDB_ASSUMED_KEY_VALUE_DISK_SIZE = 100;

static struct SYS_VAR *rocksdb_system_variables[] = {
    MYSQL_SYSVAR(lock_wait_timeout),
    MYSQL_SYSVAR(deadlock_detect),
    MYSQL_SYSVAR(deadlock_detect_depth),
    MYSQL_SYSVAR(commit_time_batch_for_recovery),
    MYSQL_SYSVAR(max_row_locks),
    MYSQL_SYSVAR(write_batch_max_bytes),
    MYSQL_SYSVAR(write_batch_flush_threshold),
    MYSQL_SYSVAR(lock_scanned_rows),
    MYSQL_SYSVAR(bulk_load),
    MYSQL_SYSVAR(bulk_load_allow_sk),
    MYSQL_SYSVAR(bulk_load_allow_unsorted),
    MYSQL_SYSVAR(trace_sst_api),
    MYSQL_SYSVAR(commit_in_the_middle),
    MYSQL_SYSVAR(blind_delete_primary_key),
    MYSQL_SYSVAR(enable_iterate_bounds),
    MYSQL_SYSVAR(read_free_rpl_tables),
    MYSQL_SYSVAR(read_free_rpl),
    MYSQL_SYSVAR(bulk_load_size),
    MYSQL_SYSVAR(bulk_load_partial_index),
    MYSQL_SYSVAR(merge_buf_size),
    MYSQL_SYSVAR(enable_bulk_load_api),
    MYSQL_SYSVAR(enable_remove_orphaned_dropped_cfs),
    MYSQL_SYSVAR(tmpdir),
    MYSQL_SYSVAR(merge_combine_read_size),
    MYSQL_SYSVAR(merge_tmp_file_removal_delay_ms),
    MYSQL_SYSVAR(skip_bloom_filter_on_read),

    MYSQL_SYSVAR(create_if_missing),
    MYSQL_SYSVAR(two_write_queues),
    MYSQL_SYSVAR(manual_wal_flush),
    MYSQL_SYSVAR(write_policy),
    MYSQL_SYSVAR(create_missing_column_families),
    MYSQL_SYSVAR(error_if_exists),
    MYSQL_SYSVAR(paranoid_checks),
    MYSQL_SYSVAR(rate_limiter_bytes_per_sec),
    MYSQL_SYSVAR(sst_mgr_rate_bytes_per_sec),
    MYSQL_SYSVAR(delayed_write_rate),
    MYSQL_SYSVAR(max_latest_deadlocks),
    MYSQL_SYSVAR(info_log_level),
    MYSQL_SYSVAR(max_open_files),
    MYSQL_SYSVAR(max_total_wal_size),
    MYSQL_SYSVAR(use_fsync),
    MYSQL_SYSVAR(wal_dir),
    MYSQL_SYSVAR(persistent_cache_path),
    MYSQL_SYSVAR(persistent_cache_size_mb),
    MYSQL_SYSVAR(wsenv_path),
    MYSQL_SYSVAR(wsenv_tenant),
    MYSQL_SYSVAR(fault_injection_options),
    MYSQL_SYSVAR(delete_obsolete_files_period_micros),
    MYSQL_SYSVAR(max_background_jobs),
    MYSQL_SYSVAR(max_background_flushes),
    MYSQL_SYSVAR(max_background_compactions),
    MYSQL_SYSVAR(max_bottom_pri_background_compactions),
    MYSQL_SYSVAR(max_log_file_size),
    MYSQL_SYSVAR(max_subcompactions),
    MYSQL_SYSVAR(log_file_time_to_roll),
    MYSQL_SYSVAR(keep_log_file_num),
    MYSQL_SYSVAR(max_manifest_file_size),
    MYSQL_SYSVAR(table_cache_numshardbits),
    MYSQL_SYSVAR(wal_ttl_seconds),
    MYSQL_SYSVAR(wal_size_limit_mb),
    MYSQL_SYSVAR(manifest_preallocation_size),
    MYSQL_SYSVAR(use_direct_reads),
    MYSQL_SYSVAR(use_direct_io_for_flush_and_compaction),
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
    MYSQL_SYSVAR(track_and_verify_wals_in_manifest),
    MYSQL_SYSVAR(stats_level),
    MYSQL_SYSVAR(access_hint_on_compaction_start),
    MYSQL_SYSVAR(new_table_reader_for_compaction_inputs),
    MYSQL_SYSVAR(compaction_readahead_size),
    MYSQL_SYSVAR(allow_concurrent_memtable_write),
    MYSQL_SYSVAR(enable_write_thread_adaptive_yield),

    MYSQL_SYSVAR(block_cache_size),
    MYSQL_SYSVAR(sim_cache_size),
    MYSQL_SYSVAR(use_clock_cache),
    MYSQL_SYSVAR(cache_high_pri_pool_ratio),
    MYSQL_SYSVAR(cache_dump),
    MYSQL_SYSVAR(cache_index_and_filter_blocks),
    MYSQL_SYSVAR(cache_index_and_filter_with_high_priority),
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
    MYSQL_SYSVAR(update_cf_options),
    MYSQL_SYSVAR(use_default_sk_cf),

    MYSQL_SYSVAR(flush_log_at_trx_commit),
    MYSQL_SYSVAR(write_disable_wal),
    MYSQL_SYSVAR(write_ignore_missing_column_families),

    MYSQL_SYSVAR(skip_fill_cache),
    MYSQL_SYSVAR(unsafe_for_binlog),

    MYSQL_SYSVAR(records_in_range),
    MYSQL_SYSVAR(force_index_records_in_range),
    MYSQL_SYSVAR(debug_optimizer_n_rows),
    MYSQL_SYSVAR(force_compute_memtable_stats),
    MYSQL_SYSVAR(force_compute_memtable_stats_cachetime),
    MYSQL_SYSVAR(debug_optimizer_no_zero_cardinality),
    MYSQL_SYSVAR(debug_cardinality_multiplier),

    MYSQL_SYSVAR(compact_cf),
    MYSQL_SYSVAR(delete_cf),
    MYSQL_SYSVAR(signal_drop_index_thread),
    MYSQL_SYSVAR(pause_background_work),
    MYSQL_SYSVAR(enable_2pc),
    MYSQL_SYSVAR(ignore_unknown_options),
    MYSQL_SYSVAR(strict_collation_check),
    MYSQL_SYSVAR(strict_collation_exceptions),
    MYSQL_SYSVAR(collect_sst_properties),
    MYSQL_SYSVAR(force_flush_memtable_now),
    MYSQL_SYSVAR(force_flush_memtable_and_lzero_now),
    MYSQL_SYSVAR(cancel_manual_compactions),
    MYSQL_SYSVAR(enable_ttl),
    MYSQL_SYSVAR(enable_ttl_read_filtering),
    MYSQL_SYSVAR(debug_ttl_rec_ts),
    MYSQL_SYSVAR(debug_ttl_snapshot_ts),
    MYSQL_SYSVAR(debug_ttl_read_filter_ts),
    MYSQL_SYSVAR(debug_ttl_ignore_pk),
    MYSQL_SYSVAR(reset_stats),
    MYSQL_SYSVAR(io_write_timeout),
    MYSQL_SYSVAR(seconds_between_stat_computes),

    MYSQL_SYSVAR(compaction_sequential_deletes),
    MYSQL_SYSVAR(compaction_sequential_deletes_window),
    MYSQL_SYSVAR(compaction_sequential_deletes_file_size),
    MYSQL_SYSVAR(compaction_sequential_deletes_count_sd),
    MYSQL_SYSVAR(print_snapshot_conflict_queries),

    MYSQL_SYSVAR(datadir),
    MYSQL_SYSVAR(create_checkpoint),
    MYSQL_SYSVAR(create_temporary_checkpoint),
    MYSQL_SYSVAR(disable_file_deletions),

    MYSQL_SYSVAR(checksums_pct),
    MYSQL_SYSVAR(store_row_debug_checksums),
    MYSQL_SYSVAR(verify_row_debug_checksums),
    MYSQL_SYSVAR(master_skip_tx_api),

    MYSQL_SYSVAR(validate_tables),
    MYSQL_SYSVAR(table_stats_sampling_pct),
    MYSQL_SYSVAR(table_stats_recalc_threshold_pct),
    MYSQL_SYSVAR(table_stats_recalc_threshold_count),
    MYSQL_SYSVAR(table_stats_max_num_rows_scanned),
    MYSQL_SYSVAR(table_stats_use_table_scan),
    MYSQL_SYSVAR(table_stats_background_thread_nice_value),

    MYSQL_SYSVAR(large_prefix),
    MYSQL_SYSVAR(allow_to_start_after_corruption),
    MYSQL_SYSVAR(error_on_suboptimal_collation),
    MYSQL_SYSVAR(stats_recalc_rate),
    MYSQL_SYSVAR(debug_manual_compaction_delay),
    MYSQL_SYSVAR(max_manual_compactions),
    MYSQL_SYSVAR(manual_compaction_threads),
    MYSQL_SYSVAR(manual_compaction_bottommost_level),
    MYSQL_SYSVAR(rollback_on_timeout),
    MYSQL_SYSVAR(use_range_locking),
    MYSQL_SYSVAR(use_range_lock_manager_as_point),
    MYSQL_SYSVAR(max_lock_memory),
    MYSQL_SYSVAR(enable_insert_with_update_caching),
    MYSQL_SYSVAR(trace_block_cache_access),
    MYSQL_SYSVAR(trace_queries),
    MYSQL_SYSVAR(max_compaction_history),
    MYSQL_SYSVAR(mrr_batch_size),

    MYSQL_SYSVAR(select_bypass_policy),
    MYSQL_SYSVAR(select_bypass_fail_unsupported),
    MYSQL_SYSVAR(select_bypass_log_failed),
    MYSQL_SYSVAR(select_bypass_rejected_query_history_size),
    MYSQL_SYSVAR(select_bypass_log_rejected),
    MYSQL_SYSVAR(select_bypass_allow_filters),
    MYSQL_SYSVAR(select_bypass_debug_row_delay),
    MYSQL_SYSVAR(select_bypass_multiget_min),
    MYSQL_SYSVAR(skip_locks_if_skip_unique_check),
    MYSQL_SYSVAR(alter_column_default_inplace),
    MYSQL_SYSVAR(partial_index_sort_max_mem),
    MYSQL_SYSVAR(instant_ddl),
    nullptr};

static int rocksdb_compact_column_family(
    THD *const thd, struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)),
    struct st_mysql_value *const value) {
  char buff[STRING_BUFFER_USUAL_SIZE];
  int len = sizeof(buff);

  DBUG_ASSERT(value != nullptr);

  if (const char *const cf = value->val_str(value, buff, &len)) {
    DBUG_EXECUTE_IF("rocksdb_compact_column_family", {
      const char act[] =
          "now signal ready_to_mark_cf_dropped_in_compact_column_family "
          "wait_for mark_cf_dropped_done_in_compact_column_family";
      DBUG_ASSERT(!debug_sync_set_action(thd, STRING_WITH_LEN(act)));
    });

    std::string cf_name = std::string(cf);
    // use rocksdb_compact_cf="" or "default" to compact default CF
    if (cf_name.empty()) cf_name = DEFAULT_CF_NAME;

    auto cfh = cf_manager.get_cf(cf_name);
    if (cfh != nullptr && rdb != nullptr) {
      rocksdb::BottommostLevelCompaction bottommost_level_compaction =
          (rocksdb::BottommostLevelCompaction)THDVAR(
              thd, manual_compaction_bottommost_level);

      int mc_id = rdb_mc_thread.request_manual_compaction(
          cfh, nullptr, nullptr, THDVAR(thd, manual_compaction_threads),
          bottommost_level_compaction);
      if (mc_id == -1) {
        my_error(ER_INTERNAL_ERROR, MYF(0),
                 "Can't schedule more manual compactions. "
                 "Increase rocksdb_max_manual_compactions or stop issuing "
                 "more manual compactions.");
        return HA_EXIT_FAILURE;
      } else if (mc_id < 0) {
        return HA_EXIT_FAILURE;
      }

      auto grd =
          create_scope_guard([&]() { rdb_mc_thread.set_client_done(mc_id); });
      // NO_LINT_DEBUG
      sql_print_information("RocksDB: Manual compaction of column family: %s\n",
                            cf);
      // Checking thd state every short cycle (100ms). This is for allowing to
      // exiting this function without waiting for CompactRange to finish.
      Rdb_manual_compaction_thread::Manual_compaction_request::mc_state
          mc_status;
      do {
        my_sleep(100000);
        mc_status = rdb_mc_thread.manual_compaction_state(mc_id);
      } while (!thd->killed &&
               (mc_status == Rdb_manual_compaction_thread::
                                 Manual_compaction_request::PENDING ||
                mc_status == Rdb_manual_compaction_thread::
                                 Manual_compaction_request::RUNNING));

      bool mc_timeout = false;
      if (thd->killed) {
        // Cancelling pending or running manual compaction with 60s timeout
        mc_timeout = rdb_mc_thread.cancel_manual_compaction_request(mc_id, 600);
      }

      mc_status = rdb_mc_thread.manual_compaction_state(mc_id);
      if (mc_status !=
          Rdb_manual_compaction_thread::Manual_compaction_request::SUCCESS) {
        std::string msg = "Manual Compaction Failed. Reason: ";
        if (thd->killed) {
          msg += "Cancelled by client.";
        } else if (mc_status == Rdb_manual_compaction_thread::
                                    Manual_compaction_request::CANCEL) {
          msg += "Cancelled by server.";
        } else {
          msg += "General failures.";
        }
        if (mc_timeout) {
          msg += " (timeout)";
        }
        my_error(ER_INTERNAL_ERROR, MYF(0), msg.c_str());
        return HA_EXIT_FAILURE;
      }
    }
  }
  return HA_EXIT_SUCCESS;
}

/*
 * Serializes an xid to a string so that it can
 * be used as a rocksdb transaction name
 */
static std::string rdb_xid_to_string(const XID &src) {
  DBUG_ASSERT(src.get_gtrid_length() >= 0 &&
              src.get_gtrid_length() <= MAXGTRIDSIZE);
  DBUG_ASSERT(src.get_bqual_length() >= 0 &&
              src.get_bqual_length() <= MAXBQUALSIZE);

  std::string buf;
  buf.reserve(RDB_XIDHDR_LEN + src.get_gtrid_length() + src.get_bqual_length());

  /*
   * expand formatID to fill 8 bytes if it doesn't already
   * then reinterpret bit pattern as unsigned and store in network order
   */
  uchar fidbuf[RDB_FORMATID_SZ];
  int64 signed_fid8 = src.get_format_id();
  const uint64 raw_fid8 = *reinterpret_cast<uint64 *>(&signed_fid8);
  rdb_netbuf_store_uint64(fidbuf, raw_fid8);
  buf.append(reinterpret_cast<const char *>(fidbuf), RDB_FORMATID_SZ);

  buf.push_back(src.get_gtrid_length());
  buf.push_back(src.get_bqual_length());
  buf.append(src.get_data(),
             (src.get_gtrid_length()) + (src.get_bqual_length()));
  return buf;
}

///////////////////////////////////////////////////////////////////////////////////////////

/*
  Drop index thread's control
*/

static void rocksdb_drop_index_wakeup_thread(
    my_core::THD *const thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)), const void *const save) {
  if (*static_cast<const bool *>(save)) {
    rdb_drop_idx_thread.signal();
  }
}

uint32_t rocksdb_perf_context_level(THD *const thd) {
  DBUG_ASSERT(thd != nullptr);

  const int session_perf_context_level = THDVAR(thd, perf_context_level);
  if (session_perf_context_level > rocksdb::PerfLevel::kUninitialized) {
    return session_perf_context_level;
  }

  /*
    Fallback to global thdvar, if session specific one was not set to a valid
    value.
  */

  const int global_perf_context_level = THDVAR(nullptr, perf_context_level);
  if (global_perf_context_level > rocksdb::PerfLevel::kUninitialized) {
    return global_perf_context_level;
  }

  return rocksdb::PerfLevel::kDisable;
}

/*
  Very short (functor-like) interface to be passed to
  Rdb_transaction::walk_tx_list()
*/

interface Rdb_tx_list_walker {
  virtual ~Rdb_tx_list_walker() {}
  virtual void process_tran(const Rdb_transaction *const) = 0;
};

/*
  This is a helper class that is passed to RocksDB to get notifications when
  a snapshot gets created.
*/

class Rdb_snapshot_notifier : public rocksdb::TransactionNotifier {
  Rdb_transaction *m_owning_tx;

  void SnapshotCreated(const rocksdb::Snapshot *snapshot) override;

 public:
  Rdb_snapshot_notifier(const Rdb_snapshot_notifier &) = delete;
  Rdb_snapshot_notifier &operator=(const Rdb_snapshot_notifier &) = delete;

  explicit Rdb_snapshot_notifier(Rdb_transaction *const owning_tx)
      : m_owning_tx(owning_tx) {}

  // If the owning Rdb_transaction gets destructed we need to not reference
  // it anymore.
  void detach() { m_owning_tx = nullptr; }
};

/* This is the base class for transactions when interacting with rocksdb.
 */
class Rdb_transaction {
 protected:
  ulonglong m_write_count = 0;
  ulonglong m_insert_count = 0;
  ulonglong m_update_count = 0;
  ulonglong m_delete_count = 0;
  // per row data
  // (with range locking, each locked range is counted here, too)
  ulonglong m_row_lock_count = 0;
  std::unordered_map<GL_INDEX_ID, ulonglong> m_auto_incr_map;

  bool m_is_delayed_snapshot = false;
  bool m_is_two_phase = false;

  std::unordered_set<Rdb_tbl_def *> modified_tables;

 private:
  /*
    Number of write operations this transaction had when we took the last
    savepoint (the idea is not to take another savepoint if we haven't made
    any changes)
  */
  ulonglong m_writes_at_last_savepoint;

 protected:
  THD *m_thd = nullptr;

  static std::multiset<Rdb_transaction *> s_tx_list;
  static Rds_mysql_mutex s_tx_list_mutex;

  Rdb_io_perf *m_tbl_io_perf;

  bool m_tx_read_only = false;

  int m_timeout_sec; /* Cached value of @@rocksdb_lock_wait_timeout */

  /* Maximum number of locks the transaction can have */
  ulonglong m_max_row_locks;

  bool m_is_tx_failed = false;
  bool m_rollback_only = false;

  std::shared_ptr<Rdb_snapshot_notifier> m_notifier;

  // This should be used only when updating binlog information.
  virtual rocksdb::WriteBatchBase *get_write_batch() = 0;
  virtual bool commit_no_binlog() = 0;

  /*
    @detail
      This function takes in the WriteBatch of the transaction to add
      all the AUTO_INCREMENT merges. It does so by iterating through
      m_auto_incr_map and then constructing key/value pairs to call merge upon.

    @param wb
   */
  rocksdb::Status merge_auto_incr_map(rocksdb::WriteBatchBase *const wb) {
    DBUG_EXECUTE_IF("myrocks_autoinc_upgrade", return rocksdb::Status::OK(););

    // Iterate through the merge map merging all keys into data dictionary.
    rocksdb::Status s;
    for (auto &it : m_auto_incr_map) {
      s = dict_manager.put_auto_incr_val(wb, it.first, it.second);
      if (!s.ok()) {
        return s;
      }
    }
    m_auto_incr_map.clear();
    return s;
  }

 protected:
  /*
    The following two are helper functions to be overloaded by child classes.
    They should provide RocksDB's savepoint semantics.
  */
  virtual void do_set_savepoint() = 0;
  virtual rocksdb::Status do_pop_savepoint() = 0;
  virtual void do_rollback_to_savepoint() = 0;

 private:
  /*
    If true, the current statement should not use a snapshot for reading.
    Note that in a multi-statement transaction, the snapshot may have been
    allocated by another statement.
  */
  bool m_stmt_ignores_snapshot = false;

  /* Snapshot-ignore mode will put away m_reads_opts.snapshot here: */
  const rocksdb::Snapshot *m_saved_snapshot= nullptr;

 public:

  void start_ignore_snapshot() {
    // note: this may be called several times for the same statement
    if (!m_stmt_ignores_snapshot) {
      m_saved_snapshot = m_read_opts.snapshot;
      m_read_opts.snapshot = nullptr;
      m_stmt_ignores_snapshot= true;
      if (!m_snapshot_timestamp)
        rdb->GetEnv()->GetCurrentTime(&m_snapshot_timestamp);
    }
  }

  void end_ignore_snapshot_if_needed() {
    if (m_stmt_ignores_snapshot) {
      m_stmt_ignores_snapshot = false;
      m_read_opts.snapshot = m_saved_snapshot;
      m_saved_snapshot = nullptr;
    }
  }
  bool in_snapshot_ignore_mode() const { return m_stmt_ignores_snapshot; }

  rocksdb::ReadOptions m_read_opts;

  const char *m_mysql_log_file_name;
  my_off_t m_mysql_log_offset;
  const char *m_mysql_gtid;
  const char *m_mysql_max_gtid;
  String m_detailed_error;
  int64_t m_snapshot_timestamp = 0;
  bool m_ddl_transaction;
  std::shared_ptr<Rdb_explicit_snapshot> m_explicit_snapshot;

  /*
    Tracks the number of tables in use through external_lock.
    This should not be reset during start_tx().
  */
  int64_t m_n_mysql_tables_in_use = 0;

  /*
    for distinction between rdb_transaction_impl and rdb_writebatch_impl
    when using walk tx list
  */
  virtual bool is_writebatch_trx() const = 0;

  static void init_mutex() {
    s_tx_list_mutex.init(key_mutex_tx_list, MY_MUTEX_INIT_FAST);
  }

  static void term_mutex() {
    DBUG_ASSERT(s_tx_list.size() == 0);
    s_tx_list_mutex.destroy();
  }

  static void walk_tx_list(Rdb_tx_list_walker *walker) {
    DBUG_ASSERT(walker != nullptr);

    RDB_MUTEX_LOCK_CHECK(s_tx_list_mutex);

    for (auto it : s_tx_list) {
      walker->process_tran(it);
    }

    RDB_MUTEX_UNLOCK_CHECK(s_tx_list_mutex);
  }

  int set_status_error(THD *const thd, const rocksdb::Status &s,
                       const Rdb_key_def &kd, const Rdb_tbl_def *const tbl_def,
                       Rdb_table_handler *const table_handler
                           MY_ATTRIBUTE((unused))) {
    DBUG_ASSERT(!s.ok());
    DBUG_ASSERT(tbl_def != nullptr);

    if (s.IsTimedOut()) {
      /*
        SQL layer has weird expectations. If we return an error when
        doing a read in DELETE IGNORE, it will ignore the error ("because it's
        an IGNORE command!) but then will fail an assert, because "error code
        was returned, but no error happened".  Do what InnoDB's
        convert_error_code_to_mysql() does: force a statement
        rollback before returning HA_ERR_LOCK_WAIT_TIMEOUT:
        */
      my_core::thd_mark_transaction_to_rollback(thd,
                                                rocksdb_rollback_on_timeout);
      m_detailed_error.copy(timeout_message(
          "index", tbl_def->full_tablename().c_str(), kd.get_name().c_str()));
      /* TODO(yzha) - row stats are gone in 8.0
      table_handler->m_lock_wait_timeout_counter.inc(); */
      rocksdb_row_lock_wait_timeouts++;

      return HA_ERR_LOCK_WAIT_TIMEOUT;
    }

    if (s.IsDeadlock()) {
      my_core::thd_mark_transaction_to_rollback(thd, 1 /* whole transaction */);
      m_detailed_error = String();
      /* TODO(yzha) - row stats are gone in 8.0
      table_handler->m_deadlock_counter.inc(); */
      rocksdb_row_lock_deadlocks++;
      return HA_ERR_LOCK_DEADLOCK;
    } else if (s.IsBusy()) {
      rocksdb_snapshot_conflict_errors++;
      if (rocksdb_print_snapshot_conflict_queries) {
        char user_host_buff[MAX_USER_HOST_SIZE + 1];
        make_user_name(thd->security_context(), user_host_buff);
        // NO_LINT_DEBUG
        sql_print_warning(
            "Got snapshot conflict errors: User: %s "
            "Query: %s",
            user_host_buff, thd->query());
      }
      m_detailed_error = String(" (snapshot conflict)", system_charset_info);
      /* TODO(yzha) - row stats are gone in 8.0
      table_handler->m_deadlock_counter.inc(); */
      return HA_ERR_ROCKSDB_STATUS_BUSY;
    }

    if (s.IsIOError() || s.IsCorruption()) {
      rdb_handle_io_error(s, RDB_IO_ERROR_GENERAL);
    }

    return ha_rocksdb::rdb_error_to_mysql(s);
  }

  THD *get_thd() const { return m_thd; }

  /* Used for tracking io_perf counters */
  void io_perf_start(Rdb_io_perf *const io_perf) {
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
        io_perf->start(rocksdb_perf_context_level(m_thd))) {
      m_tbl_io_perf = io_perf;
    }
  }

  void io_perf_end_and_record(void) {
    if (m_tbl_io_perf != nullptr) {
      m_tbl_io_perf->end_and_record(m_thd);
      m_tbl_io_perf = nullptr;
    }
  }

  void io_perf_end_and_record(Rdb_io_perf *const io_perf) {
    if (m_tbl_io_perf == io_perf) {
      io_perf_end_and_record();
    }
  }

  void update_bytes_written(ulonglong bytes_written) {
    if (m_tbl_io_perf != nullptr) {
      m_tbl_io_perf->update_bytes_written(rocksdb_perf_context_level(m_thd),
                                          bytes_written);
    }
  }

  void set_params(int timeout_sec_arg, int max_row_locks_arg) {
    m_timeout_sec = timeout_sec_arg;
    m_max_row_locks = max_row_locks_arg;
    set_lock_timeout(timeout_sec_arg);
  }

  virtual void set_lock_timeout(int timeout_sec_arg) = 0;

  ulonglong get_write_count() const { return m_write_count; }

  ulonglong get_insert_count() const { return m_insert_count; }

  ulonglong get_update_count() const { return m_update_count; }

  ulonglong get_delete_count() const { return m_delete_count; }

  ulonglong get_row_lock_count() const { return m_row_lock_count; }

  void incr_insert_count() { ++m_insert_count; }

  void incr_update_count() { ++m_update_count; }

  void incr_delete_count() { ++m_delete_count; }

  void incr_row_lock_count() { ++m_row_lock_count; }

  ulonglong get_max_row_lock_count() const { return m_max_row_locks; }

  int get_timeout_sec() const { return m_timeout_sec; }

  virtual void set_sync(bool sync) = 0;

  virtual void release_lock(const Rdb_key_def &key_descr,
                            const std::string &rowkey, bool force = false) = 0;

  virtual
  rocksdb::Status lock_range(rocksdb::ColumnFamilyHandle *const cf,
                             const rocksdb::Endpoint &start,
                             const rocksdb::Endpoint &end) = 0;

  rocksdb::Status lock_singlepoint_range(rocksdb::ColumnFamilyHandle *const cf,
                                         const rocksdb::Slice &point) {
    // Normally, one needs to "flip" the endpoint type for reverse-ordered CFs.
    // But here we are locking just one point so this is not necessary.
    rocksdb::Endpoint endp(point, false);
    return lock_range(cf, endp, endp);
  }

  virtual bool prepare() = 0;

  bool commit_or_rollback() {
    bool res;
    if (m_is_tx_failed) {
      rollback();
      res = false;
    } else {
      res = commit();
    }
    return res;
  }

  bool commit() {
    if (get_write_count() == 0) {
      rollback();
      return false;
    } else if (m_rollback_only) {
      /*
        Transactions marked as rollback_only are expected to be rolled back at
        prepare(). But there are some exceptions like below that prepare() is
        never called and commit() is called instead.
         1. Binlog is disabled
         2. No modification exists in binlog cache for the transaction (#195)
        In both cases, rolling back transaction is safe. Nothing is written to
        binlog.
       */
      my_error(ER_ROLLBACK_ONLY, MYF(0));
      rollback();
      return true;
    } else {
      my_core::thd_binlog_pos(m_thd, &m_mysql_log_file_name,
                              &m_mysql_log_offset, &m_mysql_gtid,
                              &m_mysql_max_gtid);
      binlog_manager.update(m_mysql_log_file_name, m_mysql_log_offset,
                            m_mysql_max_gtid, get_write_batch());
      return commit_no_binlog();
    }
  }

  virtual void rollback() = 0;

  void snapshot_created(const rocksdb::Snapshot *const snapshot) {
    DBUG_ASSERT(snapshot != nullptr);

    m_read_opts.snapshot = snapshot;
    // TODO: Use snapshot timestamp from rocksdb Snapshot object itself. This
    // saves the extra call to fetch current time, and allows TTL compaction
    // (which uses rocksdb timestamp) to be consistent with TTL read filtering
    // (which uses this timestamp).
    //
    // There is no correctness problem though since m_snapshot_timestamp is
    // generally set after the snapshot has been created, so compaction is
    // not dropping anything that should have been visible.
    rdb->GetEnv()->GetCurrentTime(&m_snapshot_timestamp);
    m_is_delayed_snapshot = false;
  }

  void locking_iter_created() {
    if (!m_snapshot_timestamp)
      rdb->GetEnv()->GetCurrentTime(&m_snapshot_timestamp);
  }

  virtual void acquire_snapshot(bool acquire_now) = 0;
  virtual void release_snapshot() = 0;

  bool has_snapshot() const {
    return m_read_opts.snapshot != nullptr || m_saved_snapshot;
  }

 private:
  // The Rdb_sst_info structures we are currently loading.  In a partitioned
  // table this can have more than one entry
  std::vector<std::shared_ptr<Rdb_sst_info>> m_curr_bulk_load;
  std::string m_curr_bulk_load_tablename;

  /* External merge sorts for bulk load: key ID -> merge sort instance */
  std::unordered_map<GL_INDEX_ID, Rdb_index_merge> m_key_merge;

  /*
    Used to check for duplicate entries during fast unique secondary index
    creation.
  */
  struct unique_sk_buf_info {
    bool sk_buf_switch = false;
    rocksdb::Slice sk_memcmp_key;
    rocksdb::Slice sk_memcmp_key_old;
    uchar *dup_sk_buf = nullptr;
    uchar *dup_sk_buf_old = nullptr;

    /*
      This method is meant to be called back to back during inplace creation
      of unique indexes.  It will switch between two buffers, which
      will each store the memcmp form of secondary keys, which are then
      converted to slices in sk_memcmp_key or sk_memcmp_key_old.

      Switching buffers on each iteration allows us to retain the
      sk_memcmp_key_old value for duplicate comparison.
    */
    inline uchar *swap_and_get_sk_buf() {
      sk_buf_switch = !sk_buf_switch;
      return sk_buf_switch ? dup_sk_buf : dup_sk_buf_old;
    }

    ~unique_sk_buf_info() {
      my_free(dup_sk_buf);
      dup_sk_buf = nullptr;
      my_free(dup_sk_buf_old);
      dup_sk_buf_old = nullptr;
    }
  };

 public:
  int get_key_merge(GL_INDEX_ID kd_gl_id, rocksdb::ColumnFamilyHandle *cf,
                    Rdb_index_merge **key_merge) {
    int res;
    auto it = m_key_merge.find(kd_gl_id);
    if (it == m_key_merge.end()) {
      m_key_merge.emplace(
          std::piecewise_construct, std::make_tuple(kd_gl_id),
          std::make_tuple(
              get_rocksdb_tmpdir(), THDVAR(get_thd(), merge_buf_size),
              THDVAR(get_thd(), merge_combine_read_size),
              THDVAR(get_thd(), merge_tmp_file_removal_delay_ms), cf));
      it = m_key_merge.find(kd_gl_id);
      if ((res = it->second.init()) != 0) {
        return res;
      }
    }
    *key_merge = &it->second;
    return HA_EXIT_SUCCESS;
  }

  /* Finish bulk loading for all table handlers belongs to one connection */
  int finish_bulk_load(bool *is_critical_error = nullptr,
                       bool print_client_error = true,
                       TABLE *table_arg = nullptr,
                       char *table_name_arg = nullptr) {
    if (m_curr_bulk_load.size() == 0) {
      if (is_critical_error) {
        *is_critical_error = false;
      }
      return HA_EXIT_SUCCESS;
    }

    if (THDVAR(m_thd, trace_sst_api)) {
      // NO_LINT_DEBUG
      sql_print_information(
          "SST Tracing : Finishing bulk loading operation for table '%s'",
          m_curr_bulk_load_tablename.c_str());
    }

    Ensure_cleanup cleanup([&]() {
      // Always clear everything regardless of success/failure
      m_curr_bulk_load.clear();
      m_curr_bulk_load_tablename.clear();
      m_key_merge.clear();
    });

    int rc = 0;
    if (is_critical_error) {
      *is_critical_error = true;
    }

    // PREPARE phase: finish all on-going bulk loading Rdb_sst_info and
    // collect all Rdb_sst_commit_info containing (SST files, cf)
    if (THDVAR(m_thd, trace_sst_api)) {
      // NO_LINT_DEBUG
      sql_print_information("SST Tracing : Finishing '%zu' active SST files",
                            m_curr_bulk_load.size());
    }

    int rc2 = 0;
    std::vector<Rdb_sst_info::Rdb_sst_commit_info> sst_commit_list;
    sst_commit_list.reserve(m_curr_bulk_load.size());

    for (auto &sst_info : m_curr_bulk_load) {
      Rdb_sst_info::Rdb_sst_commit_info commit_info;

      // Commit the list of SST files and move it to the end of
      // sst_commit_list, effectively transfer the ownership over
      rc2 = sst_info->finish(&commit_info, print_client_error);
      if (rc2 && rc == 0) {
        // Don't return yet - make sure we finish all the SST infos
        rc = rc2;
      }

      // Make sure we have work to do - we might be losing the race
      if (rc2 == 0 && commit_info.has_work()) {
        sst_commit_list.emplace_back(std::move(commit_info));
        DBUG_ASSERT(!commit_info.has_work());
      }
    }

    if (THDVAR(m_thd, trace_sst_api)) {
      // NO_LINT_DEBUG
      sql_print_information("SST Tracing : All active SST files finished");
    }

    if (rc) {
      return rc;
    }

    // MERGING Phase: Flush the index_merge sort buffers into SST files in
    // Rdb_sst_info and collect all Rdb_sst_commit_info containing
    // (SST files, cf)
    if (!m_key_merge.empty()) {
      if (THDVAR(m_thd, trace_sst_api)) {
        // NO_LINT_DEBUG
        sql_print_information(
            "SST Tracing : Started flushing index_merge sort buffer");
      }

      Ensure_cleanup malloc_cleanup([]() {
        /*
          Explicitly tell jemalloc to clean up any unused dirty pages at this
          point.
          See https://reviews.facebook.net/D63723 for more details.
        */
        purge_all_jemalloc_arenas();
      });

      rocksdb::Slice merge_key;
      rocksdb::Slice merge_val;
      for (auto it = m_key_merge.begin(); it != m_key_merge.end(); it++) {
        GL_INDEX_ID index_id = it->first;
        std::shared_ptr<const Rdb_key_def> keydef =
            ddl_manager.safe_find(index_id);
        std::string table_name;
        if (table_name_arg) {
          table_name = table_name_arg;
        } else {
          table_name = ddl_manager.safe_get_table_name(index_id);
          // Rdb_sst_info expects a denormalized table name in the form of
          // "./database/table"
          std::replace(table_name.begin(), table_name.end(), '.', '/');
          table_name = "./" + table_name;
        }

        // Currently, unique indexes only checked in the inplace alter path,
        // but not in allow_sk bulk load path.
        bool is_unique_index =
            table_arg &&
            table_arg->key_info[keydef->get_keyno()].flags & HA_NOSAME;

        // Unable to find key definition or table name since the
        // table could have been dropped.
        // TODO(herman): there is a race here between dropping the table
        // and detecting a drop here. If the table is dropped while bulk
        // loading is finishing, these keys being added here may
        // be missed by the compaction filter and not be marked for
        // removal. It is unclear how to lock the sql table from the storage
        // engine to prevent modifications to it while bulk load is occurring.
        if (keydef == nullptr) {
          if (is_critical_error) {
            // We used to set the error but simply ignores it. This follows
            // current behavior and we should revisit this later
            *is_critical_error = false;
          }
          return HA_ERR_KEY_NOT_FOUND;
        } else if (table_name.empty()) {
          if (is_critical_error) {
            // We used to set the error but simply ignores it. This follows
            // current behavior and we should revisit this later
            *is_critical_error = false;
          }
          return HA_ERR_NO_SUCH_TABLE;
        }
        const std::string &index_name = keydef->get_name();

        if (THDVAR(m_thd, trace_sst_api)) {
          std::string full_name;
          int err = rdb_normalize_tablename(table_name, &full_name);
          if (err != HA_EXIT_SUCCESS) {
            full_name = table_name;
          }

          // NO_LINT_DEBUG
          sql_print_information(
              "SST Tracing : Flushing index_merge sort buffer for table '%s' "
              "and index '%s'",
              full_name.c_str(), index_name.c_str());
        }

        Rdb_index_merge &rdb_merge = it->second;

        auto sst_info = std::make_shared<Rdb_sst_info>(
            rdb, table_name, index_name, rdb_merge.get_cf(),
            *rocksdb_db_options, THDVAR(get_thd(), trace_sst_api));

        if (keydef->is_partial_index()) {
          if (!THDVAR(m_thd, bulk_load_partial_index)) continue;
          // For partial indexes, we only want to materialize groups that reach
          // the materialization threshold. The idea is to buffer the rows up to
          // the threshold, and only actually insert the keys once we break the
          // threshold.
          bool materialized = false;
          rocksdb::Slice cur_prefix;
          std::vector<std::pair<rocksdb::Slice, rocksdb::Slice>> keys;
          MEM_ROOT mem_root;
          init_sql_alloc(PSI_NOT_INSTRUMENTED, &mem_root, 4024, 0);

          while ((rc2 = rdb_merge.next(&merge_key, &merge_val)) == 0) {
            if (cur_prefix.size() == 0 ||
                !keydef->value_matches_prefix(merge_key, cur_prefix)) {
              // This is a new group, so clear any rows buffered from a prior
              // group.
              free_root(&mem_root, MYF(MY_KEEP_PREALLOC));
              keys.clear();
              materialized = false;

              // Determine the length of the new group prefix
              Rdb_string_reader reader(&merge_key);
              if ((!reader.read(Rdb_key_def::INDEX_NUMBER_SIZE))) {
                rc2 = HA_ERR_ROCKSDB_CORRUPT_DATA;
                break;
              }
              for (uint i = 0; i < keydef->partial_index_keyparts(); i++) {
                if (keydef->read_memcmp_key_part(&reader, i) > 0) {
                  rc2 = HA_ERR_ROCKSDB_CORRUPT_DATA;
                  break;
                }
              }
              if (rc2) break;

              size_t cur_prefix_len =
                  reader.get_current_ptr() - merge_key.data();

              const char *key = (const char *)memdup_root(
                  &mem_root, merge_key.data(), cur_prefix_len);

              // Set the current prefix.
              cur_prefix = rocksdb::Slice(key, cur_prefix_len);
            }

            if (!materialized) {
              // Bulk load the keys if threshold is exceeded.
              if (keys.size() >= keydef->partial_index_threshold()) {
                for (const auto &k : keys) {
                  if ((rc2 = sst_info->put(k.first, k.second) != 0)) {
                    break;
                  }
                }

                if (rc2 != 0) {
                  break;
                }

                materialized = true;
                keys.clear();
              } else {
                // Threshold not exceeded, just buffer the row in the keys
                // vector.
                const char *key = (const char *)memdup_root(
                    &mem_root, merge_key.data(), merge_key.size());
                const char *val = (const char *)memdup_root(
                    &mem_root, merge_val.data(), merge_val.size());
                keys.emplace_back(rocksdb::Slice(key, merge_key.size()),
                                  rocksdb::Slice(val, merge_val.size()));
              }
            }

            if (materialized) {
              rc2 = sst_info->put(merge_key, merge_val);
              if (rc != 0) {
                rc = rc2;
                break;
              }
            }

            if (rc2) break;
          }
        } else {
          struct unique_sk_buf_info sk_info;

          if (is_unique_index) {
            uint max_packed_sk_len = keydef->max_storage_fmt_length();
            sk_info.dup_sk_buf = reinterpret_cast<uchar *>(
                my_malloc(PSI_NOT_INSTRUMENTED, max_packed_sk_len, MYF(0)));
            sk_info.dup_sk_buf_old = reinterpret_cast<uchar *>(
                my_malloc(PSI_NOT_INSTRUMENTED, max_packed_sk_len, MYF(0)));
          }

          while ((rc2 = rdb_merge.next(&merge_key, &merge_val)) == 0) {
            /* Perform uniqueness check if needed */
            if (is_unique_index &&
                check_duplicate_sk(table_arg, *keydef, &merge_key, &sk_info)) {
              /*
                Duplicate entry found when trying to create unique secondary
                key. We need to unpack the record into new_table_arg->record[0]
                as it is used inside print_keydup_error so that the error
                message shows the duplicate record.
                */
              if (keydef->unpack_record(table_arg, table_arg->record[0],
                                        &merge_key, &merge_val, false)) {
                /* Should never reach here */
                DBUG_ASSERT(0);
              }

              rc = ER_DUP_ENTRY;
              if (is_critical_error) {
                *is_critical_error = false;
              }
              break;
            }

            /*
              Insert key and slice to SST via SSTFileWriter API.
            */
            if ((rc2 = sst_info->put(merge_key, merge_val)) != 0) {
              rc = rc2;

              // Don't return yet - make sure we finish the sst_info
              break;
            }
          }
        }

        // -1 => no more items
        if (rc2 > 0 && rc == 0) {
          rc = rc2;
        }

        Rdb_sst_info::Rdb_sst_commit_info commit_info;
        rc2 = sst_info->finish(&commit_info, print_client_error);
        if (rc2 != 0 && rc == 0) {
          // Only set the error from sst_info->finish if finish failed and we
          // didn't fail before. In other words, we don't have finish's
          // success mask earlier failures
          rc = rc2;
        }

        if (rc) {
          return rc;
        }

        if (commit_info.has_work()) {
          sst_commit_list.emplace_back(std::move(commit_info));
          DBUG_ASSERT(!commit_info.has_work());
        }
      }

      if (THDVAR(m_thd, trace_sst_api)) {
        // NO_LINT_DEBUG
        sql_print_information(
            "SST Tracing : Flushing index_merge sort buffer completed");
      }
    }

    // Early return in case we lost the race completely and end up with no
    // work at all
    if (sst_commit_list.size() == 0) {
      return rc;
    }

    // INGEST phase: Group all Rdb_sst_commit_info by cf (as they might
    // have the same cf across different indexes) and call out to RocksDB
    // to ingest all SST files in one atomic operation
    rocksdb::IngestExternalFileOptions options;
    options.move_files = true;
    options.snapshot_consistency = false;
    options.allow_global_seqno = false;
    options.allow_blocking_flush = false;

    std::map<rocksdb::ColumnFamilyHandle *, rocksdb::IngestExternalFileArg>
        arg_map;

    // Group by column_family
    for (auto &commit_info : sst_commit_list) {
      if (arg_map.find(commit_info.get_cf()) == arg_map.end()) {
        rocksdb::IngestExternalFileArg arg;
        arg.column_family = commit_info.get_cf(),
        arg.external_files = commit_info.get_committed_files(),
        arg.options = options;

        arg_map.emplace(commit_info.get_cf(), arg);
      } else {
        auto &files = arg_map[commit_info.get_cf()].external_files;
        files.insert(files.end(), commit_info.get_committed_files().begin(),
                     commit_info.get_committed_files().end());
      }
    }

    std::vector<rocksdb::IngestExternalFileArg> args;
    size_t file_count = 0;
    for (auto &cf_files_pair : arg_map) {
      args.push_back(cf_files_pair.second);
      file_count += cf_files_pair.second.external_files.size();
    }

    if (THDVAR(m_thd, trace_sst_api)) {
      // NO_LINT_DEBUG
      sql_print_information(
          "SST Tracing: Calling IngestExternalFile with '%zu' files",
          file_count);
    }
    const rocksdb::Status s = rdb->IngestExternalFiles(args);
    if (THDVAR(m_thd, trace_sst_api)) {
      // NO_LINT_DEBUG
      sql_print_information(
          "SST Tracing: IngestExternalFile '%zu' files returned %s", file_count,
          s.ok() ? "ok" : "not ok");
    }

    if (!s.ok()) {
      if (print_client_error) {
        Rdb_sst_info::report_error_msg(s, nullptr);
      }
      return HA_ERR_ROCKSDB_BULK_LOAD;
    }

    // COMMIT phase: mark everything as completed. This avoids SST file
    // deletion kicking in. Otherwise SST files would get deleted if this
    // entire operation is aborted
    for (auto &commit_info : sst_commit_list) {
      commit_info.commit();
    }

    if (THDVAR(m_thd, trace_sst_api)) {
      // NO_LINT_DEBUG
      sql_print_information(
          "SST Tracing : Bulk loading operation completed for table '%s'",
          m_curr_bulk_load_tablename.c_str());
    }

    return rc;
  }

  /**
    Check whether secondary key value is duplicate or not

    @param[in] table_arg         the table currently working on
    @param[in  key_def           the key_def is being checked
    @param[in] key               secondary key storage data
    @param[out] sk_info          hold secondary key memcmp datas(new/old)
    @return
      HA_EXIT_SUCCESS  OK
      other            HA_ERR error code (can be SE-specific)
  */

  int check_duplicate_sk(const TABLE *table_arg, const Rdb_key_def &key_def,
                         const rocksdb::Slice *key,
                         struct unique_sk_buf_info *sk_info) {
    uint n_null_fields = 0;
    const rocksdb::Comparator *index_comp = key_def.get_cf()->GetComparator();

    /* Get proper SK buffer. */
    uchar *sk_buf = sk_info->swap_and_get_sk_buf();

    /* Get memcmp form of sk without extended pk tail */
    uint sk_memcmp_size =
        key_def.get_memcmp_sk_parts(table_arg, *key, sk_buf, &n_null_fields);

    sk_info->sk_memcmp_key =
        rocksdb::Slice(reinterpret_cast<char *>(sk_buf), sk_memcmp_size);

    if (sk_info->sk_memcmp_key_old.size() > 0 && n_null_fields == 0 &&
        index_comp->Compare(sk_info->sk_memcmp_key,
                            sk_info->sk_memcmp_key_old) == 0) {
      return 1;
    }

    sk_info->sk_memcmp_key_old = sk_info->sk_memcmp_key;
    return 0;
  }

  int start_bulk_load(ha_rocksdb *const bulk_load,
                      std::shared_ptr<Rdb_sst_info> sst_info) {
    /*
     If we already have an open bulk load of a table and the name doesn't
     match the current one, close out the currently running one.  This allows
     multiple bulk loads to occur on a partitioned table, but then closes
     them all out when we switch to another table.
    */
    DBUG_ASSERT(bulk_load != nullptr);

    if (!m_curr_bulk_load.empty() &&
        bulk_load->get_table_basename() != m_curr_bulk_load_tablename) {
      const auto res = finish_bulk_load();
      if (res != HA_EXIT_SUCCESS) {
        return res;
      }
    }

    if (THDVAR(m_thd, trace_sst_api)) {
      // NO_LINT_DEBUG
      sql_print_information(
          "SST Tracing : Starting bulk loading operation for table '%s'",
          bulk_load->get_table_basename().c_str());
    }

    /*
     This used to track ha_rocksdb handler objects, but those can be
     freed by the table cache while this was referencing them. Instead
     of tracking ha_rocksdb handler objects, this now tracks the
     Rdb_sst_info allocated, and both the ha_rocksdb handler and the
     Rdb_transaction both have shared pointers to them.

     On transaction complete, it will commit each Rdb_sst_info structure found.
     If the ha_rocksdb object is freed, etc., it will also commit
     the Rdb_sst_info. The Rdb_sst_info commit path needs to be idempotent.
    */
    m_curr_bulk_load.push_back(sst_info);
    m_curr_bulk_load_tablename = bulk_load->get_table_basename();
    return HA_EXIT_SUCCESS;
  }

  int num_ongoing_bulk_load() const { return m_curr_bulk_load.size(); }

  const char *get_rocksdb_tmpdir() const {
    const char *tmp_dir = THDVAR(get_thd(), tmpdir);

    /*
      We want to treat an empty string as nullptr, in these cases DDL operations
      will use the default --tmpdir passed to mysql instead.
    */
    if (tmp_dir != nullptr && *tmp_dir == '\0') {
      tmp_dir = nullptr;
    }
    return (tmp_dir);
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
  bool flush_batch() {
    if (get_write_count() == 0) return false;

    /* Commit the current transaction */
    if (commit_no_binlog()) return true;

    /* Start another one */
    start_tx();
    return false;
  }

  void set_auto_incr(const GL_INDEX_ID &gl_index_id, ulonglong curr_id) {
    m_auto_incr_map[gl_index_id] =
        std::max(m_auto_incr_map[gl_index_id], curr_id);
  }

#ifndef DBUG_OFF
  ulonglong get_auto_incr(const GL_INDEX_ID &gl_index_id) {
    if (m_auto_incr_map.count(gl_index_id) > 0) {
      return m_auto_incr_map[gl_index_id];
    }
    return 0;
  }
#endif

  virtual rocksdb::Status put(rocksdb::ColumnFamilyHandle *const column_family,
                              const rocksdb::Slice &key,
                              const rocksdb::Slice &value,
                              const bool assume_tracked) = 0;
  virtual rocksdb::Status delete_key(
      rocksdb::ColumnFamilyHandle *const column_family,
      const rocksdb::Slice &key, const bool assume_tracked) = 0;
  virtual rocksdb::Status single_delete(
      rocksdb::ColumnFamilyHandle *const column_family,
      const rocksdb::Slice &key, const bool assume_tracked) = 0;

  virtual bool has_modifications() const = 0;

  virtual rocksdb::WriteBatchBase *get_indexed_write_batch() = 0;
  /*
    Return a WriteBatch that one can write to. The writes will skip any
    transaction locking. The writes will NOT be visible to the transaction.
  */
  rocksdb::WriteBatchBase *get_blind_write_batch() {
    return get_indexed_write_batch()->GetWriteBatch();
  }

  virtual rocksdb::Status get(rocksdb::ColumnFamilyHandle *const column_family,
                              const rocksdb::Slice &key,
                              rocksdb::PinnableSlice *const value) const = 0;

  virtual rocksdb::Status get_for_update(const Rdb_key_def &key_descr,
                                         const rocksdb::Slice &key,
                                         rocksdb::PinnableSlice *const value,
                                         bool exclusive, const bool do_validate,
                                         bool no_wait) = 0;

  virtual rocksdb::Iterator *get_iterator(
      const rocksdb::ReadOptions &options,
      rocksdb::ColumnFamilyHandle *column_family,
      const std::shared_ptr<Rdb_key_def>&,
      bool use_locking_iterator=false) = 0;

  virtual void multi_get(rocksdb::ColumnFamilyHandle *const column_family,
                         const size_t num_keys, const rocksdb::Slice *keys,
                         rocksdb::PinnableSlice *values,
                         rocksdb::Status *statuses,
                         const bool sorted_input) const = 0;

  rocksdb::Iterator *get_iterator(
      rocksdb::ColumnFamilyHandle *const column_family, 
      const std::shared_ptr<Rdb_key_def>& kd,
      bool skip_bloom_filter,
      const rocksdb::Slice &eq_cond_lower_bound,
      const rocksdb::Slice &eq_cond_upper_bound, bool read_current = false,
      bool create_snapshot = true,
      bool use_locking_iterator=false) {
    // Make sure we are not doing both read_current (which implies we don't
    // want a snapshot) and create_snapshot which makes sure we create
    // a snapshot
    DBUG_ASSERT(column_family != nullptr);
    DBUG_ASSERT(!read_current || !create_snapshot);

    if (create_snapshot) acquire_snapshot(true);

    rocksdb::ReadOptions options = m_read_opts;
    const bool fill_cache = !THDVAR(get_thd(), skip_fill_cache);

    if (skip_bloom_filter) {
      const bool enable_iterate_bounds =
          THDVAR(get_thd(), enable_iterate_bounds);
      options.total_order_seek = true;
      options.iterate_lower_bound =
          enable_iterate_bounds ? &eq_cond_lower_bound : nullptr;
      options.iterate_upper_bound =
          enable_iterate_bounds ? &eq_cond_upper_bound : nullptr;
    } else {
      // With this option, Iterator::Valid() returns false if key
      // is outside of the prefix bloom filter range set at Seek().
      // Must not be set to true if not using bloom filter.
      options.prefix_same_as_start = true;
    }
    options.fill_cache = fill_cache;
    if (read_current) {
      options.snapshot = nullptr;
    }
    return get_iterator(options, column_family, kd, use_locking_iterator);
  }

  virtual bool is_tx_started() const = 0;
  virtual void start_tx() = 0;
  virtual void start_stmt(bool is_dml_statement) = 0;
  virtual void start_autocommit_stmt(bool /*is_dml_statement*/){}
  virtual void set_name() = 0;

 protected:
  // Non-virtual functions with actions to be done on transaction start and
  // commit.
  void on_commit() {
    time_t tm;
    tm = time(nullptr);
    for (auto &it : modified_tables) {
      it->m_update_time = tm;
    }
    modified_tables.clear();
  }
  void on_rollback() { modified_tables.clear(); }

 public:
  void log_table_write_op(Rdb_tbl_def *tbl) { modified_tables.insert(tbl); }

  void set_initial_savepoint() {
    /*
      Set the initial savepoint. If the first statement in the transaction
      fails, we need something to roll back to, without rolling back the
      entire transaction.
    */
    do_set_savepoint();
    m_writes_at_last_savepoint = m_write_count;
  }

  /*
    Called when a "top-level" statement inside a transaction completes
    successfully and its changes become part of the transaction's changes.
  */
  int make_stmt_savepoint_permanent() {
    // Take another RocksDB savepoint only if we had changes since the last
    // one. This is very important for long transactions doing lots of
    // SELECTs.
    if (m_writes_at_last_savepoint != m_write_count) {
      rocksdb::Status status = rocksdb::Status::NotFound();
      while ((status = do_pop_savepoint()) == rocksdb::Status::OK()) {
      }

      if (status != rocksdb::Status::NotFound()) {
        return HA_EXIT_FAILURE;
      }

      do_set_savepoint();
      m_writes_at_last_savepoint = m_write_count;
    }

    return HA_EXIT_SUCCESS;
  }

  /*
    Rollback to the savepoint we've set before the last statement
  */
  void rollback_to_stmt_savepoint() {
    if (m_writes_at_last_savepoint != m_write_count) {
      do_rollback_to_savepoint();
      /*
        RollbackToSavePoint "removes the most recent SetSavePoint()", so
        we need to set it again so that next statement can roll back to this
        stage.
        It's ok to do it here at statement end (instead of doing it at next
        statement start) because setting a savepoint is cheap.
      */
      do_set_savepoint();
      m_write_count = m_writes_at_last_savepoint;
    }
  }

  virtual void rollback_stmt() = 0;

  void set_tx_failed(bool failed_arg) { m_is_tx_failed = failed_arg; }

  bool can_prepare() const {
    if (m_rollback_only) {
      my_error(ER_ROLLBACK_ONLY, MYF(0));
      return false;
    }
    return true;
  }

  int rollback_to_savepoint(void *const savepoint MY_ATTRIBUTE((__unused__))) {
    if (has_modifications()) {
      my_error(ER_ROLLBACK_TO_SAVEPOINT, MYF(0));
      m_rollback_only = true;
      return HA_EXIT_FAILURE;
    }
    return HA_EXIT_SUCCESS;
  }

  /*
    This is used by transactions started with "START TRANSACTION WITH "
    "CONSISTENT [ROCKSDB] SNAPSHOT". When tx_read_only is turned on,
    snapshot has to be created via DB::GetSnapshot(), not via Transaction
    API.
  */
  bool is_tx_read_only() const { return m_tx_read_only; }

  bool is_two_phase() const { return m_is_two_phase; }

  void set_tx_read_only(bool val) { m_tx_read_only = val; }

  /*
    Add or remove from the global list of active transactions
    needed by information_schema queries.
  */
  void add_to_global_trx_list() {
    RDB_MUTEX_LOCK_CHECK(s_tx_list_mutex);
    s_tx_list.insert(this);
    RDB_MUTEX_UNLOCK_CHECK(s_tx_list_mutex);
  }

  void remove_from_global_trx_list(void) {
    DBUG_EXECUTE_IF("rocksdb_trx_list_crash", {
      THD *thd = new THD();
      thd->thread_stack = reinterpret_cast<char *>(&(thd));
      thd->store_globals();

      const char act[] =
          "now signal destructor_started wait_for trx_list_query";
      DBUG_ASSERT(!debug_sync_set_action(thd, STRING_WITH_LEN(act)));

      thd->restore_globals();
      delete thd;
    });
    RDB_MUTEX_LOCK_CHECK(s_tx_list_mutex);
    s_tx_list.erase(this);
    RDB_MUTEX_UNLOCK_CHECK(s_tx_list_mutex);
  }

  explicit Rdb_transaction(THD *const thd)
      : m_thd(thd), m_tbl_io_perf(nullptr) {}

  virtual ~Rdb_transaction() {
#ifndef DEBUG_OFF
    RDB_MUTEX_LOCK_CHECK(s_tx_list_mutex);
    DBUG_ASSERT(s_tx_list.find(this) == s_tx_list.end());
    RDB_MUTEX_UNLOCK_CHECK(s_tx_list_mutex);
#endif
  }
};

#ifndef DBUG_OFF
// simulate that RocksDB has reported corrupted data
static void dbug_change_status_to_corrupted(rocksdb::Status *status) {
  *status = rocksdb::Status::Corruption();
}
static void dbug_change_status_to_io_error(rocksdb::Status *status) {
  *status = rocksdb::Status::IOError();
}
static void dbug_change_status_to_incomplete(rocksdb::Status *status) {
  *status = rocksdb::Status::Incomplete();
}
#endif

/*
  This is a rocksdb transaction. Its members represent the current transaction,
  which consists of:
  - the snapshot
  - the changes we've made but are not seeing yet.

  The changes are made to individual tables, which store them here and then
  this object commits them on commit.
*/
class Rdb_transaction_impl : public Rdb_transaction {
  rocksdb::Transaction *m_rocksdb_tx = nullptr;
  rocksdb::Transaction *m_rocksdb_reuse_tx = nullptr;

 public:
  void set_lock_timeout(int timeout_sec_arg) override {
    if (m_rocksdb_tx) {
      m_rocksdb_tx->SetLockTimeout(rdb_convert_sec_to_ms(timeout_sec_arg));
    }
  }

  void set_sync(bool sync) override {
    m_rocksdb_tx->GetWriteOptions()->sync = sync;
  }

  void release_lock(const Rdb_key_def &key_descr, const std::string &rowkey,
                    bool force) override {
    if (!THDVAR(m_thd, lock_scanned_rows) || force) {
      m_rocksdb_tx->UndoGetForUpdate(key_descr.get_cf(),
                                     rocksdb::Slice(rowkey));
      // row_lock_count track row(pk)
      DBUG_ASSERT(!key_descr.is_primary_key() ||
                  (key_descr.is_primary_key() && m_row_lock_count > 0));
      // m_row_lock_count tracks per row data instead of per key data
      if (key_descr.is_primary_key() && m_row_lock_count > 0) {
        m_row_lock_count--;
      }
    }
  }

  virtual bool is_writebatch_trx() const override { return false; }

  // Lock the range between two specified endpoints
  rocksdb::Status lock_range(rocksdb::ColumnFamilyHandle *const cf,
                             const rocksdb::Endpoint &start_endp,
                             const rocksdb::Endpoint &end_endp) override {
    ++m_row_lock_count;
    return m_rocksdb_tx->GetRangeLock(cf, start_endp, end_endp);
  }
 private:
  void release_tx(void) {
    // We are done with the current active transaction object.  Preserve it
    // for later reuse.
    DBUG_ASSERT(m_rocksdb_reuse_tx == nullptr);
    m_rocksdb_reuse_tx = m_rocksdb_tx;
    m_rocksdb_tx = nullptr;
  }

  bool prepare() override {
    rocksdb::Status s;

    s = merge_auto_incr_map(m_rocksdb_tx->GetWriteBatch()->GetWriteBatch());
#ifndef DBUG_OFF
    DBUG_EXECUTE_IF("myrocks_prepare_io_error",
                    dbug_change_status_to_io_error(&s););
    DBUG_EXECUTE_IF("myrocks_prepare_incomplete",
                    dbug_change_status_to_incomplete(&s););
#endif
    if (!s.ok()) {
      std::string msg =
          "RocksDB error on COMMIT (Prepare/merge): " + s.ToString();
      my_error(ER_INTERNAL_ERROR, MYF(0), msg.c_str());
      return false;
    }

    s = m_rocksdb_tx->Prepare();
    if (!s.ok()) {
      std::string msg = "RocksDB error on COMMIT (Prepare): " + s.ToString();
      my_error(ER_INTERNAL_ERROR, MYF(0), msg.c_str());
      return false;
    }
    return true;
  }

  bool commit_no_binlog() override {
    bool res = false;
    rocksdb::Status s;

    s = merge_auto_incr_map(m_rocksdb_tx->GetWriteBatch()->GetWriteBatch());
#ifndef DBUG_OFF
    DBUG_EXECUTE_IF("myrocks_commit_merge_io_error",
                    dbug_change_status_to_io_error(&s););
    DBUG_EXECUTE_IF("myrocks_commit_merge_incomplete",
                    dbug_change_status_to_incomplete(&s););
#endif
    if (!s.ok()) {
      rdb_handle_io_error(s, RDB_IO_ERROR_TX_COMMIT);
      res = true;
      goto error;
    }

    release_snapshot();
    s = m_rocksdb_tx->Commit();
#ifndef DBUG_OFF
    DBUG_EXECUTE_IF("myrocks_commit_io_error",
                    dbug_change_status_to_io_error(&s););
    DBUG_EXECUTE_IF("myrocks_commit_incomplete",
                    dbug_change_status_to_incomplete(&s););
#endif
    if (!s.ok()) {
      rdb_handle_io_error(s, RDB_IO_ERROR_TX_COMMIT);
      res = true;
      goto error;
    }

    on_commit();
  error:
    on_rollback();
    /* Save the transaction object to be reused */
    release_tx();

    m_write_count = 0;
    m_insert_count = 0;
    m_update_count = 0;
    m_delete_count = 0;
    m_row_lock_count = 0;
    set_tx_read_only(false);
    m_rollback_only = false;
    return res;
  }

 public:
  void rollback() override {
    on_rollback();
    m_write_count = 0;
    m_insert_count = 0;
    m_update_count = 0;
    m_delete_count = 0;
    m_row_lock_count = 0;
    m_auto_incr_map.clear();
    m_ddl_transaction = false;
    if (m_rocksdb_tx) {
      release_snapshot();
      /* This will also release all of the locks: */
      m_rocksdb_tx->Rollback();

      /* Save the transaction object to be reused */
      release_tx();

      set_tx_read_only(false);
      m_rollback_only = false;
    }
  }

  void acquire_snapshot(bool acquire_now) override {
    if (m_read_opts.snapshot == nullptr && !in_snapshot_ignore_mode()) {
      const auto thd_ss = std::static_pointer_cast<Rdb_explicit_snapshot>(
          m_thd->get_explicit_snapshot());
      if (thd_ss) {
        m_explicit_snapshot = thd_ss;
      }
      if (m_explicit_snapshot) {
        auto snapshot = m_explicit_snapshot->get_snapshot()->snapshot();
        snapshot_created(snapshot);
      } else if (is_tx_read_only()) {
        snapshot_created(rdb->GetSnapshot());
      } else if (acquire_now) {
        m_rocksdb_tx->SetSnapshot();
        snapshot_created(m_rocksdb_tx->GetSnapshot());
      } else if (!m_is_delayed_snapshot) {
        m_rocksdb_tx->SetSnapshotOnNextOperation(m_notifier);
        m_is_delayed_snapshot = true;
      }
    }
  }

  void release_snapshot() override {
    bool need_clear = m_is_delayed_snapshot;

    if (m_read_opts.snapshot != nullptr) {
      m_snapshot_timestamp = 0;
      if (m_explicit_snapshot) {
        m_explicit_snapshot.reset();
        need_clear = false;
      } else if (is_tx_read_only()) {
        rdb->ReleaseSnapshot(m_read_opts.snapshot);
        need_clear = false;
      } else {
        need_clear = true;
      }
      m_read_opts.snapshot = nullptr;
    }

    if (need_clear && m_rocksdb_tx != nullptr) m_rocksdb_tx->ClearSnapshot();
    m_is_delayed_snapshot = false;
  }

  bool has_snapshot() { return m_read_opts.snapshot != nullptr; }

  rocksdb::Status put(rocksdb::ColumnFamilyHandle *const column_family,
                      const rocksdb::Slice &key, const rocksdb::Slice &value,
                      const bool assume_tracked) override {
    ++m_write_count;
    return m_rocksdb_tx->Put(column_family, key, value, assume_tracked);
  }

  rocksdb::Status delete_key(rocksdb::ColumnFamilyHandle *const column_family,
                             const rocksdb::Slice &key,
                             const bool assume_tracked) override {
    ++m_write_count;
    return m_rocksdb_tx->Delete(column_family, key, assume_tracked);
  }

  rocksdb::Status single_delete(
      rocksdb::ColumnFamilyHandle *const column_family,
      const rocksdb::Slice &key, const bool assume_tracked) override {
    ++m_write_count;
    return m_rocksdb_tx->SingleDelete(column_family, key, assume_tracked);
  }

  bool has_modifications() const override {
    return m_rocksdb_tx->GetWriteBatch() &&
           m_rocksdb_tx->GetWriteBatch()->GetWriteBatch() &&
           m_rocksdb_tx->GetWriteBatch()->GetWriteBatch()->Count() > 0;
  }

  rocksdb::WriteBatchBase *get_write_batch() override {
    if (is_two_phase()) {
      return m_rocksdb_tx->GetCommitTimeWriteBatch();
    }
    return m_rocksdb_tx->GetWriteBatch()->GetWriteBatch();
  }

  /*
    Return a WriteBatch that one can write to. The writes will skip any
    transaction locking. The writes WILL be visible to the transaction.
  */
  rocksdb::WriteBatchBase *get_indexed_write_batch() override {
    ++m_write_count;
    return m_rocksdb_tx->GetWriteBatch();
  }

  rocksdb::Status get(rocksdb::ColumnFamilyHandle *const column_family,
                      const rocksdb::Slice &key,
                      rocksdb::PinnableSlice *const value) const override {
    // clean PinnableSlice right begfore Get() for multiple gets per statement
    // the resources after the last Get in a statement are cleared in
    // handler::reset call
    value->Reset();
    global_stats.queries[QUERIES_POINT].inc();
    return m_rocksdb_tx->Get(m_read_opts, column_family, key, value);
  }

  void multi_get(rocksdb::ColumnFamilyHandle *const column_family,
                 const size_t num_keys, const rocksdb::Slice *keys,
                 rocksdb::PinnableSlice *values, rocksdb::Status *statuses,
                 const bool sorted_input) const override {
    m_rocksdb_tx->MultiGet(m_read_opts, column_family, num_keys, keys, values,
                           statuses, sorted_input);
  }

  rocksdb::Status get_for_update(const Rdb_key_def &key_descr,
                                 const rocksdb::Slice &key,
                                 rocksdb::PinnableSlice *const value,
                                 bool exclusive, const bool do_validate,
                                 bool no_wait) override {
    rocksdb::ColumnFamilyHandle *const column_family = key_descr.get_cf();
    /* check row lock limit in a trx */
    if (get_row_lock_count() >= get_max_row_lock_count()) {
      return rocksdb::Status::Aborted(rocksdb::Status::kLockLimit);
    }

    if (value != nullptr) {
      value->Reset();
      DBUG_EXECUTE_IF("rocksdb_check_uniqueness",
                      DEBUG_SYNC(m_thd, "rocksdb_after_unpin"););
    }

    if (no_wait) {
      set_lock_timeout(0);
    }
    auto restore_wait = create_scope_guard([&]() {
      if (no_wait) {
        set_lock_timeout(m_timeout_sec);
      }
    });

    rocksdb::Status s;
    // If snapshot is null, pass it to GetForUpdate and snapshot is
    // initialized there. Snapshot validation is skipped in that case.
    if (m_read_opts.snapshot == nullptr || do_validate) {
      s = m_rocksdb_tx->GetForUpdate(
          m_read_opts, column_family, key, value, exclusive,
          m_read_opts.snapshot ? do_validate : false);
    } else {
      // If snapshot is set, and if skipping validation,
      // call GetForUpdate without validation and set back old snapshot
      auto saved_snapshot = m_read_opts.snapshot;
      m_read_opts.snapshot = nullptr;
      s = m_rocksdb_tx->GetForUpdate(m_read_opts, column_family, key, value,
                                     exclusive, false);
      m_read_opts.snapshot = saved_snapshot;
    }

    // row_lock_count is to track per row instead of per key
    if (key_descr.is_primary_key()) incr_row_lock_count();
    return s;
  }

  rocksdb::Iterator *get_iterator(
      const rocksdb::ReadOptions &options,
      rocksdb::ColumnFamilyHandle *const column_family,
      const std::shared_ptr<Rdb_key_def>& kd,
      bool use_locking_iterator) override {
    global_stats.queries[QUERIES_RANGE].inc();
    if (use_locking_iterator) {
      locking_iter_created();
      return GetLockingIterator(m_rocksdb_tx, options, column_family,
                                kd, &m_row_lock_count);
    }
    else
      return m_rocksdb_tx->GetIterator(options, column_family);
  }

  const rocksdb::Transaction *get_rdb_trx() const { return m_rocksdb_tx; }

  bool is_tx_started() const override { return (m_rocksdb_tx != nullptr); }

  void start_tx() override {
    rocksdb::TransactionOptions tx_opts;
    rocksdb::WriteOptions write_opts;
    tx_opts.set_snapshot = false;
    tx_opts.lock_timeout = rdb_convert_sec_to_ms(m_timeout_sec);
    tx_opts.deadlock_detect = THDVAR(m_thd, deadlock_detect);
    tx_opts.deadlock_detect_depth = THDVAR(m_thd, deadlock_detect_depth);
    // If this variable is set, this will write commit time write batch
    // information on recovery or memtable flush.
    tx_opts.use_only_the_last_commit_time_batch_for_recovery =
        THDVAR(m_thd, commit_time_batch_for_recovery);
    tx_opts.max_write_batch_size = THDVAR(m_thd, write_batch_max_bytes);
    tx_opts.write_batch_flush_threshold =
        THDVAR(m_thd, write_batch_flush_threshold);

    write_opts.sync = (rocksdb_flush_log_at_trx_commit == FLUSH_LOG_SYNC) &&
                      rdb_sync_wal_supported();
    write_opts.disableWAL = THDVAR(m_thd, write_disable_wal);
    write_opts.ignore_missing_column_families =
        THDVAR(m_thd, write_ignore_missing_column_families);
    m_is_two_phase = rocksdb_enable_2pc;

    /*
      If m_rocksdb_reuse_tx is null this will create a new transaction object.
      Otherwise it will reuse the existing one.
    */
    m_rocksdb_tx =
        rdb->BeginTransaction(write_opts, tx_opts, m_rocksdb_reuse_tx);
    m_rocksdb_reuse_tx = nullptr;

    m_read_opts = rocksdb::ReadOptions();

    set_initial_savepoint();

    m_ddl_transaction = false;
    m_is_delayed_snapshot = false;
  }

  void set_name() override {
    XID xid;
    thd_get_xid(m_thd, reinterpret_cast<MYSQL_XID *>(&xid));
    auto name = m_rocksdb_tx->GetName();
    if (!name.empty()) {
      DBUG_ASSERT(name == rdb_xid_to_string(xid));
      return;
    }
    rocksdb::Status s = m_rocksdb_tx->SetName(rdb_xid_to_string(xid));
    DBUG_ASSERT(s.ok());
    if (!s.ok()) {
      rdb_handle_io_error(s, RDB_IO_ERROR_TX_COMMIT);
    }
  }

  /* Implementations of do_*savepoint based on rocksdB::Transaction savepoints
   */
  void do_set_savepoint() override { m_rocksdb_tx->SetSavePoint(); }
  rocksdb::Status do_pop_savepoint() override {
    return m_rocksdb_tx->PopSavePoint();
  }

  void do_rollback_to_savepoint() override {
    m_rocksdb_tx->RollbackToSavePoint();
  }

  /*
    Start a statement inside a multi-statement transaction.

    @note: If a statement uses N tables, this function will be called N times,
    for each TABLE object that is used.

    For hooking to start of statement that is its own transaction, see
    ha_rocksdb::external_lock().
  */
  void start_stmt(bool is_dml_statement) override {

    if (rocksdb_use_range_locking && is_dml_statement) {
      /*
        In Range Locking mode, RocksDB does not do "key tracking".
        Use InnoDB-like concurrency mode: make the DML statements always read
        the latest data (instead of using transaction's snapshot).
        This "downgrades" the transaction isolation to READ-COMMITTED on the
        primary, but in return the actions can be replayed on the replica.
      */
      start_ignore_snapshot();
    }

    // Set the snapshot to delayed acquisition (SetSnapshotOnNextOperation)
    acquire_snapshot(false);
  }

  void start_autocommit_stmt(bool is_dml_statement) override {
    if (rocksdb_use_range_locking && is_dml_statement) {
      start_ignore_snapshot();
    }
  }

  /*
    This must be called when last statement is rolled back, but the transaction
    continues
  */
  void rollback_stmt() override {
    /* TODO: here we must release the locks taken since the start_stmt() call */
    if (m_rocksdb_tx) {
      const rocksdb::Snapshot *const org_snapshot = m_rocksdb_tx->GetSnapshot();
      rollback_to_stmt_savepoint();

      const rocksdb::Snapshot *const cur_snapshot = m_rocksdb_tx->GetSnapshot();
      if (org_snapshot != cur_snapshot) {
        if (org_snapshot != nullptr) m_snapshot_timestamp = 0;

        m_read_opts.snapshot = cur_snapshot;
        if (cur_snapshot != nullptr) {
          rdb->GetEnv()->GetCurrentTime(&m_snapshot_timestamp);
        } else {
          m_rocksdb_tx->SetSnapshotOnNextOperation(m_notifier);
          m_is_delayed_snapshot = true;
        }
      }
    }
  }

  explicit Rdb_transaction_impl(THD *const thd)
      : Rdb_transaction(thd), m_rocksdb_tx(nullptr) {
    // Create a notifier that can be called when a snapshot gets generated.
    m_notifier = std::make_shared<Rdb_snapshot_notifier>(this);
  }

  virtual ~Rdb_transaction_impl() override {
    // Remove from the global list before all other processing is started.
    // Otherwise, information_schema.rocksdb_trx can crash on this object.
    Rdb_transaction::remove_from_global_trx_list();

    rollback();

    // Theoretically the notifier could outlive the Rdb_transaction_impl
    // (because of the shared_ptr), so let it know it can't reference
    // the transaction anymore.
    m_notifier->detach();

    // Free any transaction memory that is still hanging around.
    delete m_rocksdb_reuse_tx;
    DBUG_ASSERT(m_rocksdb_tx == nullptr);
  }
};

/* This is a rocksdb write batch. This class doesn't hold or wait on any
   transaction locks (skips rocksdb transaction API) thus giving better
   performance.

   Currently this is only used for replication threads which are guaranteed
   to be non-conflicting. Any further usage of this class should completely
   be thought thoroughly.
*/
class Rdb_writebatch_impl : public Rdb_transaction {
  rocksdb::WriteBatchWithIndex *m_batch;
  rocksdb::WriteOptions write_opts;
  // Called after commit/rollback.
  void reset() {
    m_batch->Clear();
    m_read_opts = rocksdb::ReadOptions();
    m_ddl_transaction = false;
  }

 private:
  bool prepare() override { return true; }

  bool commit_no_binlog() override {
    bool res = false;
    rocksdb::Status s;
    rocksdb::TransactionDBWriteOptimizations optimize;
    optimize.skip_concurrency_control = true;

    s = merge_auto_incr_map(m_batch->GetWriteBatch());
    if (!s.ok()) {
      rdb_handle_io_error(s, RDB_IO_ERROR_TX_COMMIT);
      res = true;
      goto error;
    }

    release_snapshot();

    s = rdb->Write(write_opts, optimize, m_batch->GetWriteBatch());
    if (!s.ok()) {
      rdb_handle_io_error(s, RDB_IO_ERROR_TX_COMMIT);
      res = true;
      goto error;
    }
    on_commit();
  error:
    on_rollback();
    reset();

    m_write_count = 0;
    m_insert_count = 0;
    m_update_count = 0;
    m_delete_count = 0;
    set_tx_read_only(false);
    m_rollback_only = false;
    return res;
  }

  /* Implementations of do_*savepoint based on rocksdB::WriteBatch savepoints */
  void do_set_savepoint() override { m_batch->SetSavePoint(); }
  rocksdb::Status do_pop_savepoint() override {
    return m_batch->PopSavePoint();
  }

  void do_rollback_to_savepoint() override { m_batch->RollbackToSavePoint(); }

 public:
  bool is_writebatch_trx() const override { return true; }

  void set_lock_timeout(int timeout_sec_arg MY_ATTRIBUTE((unused))) override {
    // Nothing to do here.
  }

  void set_sync(bool sync) override { write_opts.sync = sync; }

  void release_lock(const Rdb_key_def &key_descr MY_ATTRIBUTE((unused)),
                    const std::string &rowkey MY_ATTRIBUTE((unused)),
                    bool force MY_ATTRIBUTE((unused))) override {
    // Nothing to do here since we don't hold any row locks.
  }

  rocksdb::Status lock_range(rocksdb::ColumnFamilyHandle *const,
                             const rocksdb::Endpoint&,
                             const rocksdb::Endpoint&) override {
    return rocksdb::Status::OK();
  }

  void rollback() override {
    on_rollback();
    m_write_count = 0;
    m_insert_count = 0;
    m_update_count = 0;
    m_delete_count = 0;
    m_row_lock_count = 0;
    release_snapshot();

    reset();
    set_tx_read_only(false);
    m_rollback_only = false;
  }

  void acquire_snapshot(bool acquire_now MY_ATTRIBUTE((unused))) override {
    if (m_read_opts.snapshot == nullptr) snapshot_created(rdb->GetSnapshot());
  }

  void release_snapshot() override {
    if (m_read_opts.snapshot != nullptr) {
      rdb->ReleaseSnapshot(m_read_opts.snapshot);
      m_read_opts.snapshot = nullptr;
    }
  }

  rocksdb::Status put(rocksdb::ColumnFamilyHandle *const column_family,
                      const rocksdb::Slice &key, const rocksdb::Slice &value,
                      const bool assume_tracked
                          MY_ATTRIBUTE((unused))) override {
    ++m_write_count;
    m_batch->Put(column_family, key, value);
    // Note Put/Delete in write batch doesn't return any error code. We simply
    // return OK here.
    return rocksdb::Status::OK();
  }

  rocksdb::Status delete_key(rocksdb::ColumnFamilyHandle *const column_family,
                             const rocksdb::Slice &key,
                             const bool assume_tracked
                                 MY_ATTRIBUTE((unused))) override {
    ++m_write_count;
    m_batch->Delete(column_family, key);
    return rocksdb::Status::OK();
  }

  rocksdb::Status single_delete(
      rocksdb::ColumnFamilyHandle *const column_family,
      const rocksdb::Slice &key, const bool /* assume_tracked */) override {
    ++m_write_count;
    m_batch->SingleDelete(column_family, key);
    return rocksdb::Status::OK();
  }

  bool has_modifications() const override {
    return m_batch->GetWriteBatch()->Count() > 0;
  }

  rocksdb::WriteBatchBase *get_write_batch() override { return m_batch; }

  rocksdb::WriteBatchBase *get_indexed_write_batch() override {
    ++m_write_count;
    return m_batch;
  }

  rocksdb::Status get(rocksdb::ColumnFamilyHandle *const column_family,
                      const rocksdb::Slice &key,
                      rocksdb::PinnableSlice *const value) const override {
    value->Reset();
    return m_batch->GetFromBatchAndDB(rdb, m_read_opts, column_family, key,
                                      value);
  }

  void multi_get(rocksdb::ColumnFamilyHandle *const column_family,
                 const size_t num_keys, const rocksdb::Slice *keys,
                 rocksdb::PinnableSlice *values, rocksdb::Status *statuses,
                 const bool sorted_input) const override {
    m_batch->MultiGetFromBatchAndDB(rdb, m_read_opts, column_family, num_keys,
                                    keys, values, statuses, sorted_input);
  }

  rocksdb::Status get_for_update(const Rdb_key_def &key_descr,
                                 const rocksdb::Slice &key,
                                 rocksdb::PinnableSlice *const value,
                                 bool /* exclusive */,
                                 const bool /* do_validate */,
                                 bool /* no_wait */) override {
    rocksdb::ColumnFamilyHandle *const column_family = key_descr.get_cf();
    if (value == nullptr) {
      rocksdb::PinnableSlice pin_val;
      rocksdb::Status s = get(column_family, key, &pin_val);
      pin_val.Reset();
      return s;
    }

    return get(column_family, key, value);
  }

  rocksdb::Iterator *get_iterator(
      const rocksdb::ReadOptions &options,
      rocksdb::ColumnFamilyHandle *const /* column_family */,
      const std::shared_ptr<Rdb_key_def>&,
      bool /*use_locking_iterator*/) override {
    const auto it = rdb->NewIterator(options);
    return m_batch->NewIteratorWithBase(it);
  }

  bool is_tx_started() const override { return (m_batch != nullptr); }

  void start_tx() override {
    reset();
    write_opts.sync = (rocksdb_flush_log_at_trx_commit == FLUSH_LOG_SYNC) &&
                      rdb_sync_wal_supported();
    write_opts.disableWAL = THDVAR(m_thd, write_disable_wal);
    write_opts.ignore_missing_column_families =
        THDVAR(m_thd, write_ignore_missing_column_families);

    set_initial_savepoint();
  }

  void start_stmt(bool /*is_dml_statement*/) override {}
  void set_name() override {}


  void rollback_stmt() override {
    if (m_batch) rollback_to_stmt_savepoint();
  }

  explicit Rdb_writebatch_impl(THD *const thd)
      : Rdb_transaction(thd), m_batch(nullptr) {
    m_batch = new rocksdb::WriteBatchWithIndex(rocksdb::BytewiseComparator(), 0,
                                               true);
  }

  virtual ~Rdb_writebatch_impl() override {
    // Remove from the global list before all other processing is started.
    // Otherwise, information_schema.rocksdb_trx can crash on this object.
    Rdb_transaction::remove_from_global_trx_list();

    rollback();
    delete m_batch;
  }
};

void Rdb_snapshot_notifier::SnapshotCreated(
    const rocksdb::Snapshot *const snapshot) {
  if (m_owning_tx != nullptr) {
    m_owning_tx->snapshot_created(snapshot);
  }
}

std::multiset<Rdb_transaction *> Rdb_transaction::s_tx_list;
Rds_mysql_mutex Rdb_transaction::s_tx_list_mutex;

/* data structure to hold per THD data */
class Rdb_ha_data {
 public:
  Rdb_ha_data()
      : checkpoint_dir(nullptr), trx(nullptr), disable_file_deletions(false) {}

  ~Rdb_ha_data() { clear_checkpoint_dir(); }

  const char *get_checkpoint_dir() const { return checkpoint_dir; }

  void set_checkpoint_dir(const char *checkpoint_dir_) {
    clear_checkpoint_dir();
    if (checkpoint_dir_ != nullptr) {
      checkpoint_dir = strdup(checkpoint_dir_);
    }
  }

  Rdb_transaction *get_trx() const { return trx; }

  void set_trx(Rdb_transaction *t) { trx = t; }

  bool get_disable_file_deletions() const { return disable_file_deletions; }

  void set_disable_file_deletions(bool d) { disable_file_deletions = d; }

 private:
  void clear_checkpoint_dir() {
    if (checkpoint_dir) {
      free(checkpoint_dir);
      checkpoint_dir = nullptr;
    }
  }

 private:
  char *checkpoint_dir;
  Rdb_transaction *trx;
  bool disable_file_deletions;
};

static Rdb_ha_data *&get_ha_data(THD *const thd) {
  Rdb_ha_data **ha_data =
      reinterpret_cast<Rdb_ha_data **>(my_core::thd_ha_data(thd, rocksdb_hton));
  if (*ha_data == nullptr) {
    *ha_data = new Rdb_ha_data();
  }
  return *ha_data;
}

static void destroy_ha_data(THD *const thd) {
  Rdb_ha_data *&ha_data = get_ha_data(thd);
  delete ha_data;
  ha_data = nullptr;
}

Rdb_transaction *get_tx_from_thd(THD *const thd) {
  return get_ha_data(thd)->get_trx();
}

static void set_tx_on_thd(THD *const thd, Rdb_transaction *trx) {
  return get_ha_data(thd)->set_trx(trx);
}

class Rdb_perf_context_guard {
  Rdb_io_perf m_io_perf;
  Rdb_io_perf *m_io_perf_ptr;
  Rdb_transaction *m_tx;
  THD *m_thd;

 public:
  Rdb_perf_context_guard(const Rdb_perf_context_guard &) = delete;
  Rdb_perf_context_guard &operator=(const Rdb_perf_context_guard &) = delete;

  explicit Rdb_perf_context_guard(Rdb_io_perf *io_perf, THD *thd)
      : m_io_perf_ptr(io_perf), m_tx(nullptr), m_thd(thd) {
    m_io_perf_ptr->start(rocksdb_perf_context_level(m_thd));
  }

  explicit Rdb_perf_context_guard(Rdb_transaction *tx, THD *thd)
      : m_io_perf_ptr(nullptr), m_tx(tx), m_thd(thd) {
    /*
      if perf_context information is already being recorded, this becomes a
      no-op
    */
    if (tx != nullptr) {
      tx->io_perf_start(&m_io_perf);
    }
  }

  ~Rdb_perf_context_guard() {
    if (m_tx != nullptr) {
      m_tx->io_perf_end_and_record();
    } else if (m_io_perf_ptr != nullptr) {
      m_io_perf_ptr->end_and_record(m_thd);
    }
  }
};

/*
  TODO: maybe, call this in external_lock() and store in ha_rocksdb..
*/

static Rdb_transaction *get_or_create_tx(THD *const thd) {
  Rdb_transaction *tx = get_tx_from_thd(thd);
  // TODO: this is called too many times.. O(#rows)
  if (tx == nullptr) {
    if ((rpl_skip_tx_api && thd->rli_slave) ||
        (THDVAR(thd, master_skip_tx_api) && !thd->rli_slave)) {
      tx = new Rdb_writebatch_impl(thd);
    } else {
      tx = new Rdb_transaction_impl(thd);
    }
    tx->set_params(THDVAR(thd, lock_wait_timeout), THDVAR(thd, max_row_locks));
    tx->start_tx();
    set_tx_on_thd(thd, tx);

    // Add the transaction to the global list of transactions
    // once it is fully constructed.
    tx->add_to_global_trx_list();
  } else {
    tx->set_params(THDVAR(thd, lock_wait_timeout), THDVAR(thd, max_row_locks));
    if (!tx->is_tx_started()) {
      tx->start_tx();
    }
  }

  return tx;
}

static int rocksdb_close_connection(
    handlerton *const hton MY_ATTRIBUTE((__unused__)), THD *const thd) {
  Rdb_transaction *tx = get_tx_from_thd(thd);

  if (tx != nullptr) {
    bool is_critical_error;
    int rc = tx->finish_bulk_load(&is_critical_error, false);
    if (rc != 0 && is_critical_error) {
      // NO_LINT_DEBUG
      sql_print_error(
          "RocksDB: Error %d finalizing last SST file while "
          "disconnecting",
          rc);
    }

    delete tx;
    tx = nullptr;
    set_tx_on_thd(thd, nullptr);
  }
  const char *checkpoint_dir = get_ha_data(thd)->get_checkpoint_dir();
  if (checkpoint_dir != nullptr) {
    rocksdb_remove_checkpoint(checkpoint_dir);
  }
  if (get_ha_data(thd)->get_disable_file_deletions()) {
    rdb->EnableFileDeletions(false);
  }
  destroy_ha_data(thd);
  return HA_EXIT_SUCCESS;
}

static int rocksdb_create_temporary_checkpoint_validate(
    my_core::THD *const thd, my_core::SYS_VAR *const /* unused */,
    void *const save, my_core::st_mysql_value *const value) {
  DBUG_ASSERT(rdb != nullptr);
  DBUG_ASSERT(thd != nullptr);

  const char *current_checkpoint_dir = THDVAR(thd, create_temporary_checkpoint);
  char buf[FN_REFLEN];
  int len = sizeof(buf);
  const char *new_checkpoint_dir = value->val_str(value, buf, &len);

  if (current_checkpoint_dir != nullptr && new_checkpoint_dir != nullptr) {
    *reinterpret_cast<const char **>(save) = current_checkpoint_dir;
    my_error(
        ER_GET_ERRMSG, MYF(0), HA_ERR_ROCKSDB_STATUS_INVALID_ARGUMENT,
        "Invalid argument: Temporary checkpoint already exists for session",
        rocksdb_hton_name);
    return HA_EXIT_FAILURE;
  } else if (new_checkpoint_dir != nullptr) {
    const auto res = rocksdb_create_checkpoint(new_checkpoint_dir);
    if (res == HA_EXIT_SUCCESS) {
      *reinterpret_cast<const char **>(save) = new_checkpoint_dir;
      get_ha_data(thd)->set_checkpoint_dir(new_checkpoint_dir);
    } else {
      return res;
    }
  } else if (current_checkpoint_dir != nullptr) {
    const auto res = rocksdb_remove_checkpoint(current_checkpoint_dir);
    *reinterpret_cast<const char **>(save) = nullptr;
    get_ha_data(thd)->set_checkpoint_dir(nullptr);
    if (res != HA_EXIT_SUCCESS) {
      return res;
    }
  } else {
    *reinterpret_cast<const char **>(save) = nullptr;
    get_ha_data(thd)->set_checkpoint_dir(nullptr);
  }
  return HA_EXIT_SUCCESS;
}

static void rocksdb_disable_file_deletions_update(
    my_core::THD *const thd, my_core::SYS_VAR *const /* unused */,
    void *const var_ptr, const void *const save) {
  DBUG_ASSERT(rdb != nullptr);
  DBUG_ASSERT(thd != nullptr);

  bool val = *static_cast<bool *>(var_ptr) = *static_cast<const bool *>(save);
  bool old_val = get_ha_data(thd)->get_disable_file_deletions();
  if (val && !old_val) {
    rdb->DisableFileDeletions();
    get_ha_data(thd)->set_disable_file_deletions(true);
  } else if (!val && old_val) {
    rdb->EnableFileDeletions(false);
    get_ha_data(thd)->set_disable_file_deletions(false);
  }
}

/**
  Called by hton->flush_logs after MySQL group commit prepares a set of
  transactions.
  @param[in]	hton			RocksDB handlerton
  @param[in]	binlog_group_flush	true if we got invoked by binlog
  group commit during flush stage, false in other cases.
*/
static bool rocksdb_flush_wal(handlerton *const hton MY_ATTRIBUTE((__unused__)),
                              bool binlog_group_flush) {
  DBUG_ASSERT(rdb != nullptr);

  // Don't flush log if 2pc isn't enabled
  if (binlog_group_flush && !rocksdb_enable_2pc) return HA_EXIT_SUCCESS;

  rocksdb::Status s;
  if ((!binlog_group_flush && !rocksdb_db_options->allow_mmap_writes) ||
      rocksdb_flush_log_at_trx_commit != FLUSH_LOG_NEVER) {
    rocksdb_wal_group_syncs++;
    bool sync = rdb_sync_wal_supported() &&
                (!binlog_group_flush ||
                 rocksdb_flush_log_at_trx_commit == FLUSH_LOG_SYNC);
    s = rdb->FlushWAL(sync);
  }

  if (!s.ok()) {
    rdb_log_status_error(s);
    return HA_EXIT_FAILURE;
  }

  return HA_EXIT_SUCCESS;
}

/**
  Called during create user table DDL. Block innodb table
  if rocksdb plugin is installed in the system.

  @param[in]       Target SE of table
  @retval true     Block table creation in target SE
  @retval false    Allow table creation in target SE
 */
static bool rocksdb_user_table_blocked(legacy_db_type db_type) {
  return db_type == DB_TYPE_INNODB;
}

/**
  For a slave, prepare() updates the slave_gtid_info table which tracks the
  replication progress.
*/
static int rocksdb_prepare(handlerton *const hton MY_ATTRIBUTE((__unused__)),
                           THD *const thd, bool prepare_tx) {
  Rdb_transaction *tx = get_tx_from_thd(thd);
  if (!tx->can_prepare()) {
    return HA_EXIT_FAILURE;
  }

  /* TODO(yzha) - 0f402cb8381b - Improve singled thread replication performance
   */
  if (prepare_tx ||
      (!my_core::thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN))) {
    /* We were instructed to prepare the whole transaction, or
    this is an SQL statement end and autocommit is on */
    if (tx->is_two_phase()) {
      if (thd->durability_property == HA_IGNORE_DURABILITY) {
        tx->set_sync(false);
      }
      // For write unprepared, set_name is called at the start of a transaction.
      if (rocksdb_write_policy != rocksdb::TxnDBWritePolicy::WRITE_UNPREPARED) {
        tx->set_name();
      }
      if (!tx->prepare()) {
        return HA_EXIT_FAILURE;
      }
    }

    DEBUG_SYNC(thd, "rocksdb.prepared");
  } else {
    tx->make_stmt_savepoint_permanent();
    tx->end_ignore_snapshot_if_needed();
  }

  return HA_EXIT_SUCCESS;
}

/**
 do nothing for prepare/commit by xid
 this is needed to avoid crashes in XA scenarios
*/
/* TODO(yzha) - Review failures code as return type as changed to xa_status_code
 */
static xa_status_code rocksdb_commit_by_xid(
    handlerton *const hton MY_ATTRIBUTE((__unused__)), XID *const xid) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(hton != nullptr);
  DBUG_ASSERT(xid != nullptr);
  DBUG_ASSERT(commit_latency_stats != nullptr);

  auto clock = rocksdb::Env::Default()->GetSystemClock().get();
  rocksdb::StopWatchNano timer(clock, true);

  const auto name = rdb_xid_to_string(*xid);
  DBUG_ASSERT(!name.empty());

  rocksdb::Transaction *const trx = rdb->GetTransactionByName(name);

  if (trx == nullptr) {
    DBUG_RETURN(XAER_NOTA);
  }

  const rocksdb::Status s = trx->Commit();

  if (!s.ok()) {
    rdb_log_status_error(s);
    DBUG_RETURN(XAER_RMERR);
  }

  delete trx;

  // `Add()` is implemented in a thread-safe manner.
  commit_latency_stats->Add(timer.ElapsedNanos() / 1000);

  DBUG_RETURN(XA_OK);
}

/* TODO(yzha) - Review failures code as return type as changed to xa_status_code
 */
static xa_status_code rocksdb_rollback_by_xid(
    handlerton *const hton MY_ATTRIBUTE((__unused__)), XID *const xid) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(hton != nullptr);
  DBUG_ASSERT(xid != nullptr);
  DBUG_ASSERT(rdb != nullptr);

  const auto name = rdb_xid_to_string(*xid);

  rocksdb::Transaction *const trx = rdb->GetTransactionByName(name);

  if (trx == nullptr) {
    DBUG_RETURN(XAER_NOTA);
  }

  const rocksdb::Status s = trx->Rollback();

  if (!s.ok()) {
    rdb_log_status_error(s);
    DBUG_RETURN(XAER_RMERR);
  }

  delete trx;

  DBUG_RETURN(XA_OK);
}

/**
  Rebuilds an XID from a serialized version stored in a string.
*/
static void rdb_xid_from_string(const std::string &src, XID *const dst) {
  DBUG_ASSERT(dst != nullptr);
  uint offset = 0;
  uint64 raw_fid8 =
      rdb_netbuf_to_uint64(reinterpret_cast<const uchar *>(src.data()));
  const int64 signed_fid8 = *reinterpret_cast<int64 *>(&raw_fid8);
  dst->reset();
  dst->set_format_id(signed_fid8);
  offset += RDB_FORMATID_SZ;
  dst->set_gtrid_length(src.at(offset));
  offset += RDB_GTRID_SZ;
  dst->set_bqual_length(src.at(offset));
  offset += RDB_BQUAL_SZ;

  DBUG_ASSERT(dst->get_gtrid_length() >= 0 &&
              dst->get_gtrid_length() <= MAXGTRIDSIZE);
  DBUG_ASSERT(dst->get_bqual_length() >= 0 &&
              dst->get_bqual_length() <= MAXBQUALSIZE);

  dst->set_data(src.c_str() + RDB_XIDHDR_LEN,
                (dst->get_gtrid_length()) + (dst->get_bqual_length()));
}

static void rocksdb_recover_binlog_pos_internal(
    /*!< in: print stderr */
    bool print_stderr,
    /*!< out: Max valid binlog gtid*/
    Gtid *binlog_max_gtid,
    /*!< out: Last valid binlog file */
    char *binlog_file,
    /*!< out: Last valid binlog pos */
    my_off_t *binlog_pos) {
  DBUG_ASSERT(binlog_file && binlog_pos);

  char file_buf[FN_REFLEN + 1] = {0};
  my_off_t pos = ULLONG_MAX;
  char gtid_buf[FN_REFLEN + 1] = {0};

  if (!binlog_manager.read(file_buf, &pos, gtid_buf)) {
    return;
  }

  memcpy(binlog_file, file_buf, FN_REFLEN + 1);
  *binlog_pos = pos;

  if (print_stderr) {
    // NO_LINT_DEBUG
    fprintf(stderr,
            "RocksDB: Last binlog file position %llu,"
            " file name %s\n",
            pos, file_buf);
  }

  if (binlog_max_gtid && *gtid_buf) {
    global_sid_lock->rdlock();
    binlog_max_gtid->parse(global_sid_map, gtid_buf);
    global_sid_lock->unlock();
    if (print_stderr) {
      // NO_LINT_DEBUG
      fprintf(stderr, "RocksDB: Last MySQL Gtid %s\n", gtid_buf);
    }
  }
}

static void rocksdb_recover_binlog_pos(
    /*!< in: rocksdb handler */
    handlerton *const hton MY_ATTRIBUTE((__unused__)),
    /*!< out: Max valid binlog gtid*/
    Gtid *binlog_max_gtid,
    /*!< out: Last valid binlog file */
    char *binlog_file,
    /*!< out: Last valid binlog pos */
    my_off_t *binlog_pos) {
  rocksdb_recover_binlog_pos_internal(/* print_stderr */ true, binlog_max_gtid,
                                      binlog_file, binlog_pos);
}

/** This function is used to sync binlog positions and Gtid.
 * @return false on success. true on failure
 */
static bool rocksdb_update_binlog_pos(
    handlerton *,             /*!< in: RocksDB handlerton */
    const char *file,         /*!< in: Valid binlog file */
    const my_off_t *offset,   /*!< in: Valid binlog offset */
    const char *max_gtid_buf) /*!< in: Max valid binlog gtid in str format */
{
  DBUG_ASSERT(file && offset && max_gtid_buf);

  bool sync = false;
  DBUG_EXECUTE_IF("update_binlog_pos", sync = true;);

  if (binlog_manager.persist_pos(file, *offset, max_gtid_buf, sync))
    return true;

  return false;
}

/**
  Reading last committed binary log info from RocksDB system row.
  The info is needed for crash safe slave/master to work.
*/
static int rocksdb_recover(handlerton *const hton MY_ATTRIBUTE((__unused__)),
                           XA_recover_txn *const xid_list, uint len,
                           MEM_ROOT *mem_root MY_ATTRIBUTE((__unused__))) {
  if (len == 0 || xid_list == nullptr) {
    return HA_EXIT_SUCCESS;
  }

  std::vector<rocksdb::Transaction *> trans_list;
  rdb->GetAllPreparedTransactions(&trans_list);

  uint count = 0;
  for (auto &trans : trans_list) {
    if (count >= len) {
      break;
    }
    auto name = trans->GetName();
    rdb_xid_from_string(name, &xid_list[count].id);
    count++;
  }
  return count;
}

static int rocksdb_commit(handlerton *const hton MY_ATTRIBUTE((__unused__)),
                          THD *const thd, bool all) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(hton != nullptr);
  DBUG_ASSERT(thd != nullptr);
  DBUG_ASSERT(commit_latency_stats != nullptr);

  auto clock = rocksdb::Env::Default()->GetSystemClock().get();
  rocksdb::StopWatchNano timer(clock, true);

  /* note: h->external_lock(F_UNLCK) is called after this function is called) */
  Rdb_transaction *tx = get_tx_from_thd(thd);

  /* this will trigger saving of perf_context information */
  Rdb_perf_context_guard guard(tx, thd);

  if (tx != nullptr) {
    if (all || (!my_core::thd_test_options(
                   thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN))) {
      /*
        We get here
         - For a COMMIT statement that finishes a multi-statement transaction
         - For a statement that has its own transaction
      */
      tx->end_ignore_snapshot_if_needed();
      if (tx->commit()) {
        DBUG_RETURN(HA_ERR_ROCKSDB_COMMIT_FAILED);
      }
    } else {
      /*
        We get here when committing a statement within a transaction.
      */
      tx->set_tx_failed(false);
      tx->make_stmt_savepoint_permanent();
      tx->end_ignore_snapshot_if_needed();
    }

    if (my_core::thd_tx_isolation(thd) <= ISO_READ_COMMITTED) {
      // For READ_COMMITTED, we release any existing snapshot so that we will
      // see any changes that occurred since the last statement.
      tx->release_snapshot();
    }
  }

  // `Add()` is implemented in a thread-safe manner.
  commit_latency_stats->Add(timer.ElapsedNanos() / 1000);

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

static int rocksdb_rollback(handlerton *const hton MY_ATTRIBUTE((__unused__)),
                            THD *const thd, bool rollback_tx) {
  Rdb_transaction *tx = get_tx_from_thd(thd);
  Rdb_perf_context_guard guard(tx, thd);

  if (tx != nullptr) {
    if (rollback_tx) {
      /*
        We get here, when
        - ROLLBACK statement is issued.

        Discard the changes made by the transaction
      */
      tx->rollback();
    } else {
      /*
        We get here when
        - a statement with AUTOCOMMIT=1 is being rolled back (because of some
          error)
        - a statement inside a transaction is rolled back
      */

      tx->end_ignore_snapshot_if_needed();
      tx->rollback_stmt();
      tx->set_tx_failed(true);
    }

    if (my_core::thd_tx_isolation(thd) <= ISO_READ_COMMITTED) {
      // For READ_COMMITTED, we release any existing snapshot so that we will
      // see any changes that occurred since the last statement.
      tx->release_snapshot();
    }
  }
  return HA_EXIT_SUCCESS;
}

static bool print_stats(THD *const thd, std::string const &type,
                        std::string const &name, std::string const &status,
                        stat_print_fn *stat_print) {
  return stat_print(thd, type.c_str(), type.size(), name.c_str(), name.size(),
                    status.c_str(), status.size());
}

static std::string format_string(const char *const format, ...) {
  std::string res;
  va_list args;
  va_list args_copy;
  char static_buff[256];

  DBUG_ASSERT(format != nullptr);

  va_start(args, format);
  va_copy(args_copy, args);

  // Calculate how much space we will need
  int len = vsnprintf(nullptr, 0, format, args);
  va_end(args);

  if (len < 0) {
    res = std::string("<format error>");
  } else if (len == 0) {
    // Shortcut for an empty string
    res = std::string("");
  } else {
    // For short enough output use a static buffer
    char *buff = static_buff;
    std::unique_ptr<char[]> dynamic_buff = nullptr;

    len++;  // Add one for null terminator

    // for longer output use an allocated buffer
    if (static_cast<uint>(len) > sizeof(static_buff)) {
      dynamic_buff.reset(new char[len]);
      buff = dynamic_buff.get();
    }

    // Now re-do the vsnprintf with the buffer which is now large enough
    (void)vsnprintf(buff, len, format, args_copy);

    // Convert to a std::string.  Note we could have created a std::string
    // large enough and then converted the buffer to a 'char*' and created
    // the output in place.  This would probably work but feels like a hack.
    // Since this isn't code that needs to be super-performant we are going
    // with this 'safer' method.
    res = std::string(buff);
  }

  va_end(args_copy);

  return res;
}

class Rdb_snapshot_status : public Rdb_tx_list_walker {
 private:
  std::string m_data;

  static std::string current_timestamp(void) {
    static const char *const format = "%d-%02d-%02d %02d:%02d:%02d";
    time_t currtime;
    struct tm currtm;

    time(&currtime);

    localtime_r(&currtime, &currtm);

    return format_string(format, currtm.tm_year + 1900, currtm.tm_mon + 1,
                         currtm.tm_mday, currtm.tm_hour, currtm.tm_min,
                         currtm.tm_sec);
  }

  static std::string get_header(void) {
    return "\n============================================================\n" +
           current_timestamp() +
           " ROCKSDB TRANSACTION MONITOR OUTPUT\n"
           "============================================================\n"
           "---------\n"
           "SNAPSHOTS\n"
           "---------\n"
           "LIST OF SNAPSHOTS FOR EACH SESSION:\n";
  }

  static std::string get_footer(void) {
    return "-----------------------------------------\n"
           "END OF ROCKSDB TRANSACTION MONITOR OUTPUT\n"
           "=========================================\n";
  }

  template<class PathStruct>
  static Rdb_deadlock_info::Rdb_dl_trx_info get_dl_txn_info(
      const PathStruct &txn, const GL_INDEX_ID &gl_index_id) {
    Rdb_deadlock_info::Rdb_dl_trx_info txn_data;

    txn_data.trx_id = txn.m_txn_id;

    txn_data.table_name = ddl_manager.safe_get_table_name(gl_index_id);
    if (txn_data.table_name.empty()) {
      txn_data.table_name =
          "NOT FOUND; INDEX_ID: " + std::to_string(gl_index_id.index_id);
    }

    auto kd = ddl_manager.safe_find(gl_index_id);
    txn_data.index_name =
        (kd) ? kd->get_name()
             : "NOT FOUND; INDEX_ID: " + std::to_string(gl_index_id.index_id);

    std::shared_ptr<rocksdb::ColumnFamilyHandle> cfh =
        cf_manager.get_cf(txn.m_cf_id);

    // Retrieve CF name from CF handle object, and it is safe if the CF is
    // removed from cf_manager at this point.
    txn_data.cf_name = (cfh)
                           ? cfh->GetName()
                           : "NOT FOUND; CF_ID: " + std::to_string(txn.m_cf_id);

    txn_data.waiting_key = format_wait_key(txn);

    txn_data.exclusive_lock = txn.m_exclusive;

    return txn_data;
  }

  // Get the key to use to find the index number (and then, index name)
  //   Two functions with matching signatures so get_dl_path_trx_info() template
  //   can be used with both point and range locking.
  static const std::string& get_key_for_indexnr(
      const rocksdb::DeadlockInfo& info) {
    return info.m_waiting_key;
  }
  static const std::string& get_key_for_indexnr(
      const rocksdb::RangeDeadlockInfo& info) {
    // Range locks do not span across indexes, so take the left bound
    return info.m_start.slice;
  }

  // Print the locked key (or range) in hex
  //   Two functions with matching signatures so get_dl_path_trx_info() template
  //   can be used with both point and range locking.
  static std::string format_wait_key(const rocksdb::DeadlockInfo& info) {
    return rdb_hexdump(info.m_waiting_key.c_str(), info.m_waiting_key.length());
  }
  static std::string format_wait_key(const rocksdb::RangeDeadlockInfo& info) {
    return rdb_hexdump_range(info.m_start, info.m_end);
  }

  // Get deadlock path info. A templated function so one can use it with both
  // point and range locking.
  template<class PathStruct>
  static Rdb_deadlock_info get_dl_path_trx_info(
      const PathStruct &path_entry) {
    Rdb_deadlock_info deadlock_info;

    for (auto it = path_entry.path.begin(); it != path_entry.path.end(); it++) {
      const auto &txn = *it;
      auto waiting_key = get_key_for_indexnr(txn);
      const GL_INDEX_ID gl_index_id = {
          txn.m_cf_id, rdb_netbuf_to_uint32(reinterpret_cast<const uchar *>(
                           waiting_key.c_str()))};
      deadlock_info.path.push_back(get_dl_txn_info(txn, gl_index_id));
    }
    DBUG_ASSERT_IFF(path_entry.limit_exceeded, path_entry.path.empty());
    /* print the first txn in the path to display the full deadlock cycle */
    if (!path_entry.path.empty() && !path_entry.limit_exceeded) {
      const auto &deadlocking_txn = *(path_entry.path.end() - 1);
      deadlock_info.victim_trx_id = deadlocking_txn.m_txn_id;
      deadlock_info.deadlock_time = path_entry.deadlock_time;
    }
    return deadlock_info;
  }

 public:
  Rdb_snapshot_status() : m_data(get_header()) {}

  std::string getResult() { return m_data + get_footer(); }

  /* Implement Rdb_transaction interface */
  /* Create one row in the snapshot status table */
  void process_tran(const Rdb_transaction *const tx) override {
    DBUG_ASSERT(tx != nullptr);

    /* Calculate the duration the snapshot has existed */
    int64_t snapshot_timestamp = tx->m_snapshot_timestamp;
    if (snapshot_timestamp != 0 && tx->has_snapshot()) {
      int64_t curr_time;
      rdb->GetEnv()->GetCurrentTime(&curr_time);

      THD *thd = tx->get_thd();
      char buffer[1024];
      thd_security_context_internal(thd, buffer, sizeof buffer, 0,
                                    current_thd->variables.show_query_digest);
      m_data += format_string(
          "---SNAPSHOT, ACTIVE %lld sec\n"
          "%s\n"
          "lock count %llu, write count %llu\n"
          "insert count %llu, update count %llu, delete count %llu\n",
          curr_time - snapshot_timestamp, buffer, tx->get_row_lock_count(),
          tx->get_write_count(), tx->get_insert_count(), tx->get_update_count(),
          tx->get_delete_count());
    }
  }

  template<class PathStruct>
  void populate_deadlock_buffer_tmpl(PathStruct &dlock_buffer) {
    m_data += "----------LATEST DETECTED DEADLOCKS----------\n";

    for (const auto &path_entry : dlock_buffer) {
      std::string path_data;
      if (path_entry.limit_exceeded) {
        path_data += "\n-------DEADLOCK EXCEEDED MAX DEPTH-------\n";
      } else {
        path_data +=
            "\n*** DEADLOCK PATH\n"
            "=========================================\n";
        const auto dl_info = get_dl_path_trx_info(path_entry);
        const auto deadlock_time = dl_info.deadlock_time;
        for (auto it = dl_info.path.begin(); it != dl_info.path.end(); it++) {
          const auto &trx_info = *it;
          path_data += format_string(
              "TIMESTAMP: %" PRId64
              "\n"
              "TRANSACTION ID: %u\n"
              "COLUMN FAMILY NAME: %s\n"
              "WAITING KEY: %s\n"
              "LOCK TYPE: %s\n"
              "INDEX NAME: %s\n"
              "TABLE NAME: %s\n",
              deadlock_time, trx_info.trx_id, trx_info.cf_name.c_str(),
              trx_info.waiting_key.c_str(),
              trx_info.exclusive_lock ? "EXCLUSIVE" : "SHARED",
              trx_info.index_name.c_str(), trx_info.table_name.c_str());
          if (it != dl_info.path.end() - 1) {
            path_data += "---------------WAITING FOR---------------\n";
          }
        }
        path_data += format_string(
            "\n--------TRANSACTION ID: %u GOT DEADLOCK---------\n",
            dl_info.victim_trx_id);
      }
      m_data += path_data;
    }
  }

  void populate_deadlock_buffer() {
    if (range_lock_mgr) {
      auto dlock_buffer = range_lock_mgr->GetRangeDeadlockInfoBuffer();
      populate_deadlock_buffer_tmpl(dlock_buffer);
    } else {
      auto dlock_buffer = rdb->GetDeadlockInfoBuffer();
      populate_deadlock_buffer_tmpl(dlock_buffer);
    }
  }

  std::vector<Rdb_deadlock_info> get_deadlock_info() {
    std::vector<Rdb_deadlock_info> deadlock_info;

    if (range_lock_mgr) {
      auto dlock_buffer = range_lock_mgr->GetRangeDeadlockInfoBuffer();
      for (const auto &path_entry : dlock_buffer) {
        if (!path_entry.limit_exceeded) {
          deadlock_info.push_back(get_dl_path_trx_info(path_entry));
        }
      }
    } else {
      auto dlock_buffer = rdb->GetDeadlockInfoBuffer();
      for (const auto &path_entry : dlock_buffer) {
        if (!path_entry.limit_exceeded) {
          deadlock_info.push_back(get_dl_path_trx_info(path_entry));
        }
      }
    }
    return deadlock_info;
  }
};

/**
 * @brief
 * walks through all non-replication transactions and copies
 * out relevant information for information_schema.rocksdb_trx
 */
class Rdb_trx_info_aggregator : public Rdb_tx_list_walker {
 private:
  std::vector<Rdb_trx_info> *m_trx_info;

 public:
  explicit Rdb_trx_info_aggregator(std::vector<Rdb_trx_info> *const trx_info)
      : m_trx_info(trx_info) {}

  void process_tran(const Rdb_transaction *const tx) override {
    static const std::map<int, std::string> state_map = {
        {rocksdb::Transaction::STARTED, "STARTED"},
        {rocksdb::Transaction::AWAITING_PREPARE, "AWAITING_PREPARE"},
        {rocksdb::Transaction::PREPARED, "PREPARED"},
        {rocksdb::Transaction::AWAITING_COMMIT, "AWAITING_COMMIT"},
        {rocksdb::Transaction::COMMITED, "COMMITED"},
        {rocksdb::Transaction::AWAITING_ROLLBACK, "AWAITING_ROLLBACK"},
        {rocksdb::Transaction::ROLLEDBACK, "ROLLEDBACK"},
    };

    DBUG_ASSERT(tx != nullptr);

    THD *const thd = tx->get_thd();
    ulong thread_id = thd_thread_id(thd);

    if (tx->is_writebatch_trx()) {
      const auto wb_impl = static_cast<const Rdb_writebatch_impl *>(tx);
      DBUG_ASSERT(wb_impl);
      m_trx_info->push_back(
          {"",                            /* name */
           0,                             /* trx_id */
           wb_impl->get_write_count(), 0, /* lock_count */
           0,                             /* timeout_sec */
           "",                            /* state */
           "",                            /* waiting_key */
           0,                             /* waiting_cf_id */
           1,                             /*is_replication */
           1,                             /* skip_trx_api */
           wb_impl->is_tx_read_only(), 0, /* deadlock detection */
           wb_impl->num_ongoing_bulk_load(), thread_id, "" /* query string */});
    } else {
      const auto tx_impl = static_cast<const Rdb_transaction_impl *>(tx);
      DBUG_ASSERT(tx_impl);
      const rocksdb::Transaction *rdb_trx = tx_impl->get_rdb_trx();

      if (rdb_trx == nullptr) {
        return;
      }

      std::string query_str;
      thd_query_safe(thd, &query_str);

      const auto state_it = state_map.find(rdb_trx->GetState());
      DBUG_ASSERT(state_it != state_map.end());
      const int is_replication = (thd->rli_slave != nullptr);
      uint32_t waiting_cf_id;
      std::string waiting_key;
      rdb_trx->GetWaitingTxns(&waiting_cf_id, &waiting_key),

          m_trx_info->push_back(
              {rdb_trx->GetName(), rdb_trx->GetID(), tx_impl->get_write_count(),
               tx_impl->get_row_lock_count(), tx_impl->get_timeout_sec(),
               state_it->second, waiting_key, waiting_cf_id, is_replication,
               0, /* skip_trx_api */
               tx_impl->is_tx_read_only(), rdb_trx->IsDeadlockDetect(),
               tx_impl->num_ongoing_bulk_load(), thread_id, query_str});
    }
  }
};

/*
  returns a vector of info for all non-replication threads
  for use by information_schema.rocksdb_trx
*/
std::vector<Rdb_trx_info> rdb_get_all_trx_info() {
  std::vector<Rdb_trx_info> trx_info;
  Rdb_trx_info_aggregator trx_info_agg(&trx_info);
  Rdb_transaction::walk_tx_list(&trx_info_agg);
  return trx_info;
}

/*
  returns a vector of info of recent deadlocks
  for use by information_schema.rocksdb_deadlock
*/
std::vector<Rdb_deadlock_info> rdb_get_deadlock_info() {
  Rdb_snapshot_status showStatus;
  Rdb_transaction::walk_tx_list(&showStatus);
  return showStatus.get_deadlock_info();
}

/* Generate the snapshot status table */
static bool rocksdb_show_snapshot_status(
    handlerton *const hton MY_ATTRIBUTE((__unused__)), THD *const thd,
    stat_print_fn *const stat_print) {
  Rdb_snapshot_status showStatus;

  Rdb_transaction::walk_tx_list(&showStatus);
  showStatus.populate_deadlock_buffer();

  /* Send the result data back to MySQL */
  return print_stats(thd, "rocksdb", "", showStatus.getResult(), stat_print);
}

/* Generate the binlog position status table */
static bool rocksdb_show_binlog_position(THD *const thd,
                                         stat_print_fn *const stat_print) {
  std::ostringstream oss;

  /* Get binlog position in SE */
  char binlog_file[FN_REFLEN + 1] = {0};
  my_off_t binlog_pos = ULLONG_MAX;
  Gtid max_gtid{0, 0};
  rocksdb_recover_binlog_pos_internal(/* print_stderr */ false, &max_gtid,
                                      binlog_file, &binlog_pos);

  char gtid_buf[Gtid::MAX_TEXT_LENGTH + 1] = {0};
  if (!max_gtid.is_empty()) {
    global_sid_lock->rdlock();
    max_gtid.to_string(global_sid_map, gtid_buf);
    global_sid_lock->unlock();
  }

  oss << "\n"
      << "BINLOG FILE " << binlog_file << "\n"
      << "BINLOG OFFSET " << binlog_pos << "\n"
      << "MAX GTID " << gtid_buf << "\n";

  return print_stats(thd, "BINLOG POSITION", "rocksdb", oss.str(), stat_print);
}

/*
  This is called for SHOW ENGINE ROCKSDB STATUS | LOGS | etc.

  For now, produce info about live files (which gives an imprecise idea about
  what column families are there).
*/
static bool rocksdb_show_status(handlerton *const hton, THD *const thd,
                                stat_print_fn *const stat_print,
                                enum ha_stat_type stat_type) {
  DBUG_ASSERT(hton != nullptr);
  DBUG_ASSERT(thd != nullptr);
  DBUG_ASSERT(stat_print != nullptr);

  bool res = false;
  char buf[100] = {'\0'};

  if (stat_type == HA_ENGINE_STATUS) {
    DBUG_ASSERT(rdb != nullptr);

    std::string str;

    /* Global DB Statistics */
    if (rocksdb_stats) {
      str = rocksdb_stats->ToString();

      // Use the same format as internal RocksDB statistics entries to make
      // sure that output will look unified.
      DBUG_ASSERT(commit_latency_stats != nullptr);

      snprintf(buf, sizeof(buf),
               "rocksdb.commit_latency statistics "
               "Percentiles :=> 50 : %.2f 95 : %.2f "
               "99 : %.2f 100 : %.2f\n",
               commit_latency_stats->Percentile(50),
               commit_latency_stats->Percentile(95),
               commit_latency_stats->Percentile(99),
               commit_latency_stats->Percentile(100));
      str.append(buf);

      uint64_t v = 0;

      // Retrieve additional stalling related numbers from RocksDB and append
      // them to the buffer meant for displaying detailed statistics. The intent
      // here is to avoid adding another row to the query output because of
      // just two numbers.
      //
      // NB! We're replacing hyphens with underscores in output to better match
      // the existing naming convention.
      if (rdb->GetIntProperty("rocksdb.is-write-stopped", &v)) {
        snprintf(buf, sizeof(buf), "rocksdb.is_write_stopped COUNT : %lu\n", v);
        str.append(buf);
      }

      if (rdb->GetIntProperty("rocksdb.actual-delayed-write-rate", &v)) {
        snprintf(buf, sizeof(buf),
                 "rocksdb.actual_delayed_write_rate "
                 "COUNT : %lu\n",
                 v);
        str.append(buf);
      }

      res |= print_stats(thd, "STATISTICS", "rocksdb", str, stat_print);
    }

    /* Per DB stats */
    if (rdb->GetProperty("rocksdb.dbstats", &str)) {
      res |= print_stats(thd, "DBSTATS", "rocksdb", str, stat_print);
    }

    /* Per column family stats */
    for (const auto &cf_name : cf_manager.get_cf_names()) {
      std::shared_ptr<rocksdb::ColumnFamilyHandle> cfh =
          cf_manager.get_cf(cf_name);
      if (!cfh) {
        continue;
      }

      // Retrieve information from CF handle object.
      // Even if the CF is removed from CF_manager, the handle object
      // is valid.
      if (!rdb->GetProperty(cfh.get(), "rocksdb.cfstats", &str)) {
        continue;
      }

      res |= print_stats(thd, "CF_COMPACTION", cf_name, str, stat_print);
    }

    /* Memory Statistics */
    std::vector<rocksdb::DB *> dbs;
    std::unordered_set<const rocksdb::Cache *> cache_set;
    size_t internal_cache_count = 0;
    size_t kDefaultInternalCacheSize = 8 * 1024 * 1024;

    dbs.push_back(rdb);
    cache_set.insert(rocksdb_tbl_options->block_cache.get());

    for (const auto &cf_handle : cf_manager.get_all_cf()) {
      // It is safe if the CF handle is removed from cf_manager
      // at this point.
      rocksdb::ColumnFamilyDescriptor cf_desc;
      cf_handle->GetDescriptor(&cf_desc);
      auto *const table_factory = cf_desc.options.table_factory.get();

      if (table_factory != nullptr) {
        std::string tf_name = table_factory->Name();

        if (tf_name.find("BlockBasedTable") != std::string::npos) {
          const auto bbt_opt =
              table_factory->GetOptions<rocksdb::BlockBasedTableOptions>();

          if (bbt_opt != nullptr) {
            if (bbt_opt->block_cache.get() != nullptr) {
              cache_set.insert(bbt_opt->block_cache.get());
            } else {
              internal_cache_count++;
            }
            cache_set.insert(bbt_opt->block_cache_compressed.get());
          }
        }
      }
    }

    std::map<rocksdb::MemoryUtil::UsageType, uint64_t> temp_usage_by_type;
    str.clear();
    rocksdb::MemoryUtil::GetApproximateMemoryUsageByType(dbs, cache_set,
                                                         &temp_usage_by_type);

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
    res |= print_stats(thd, "MEMORY_STATS", "rocksdb", str, stat_print);

    /* Show the background thread status */
    std::vector<rocksdb::ThreadStatus> thread_list;
    rocksdb::Status s = rdb->GetEnv()->GetThreadList(&thread_list);

    // GetThreadList() may return Status::NotSupported when
    // ROCKSDB_USING_THREAD_STATUS is not defined
    if (!s.ok() && !s.IsNotSupported()) {
      // NO_LINT_DEBUG
      sql_print_error("RocksDB: Returned error (%s) from GetThreadList.\n",
                      s.ToString().c_str());
      res |= true;
    } else {
      /* For each background thread retrieved, print out its information */
      for (auto &it : thread_list) {
        /* Only look at background threads. Ignore user threads, if any. */
        if (it.thread_type > rocksdb::ThreadStatus::LOW_PRIORITY) {
          continue;
        }

        str = "\nthread_type: " + it.GetThreadTypeName(it.thread_type) +
              "\ncf_name: " + it.cf_name +
              "\noperation_type: " + it.GetOperationName(it.operation_type) +
              "\noperation_stage: " +
              it.GetOperationStageName(it.operation_stage) +
              "\nelapsed_time_ms: " + it.MicrosToString(it.op_elapsed_micros);

        for (auto &it_props : it.InterpretOperationProperties(
                 it.operation_type, it.op_properties)) {
          str += "\n" + it_props.first + ": " + std::to_string(it_props.second);
        }

        str += "\nstate_type: " + it.GetStateName(it.state_type);

        res |= print_stats(thd, "BG_THREADS", std::to_string(it.thread_id), str,
                           stat_print);
      }
    }

    /* Explicit snapshot information */
    str = Rdb_explicit_snapshot::dump_snapshots();
    if (!str.empty()) {
      res |= print_stats(thd, "EXPLICIT_SNAPSHOTS", "rocksdb", str, stat_print);
    }
    /* Binlog position information */
    res |= rocksdb_show_binlog_position(thd, stat_print);
  } else if (stat_type == HA_ENGINE_TRX) {
    /* Handle the SHOW ENGINE ROCKSDB TRANSACTION STATUS command */
    res |= rocksdb_show_snapshot_status(hton, thd, stat_print);
  }

  if (res) {
    my_error(ER_UNKNOWN_ERROR, MYF(0), "SHOW ENGINE");
  }
  return res;
}

/*
  Implements Log_resource lock.

  returns false on success
*/
static bool rocksdb_lock_hton_log(
    handlerton *const MY_ATTRIBUTE((__unused__))) {
  DBUG_ASSERT(rdb != nullptr);
  return !rdb->LockWAL().ok();
}

/*
  Implements Log_resource unlock.

  returns false on success
*/
static bool rocksdb_unlock_hton_log(handlerton *const /* unused */) {
  DBUG_ASSERT(rdb != nullptr);
  return !rdb->UnlockWAL().ok();
}

/*
  Implements Log_resource collect_info.

  Produces JSON object with following structure:

  "RocksDB": {
    "wal_files": [
      { "log_number": N, "path_name": "...", "file_size_bytes": K },
      ...
    ]
  }

  returns JSON dom to receive the log info
*/
static bool rocksdb_collect_hton_log_info(handlerton *const /* unused */,
                                          Json_dom *json) {
  bool ret_val = false;
  rocksdb::VectorLogPtr live_wal_files;
  const auto s = rdb->GetSortedWalFiles(live_wal_files);

  if (!s.ok()) {
    return true;
  }

  Json_object *json_engines = static_cast<Json_object *>(json);
  Json_object json_rocksdb;
  Json_array json_wal_files;

  size_t index = 0;
  for (const auto &wal : live_wal_files) {
    if (wal->Type() == rocksdb::WalFileType::kAliveLogFile) {
      Json_uint json_log_number(wal->LogNumber());
      Json_string json_path_name(wal->PathName());
      Json_uint json_size_file_bytes(wal->SizeFileBytes());

      Json_object json_wal;
      ret_val = json_wal.add_clone("log_number", &json_log_number);
      if (!ret_val) ret_val = json_wal.add_clone("path_name", &json_path_name);
      if (!ret_val)
        ret_val = json_wal.add_clone("size_file_bytes", &json_size_file_bytes);
      if (!ret_val) ret_val = json_wal_files.insert_clone(index, &json_wal);
      if (ret_val) break;
      ++index;
    }
  }

  if (!ret_val) ret_val = json_rocksdb.add_clone("wal_files", &json_wal_files);

  if (!ret_val) ret_val = json_engines->add_clone("RocksDB", &json_rocksdb);

  return ret_val;
}

/*
  @param is_dml_statement   If true, we are is a DML statement
*/
static inline void rocksdb_register_tx(
    handlerton *const hton MY_ATTRIBUTE((__unused__)), THD *const thd,
    Rdb_transaction *const tx,
    bool is_dml_stmt) {
  DBUG_ASSERT(tx != nullptr);

  trans_register_ha(thd, false, rocksdb_hton, NULL);
  if (rocksdb_write_policy == rocksdb::TxnDBWritePolicy::WRITE_UNPREPARED) {
    // Some internal operations will call trans_register_ha, but they do not
    // go through 2pc. In this case, the xid is set with query_id == 0, which
    // means that rocksdb will receive transactions with duplicate names.
    //
    // Skip setting name in these cases.
    if (thd->query_id != 0) {
      tx->set_name();
    }
  }
  if (my_core::thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN)) {
    tx->start_stmt(is_dml_stmt);
    trans_register_ha(thd, true, rocksdb_hton, NULL);
  } else {
    tx->start_autocommit_stmt(is_dml_stmt);
  }
}

static int rocksdb_explicit_snapshot(
    handlerton *const /* hton */, /*!< in: RocksDB handlerton */
    THD *const thd,               /*!< in: MySQL thread handle */
    snapshot_info_st *ss_info)    /*!< out: Snapshot information */
{
  switch (ss_info->op) {
    case snapshot_operation::SNAPSHOT_CREATE: {
      if (mysql_bin_log_is_open()) {
        mysql_bin_log_lock_commits(ss_info);
      }
      auto s = Rdb_explicit_snapshot::create(ss_info, rdb, rdb->GetSnapshot());
      if (mysql_bin_log_is_open()) {
        mysql_bin_log_unlock_commits(ss_info);
      }

      thd->set_explicit_snapshot(s);
      return s == nullptr ? HA_EXIT_FAILURE : HA_EXIT_SUCCESS;
    }
    case snapshot_operation::SNAPSHOT_ATTACH: {
      auto s = Rdb_explicit_snapshot::get(ss_info->snapshot_id);
      if (!s) {
        return HA_EXIT_FAILURE;
      }
      *ss_info = s->ss_info;
      thd->set_explicit_snapshot(s);
      return HA_EXIT_SUCCESS;
    }
    case snapshot_operation::SNAPSHOT_RELEASE: {
      if (!thd->get_explicit_snapshot()) {
        return HA_EXIT_FAILURE;
      }
      *ss_info = thd->get_explicit_snapshot()->ss_info;
      thd->set_explicit_snapshot(nullptr);
      return HA_EXIT_SUCCESS;
    }
    default:
      DBUG_ASSERT(false);
      return HA_EXIT_FAILURE;
  }
  return HA_EXIT_FAILURE;
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
    handlerton *const hton, /*!< in: RocksDB handlerton */
    THD *const thd          /*!< in: MySQL thread handle of the
                            user for whom the transaction should
                            be committed */
) {
  ulong const tx_isolation = my_core::thd_tx_isolation(thd);

  if (tx_isolation != ISO_REPEATABLE_READ) {
    my_error(ER_ISOLATION_LEVEL_WITH_CONSISTENT_SNAPSHOT, MYF(0));
    return HA_EXIT_FAILURE;
  }

  Rdb_transaction *const tx = get_or_create_tx(thd);
  Rdb_perf_context_guard guard(tx, thd);

  DBUG_ASSERT(!tx->has_snapshot());
  tx->set_tx_read_only(true);
  rocksdb_register_tx(hton, thd, tx, false);
  tx->acquire_snapshot(true);

  return HA_EXIT_SUCCESS;
}

static int rocksdb_start_tx_with_shared_read_view(
    handlerton *const hton,    /*!< in: RocksDB handlerton */
    THD *const thd,            /*!< in: MySQL thread handle of the
                               user for whom the transaction should
                               be committed */
    snapshot_info_st *ss_info) /*!< out: Snapshot info like binlog file, pos,
                               gtid executed and snapshot ID */
{
  DBUG_ASSERT(thd != nullptr);
  DBUG_ASSERT(ss_info != nullptr);

  int error = HA_EXIT_SUCCESS;

  ulong const tx_isolation = my_core::thd_tx_isolation(thd);
  if (tx_isolation != ISO_REPEATABLE_READ) {
    my_error(ER_ISOLATION_LEVEL_WITH_CONSISTENT_SNAPSHOT, MYF(0));
    return HA_EXIT_FAILURE;
  }

  std::shared_ptr<Rdb_explicit_snapshot> explicit_snapshot;
  const auto op = ss_info->op;
  Rdb_transaction *tx = nullptr;

  DBUG_ASSERT(op == snapshot_operation::SNAPSHOT_CREATE ||
              op == snapshot_operation::SNAPSHOT_ATTACH);

  // case: if binlogs are available get binlog file/pos and gtid info
  if (op == snapshot_operation::SNAPSHOT_CREATE && mysql_bin_log_is_open()) {
    mysql_bin_log_lock_commits(ss_info);
  }

  if (op == snapshot_operation::SNAPSHOT_ATTACH) {
    explicit_snapshot = Rdb_explicit_snapshot::get(ss_info->snapshot_id);
    if (!explicit_snapshot) {
      my_printf_error(ER_UNKNOWN_ERROR, "Snapshot %llu does not exist", MYF(0),
                      ss_info->snapshot_id);
      error = HA_EXIT_FAILURE;
    }
  }

  // case: all good till now
  if (error == HA_EXIT_SUCCESS) {
    tx = get_or_create_tx(thd);
    Rdb_perf_context_guard guard(tx, thd);

    if (explicit_snapshot) {
      tx->m_explicit_snapshot = explicit_snapshot;
    }

    DBUG_ASSERT(!tx->has_snapshot());
    tx->set_tx_read_only(true);
    rocksdb_register_tx(hton, thd, tx, false);
    tx->acquire_snapshot(true);

    // case: an explicit snapshot was not assigned to this transaction
    if (!tx->m_explicit_snapshot) {
      tx->m_explicit_snapshot =
          Rdb_explicit_snapshot::create(ss_info, rdb, tx->m_read_opts.snapshot);
      if (!tx->m_explicit_snapshot) {
        my_printf_error(ER_UNKNOWN_ERROR, "Could not create snapshot", MYF(0));
        error = HA_EXIT_FAILURE;
      }
    }
  }

  // case: unlock the binlog
  if (op == snapshot_operation::SNAPSHOT_CREATE && mysql_bin_log_is_open()) {
    mysql_bin_log_unlock_commits(ss_info);
  }

  DBUG_ASSERT(error == HA_EXIT_FAILURE || tx->m_explicit_snapshot);

  // copy over the snapshot details to pass to the upper layers
  if (tx->m_explicit_snapshot) {
    *ss_info = tx->m_explicit_snapshot->ss_info;
    ss_info->op = op;
  }

  return error;
}

/* Dummy SAVEPOINT support. This is needed for long running transactions
 * like mysqldump (https://bugs.mysql.com/bug.php?id=71017).
 * Current SAVEPOINT does not correctly handle ROLLBACK and does not return
 * errors. This needs to be addressed in future versions (Issue#96).
 */
static int rocksdb_savepoint(handlerton *const hton MY_ATTRIBUTE((__unused__)),
                             THD *const thd MY_ATTRIBUTE((__unused__)),
                             void *const savepoint MY_ATTRIBUTE((__unused__))) {
  return HA_EXIT_SUCCESS;
}

static int rocksdb_rollback_to_savepoint(
    handlerton *const hton MY_ATTRIBUTE((__unused__)), THD *const thd,
    void *const savepoint) {
  Rdb_transaction *tx = get_tx_from_thd(thd);
  return tx->rollback_to_savepoint(savepoint);
}

static bool rocksdb_rollback_to_savepoint_can_release_mdl(
    handlerton *const /* hton */, THD *const /* thd */) {
  return true;
}

static void rocksdb_get_stats(ha_statistics *stats, Rdb_tbl_def *tbl_def) {
  stats->records = 0;
  stats->index_file_length = 0ul;
  stats->data_file_length = 0ul;
  stats->mean_rec_length = 0;

  for (uint i = 0; i < tbl_def->m_key_count; i++) {
    auto key_def = tbl_def->m_key_descr_arr[i];
    if (key_def->is_primary_key()) {
      stats->data_file_length = key_def->m_stats.m_actual_disk_size;
      stats->records = key_def->m_stats.m_rows;
    } else {
      stats->index_file_length += key_def->m_stats.m_actual_disk_size;
    }
  }
}

/*
  This is called for INFORMATION_SCHEMA.TABLES
*/
static bool rocksdb_get_table_statistics(
    const char *db_name, const char *table_name,
    dd::Object_id /*se_private_id*/,
    const dd::Properties & /*ts_se_private_data*/,
    const dd::Properties & /*tbl_se_private_data*/, uint /*stat_flags*/,
    ha_statistics *stats) {
  std::string fullname = db_name;
  fullname.append(".");
  fullname.append(table_name);

  // We are called from within metadata lock MDL_EXPLICIT, so it should be
  // safe to access Rdb_tbl_def here
  auto tbl_def = ddl_manager.find(fullname);
  if (!tbl_def) {
    // Table is missing due to a possible race condition
    my_error(HA_ERR_NO_SUCH_TABLE, MYF(0), "Table is missing");
    return true;
  }

  int ret = ha_rocksdb::update_stats(stats, tbl_def);
  if (ret != HA_EXIT_SUCCESS) {
    my_error(ret, MYF(0), "Failed to update table stats");
    return true;
  }

  return false;
}

static rocksdb::Status check_rocksdb_options_compatibility(
    const char *const dbpath, const rocksdb::Options &main_opts,
    const std::vector<rocksdb::ColumnFamilyDescriptor> &cf_descr) {
  DBUG_ASSERT(rocksdb_datadir != nullptr);

  rocksdb::DBOptions loaded_db_opt;
  std::vector<rocksdb::ColumnFamilyDescriptor> loaded_cf_descs;
  rocksdb::Status status =
      LoadLatestOptions(dbpath, rocksdb::Env::Default(), &loaded_db_opt,
                        &loaded_cf_descs, rocksdb_ignore_unknown_options);

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
    return rocksdb::Status::NotSupported(
        "Mismatched size of column family "
        "descriptors.");
  }

  // Please see RocksDB documentation for more context about why we need to set
  // user-defined functions and pointer-typed options manually.
  for (size_t i = 0; i < loaded_cf_descs.size(); i++) {
    loaded_cf_descs[i].options.compaction_filter =
        cf_descr[i].options.compaction_filter;
    loaded_cf_descs[i].options.compaction_filter_factory =
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
  status = CheckOptionsCompatibility(dbpath, rocksdb::Env::Default(), main_opts,
                                     loaded_cf_descs,
                                     rocksdb_ignore_unknown_options);

  return status;
}

static uint rocksdb_partition_flags() { return (HA_CANNOT_PARTITION_FK); }

bool rdb_has_wsenv() {
#if FB_HAVE_WSENV
  return rocksdb_wsenv_path != nullptr && *rocksdb_wsenv_path;
#else
  return false;
#endif
}

bool rdb_sync_wal_supported() {
#if FB_HAVE_WSENV
  // wsenv doesn't support SyncWAL=true yet
  return !rdb_has_wsenv();
#else
  return true;
#endif
}

/* Clean up tables leftover from truncation */
void rocksdb_truncation_table_cleanup(void) {
  /* Scan for tables that have the truncation prefix */
  struct Rdb_truncate_tbls : public Rdb_tables_scanner {
   public:
    std::vector<Rdb_tbl_def *> m_tbl_list;
    int add_table(Rdb_tbl_def *tdef) override {
      DBUG_ASSERT(tdef != nullptr);
      if (tdef->base_tablename().find(TRUNCATE_TABLE_PREFIX) !=
          std::string::npos) {
        m_tbl_list.push_back(tdef);
      }
      return HA_EXIT_SUCCESS;
    }
  } collector;
  ddl_manager.scan_for_tables(&collector);

  /*
    For now, delete any table found. It's possible to rename them back,
    but there's a risk the rename can potentially lead to other inconsistencies.
    Removing the old table (which is being truncated anyway) seems to be the
    safest solution.
  */
  ha_rocksdb table(rocksdb_hton, nullptr);
  for (Rdb_tbl_def *tbl_def : collector.m_tbl_list) {
    // NO_LINT_DEBUG
    sql_print_warning("MyRocks: Removing truncated leftover table %s",
                      tbl_def->full_tablename().c_str());
    table.delete_table(tbl_def);
  }
}

/*
  Storage Engine initialization function, invoked when plugin is loaded.
*/

static int rocksdb_init_internal(void *const p) {
  DBUG_ENTER_FUNC();

#ifdef FB_HAVE_WSENV
  // Initialize WSEnv with rocksdb_ws_env_path
  if (rdb_has_wsenv()) {
    // NO_LINT_DEBUG
    sql_print_information(
        "RocksDB: Initializing WSEnvironment: "
        "rocksdb_wsenv_path = %s, rocksdb_wsenv_tenant = %s",
        rocksdb_wsenv_path, rocksdb_wsenv_tenant);

    std::string rdb_tenant(rocksdb_wsenv_tenant);
    facebook::rocks::WSEnvCreationArgs args(rdb_tenant);
    RegisterWarmStorageSimple(args);
    rocksdb::Env *ws_env = nullptr;
    auto s = rocksdb::Env::LoadEnv(rocksdb_wsenv_path, &ws_env);
    if (s.ok()) {
      rocksdb_db_options->env = ws_env;
    } else {
      rdb_log_status_error(s, "Can't initialize WSEnvironment");
      DBUG_RETURN(HA_EXIT_FAILURE);
    }
  }
#else
  if (rocksdb_wsenv_path != nullptr && *rocksdb_wsenv_path) {
    // We've turned on WSEnv in the wrong build
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: WSEnvironment not supported. ");
    DBUG_RETURN(HA_EXIT_FAILURE);
  }
#endif

  if (rdb_has_rocksdb_corruption()) {
    // NO_LINT_DEBUG
    sql_print_error(
        "RocksDB: There was a corruption detected in RockDB files. "
        "Check error log emitted earlier for more details.");
    if (rocksdb_allow_to_start_after_corruption) {
      // NO_LINT_DEBUG
      sql_print_information(
          "RocksDB: Remove rocksdb_allow_to_start_after_corruption to prevent "
          "server operating if RocksDB corruption is detected.");
    } else {
      // NO_LINT_DEBUG
      sql_print_error(
          "RocksDB: The server will exit normally and stop restart "
          "attempts. Remove %s file from data directory and "
          "start mysqld manually.",
          rdb_corruption_marker_file_name().c_str());

      // Flush error log to ensure everything is written before exit
      flush_error_log_messages();
      exit(0);
    }
  }

  DBUG_EXECUTE_IF("rocksdb_init_failure_files_corruption",
                  { DBUG_RETURN(HA_EXIT_FAILURE); });

  if (opt_rocksdb_fault_injection_options != nullptr &&
      *opt_rocksdb_fault_injection_options != '\0') {
    bool retryable;
    uint32_t failure_ratio;
    std::vector<rocksdb::FileType> types;
    if (parse_fault_injection_params(&retryable, &failure_ratio, &types)) {
      DBUG_RETURN(HA_EXIT_FAILURE);
    }

    auto fs = std::make_shared<rocksdb::FaultInjectionTestFS>(
        rocksdb_db_options->env->GetFileSystem());

    rocksdb::IOStatus error_msg = rocksdb::IOStatus::IOError("IO Error");
    error_msg.SetRetryable(retryable);

    uint32_t seed = rand();
    // NO_LINT_DEBUG
    sql_print_information(
        "RocksDB: Initializing fault injection with params (retry=%d, "
        "failure_ratio=%d, seed=%d)",
        retryable, failure_ratio, seed);
    fs->SetRandomWriteError(seed, failure_ratio, error_msg,
                            /* inject_for_all_file_types */ false, types);
    fs->EnableWriteErrorInjection();

    static auto fault_env_guard =
        std::make_shared<rocksdb::CompositeEnvWrapper>(rocksdb_db_options->env,
                                                       fs);
    rocksdb_db_options->env = fault_env_guard.get();
  }

  // Validate the assumption about the size of ROCKSDB_SIZEOF_HIDDEN_PK_COLUMN.
  static_assert(sizeof(longlong) == 8, "Assuming that longlong is 8 bytes.");

  init_rocksdb_psi_keys();

  rocksdb_hton = (handlerton *)p;

  rdb_open_tables.init();
  Ensure_cleanup rdb_open_tables_cleanup([]() { rdb_open_tables.free(); });

#ifdef HAVE_PSI_INTERFACE
  rdb_bg_thread.init(rdb_signal_bg_psi_mutex_key, rdb_signal_bg_psi_cond_key);
  rdb_drop_idx_thread.init(rdb_signal_drop_idx_psi_mutex_key,
                           rdb_signal_drop_idx_psi_cond_key);
  rdb_is_thread.init(rdb_signal_is_psi_mutex_key, rdb_signal_is_psi_cond_key);
  rdb_mc_thread.init(rdb_signal_mc_psi_mutex_key, rdb_signal_mc_psi_cond_key);
#else
  rdb_bg_thread.init();
  rdb_drop_idx_thread.init();
  rdb_is_thread.init();
  rdb_mc_thread.init();
#endif
  rdb_collation_data_mutex.init(rdb_collation_data_mutex_key,
                                MY_MUTEX_INIT_FAST);
  rdb_mem_cmp_space_mutex.init(rdb_mem_cmp_space_mutex_key, MY_MUTEX_INIT_FAST);

#if defined(HAVE_PSI_INTERFACE)
  rdb_collation_exceptions =
      new Regex_list_handler(key_rwlock_collation_exception_list);
#else
  rdb_collation_exceptions = new Regex_list_handler();
#endif

  rdb_sysvars_mutex.init(rdb_sysvars_psi_mutex_key, MY_MUTEX_INIT_FAST);
  rdb_block_cache_resize_mutex.init(rdb_block_cache_resize_mutex_key,
                                    MY_MUTEX_INIT_FAST);
  rdb_bottom_pri_background_compactions_resize_mutex.init(
      rdb_bottom_pri_background_compactions_resize_mutex_key,
      MY_MUTEX_INIT_FAST);
  Rdb_transaction::init_mutex();

  DBUG_EXECUTE_IF("rocksdb_init_failure_mutexes_initialized", {
    DBUG_RETURN(HA_EXIT_FAILURE);
  });

  rocksdb_hton->state = SHOW_OPTION_YES;
  rocksdb_hton->create = rocksdb_create_handler;
  rocksdb_hton->close_connection = rocksdb_close_connection;
  rocksdb_hton->prepare = rocksdb_prepare;
  rocksdb_hton->commit_by_xid = rocksdb_commit_by_xid;
  rocksdb_hton->rollback_by_xid = rocksdb_rollback_by_xid;
  rocksdb_hton->recover_binlog_pos = rocksdb_recover_binlog_pos;
  rocksdb_hton->update_binlog_pos = rocksdb_update_binlog_pos;
  rocksdb_hton->recover = rocksdb_recover;
  rocksdb_hton->commit = rocksdb_commit;
  rocksdb_hton->rollback = rocksdb_rollback;
  rocksdb_hton->db_type = DB_TYPE_ROCKSDB;
  rocksdb_hton->show_status = rocksdb_show_status;
  rocksdb_hton->lock_hton_log = rocksdb_lock_hton_log;
  rocksdb_hton->unlock_hton_log = rocksdb_unlock_hton_log;
  rocksdb_hton->collect_hton_log_info = rocksdb_collect_hton_log_info;
  rocksdb_hton->explicit_snapshot = rocksdb_explicit_snapshot;
  rocksdb_hton->start_consistent_snapshot =
      rocksdb_start_tx_and_assign_read_view;
  rocksdb_hton->start_shared_snapshot = rocksdb_start_tx_with_shared_read_view;
  rocksdb_hton->savepoint_set = rocksdb_savepoint;
  rocksdb_hton->savepoint_rollback = rocksdb_rollback_to_savepoint;
  rocksdb_hton->savepoint_rollback_can_release_mdl =
      rocksdb_rollback_to_savepoint_can_release_mdl;
  rocksdb_hton->get_table_statistics = rocksdb_get_table_statistics;
  rocksdb_hton->flush_logs = rocksdb_flush_wal;
  rocksdb_hton->is_user_table_blocked = rocksdb_user_table_blocked;
  rocksdb_hton->handle_single_table_select = rocksdb_handle_single_table_select;

  rocksdb_hton->flags = HTON_TEMPORARY_NOT_SUPPORTED |
                        HTON_SUPPORTS_EXTENDED_KEYS | HTON_CAN_RECREATE;

  rocksdb_hton->partition_flags = rocksdb_partition_flags;

  if (rocksdb_db_options->max_open_files > (long)open_files_limit) {
    // NO_LINT_DEBUG
    sql_print_information(
        "RocksDB: rocksdb_max_open_files should not be "
        "greater than the open_files_limit, effective value "
        "of rocksdb_max_open_files is being set to "
        "open_files_limit / 2.");
    rocksdb_db_options->max_open_files = open_files_limit / 2;
  } else if (rocksdb_db_options->max_open_files == -2) {
    rocksdb_db_options->max_open_files = open_files_limit / 2;
  }

  rdb_read_free_regex_handler.set_patterns(DEFAULT_READ_FREE_RPL_TABLES);

  rocksdb_stats = rocksdb::CreateDBStatistics();
  rocksdb_stats->set_stats_level(
      static_cast<rocksdb::StatsLevel>(rocksdb_stats_level));
  rocksdb_stats_level = rocksdb_stats->get_stats_level();
  rocksdb_db_options->statistics = rocksdb_stats;

  if (rocksdb_rate_limiter_bytes_per_sec != 0) {
    rocksdb_rate_limiter.reset(
        rocksdb::NewGenericRateLimiter(rocksdb_rate_limiter_bytes_per_sec));
    rocksdb_db_options->rate_limiter = rocksdb_rate_limiter;
  }

  rocksdb_db_options->delayed_write_rate = rocksdb_delayed_write_rate;

  std::shared_ptr<Rdb_logger> myrocks_logger = std::make_shared<Rdb_logger>();
  rocksdb::Status s = rocksdb::CreateLoggerFromOptions(
      rocksdb_datadir, *rocksdb_db_options, &rocksdb_db_options->info_log);
  if (s.ok()) {
    myrocks_logger->SetRocksDBLogger(rocksdb_db_options->info_log);
  }

  rocksdb_db_options->info_log = myrocks_logger;
  myrocks_logger->SetInfoLogLevel(
      static_cast<rocksdb::InfoLogLevel>(rocksdb_info_log_level));
  rocksdb_db_options->wal_dir = rocksdb_wal_dir;

  rocksdb_db_options->wal_recovery_mode =
      static_cast<rocksdb::WALRecoveryMode>(rocksdb_wal_recovery_mode);

  rocksdb_db_options->track_and_verify_wals_in_manifest =
      rocksdb_track_and_verify_wals_in_manifest;

  rocksdb_db_options->access_hint_on_compaction_start =
      static_cast<rocksdb::Options::AccessHint>(
          rocksdb_access_hint_on_compaction_start);

  if (rocksdb_db_options->allow_mmap_reads &&
      rocksdb_db_options->use_direct_reads) {
    // allow_mmap_reads implies !use_direct_reads and RocksDB will not open if
    // mmap_reads and direct_reads are both on.   (NO_LINT_DEBUG)
    sql_print_error(
        "RocksDB: Can't enable both use_direct_reads "
        "and allow_mmap_reads\n");
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  // Check whether the filesystem backing rocksdb_datadir allows O_DIRECT
  if (rocksdb_db_options->use_direct_reads ||
      rocksdb_db_options->use_direct_io_for_flush_and_compaction) {
    rocksdb::EnvOptions soptions;
    rocksdb::Status check_status;
    rocksdb::Env *const env = rocksdb_db_options->env;

    std::string fname = format_string("%s/DIRECT_CHECK", rocksdb_datadir);
    if (env->FileExists(fname).ok()) {
      std::unique_ptr<rocksdb::SequentialFile> file;
      soptions.use_direct_reads = true;
      check_status = env->NewSequentialFile(fname, &file, soptions);
    } else {
      std::unique_ptr<rocksdb::WritableFile> file;
      soptions.use_direct_writes = true;
      check_status = env->NewWritableFile(fname, &file, soptions);
      if (file != nullptr) {
        file->Close();
      }
      env->DeleteFile(fname);
    }

    if (!check_status.ok()) {
      // NO_LINT_DEBUG
      sql_print_error(
          "RocksDB: Unable to use direct io in rocksdb-datadir:"
          "(%s)",
          check_status.ToString().c_str());
      DBUG_RETURN(HA_EXIT_FAILURE);
    }
  }

  DBUG_EXECUTE_IF("rocksdb_init_failure_reads", {
    DBUG_RETURN(HA_EXIT_FAILURE);
  });

  if (rocksdb_db_options->allow_mmap_writes &&
      rocksdb_db_options->use_direct_io_for_flush_and_compaction) {
    // See above comment for allow_mmap_reads. (NO_LINT_DEBUG)
    sql_print_error(
        "RocksDB: Can't enable both "
        "use_direct_io_for_flush_and_compaction and "
        "allow_mmap_writes\n");
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  if (rocksdb_db_options->allow_mmap_writes &&
      rocksdb_flush_log_at_trx_commit != FLUSH_LOG_NEVER) {
    // NO_LINT_DEBUG
    sql_print_error(
        "RocksDB: rocksdb_flush_log_at_trx_commit needs to be 0 "
        "to use allow_mmap_writes");
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  // sst_file_manager will move deleted rocksdb sst files to trash_dir
  // to be deleted in a background thread.
  std::string trash_dir = std::string(rocksdb_datadir) + "/trash";
  rocksdb_db_options->sst_file_manager.reset(NewSstFileManager(
      rocksdb_db_options->env, myrocks_logger, trash_dir,
      rocksdb_sst_mgr_rate_bytes_per_sec, true /* delete_existing_trash */));

  std::vector<std::string> cf_names;
  rocksdb::Status status;
  status = rocksdb::DB::ListColumnFamilies(*rocksdb_db_options, rocksdb_datadir,
                                           &cf_names);
  DBUG_EXECUTE_IF("rocksdb_init_failure_list_cf", {
    // Simulate ListColumnFamilies failure
    status = rocksdb::Status::Corruption();
  });
  if (!status.ok()) {
    /*
      When we start on an empty datadir, ListColumnFamilies returns IOError
      with subcode = kPathNotFound.
    */
    if (status.IsPathNotFound()) {
      // NO_LINT_DEBUG
      sql_print_information(
          "RocksDB: Got kPathNotFound when listing column families");

      // NO_LINT_DEBUG
      sql_print_information(
          "RocksDB:   assuming that we're creating a new database");
    } else {
      rdb_log_status_error(status, "Error listing column families");
      DBUG_RETURN(HA_EXIT_FAILURE);
    }
  } else {
    // NO_LINT_DEBUG
    sql_print_information("RocksDB: %ld column families found",
                          cf_names.size());
  }

  std::vector<rocksdb::ColumnFamilyDescriptor> cf_descr;
  std::vector<rocksdb::ColumnFamilyHandle *> cf_handles;

  rocksdb_tbl_options->index_type =
      (rocksdb::BlockBasedTableOptions::IndexType)rocksdb_index_type;

  if (!rocksdb_tbl_options->no_block_cache) {
    std::shared_ptr<rocksdb::MemoryAllocator> memory_allocator;
    if (!rocksdb_cache_dump) {
#ifdef HAVE_JEMALLOC
      size_t block_size = rocksdb_tbl_options->block_size;
      rocksdb::JemallocAllocatorOptions alloc_opt;
      // Limit jemalloc tcache memory usage. The range
      // [block_size/4, block_size] should be enough to cover most of
      // block cache allocation sizes.
      alloc_opt.limit_tcache_size = true;
      alloc_opt.tcache_size_lower_bound = block_size / 4;
      alloc_opt.tcache_size_upper_bound = block_size;
      rocksdb::Status new_alloc_status =
          rocksdb::NewJemallocNodumpAllocator(alloc_opt, &memory_allocator);
      if (!new_alloc_status.ok()) {
        // Fallback to use default malloc/free.
        rdb_log_status_error(new_alloc_status,
                             "Error excluding block cache from core dump");
        memory_allocator = nullptr;
        DBUG_RETURN(HA_EXIT_FAILURE);
      }
#else
      // NO_LINT_DEBUG
      sql_print_warning(
          "Ignoring rocksdb_cache_dump because jemalloc is missing.");
#endif  // HAVE_JEMALLOC
    }
    std::shared_ptr<rocksdb::Cache> block_cache =
        rocksdb_use_clock_cache
            ? rocksdb::NewClockCache(rocksdb_block_cache_size)
            : rocksdb::NewLRUCache(
                  rocksdb_block_cache_size, -1 /*num_shard_bits*/,
                  false /*strict_capcity_limit*/,
                  rocksdb_cache_high_pri_pool_ratio, memory_allocator);
    if (rocksdb_sim_cache_size > 0) {
      // Simulated cache enabled
      // Wrap block cache inside a simulated cache and pass it to RocksDB
      rocksdb_tbl_options->block_cache =
          rocksdb::NewSimCache(block_cache, rocksdb_sim_cache_size, 6);
    } else {
      // Pass block cache to RocksDB
      rocksdb_tbl_options->block_cache = block_cache;
    }
  }

  if (rocksdb_collect_sst_properties) {
    properties_collector_factory =
        std::make_shared<Rdb_tbl_prop_coll_factory>(&ddl_manager);

    rocksdb_set_compaction_options(nullptr, nullptr, nullptr, nullptr);

    RDB_MUTEX_LOCK_CHECK(rdb_sysvars_mutex);

    DBUG_ASSERT(rocksdb_table_stats_sampling_pct <=
                RDB_TBL_STATS_SAMPLE_PCT_MAX);
    properties_collector_factory->SetTableStatsSamplingPct(
        rocksdb_table_stats_sampling_pct);

    RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
  }

  if (rocksdb_persistent_cache_size_mb > 0) {
    std::shared_ptr<rocksdb::PersistentCache> pcache;
    uint64_t cache_size_bytes = rocksdb_persistent_cache_size_mb * 1024 * 1024;
    status = rocksdb::NewPersistentCache(
        rocksdb::Env::Default(), std::string(rocksdb_persistent_cache_path),
        cache_size_bytes, myrocks_logger, true, &pcache);
    if (!status.ok()) {
      // NO_LINT_DEBUG
      sql_print_error("RocksDB: Persistent cache returned error: (%s)",
                      status.getState());
      DBUG_RETURN(HA_EXIT_FAILURE);
    }
    rocksdb_tbl_options->persistent_cache = pcache;
  } else if (strlen(rocksdb_persistent_cache_path)) {
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Must specify rocksdb_persistent_cache_size_mb");
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  DBUG_EXECUTE_IF("rocksdb_init_failure_cache", {
    DBUG_RETURN(HA_EXIT_FAILURE);
  });

  std::unique_ptr<Rdb_cf_options> cf_options_map(new Rdb_cf_options());
  if (!cf_options_map->init(*rocksdb_tbl_options, properties_collector_factory,
                            rocksdb_default_cf_options,
                            rocksdb_override_cf_options)) {
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Failed to initialize CF options map.");
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  DBUG_EXECUTE_IF("rocksdb_init_failure_cf_options", {
    DBUG_RETURN(HA_EXIT_FAILURE);
  });

  /*
    If there are no column families, we're creating the new database.
    Create one column family named "default".
  */
  if (cf_names.size() == 0) cf_names.push_back(DEFAULT_CF_NAME);

  std::vector<int> compaction_enabled_cf_indices;

  // NO_LINT_DEBUG
  sql_print_information("RocksDB: Column Families at start:");
  for (size_t i = 0; i < cf_names.size(); ++i) {
    rocksdb::ColumnFamilyOptions opts;
    cf_options_map->get_cf_options(cf_names[i], &opts);

    // NO_LINT_DEBUG
    sql_print_information("  cf=%s", cf_names[i].c_str());

    // NO_LINT_DEBUG
    sql_print_information("    write_buffer_size=%ld", opts.write_buffer_size);

    // NO_LINT_DEBUG
    sql_print_information("    target_file_size_base=%" PRIu64,
                          opts.target_file_size_base);

    /*
      Temporarily disable compactions to prevent a race condition where
      compaction starts before compaction filter is ready.
    */
    if (!opts.disable_auto_compactions) {
      compaction_enabled_cf_indices.push_back(i);
      opts.disable_auto_compactions = true;
    }
    cf_descr.push_back(rocksdb::ColumnFamilyDescriptor(cf_names[i], opts));
  }

  rocksdb::Options main_opts(*rocksdb_db_options,
                             cf_options_map->get_defaults());

  rocksdb::TransactionDBOptions tx_db_options;
  tx_db_options.transaction_lock_timeout = 2000;  // 2 seconds
  tx_db_options.custom_mutex_factory = std::make_shared<Rdb_mutex_factory>();
  tx_db_options.write_policy =
      static_cast<rocksdb::TxnDBWritePolicy>(rocksdb_write_policy);
  
  if (rocksdb_use_range_locking && rocksdb_use_range_lock_manager_as_point) {
    //rdb_log_status_error(
    //    status, "Can't have both range_locking and range_lock_manager_as_point");
    //DBUG_RETURN(HA_EXIT_FAILURE);
    rocksdb_use_range_lock_manager_as_point= 0;
  }


  if (rocksdb_use_range_locking) {
    range_lock_mgr.reset(
      rocksdb::NewRangeLockManager(tx_db_options.custom_mutex_factory));
    tx_db_options.lock_mgr_handle = range_lock_mgr;
  }
  if (rocksdb_use_range_lock_manager_as_point) {
    range_lock_mgr_used_as_point.reset(
      rocksdb::NewRangeLockManager(tx_db_options.custom_mutex_factory));
    tx_db_options.lock_mgr_handle = range_lock_mgr_used_as_point;
  }

  status =
      check_rocksdb_options_compatibility(rocksdb_datadir, main_opts, cf_descr);

  DBUG_EXECUTE_IF("rocksdb_init_failure_incompatible_options", {
    // Simulate ListColumnFamilies failure
    status = rocksdb::Status::Corruption();
  });

  // We won't start if we'll determine that there's a chance of data corruption
  // because of incompatible options.
  if (!status.ok()) {
    rdb_log_status_error(
        status, "Compatibility check against existing database options failed");
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  // NO_LINT_DEBUG
  sql_print_information("RocksDB: Opening TransactionDB...");
  status = rocksdb::TransactionDB::Open(
      main_opts, tx_db_options, rocksdb_datadir, cf_descr, &cf_handles, &rdb);

  DBUG_EXECUTE_IF("rocksdb_init_failure_open_db", {
    // Simulate opening TransactionDB failure
    status = rocksdb::Status::Corruption();
    // fix a memory leak caused by not calling cf_manager.init()
    for (auto cfh_ptr : cf_handles) delete (cfh_ptr);
  });

  if (!status.ok()) {
    rdb_log_status_error(status, "Error opening instance");
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  if (range_lock_mgr)
  {
    range_lock_mgr->SetMaxLockMemory(rocksdb_max_lock_memory);
    sql_print_information("RocksDB: USING RANGE LOCKING");
    sql_print_information("RocksDB: Max lock memory=%llu", rocksdb_max_lock_memory);
  }
  else
    sql_print_information("RocksDB: USING POINT LOCKING");

  cf_manager.init(std::move(cf_options_map), &cf_handles);

  // NO_LINT_DEBUG
  sql_print_information("RocksDB: Initializing data dictionary...");

  if (st_rdb_exec_time.exec("Rdb_dict_manager::init", [&]() {
        return dict_manager.init(rdb, &cf_manager,
                                 rocksdb_enable_remove_orphaned_dropped_cfs);
      })) {
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Failed to initialize data dictionary.");
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  // NO_LINT_DEBUG
  sql_print_information("RocksDB: Initializing binlog manager...");

  if (st_rdb_exec_time.exec("Rdb_binlog_manager::init", [&]() {
        return binlog_manager.init(&dict_manager);
      })) {
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Failed to initialize binlog manager.");
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  // NO_LINT_DEBUG
  sql_print_information("RocksDB: Initializing DDL Manager...");

  if (st_rdb_exec_time.exec("Rdb_ddl_manager::init", [&]() {
        return ddl_manager.init(&dict_manager, &cf_manager,
                                rocksdb_validate_tables);
      })) {
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Failed to initialize DDL manager.");
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  DBUG_EXECUTE_IF("rocksdb_init_failure_managers",
                  { DBUG_RETURN(HA_EXIT_FAILURE); });

  Rdb_sst_info::init(rdb);

  /*
    Enable auto compaction, things needed for compaction filter are finished
    initializing
  */
  std::vector<rocksdb::ColumnFamilyHandle *> compaction_enabled_cf_handles;
  compaction_enabled_cf_handles.reserve(compaction_enabled_cf_indices.size());
  for (const auto &index : compaction_enabled_cf_indices) {
    compaction_enabled_cf_handles.push_back(cf_handles[index]);
  }

  status = rdb->EnableAutoCompaction(compaction_enabled_cf_handles);

  if (!status.ok()) {
    rdb_log_status_error(status, "Error enabling compaction");
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

#ifndef HAVE_PSI_INTERFACE
  auto err = rdb_bg_thread.create_thread(BG_THREAD_NAME);
#else
  auto err = rdb_bg_thread.create_thread(BG_THREAD_NAME,
                                         rdb_background_psi_thread_key);
#endif
  if (err != 0) {
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Couldn't start the background thread: (errno=%d)",
                    err);
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

#ifndef HAVE_PSI_INTERFACE
  err = rdb_drop_idx_thread.create_thread(INDEX_THREAD_NAME);
#else
  err = rdb_drop_idx_thread.create_thread(INDEX_THREAD_NAME,
                                          rdb_drop_idx_psi_thread_key);
#endif
  if (err != 0) {
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Couldn't start the drop index thread: (errno=%d)",
                    err);
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

#ifndef HAVE_PSI_INTERFACE
  err = rdb_is_thread.create_thread(INDEX_STATS_THREAD_NAME);
#else
  err = rdb_is_thread.create_thread(INDEX_STATS_THREAD_NAME,
                                    rdb_is_psi_thread_key);
#endif
  if (err != 0) {
    // NO_LINT_DEBUG
    sql_print_error(
        "RocksDB: Couldn't start the index stats calculation thread: "
        "(errno=%d)",
        err);
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  err = rdb_mc_thread.create_thread(MANUAL_COMPACTION_THREAD_NAME
#ifdef HAVE_PSI_INTERFACE
                                    ,
                                    rdb_mc_psi_thread_key
#endif
  );
  if (err != 0) {
    // NO_LINT_DEBUG
    sql_print_error(
        "RocksDB: Couldn't start the manual compaction thread: (errno=%d)",
        err);
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  DBUG_EXECUTE_IF("rocksdb_init_failure_threads",
                  { DBUG_RETURN(HA_EXIT_FAILURE); });

  rdb_set_collation_exception_list(rocksdb_strict_collation_exceptions);

  if (rocksdb_pause_background_work) {
    rdb->PauseBackgroundWork();
  }

  // NO_LINT_DEBUG
  sql_print_information("RocksDB: global statistics using %s indexer",
                        STRINGIFY_ARG(RDB_INDEXER));
#if defined(HAVE_SCHED_GETCPU)
  if (sched_getcpu() == -1) {
    // NO_LINT_DEBUG
    sql_print_information(
        "RocksDB: sched_getcpu() failed - "
        "global statistics will use thread_id_indexer_t instead");
  }
#endif

  err = my_error_register(rdb_get_error_message, HA_ERR_ROCKSDB_FIRST,
                          HA_ERR_ROCKSDB_LAST);
  if (err != 0) {
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Couldn't initialize error messages");
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  // Creating an instance of HistogramImpl should only happen after RocksDB
  // has been successfully initialized.
  commit_latency_stats = new rocksdb::HistogramImpl();

  // Construct a list of directories which will be monitored by I/O watchdog
  // to make sure that we won't lose write access to them.
  std::vector<std::string> directories;

  // 1. Data directory.
  directories.push_back(mysql_real_data_home);

  // 2. Transaction logs.
  if (myrocks::rocksdb_wal_dir && *myrocks::rocksdb_wal_dir) {
    directories.push_back(myrocks::rocksdb_wal_dir);
  }

  io_watchdog = new Rdb_io_watchdog(std::move(directories));
  io_watchdog->reset_timeout(rocksdb_io_write_timeout_secs);

  compaction_stats.resize_history(rocksdb_max_compaction_history);

  // Remove tables that may have been leftover during truncation
  rocksdb_truncation_table_cleanup();

  // NO_LINT_DEBUG
  sql_print_information(
      "MyRocks storage engine plugin has been successfully "
      "initialized.");

  st_rdb_exec_time.report();

  // Skip cleaning up rdb_open_tables as we've succeeded
  rdb_open_tables_cleanup.skip();

  rocksdb_set_max_bottom_pri_background_compactions_internal(
      rocksdb_max_bottom_pri_background_compactions);

  DBUG_EXECUTE_IF("rocksdb_init_failure_everything_initialized",
                  { DBUG_RETURN(HA_EXIT_FAILURE); });

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

// This function cleans up after both normal and init-failure scenarios.
// It can be unsafe to perform some cleanup after initalization failure,
// so currently only some code is executed for such scenarios.
// More code will be safeguarded and executed for both scenarios, and
// eventually minimalShutdown parameter will be removed (always = false).
// @parameter minimalShutdown - only perform uninitialization deemed safe
// (experimentation).
static int rocksdb_shutdown(bool minimalShutdown) {
  int error = 0;

  if (!minimalShutdown) {
    // signal the drop index thread to stop
    rdb_drop_idx_thread.signal(true);

    // Flush all memtables for not losing data, even if WAL is disabled.
    rocksdb_flush_all_memtables();

    // Stop all rocksdb background work
    if (rdb && rdb->GetBaseDB()) {
      CancelAllBackgroundWork(rdb->GetBaseDB(), true);
    }

    // Signal the background thread to stop and to persist all stats collected
    // from background flushes and compactions. This will add more keys to a new
    // memtable, but since the memtables were just flushed, it should not
    // trigger a flush that can stall due to background threads being stopped.
    // As long as these keys are stored in a WAL file, they can be retrieved on
    // restart.
    rdb_bg_thread.signal(true);

    // signal the index stats calculation thread to stop
    rdb_is_thread.signal(true);

    // signal the manual compaction thread to stop
    rdb_mc_thread.signal(true);

    // Wait for the background thread to finish.
    // NO_LINT_DEBUG
    sql_print_information("Waiting for MyRocks background to finish");
    auto err = rdb_bg_thread.join();
    if (err != 0) {
      // We'll log the message and continue because we're shutting down and
      // continuation is the optimal strategy.
      // NO_LINT_DEBUG
      sql_print_error(
          "RocksDB: Couldn't stop the background thread: (errno=%d)", err);
    }

    // Wait for the drop index thread to finish.
    // NO_LINT_DEBUG
    sql_print_information("Waiting for MyRocks drop index thread to finish");
    err = rdb_drop_idx_thread.join();
    if (err != 0) {
      // NO_LINT_DEBUG
      sql_print_error("RocksDB: Couldn't stop the index thread: (errno=%d)",
                      err);
    }

    // Wait for the index stats calculation thread to finish.
    // NO_LINT_DEBUG
    sql_print_information("Waiting for MyRocks index stats thread to finish");
    err = rdb_is_thread.join();
    if (err != 0) {
      // NO_LINT_DEBUG
      sql_print_error(
          "RocksDB: Couldn't stop the index stats calculation thread: "
          "(errno=%d)",
          err);
    }

    // Wait for the manual compaction thread to finish.
    // NO_LINT_DEBUG
    sql_print_information("Waiting for MyRocks compaction thread to finish");
    err = rdb_mc_thread.join();
    if (err != 0) {
      // NO_LINT_DEBUG
      sql_print_error(
          "RocksDB: Couldn't stop the manual compaction thread: (errno=%d)",
          err);
    }

    if (rdb_open_tables.count()) {
      // Looks like we are getting unloaded and yet we have some open tables
      // left behind.
      sql_print_error("RocksDB: there're tables still opened during shutdown");
      error = 1;
    }

    rdb_open_tables.free();
  }

  // The only code executed during both normal and init faiure shutdown are
  // deleting mutexes. The plan is to gradually increase this section so there's
  // one single shutdown function for all scenarios.

  rdb_sysvars_mutex.destroy();
  rdb_block_cache_resize_mutex.destroy();
  rdb_bottom_pri_background_compactions_resize_mutex.destroy();

  if (!minimalShutdown) {
    delete rdb_collation_exceptions;
    rdb_collation_exceptions = nullptr;
  }

  rdb_collation_data_mutex.destroy();
  rdb_mem_cmp_space_mutex.destroy();

  Rdb_transaction::term_mutex();

  if (!minimalShutdown) {
    for (auto &it : rdb_collation_data) {
      delete it;
      it = nullptr;
    }

    ddl_manager.cleanup();
    binlog_manager.cleanup();
    dict_manager.cleanup();
    cf_manager.cleanup();

    delete rdb;
    rdb = nullptr;

    delete commit_latency_stats;
    commit_latency_stats = nullptr;

    delete io_watchdog;
    io_watchdog = nullptr;

    // Disown the cache data since we're shutting down.
    // This results in memory leaks but it improved the shutdown time.
    // Don't disown when running under valgrind
#ifndef HAVE_purify
    if (rocksdb_tbl_options->block_cache) {
      rocksdb_tbl_options->block_cache->DisownData();
    }
#endif /* HAVE_purify */
  }

  rocksdb_db_options = nullptr;
  rocksdb_tbl_options = nullptr;
  rocksdb_stats = nullptr;

  if (!minimalShutdown) {
    my_error_unregister(HA_ERR_ROCKSDB_FIRST, HA_ERR_ROCKSDB_LAST);
  }

  return error;
}

/*
  Storage Engine deinitialization function, invoked when plugin is unloaded.
*/

static int rocksdb_done_func(void *const p MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  int error = rocksdb_shutdown(/* minimalShutdown = */ false);

  DBUG_RETURN(error);
}

static int rocksdb_init_func(void *const p) {
  int ret = rocksdb_init_internal(p);
  if (ret) {
    rocksdb_done_func(p);
  }
  return ret;
}

// If the iterator is not valid it might be because of EOF but might be due
// to IOError or corruption. The good practice is always check it.
// https://github.com/facebook/rocksdb/wiki/Iterator#error-handling
bool is_valid_iterator(rocksdb::Iterator *scan_it) {
  if (scan_it->Valid()) {
    return true;
  } else {
    rocksdb::Status s = scan_it->status();
    DBUG_EXECUTE_IF("rocksdb_return_status_corrupted",
                    dbug_change_status_to_corrupted(&s););
    if (s.IsIOError() || s.IsCorruption()) {
      if (s.IsCorruption()) {
        rdb_persist_corruption_marker();
      }
      rdb_handle_io_error(s, RDB_IO_ERROR_GENERAL);
    }
    return false;
  }
}

/**
  @brief
  Example of simple lock controls. The "table_handler" it creates is a
  structure we will pass to each ha_rocksdb handler. Do you have to have
  one of these? Well, you have pieces that are used for locking, and
  they are needed to function.
*/

Rdb_table_handler *Rdb_open_tables_map::get_table_handler(
    const char *const table_name) {
  DBUG_ASSERT(table_name != nullptr);

  Rdb_table_handler *table_handler;

  std::string table_name_str(table_name);

  // First, look up the table in the hash map.
  RDB_MUTEX_LOCK_CHECK(m_mutex);
  const auto it = m_table_map.find(table_name_str);
  if (it != m_table_map.end()) {
    // Found it
    table_handler = it->second;
  } else {
    char *tmp_name;

    // Since we did not find it in the hash map, attempt to create and add it
    // to the hash map.
    if (!(table_handler = reinterpret_cast<Rdb_table_handler *>(
              my_multi_malloc(PSI_NOT_INSTRUMENTED, MYF(MY_WME | MY_ZEROFILL),
                              &table_handler, sizeof(*table_handler), &tmp_name,
                              table_name_str.length() + 1, NullS)))) {
      // Allocating a new Rdb_table_handler and a new table name failed.
      RDB_MUTEX_UNLOCK_CHECK(m_mutex);
      return nullptr;
    }

    table_handler->m_ref_count = 0;
    table_handler->m_table_name_length = table_name_str.length();
    table_handler->m_table_name = tmp_name;
    strxmov(table_handler->m_table_name, table_name, NullS);

    m_table_map.emplace(table_name_str, table_handler);

    thr_lock_init(&table_handler->m_thr_lock);
    table_handler->m_io_perf_read.init();
    table_handler->m_io_perf_write.init();
  }
  DBUG_ASSERT(table_handler->m_ref_count >= 0);
  table_handler->m_ref_count++;

  RDB_MUTEX_UNLOCK_CHECK(m_mutex);

  return table_handler;
}

std::vector<std::string> rdb_get_open_table_names(void) {
  return rdb_open_tables.get_table_names();
}

std::vector<std::string> Rdb_open_tables_map::get_table_names(void) const {
  const Rdb_table_handler *table_handler;
  std::vector<std::string> names;

  RDB_MUTEX_LOCK_CHECK(m_mutex);
  for (const auto &kv : m_table_map) {
    table_handler = kv.second;
    DBUG_ASSERT(table_handler != nullptr);
    names.push_back(table_handler->m_table_name);
  }
  RDB_MUTEX_UNLOCK_CHECK(m_mutex);

  return names;
}

void ha_rocksdb::load_auto_incr_value() {
  ulonglong auto_incr = 0;
  bool validate_last = false, use_datadic = true;
#ifndef DBUG_OFF
  DBUG_EXECUTE_IF("myrocks_autoinc_upgrade", use_datadic = false;);
  validate_last = true;
#endif

  if (use_datadic && dict_manager.get_auto_incr_val(
                         m_tbl_def->get_autoincr_gl_index_id(), &auto_incr)) {
    update_auto_incr_val(auto_incr);
  }

  // If we find nothing in the data dictionary, or if we are in debug mode,
  // then call index_last to get the last value.
  //
  // This is needed when upgrading from a server that did not support
  // persistent auto_increment, of if the table is empty.
  //
  // For debug mode, we are just verifying that the data dictionary value is
  // greater than or equal to the maximum value in the table.
  if (auto_incr == 0 || validate_last) {
    auto_incr = load_auto_incr_value_from_index();
    update_auto_incr_val(auto_incr);
  }

  // If we failed to find anything from the data dictionary and index, then
  // initialize auto_increment to 1.
  if (m_tbl_def->m_auto_incr_val == 0) {
    update_auto_incr_val(1);
  }
}

ulonglong ha_rocksdb::load_auto_incr_value_from_index() {
  const int save_active_index = active_index;
  active_index = table->s->next_number_index;
  const uint8 save_table_status = table->m_status;

  DBUG_ASSERT(!m_key_descr_arr[active_index_pos()]->is_partial_index());
  std::unique_ptr<Rdb_iterator> save_iterator(new Rdb_iterator_base(
      ha_thd(), m_key_descr_arr[active_index_pos()], m_pk_descr, m_tbl_def));
  std::swap(m_iterator, save_iterator);

  ulonglong last_val = 0;

  Rdb_transaction *const tx = get_or_create_tx(table->in_use);
  const bool is_new_snapshot = !tx->has_snapshot();
  if (is_new_snapshot) {
    tx->acquire_snapshot(true);
  }

  // Do a lookup. We only need index column, so it should be index-only.
  // (another reason to make it index-only is that table->read_set is not set
  // appropriately and non-index-only lookup will not read the value)
  const bool save_keyread_only = m_keyread_only;
  m_keyread_only = true;
  m_converter->set_is_key_requested(true);

  if (!index_last(table->record[0])) {
    Field *field =
        table->key_info[table->s->next_number_index].key_part[0].field;
    ulonglong max_val = field->get_max_int_value();
    my_bitmap_map *const old_map =
        dbug_tmp_use_all_columns(table, table->read_set);
    last_val = field->val_int();
    if (last_val != max_val) {
      last_val++;
    }
#ifndef DBUG_OFF
    ulonglong dd_val;
    if (last_val <= max_val) {
      const auto &gl_index_id = m_tbl_def->get_autoincr_gl_index_id();
      if (dict_manager.get_auto_incr_val(gl_index_id, &dd_val) &&
          tx->get_auto_incr(gl_index_id) == 0) {
        DBUG_ASSERT(dd_val >= last_val);
      }
    }
#endif
    dbug_tmp_restore_column_map(table->read_set, old_map);
  }

  m_keyread_only = save_keyread_only;
  if (is_new_snapshot) {
    tx->release_snapshot();
  }

  table->m_status = save_table_status;
  active_index = save_active_index;

  /*
    Do what ha_rocksdb::index_end() does.
    (Why don't we use index_init/index_end? class handler defines index_init
    as private, for some reason).
    */
  std::swap(m_iterator, save_iterator);

  return last_val;
}

void ha_rocksdb::update_auto_incr_val(ulonglong val) {
  ulonglong auto_incr_val = m_tbl_def->m_auto_incr_val;
  while (
      auto_incr_val < val &&
      !m_tbl_def->m_auto_incr_val.compare_exchange_weak(auto_incr_val, val)) {
    // Do nothing - just loop until auto_incr_val is >= val or we successfully
    // set it
  }
}

void ha_rocksdb::update_auto_incr_val_from_field() {
  Field *field;
  ulonglong new_val, max_val;
  field = table->key_info[table->s->next_number_index].key_part[0].field;
  max_val = field->get_max_int_value();

  my_bitmap_map *const old_map =
      dbug_tmp_use_all_columns(table, table->read_set);
  new_val = field->val_int();
  // don't increment if we would wrap around
  if (new_val != max_val) {
    new_val++;
  }

  dbug_tmp_restore_column_map(table->read_set, old_map);

  // Only update if positive value was set for auto_incr column.
  if (new_val <= max_val) {
    Rdb_transaction *const tx = get_or_create_tx(table->in_use);
    tx->set_auto_incr(m_tbl_def->get_autoincr_gl_index_id(), new_val);

    // Update the in memory auto_incr value in m_tbl_def.
    update_auto_incr_val(new_val);
  }
}

int ha_rocksdb::load_hidden_pk_value() {
  const int save_active_index = active_index;
  active_index = MAX_KEY;
  const uint8 save_table_status = table->m_status;

  std::unique_ptr<Rdb_iterator> save_iterator(new Rdb_iterator_base(
      ha_thd(), m_key_descr_arr[active_index_pos()], m_pk_descr, m_tbl_def));
  std::swap(m_iterator, save_iterator);

  Rdb_transaction *const tx = get_or_create_tx(table->in_use);
  const bool is_new_snapshot = !tx->has_snapshot();

  longlong hidden_pk_id = 1;
  longlong old = 0;
  int rc = 0;
  // Do a lookup.
  if (!index_last(table->record[0])) {
    /*
      Decode PK field from the key
    */
    rc = read_hidden_pk_id_from_rowkey(&hidden_pk_id);
    if (rc) {
      goto exit;
    }

    hidden_pk_id++;
  }

  old = m_tbl_def->m_hidden_pk_val;
  while (old < hidden_pk_id &&
         !m_tbl_def->m_hidden_pk_val.compare_exchange_weak(old, hidden_pk_id)) {
  }

exit:
  if (is_new_snapshot) {
    tx->release_snapshot();
  }

  table->m_status = save_table_status;
  active_index = save_active_index;
  std::swap(m_iterator, save_iterator);

  return rc;
}

/* Get PK value from m_tbl_def->m_hidden_pk_info. */
longlong ha_rocksdb::update_hidden_pk_val() {
  DBUG_ASSERT(has_hidden_pk(table));
  const longlong new_val = m_tbl_def->m_hidden_pk_val++;
  return new_val;
}

/* Get the id of the hidden pk id from m_last_rowkey */
int ha_rocksdb::read_hidden_pk_id_from_rowkey(longlong *const hidden_pk_id) {
  DBUG_ASSERT(table != nullptr);
  DBUG_ASSERT(has_hidden_pk(table));

  rocksdb::Slice rowkey_slice(m_last_rowkey.ptr(), m_last_rowkey.length());

  // Get hidden primary key from old key slice
  Rdb_string_reader reader(&rowkey_slice);
  if ((!reader.read(Rdb_key_def::INDEX_NUMBER_SIZE))) {
    return HA_ERR_ROCKSDB_CORRUPT_DATA;
  }

  const int length = Field_longlong::PACK_LENGTH;
  const uchar *from = reinterpret_cast<const uchar *>(reader.read(length));
  if (from == nullptr) {
    /* Mem-comparable image doesn't have enough bytes */
    return HA_ERR_ROCKSDB_CORRUPT_DATA;
  }

  *hidden_pk_id = rdb_netbuf_read_uint64(&from);
  return HA_EXIT_SUCCESS;
}

/**
  @brief
  Free lock controls. We call this whenever we close a table. If the table had
  the last reference to the table_handler, then we free the memory associated
  with it.
*/

void Rdb_open_tables_map::release_table_handler(
    Rdb_table_handler *const table_handler) {
  RDB_MUTEX_LOCK_CHECK(m_mutex);

  DBUG_ASSERT(table_handler != nullptr);
  DBUG_ASSERT(table_handler->m_ref_count > 0);
  if (!--table_handler->m_ref_count) {
    // Last rereference was released. Tear down the hash entry.
    const auto ret MY_ATTRIBUTE((__unused__)) =
        m_table_map.erase(std::string(table_handler->m_table_name));
    DBUG_ASSERT(ret == 1);  // the hash entry must actually be found and deleted
    my_core::thr_lock_delete(&table_handler->m_thr_lock);
    my_free(table_handler);
  }

  RDB_MUTEX_UNLOCK_CHECK(m_mutex);
}

static handler *rocksdb_create_handler(my_core::handlerton *const hton,
                                       my_core::TABLE_SHARE *const table_arg,
                                       bool partitioned,
                                       my_core::MEM_ROOT *const mem_root) {
  if (partitioned) {
    ha_rockspart *file = new (mem_root) ha_rockspart(hton, table_arg);
    if (file && file->init_partitioning(mem_root)) {
      destroy(file);
      return (nullptr);
    }
    return (file);
  }

  return new (mem_root) ha_rocksdb(hton, table_arg);
}

ha_rocksdb::ha_rocksdb(my_core::handlerton *const hton,
                       my_core::TABLE_SHARE *const table_arg)
    : handler(hton, table_arg),
      m_table_handler(nullptr),
      m_tbl_def(nullptr),
      m_pk_descr(nullptr),
      m_key_descr_arr(nullptr),
      m_pk_can_be_decoded(false),
      m_pk_packed_tuple(nullptr),
      m_sk_packed_tuple(nullptr),
      m_end_key_packed_tuple(nullptr),
      m_sk_packed_tuple_old(nullptr),
      m_pack_buffer(nullptr),
      m_lock_rows(RDB_LOCK_NONE),
      m_use_range_locking(false),
      m_keyread_only(false),
      m_iteration_only(false),
      m_insert_with_update(false),
      m_dup_key_found(false),
      mrr_rowid_reader(nullptr),
      mrr_n_elements(0),
      mrr_enabled_keyread(false),
      mrr_used_cpk(false),
      m_in_rpl_delete_rows(false),
      m_in_rpl_update_rows(false),
      m_need_build_decoder(false) {}

ha_rocksdb::~ha_rocksdb() {
  int err MY_ATTRIBUTE((__unused__));
  err = finalize_bulk_load(false);
  if (err != 0) {
    // NO_LINT_DEBUG
    sql_print_error(
        "RocksDB: Error %d finalizing bulk load while closing "
        "handler.",
        err);
  }
}

const std::string &ha_rocksdb::get_table_basename() const {
  return m_tbl_def->base_tablename();
}

/**
  @return
    false  OK
    other  Error inpacking the data
*/
bool ha_rocksdb::init_with_fields() {
  DBUG_ENTER_FUNC();

  const uint pk = table_share->primary_key;
  if (pk != MAX_KEY) {
    const uint key_parts = table_share->key_info[pk].user_defined_key_parts;
    check_keyread_allowed(m_pk_can_be_decoded, table_share, pk /*PK*/,
                          key_parts - 1, true);
  } else {
    m_pk_can_be_decoded = false;
  }
  cached_table_flags = table_flags();

  DBUG_RETURN(false); /* Ok */
}

/*
  If the key is a TTL key, we may need to filter it out.

  The purpose of read filtering for tables with TTL is to ensure that
  during a transaction a key which has expired already but not removed by
  compaction yet is not returned to the user.

  Without this the user might be hit with problems such as disappearing
  rows within a transaction, etc, because the compaction filter ignores
  snapshots when filtering keys.
*/
bool rdb_should_hide_ttl_rec(const Rdb_key_def &kd,
                             const rocksdb::Slice &ttl_rec_val,
                             Rdb_transaction *tx) {
  DBUG_ASSERT(kd.has_ttl());
  DBUG_ASSERT(kd.m_ttl_rec_offset != UINT_MAX);
  THD *thd = tx->get_thd();
  const int64_t curr_ts = tx->m_snapshot_timestamp;

  /*
    Curr_ts can only be 0 if there are no snapshots open.
    should_hide_ttl_rec can only be called when there is >=1 snapshots, unless
    we are filtering on the write path (single INSERT/UPDATE) in which case
    we are passed in the current time as curr_ts.

    In the event curr_ts is 0, we always decide not to filter the record. We
    also log a warning and increment a diagnostic counter.
  */
  if (curr_ts == 0) {
    DBUG_ASSERT(false);
    push_warning_printf(thd, Sql_condition::SL_WARNING, ER_WRONG_ARGUMENTS,
                        "TTL read filtering called with no snapshot.");
    rdb_update_global_stats(ROWS_UNFILTERED_NO_SNAPSHOT, 1);
    return false;
  }

  if (!rdb_is_ttl_read_filtering_enabled() || !rdb_is_ttl_enabled()) {
    return false;
  }

  Rdb_string_reader reader(&ttl_rec_val);

  /*
    Find where the 8-byte ttl is for each record in this index.
  */
  uint64 ts;
  if (!reader.read(kd.m_ttl_rec_offset) || reader.read_uint64(&ts)) {
    /*
      This condition should never be reached since all TTL records have an
      8 byte ttl field in front. Don't filter the record out, and log an error.
    */
    std::string buf;
    buf = rdb_hexdump(ttl_rec_val.data(), ttl_rec_val.size(),
                      RDB_MAX_HEXDUMP_LEN);
    const GL_INDEX_ID gl_index_id = kd.get_gl_index_id();
    // NO_LINT_DEBUG
    sql_print_error(
        "Decoding ttl from PK value failed, "
        "for index (%u,%u), val: %s",
        gl_index_id.cf_id, gl_index_id.index_id, buf.c_str());
    DBUG_ASSERT(0);
    return false;
  }

  /* Hide record if it has expired before the current snapshot time. */
  uint64 read_filter_ts = 0;
#ifndef DBUG_OFF
  read_filter_ts += rdb_dbug_set_ttl_read_filter_ts();
#endif
  bool is_hide_ttl =
      ts + kd.m_ttl_duration + read_filter_ts <= static_cast<uint64>(curr_ts);
  if (is_hide_ttl) {
    rdb_update_global_stats(ROWS_FILTERED, 1);

    /* increment examined row count when rows are skipped */
    thd->inc_examined_row_count(1);
    DEBUG_SYNC(thd, "rocksdb.ttl_rows_examined");
  }
  return is_hide_ttl;
}

#ifndef DBUG_OFF
void dbug_append_garbage_at_end(rocksdb::PinnableSlice *on_disk_rec) {
  std::string str(on_disk_rec->data(), on_disk_rec->size());
  on_disk_rec->Reset();
  str.append("abc");
  on_disk_rec->PinSelf(rocksdb::Slice(str));
}

void dbug_truncate_record(rocksdb::PinnableSlice *on_disk_rec) {
  on_disk_rec->remove_suffix(on_disk_rec->size());
}

void dbug_modify_rec_varchar12(rocksdb::PinnableSlice *on_disk_rec) {
  std::string res;
  // The record is NULL-byte followed by VARCHAR(10).
  // Put the NULL-byte
  res.append("\0", 1);
  // Then, add a valid VARCHAR(12) value.
  res.append("\xC", 1);
  res.append("123456789ab", 12);

  on_disk_rec->Reset();
  on_disk_rec->PinSelf(rocksdb::Slice(res));
}

void dbug_create_err_inplace_alter() {
  my_printf_error(ER_UNKNOWN_ERROR,
                  "Intentional failure in inplace alter occurred.", MYF(0));
}
#endif

int ha_rocksdb::convert_record_from_storage_format(
    const rocksdb::Slice *const key, uchar *const buf) {
  DBUG_EXECUTE_IF("myrocks_simulate_bad_row_read1",
                  dbug_append_garbage_at_end(&m_retrieved_record););
  DBUG_EXECUTE_IF("myrocks_simulate_bad_row_read2",
                  dbug_truncate_record(&m_retrieved_record););
  DBUG_EXECUTE_IF("myrocks_simulate_bad_row_read3",
                  dbug_modify_rec_varchar12(&m_retrieved_record););

  return convert_record_from_storage_format(key, &m_retrieved_record, buf);
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
    rdb_converter::setup_read_decoders()  Sets up data structures which tell
  which columns to decode.

  @return
    0      OK
    other  Error inpacking the data
*/

int ha_rocksdb::convert_record_from_storage_format(
    const rocksdb::Slice *const key, const rocksdb::Slice *const value,
    uchar *const buf) {
  return m_converter->decode(m_pk_descr, buf, key, value);
}

int ha_rocksdb::alloc_key_buffers(const TABLE *const table_arg,
                                  const Rdb_tbl_def *const tbl_def_arg) {
  DBUG_ENTER_FUNC();

  std::shared_ptr<Rdb_key_def> *const kd_arr = tbl_def_arg->m_key_descr_arr;

  uint max_packed_sk_len = 0;
  uint pack_key_len = 0;

  m_pk_descr = kd_arr[pk_index(table_arg, tbl_def_arg)];

  // move this into get_table_handler() ??
  m_pk_descr->setup(table_arg, tbl_def_arg);

  pack_key_len = m_pk_descr->max_storage_fmt_length();
  m_pk_packed_tuple = reinterpret_cast<uchar *>(
      my_malloc(PSI_NOT_INSTRUMENTED, pack_key_len, MYF(0)));

  /* Sometimes, we may use m_sk_packed_tuple for storing packed PK */
  max_packed_sk_len = pack_key_len;
  for (uint i = 0; i < table_arg->s->keys; i++) {
    /* Primary key was processed above */
    if (i == table_arg->s->primary_key) continue;

    // TODO: move this into get_table_handler() ??
    kd_arr[i]->setup(table_arg, tbl_def_arg);

    const uint packed_len = kd_arr[i]->max_storage_fmt_length();
    if (packed_len > max_packed_sk_len) {
      max_packed_sk_len = packed_len;
    }
  }

  m_sk_packed_tuple = reinterpret_cast<uchar *>(
      my_malloc(PSI_NOT_INSTRUMENTED, max_packed_sk_len, MYF(0)));
  m_sk_packed_tuple_old = reinterpret_cast<uchar *>(
      my_malloc(PSI_NOT_INSTRUMENTED, max_packed_sk_len, MYF(0)));
  m_end_key_packed_tuple = reinterpret_cast<uchar *>(
      my_malloc(PSI_NOT_INSTRUMENTED, max_packed_sk_len, MYF(0)));
  m_pack_buffer = reinterpret_cast<uchar *>(
      my_malloc(PSI_NOT_INSTRUMENTED, max_packed_sk_len, MYF(0)));

  if (m_pk_packed_tuple == nullptr || m_sk_packed_tuple == nullptr ||
      m_sk_packed_tuple_old == nullptr || m_end_key_packed_tuple == nullptr ||
      m_pack_buffer == nullptr) {
    // One or more of the above allocations failed.  Clean up and exit
    free_key_buffers();

    DBUG_RETURN(HA_ERR_OUT_OF_MEM);
  }

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

void ha_rocksdb::free_key_buffers() {
  my_free(m_pk_packed_tuple);
  m_pk_packed_tuple = nullptr;

  my_free(m_sk_packed_tuple);
  m_sk_packed_tuple = nullptr;

  my_free(m_sk_packed_tuple_old);
  m_sk_packed_tuple_old = nullptr;

  my_free(m_end_key_packed_tuple);
  m_end_key_packed_tuple = nullptr;

  my_free(m_pack_buffer);
  m_pack_buffer = nullptr;

  release_blob_buffer();
}

/**
  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::open(const char *const name,
                     int mode MY_ATTRIBUTE((__unused__)),
                     uint test_if_locked MY_ATTRIBUTE((__unused__)),
                     const dd::Table *table_def MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  int err = close();
  if (err) {
    DBUG_RETURN(err);
  }

  m_table_handler = rdb_open_tables.get_table_handler(name);

  if (m_table_handler == nullptr) {
    DBUG_RETURN(HA_ERR_OUT_OF_MEM);
  }

  my_core::thr_lock_data_init(&m_table_handler->m_thr_lock, &m_db_lock,
                              nullptr);
  m_io_perf.init(&m_table_handler->m_table_perf_context,
                 &m_table_handler->m_io_perf_read,
                 &m_table_handler->m_io_perf_write, &stats);
  Rdb_perf_context_guard guard(&m_io_perf, ha_thd());

  std::string fullname;
  err = rdb_normalize_tablename(name, &fullname);
  if (err != HA_EXIT_SUCCESS) {
    rdb_open_tables.release_table_handler(m_table_handler);
    DBUG_RETURN(err);
  }

  m_tbl_def = ddl_manager.find(fullname);
  if (m_tbl_def == nullptr) {
    rdb_open_tables.release_table_handler(m_table_handler);
    my_error(ER_INTERNAL_ERROR, MYF(0),
             "Attempt to open a table that is not present in RocksDB-SE data "
             "dictionary");
    DBUG_RETURN(HA_ERR_ROCKSDB_INVALID_TABLE);
  }

  m_lock_rows = RDB_LOCK_NONE;
  m_use_range_locking = false;
  m_locked_row_action = THR_WAIT;
  m_key_descr_arr = m_tbl_def->m_key_descr_arr;

  /*
    Full table scan actually uses primary key
    (UPDATE needs to know this, otherwise it will go into infinite loop on
    queries like "UPDATE tbl SET pk=pk+100")
  */
  key_used_on_scan = table->s->primary_key;

  // close() above has already called free_key_buffers(). No need to do it here.
  err = alloc_key_buffers(table, m_tbl_def);

  if (err) {
    rdb_open_tables.release_table_handler(m_table_handler);
    DBUG_RETURN(err);
  }

  /*
    init_with_fields() is used to initialize table flags based on the field
    definitions in table->field[].
    It is called by open_binary_frm(), but that function calls the method for
    a temporary ha_rocksdb object which is later destroyed.

    If we are here in ::open(), then init_with_fields() has not been called
    for this object. Call it ourselves, we want all member variables to be
    properly initialized.
  */
  init_with_fields();

  /* Initialize decoder */
  m_converter.reset(new Rdb_converter(ha_thd(), m_tbl_def, table));

  /*
     Update m_ttl_bytes address to same as Rdb_converter's m_ttl_bytes.
     Remove this code after moving convert_record_to_storage_format() into
     Rdb_converter class.
  */
  m_ttl_bytes = m_converter->get_ttl_bytes_buffer();

  info(HA_STATUS_NO_LOCK | HA_STATUS_VARIABLE | HA_STATUS_CONST);

  /*
    The following load_XXX code calls row decode functions, and they do
    that without having done ::external_lock() or index_init()/rnd_init().
    (Note: this also means we're doing a read when there was no
    rdb_converter::setup_field_encoders() call)

    Initialize the necessary variables for them:
  */

  /* Load auto_increment value only once on first use. */
  if (table->found_next_number_field && m_tbl_def->m_auto_incr_val == 0) {
    load_auto_incr_value();
  }

  /* Load hidden pk only once on first use. */
  if (has_hidden_pk(table) && m_tbl_def->m_hidden_pk_val == 0 &&
      (err = load_hidden_pk_value()) != HA_EXIT_SUCCESS) {
    rdb_open_tables.release_table_handler(m_table_handler);
    free_key_buffers();
    DBUG_RETURN(err);
  }

  /* Index block size in MyRocks: used by MySQL in query optimization */
  stats.block_size = rocksdb_tbl_options->block_size;

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

int ha_rocksdb::close(void) {
  DBUG_ENTER_FUNC();

  m_pk_descr = nullptr;
  m_key_descr_arr = nullptr;
  m_converter = nullptr;
  m_iterator = nullptr;
  free_key_buffers();

  if (m_table_handler != nullptr) {
    rdb_open_tables.release_table_handler(m_table_handler);
    m_table_handler = nullptr;
  }

  // These are needed to suppress valgrind errors in rocksdb.partition
  m_last_rowkey.mem_free();
  m_sk_tails.free();
  m_sk_tails_old.free();
  m_pk_unpack_info.free();

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

static const char *rdb_error_messages[] = {
    "Table must have a PRIMARY KEY.",
    "Specifying DATA DIRECTORY for an individual table is not supported.",
    "Specifying INDEX DIRECTORY for an individual table is not supported.",
    "RocksDB commit failed.",
    "Failure during bulk load operation.",
    "Found data corruption.",
    "CRC checksum mismatch.",
    "Invalid table.",
    "Could not access RocksDB properties.",
    "File I/O error during merge/sort operation.",
    "RocksDB status: not found.",
    "RocksDB status: corruption.",
    "RocksDB status: not supported.",
    "RocksDB status: invalid argument.",
    "RocksDB status: io error.",
    "RocksDB status: no space.",
    "RocksDB status: merge in progress.",
    "RocksDB status: incomplete.",
    "RocksDB status: shutdown in progress.",
    "RocksDB status: timed out.",
    "RocksDB status: aborted.",
    "RocksDB status: lock limit reached.",
    "RocksDB status: busy.",
    "RocksDB status: deadlock.",
    "RocksDB status: expired.",
    "RocksDB status: try again.",
};

static_assert((sizeof(rdb_error_messages) / sizeof(rdb_error_messages[0])) ==
                  ((HA_ERR_ROCKSDB_LAST - HA_ERR_ROCKSDB_FIRST) + 1),
              "Number of error messages doesn't match number of error codes");

static const char *rdb_get_error_message(int nr) {
  return rdb_error_messages[nr - HA_ERR_ROCKSDB_FIRST];
}

bool ha_rocksdb::get_error_message(const int error, String *const buf) {
  DBUG_ENTER_FUNC();

  static_assert(HA_ERR_ROCKSDB_LAST > HA_ERR_FIRST,
                "HA_ERR_ROCKSDB_LAST > HA_ERR_FIRST");
  static_assert(HA_ERR_ROCKSDB_LAST > HA_ERR_LAST,
                "HA_ERR_ROCKSDB_LAST > HA_ERR_LAST");

  if (error == HA_ERR_LOCK_WAIT_TIMEOUT || error == HA_ERR_LOCK_DEADLOCK ||
      error == HA_ERR_ROCKSDB_STATUS_BUSY) {
    Rdb_transaction *const tx = get_tx_from_thd(ha_thd());
    DBUG_ASSERT(tx != nullptr);
    buf->append(tx->m_detailed_error);
    DBUG_RETURN(true);
  }

  if (error >= HA_ERR_ROCKSDB_FIRST && error <= HA_ERR_ROCKSDB_LAST) {
    buf->append(rdb_error_messages[error - HA_ERR_ROCKSDB_FIRST]);
  }

  // We can be called with the values which are < HA_ERR_FIRST because most
  // MySQL internal functions will just return HA_EXIT_FAILURE in case of
  // an error.

  DBUG_RETURN(false);
}

/*
  Generalized way to convert RocksDB status errors into MySQL error code, and
  print error message.

  Each error code below maps to a RocksDB status code found in:
  rocksdb/include/rocksdb/status.h
*/
int ha_rocksdb::rdb_error_to_mysql(const rocksdb::Status &s,
                                   const char *opt_msg) {
  DBUG_ASSERT(!s.ok());

  int err;
  switch (s.code()) {
    case rocksdb::Status::Code::kOk:
      err = HA_EXIT_SUCCESS;
      break;
    case rocksdb::Status::Code::kNotFound:
      err = HA_ERR_ROCKSDB_STATUS_NOT_FOUND;
      break;
    case rocksdb::Status::Code::kCorruption:
      err = HA_ERR_ROCKSDB_STATUS_CORRUPTION;
      break;
    case rocksdb::Status::Code::kNotSupported:
      err = HA_ERR_ROCKSDB_STATUS_NOT_SUPPORTED;
      break;
    case rocksdb::Status::Code::kInvalidArgument:
      err = HA_ERR_ROCKSDB_STATUS_INVALID_ARGUMENT;
      break;
    case rocksdb::Status::Code::kIOError:
      err = (s.IsNoSpace()) ? HA_ERR_ROCKSDB_STATUS_NO_SPACE
                            : HA_ERR_ROCKSDB_STATUS_IO_ERROR;
      break;
    case rocksdb::Status::Code::kMergeInProgress:
      err = HA_ERR_ROCKSDB_STATUS_MERGE_IN_PROGRESS;
      break;
    case rocksdb::Status::Code::kIncomplete:
      err = HA_ERR_ROCKSDB_STATUS_INCOMPLETE;
      break;
    case rocksdb::Status::Code::kShutdownInProgress:
      err = HA_ERR_ROCKSDB_STATUS_SHUTDOWN_IN_PROGRESS;
      break;
    case rocksdb::Status::Code::kTimedOut:
      err = HA_ERR_ROCKSDB_STATUS_TIMED_OUT;
      break;
    case rocksdb::Status::Code::kAborted:
      err = (s.IsLockLimit()) ? HA_ERR_ROCKSDB_STATUS_LOCK_LIMIT
                              : HA_ERR_ROCKSDB_STATUS_ABORTED;
      break;
    case rocksdb::Status::Code::kBusy:
      err = (s.IsDeadlock()) ? HA_ERR_ROCKSDB_STATUS_DEADLOCK
                             : HA_ERR_ROCKSDB_STATUS_BUSY;
      break;
    case rocksdb::Status::Code::kExpired:
      err = HA_ERR_ROCKSDB_STATUS_EXPIRED;
      break;
    case rocksdb::Status::Code::kTryAgain:
      err = HA_ERR_ROCKSDB_STATUS_TRY_AGAIN;
      break;
    default:
      DBUG_ASSERT(0);
      return -1;
  }

  std::string errMsg;
  if (s.IsLockLimit()) {
    errMsg =
        "Operation aborted: Failed to acquire lock due to "
        "rocksdb_max_row_locks limit";
  } else {
    errMsg = s.ToString();
  }

  if (opt_msg) {
    std::string concatenated_error = errMsg + " (" + std::string(opt_msg) + ")";
    my_error(ER_GET_ERRMSG, MYF(0), s.code(), concatenated_error.c_str(),
             rocksdb_hton_name);
  } else {
    my_error(ER_GET_ERRMSG, MYF(0), s.code(), errMsg.c_str(),
             rocksdb_hton_name);
  }

  return err;
}

/*
  Create structures needed for storing data in rocksdb. This is called when the
  table is created. The structures will be shared by all TABLE* objects.

  @param
    table_arg        Table with definition
    db_table         "dbname.tablename"
    len              strlen of the above
    actual_user_table_name actual table name in case of alter
                     table copy algorithm
    tbl_def_arg      tbl_def whose key_descr is being created/populated
    old_tbl_def_arg  tbl_def from which keys are being copied over from
                     (for use during inplace alter)

  @return
    0      - Ok
    other  - error, either given table ddl is not supported by rocksdb or OOM.
*/
int ha_rocksdb::create_key_defs(
    const TABLE *const table_arg, Rdb_tbl_def *const tbl_def_arg,
    const std::string &actual_user_table_name,
    const TABLE *const old_table_arg /* = nullptr */,
    const Rdb_tbl_def *const old_tbl_def_arg
    /* = nullptr */) const {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(table_arg->s != nullptr);

  DBUG_EXECUTE_IF("rocksdb_truncate_failure", {
    my_error(ER_INTERNAL_ERROR, MYF(0), "Simulated truncation failure.");
    DBUG_RETURN(HA_EXIT_FAILURE);
  });

  DBUG_EXECUTE_IF("rocksdb_truncate_failure_crash", DBUG_SUICIDE(););

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
  if (create_cfs(table_arg, tbl_def_arg, actual_user_table_name, &cfs)) {
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  uint64 ttl_duration = 0;
  std::string ttl_column;
  uint ttl_field_offset;

  uint err;
  if ((err = Rdb_key_def::extract_ttl_duration(table_arg, tbl_def_arg,
                                               &ttl_duration))) {
    DBUG_RETURN(err);
  }

  if ((err = Rdb_key_def::extract_ttl_col(table_arg, tbl_def_arg, &ttl_column,
                                          &ttl_field_offset))) {
    DBUG_RETURN(err);
  }

  /* We don't currently support TTL on tables with hidden primary keys. */
  if (ttl_duration > 0 && has_hidden_pk(table_arg)) {
    my_error(ER_RDB_TTL_UNSUPPORTED, MYF(0));
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  /*
    If TTL duration is not specified but TTL column was specified, throw an
    error because TTL column requires duration.
  */
  if (ttl_duration == 0 && !ttl_column.empty()) {
    my_error(ER_RDB_TTL_COL_FORMAT, MYF(0), ttl_column.c_str());
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  if (!old_tbl_def_arg) {
    /*
      old_tbl_def doesn't exist. this means we are in the process of creating
      a new table.

      Get the index numbers (this will update the next_index_number)
      and create Rdb_key_def structures.
    */
    for (uint i = 0; i < tbl_def_arg->m_key_count; i++) {
      if (create_key_def(table_arg, i, tbl_def_arg, &m_key_descr_arr[i], cfs[i],
                         ttl_duration, ttl_column)) {
        DBUG_RETURN(HA_EXIT_FAILURE);
      }
    }
  } else {
    /*
      old_tbl_def exists.  This means we are creating a new tbl_def as part of
      in-place alter table.  Copy over existing keys from the old_tbl_def and
      generate the necessary new key definitions if any.
    */
    if (create_inplace_key_defs(table_arg, tbl_def_arg, old_table_arg,
                                old_tbl_def_arg, cfs, ttl_duration,
                                ttl_column)) {
      DBUG_RETURN(HA_EXIT_FAILURE);
    }
  }

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

/*
  Checks index parameters and creates column families needed for storing data
  in rocksdb if necessary.

  @param in
    table_arg     Table with definition
    db_table      Table name
    tbl_def_arg   Table def structure being populated
    actual_user_table_name actual table name in case
                   of alter table copy algorithm
  @param out
    cfs           CF info for each key definition in 'key_info' order

  @return
    0      - Ok
    other  - error
*/
int ha_rocksdb::create_cfs(
    const TABLE *const table_arg, Rdb_tbl_def *const tbl_def_arg,
    const std::string &actual_user_table_name,
    std::array<struct key_def_cf_info, MAX_INDEXES + 1> *const cfs) const {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(table_arg->s != nullptr);

  char tablename_sys[NAME_LEN + 1];
  my_core::filename_to_tablename(tbl_def_arg->base_tablename().c_str(),
                                 tablename_sys, sizeof(tablename_sys));

  std::string table_with_enforced_collation =
      actual_user_table_name.empty() ? tablename_sys : actual_user_table_name;
  uint primary_key_index = pk_index(table_arg, tbl_def_arg);

  /*
    The first loop checks the index parameters and creates
    column families if necessary.
  */
  for (uint i = 0; i < tbl_def_arg->m_key_count; i++) {
    std::shared_ptr<rocksdb::ColumnFamilyHandle> cf_handle;

    // Internal consistency check to make sure that data in TABLE and
    // Rdb_tbl_def structures matches. Either both are missing or both are
    // specified. Yes, this is critical enough to make it into SHIP_ASSERT.
    SHIP_ASSERT(!table_arg->part_info == tbl_def_arg->base_partition().empty());

    // Generate the name for the column family to use.
    bool per_part_match_found = false;
    std::string cf_name =
        generate_cf_name(i, table_arg, tbl_def_arg, &per_part_match_found);

    // Prevent create from using the system column family.
    if (cf_name == DEFAULT_SYSTEM_CF_NAME) {
      my_error(ER_WRONG_ARGUMENTS, MYF(0),
               "column family not valid for storing index data.");
      DBUG_RETURN(HA_EXIT_FAILURE);
    }

    DBUG_EXECUTE_IF("rocksdb_create_primary_cf", {
      if (cf_name == "cf_primary_key") {
        THD *const thd = my_core::thd_get_current_thd();
        const char act[] =
            "now signal ready_to_mark_cf_dropped_in_create_cfs "
            "wait_for mark_cf_dropped_done_in_create_cfs";
        DBUG_ASSERT(!debug_sync_set_action(thd, STRING_WITH_LEN(act)));
      }
    });

    DBUG_EXECUTE_IF("rocksdb_create_secondary_cf", {
      if (cf_name == "cf_secondary_key") {
        THD *const thd = my_core::thd_get_current_thd();
        const char act[] =
            "now signal ready_to_mark_cf_dropped_in_create_cfs "
            "wait_for mark_cf_dropped_done_in_create_cfs";
        DBUG_ASSERT(!debug_sync_set_action(thd, STRING_WITH_LEN(act)));
      }
    });

    // if not specified, use default CF name
    if (cf_name.empty()) {
      if (i != primary_key_index && rocksdb_use_default_sk_cf)
        cf_name = DEFAULT_SK_CF_NAME;
      else
        cf_name = DEFAULT_CF_NAME;
    }

    // Here's how `get_or_create_cf` will use the input parameters:
    //
    // `cf_name` - will be used as a CF name.
    {
      std::lock_guard<Rdb_dict_manager> dm_lock(dict_manager);
      cf_handle = cf_manager.get_or_create_cf(rdb, cf_name);
      if (!cf_handle) {
        DBUG_RETURN(HA_EXIT_FAILURE);
      }

      uint32 cf_id = cf_handle->GetID();

      // If the cf is marked as dropped, we fail it here.
      // The cf can be dropped after this point, we will
      // check again when committing metadata changes.
      if (dict_manager.get_dropped_cf(cf_id)) {
        my_error(ER_CF_DROPPED, MYF(0), cf_name.c_str());
        DBUG_RETURN(HA_EXIT_FAILURE);
      }

      if (cf_manager.create_cf_flags_if_needed(&dict_manager,
                                               cf_handle->GetID(), cf_name,
                                               per_part_match_found)) {
        DBUG_RETURN(HA_EXIT_FAILURE);
      }
    }

    // The CF can be dropped from cf_manager at this point. This is part of
    // create table or alter table. If the drop happens before metadata are
    // written, create table or alter table will fail.
    auto &cf = (*cfs)[i];

    cf.cf_handle = cf_handle;
    cf.is_reverse_cf = Rdb_cf_manager::is_cf_name_reverse(cf_name.c_str());
    cf.is_per_partition_cf = per_part_match_found;
  }

  DBUG_RETURN(HA_EXIT_SUCCESS);
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
int ha_rocksdb::create_inplace_key_defs(
    const TABLE *const table_arg, Rdb_tbl_def *const tbl_def_arg,
    const TABLE *const old_table_arg, const Rdb_tbl_def *const old_tbl_def_arg,
    const std::array<key_def_cf_info, MAX_INDEXES + 1> &cfs,
    uint64 ttl_duration, const std::string &ttl_column) const {
  DBUG_ENTER_FUNC();

  std::shared_ptr<Rdb_key_def> *const old_key_descr =
      old_tbl_def_arg->m_key_descr_arr;
  std::shared_ptr<Rdb_key_def> *const new_key_descr =
      tbl_def_arg->m_key_descr_arr;
  const std::unordered_map<std::string, uint> old_key_pos =
      get_old_key_positions(table_arg, tbl_def_arg, old_table_arg,
                            old_tbl_def_arg);

  uint i;
  for (i = 0; i < tbl_def_arg->m_key_count; i++) {
    const auto &it = old_key_pos.find(get_key_name(i, table_arg, tbl_def_arg));

    if (it != old_key_pos.end()) {
      /*
        Found matching index in old table definition, so copy it over to the
        new one created.
      */
      const Rdb_key_def &okd = *old_key_descr[it->second];

      const GL_INDEX_ID gl_index_id = okd.get_gl_index_id();
      struct Rdb_index_info index_info;
      if (!dict_manager.get_index_info(gl_index_id, &index_info)) {
        // NO_LINT_DEBUG
        sql_print_error(
            "RocksDB: Could not get index information "
            "for Index Number (%u,%u), table %s",
            gl_index_id.cf_id, gl_index_id.index_id,
            old_tbl_def_arg->full_tablename().c_str());
        DBUG_RETURN(HA_EXIT_FAILURE);
      }

      uint32 ttl_rec_offset =
          Rdb_key_def::has_index_flag(index_info.m_index_flags,
                                      Rdb_key_def::TTL_FLAG)
              ? Rdb_key_def::calculate_index_flag_offset(
                    index_info.m_index_flags, Rdb_key_def::TTL_FLAG)
              : UINT_MAX;

      /*
        We can't use the copy constructor because we need to update the
        keynr within the pack_info for each field and the keyno of the keydef
        itself.
      */
      new_key_descr[i] = std::make_shared<Rdb_key_def>(
          okd.get_index_number(), i, okd.get_shared_cf(),
          index_info.m_index_dict_version, index_info.m_index_type,
          index_info.m_kv_version, okd.m_is_reverse_cf,
          okd.m_is_per_partition_cf, okd.m_name.c_str(),
          dict_manager.get_stats(gl_index_id), index_info.m_index_flags,
          ttl_rec_offset, index_info.m_ttl_duration);
    } else if (create_key_def(table_arg, i, tbl_def_arg, &new_key_descr[i],
                              cfs[i], ttl_duration, ttl_column)) {
      DBUG_RETURN(HA_EXIT_FAILURE);
    }

    DBUG_ASSERT(new_key_descr[i] != nullptr);
    new_key_descr[i]->setup(table_arg, tbl_def_arg);
  }

  tbl_def_arg->m_tbl_stats.set(new_key_descr[0]->m_stats.m_rows, 0, 0);

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

std::unordered_map<std::string, uint> ha_rocksdb::get_old_key_positions(
    const TABLE *const table_arg, const Rdb_tbl_def *const tbl_def_arg,
    const TABLE *const old_table_arg,
    const Rdb_tbl_def *const old_tbl_def_arg) const {
  DBUG_ENTER_FUNC();

  std::shared_ptr<Rdb_key_def> *const old_key_descr =
      old_tbl_def_arg->m_key_descr_arr;
  std::unordered_map<std::string, uint> old_key_pos;
  std::unordered_map<std::string, uint> new_key_pos;
  uint i;

  for (i = 0; i < tbl_def_arg->m_key_count; i++) {
    new_key_pos[get_key_name(i, table_arg, tbl_def_arg)] = i;
  }

  for (i = 0; i < old_tbl_def_arg->m_key_count; i++) {
    if (is_hidden_pk(i, old_table_arg, old_tbl_def_arg)) {
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
    const KEY *const old_key = &old_table_arg->key_info[i];
    const auto &it = new_key_pos.find(old_key->name);
    if (it == new_key_pos.end()) {
      continue;
    }

    KEY *const new_key = &table_arg->key_info[it->second];

    /*
      Check that the key is identical between old and new tables.
      If not, we still need to create a new index.

      The exception is if there is an index changed from unique to non-unique,
      in these cases we don't need to rebuild as they are stored the same way in
      RocksDB.
    */
    bool unique_to_non_unique =
        ((old_key->flags ^ new_key->flags) == HA_NOSAME) &&
        (old_key->flags & HA_NOSAME);

    if (compare_keys(old_key, new_key) && !unique_to_non_unique) {
      continue;
    }

    /* Check to make sure key parts match. */
    if (compare_key_parts(old_key, new_key)) {
      continue;
    }

    old_key_pos[old_key->name] = i;
  }

  DBUG_RETURN(old_key_pos);
}

/* Check to see if two keys are identical. */
int ha_rocksdb::compare_keys(const KEY *const old_key,
                             const KEY *const new_key) const {
  DBUG_ENTER_FUNC();

  /* Check index name. */
  if (strcmp(old_key->name, new_key->name) != 0) {
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  /* If index algorithms are different then keys are different. */
  if (old_key->algorithm != new_key->algorithm) {
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  /* Check that the key is identical between old and new tables.  */
  if ((old_key->flags ^ new_key->flags) & HA_KEYFLAG_MASK) {
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  /* Check index comment. (for column family changes) */
  std::string old_comment(old_key->comment.str, old_key->comment.length);
  std::string new_comment(new_key->comment.str, new_key->comment.length);
  if (old_comment.compare(new_comment) != 0) {
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

/* Check two keys to ensure that key parts within keys match */
int ha_rocksdb::compare_key_parts(const KEY *const old_key,
                                  const KEY *const new_key) const {
  DBUG_ENTER_FUNC();

  /* Skip if key parts do not match, as it is a different key */
  if (new_key->user_defined_key_parts != old_key->user_defined_key_parts) {
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  /* Check to see that key parts themselves match */
  for (uint i = 0; i < old_key->user_defined_key_parts; i++) {
    if (strcmp(old_key->key_part[i].field->field_name,
               new_key->key_part[i].field->field_name) != 0) {
      DBUG_RETURN(HA_EXIT_FAILURE);
    }

    /* Check if prefix index key part length has changed */
    if (old_key->key_part[i].length != new_key->key_part[i].length) {
      DBUG_RETURN(HA_EXIT_FAILURE);
    }
  }

  DBUG_RETURN(HA_EXIT_SUCCESS);
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
int ha_rocksdb::create_key_def(const TABLE *const table_arg, const uint i,
                               const Rdb_tbl_def *const tbl_def_arg,
                               std::shared_ptr<Rdb_key_def> *const new_key_def,
                               const struct key_def_cf_info &cf_info,
                               uint64 ttl_duration,
                               const std::string &ttl_column) const {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(*new_key_def == nullptr);

  const uint index_id = ddl_manager.get_and_update_next_number(&dict_manager);
  const uint16_t index_dict_version = Rdb_key_def::INDEX_INFO_VERSION_LATEST;
  uchar index_type;
  uint16_t kv_version;

  if (is_hidden_pk(i, table_arg, tbl_def_arg)) {
    index_type = Rdb_key_def::INDEX_TYPE_HIDDEN_PRIMARY;
    kv_version = Rdb_key_def::PRIMARY_FORMAT_VERSION_LATEST;
  } else if (i == table_arg->s->primary_key) {
    index_type = Rdb_key_def::INDEX_TYPE_PRIMARY;
    uint16 pk_latest_version = Rdb_key_def::PRIMARY_FORMAT_VERSION_LATEST;
    kv_version = pk_latest_version;
  } else {
    index_type = Rdb_key_def::INDEX_TYPE_SECONDARY;
    uint16 sk_latest_version = Rdb_key_def::SECONDARY_FORMAT_VERSION_LATEST;
    kv_version = sk_latest_version;
  }

  uint32 index_flags = (ttl_duration > 0 ? Rdb_key_def::TTL_FLAG : 0);

  uint32 ttl_rec_offset =
      Rdb_key_def::has_index_flag(index_flags, Rdb_key_def::TTL_FLAG)
          ? Rdb_key_def::calculate_index_flag_offset(index_flags,
                                                     Rdb_key_def::TTL_FLAG)
          : UINT_MAX;

  const char *const key_name = get_key_name(i, table_arg, m_tbl_def);
  *new_key_def = std::make_shared<Rdb_key_def>(
      index_id, i, cf_info.cf_handle, index_dict_version, index_type,
      kv_version, cf_info.is_reverse_cf, cf_info.is_per_partition_cf, key_name,
      Rdb_index_stats(), index_flags, ttl_rec_offset, ttl_duration);

  if (!ttl_column.empty()) {
    (*new_key_def)->m_ttl_column = ttl_column;
  }

  if ((*new_key_def)->extract_partial_index_info(table_arg, tbl_def_arg)) {
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  // initialize key_def
  (*new_key_def)->setup(table_arg, tbl_def_arg);
  DBUG_RETURN(HA_EXIT_SUCCESS);
}

bool rdb_is_tablename_normalized(const std::string &tablename) {
  return tablename.size() < 2 || (tablename[0] != '.' && tablename[1] != '/');
}

int rdb_normalize_tablename(const std::string &tablename,
                            std::string *const strbuf) {
  if (tablename.size() < 2 || tablename[0] != '.' || tablename[1] != '/') {
    DBUG_ASSERT(0);  // We were not passed table name?
    return HA_ERR_ROCKSDB_INVALID_TABLE;
  }

  size_t pos = tablename.find_first_of('/', 2);
  if (pos == std::string::npos) {
    DBUG_ASSERT(0);  // We were not passed table name?
    return HA_ERR_ROCKSDB_INVALID_TABLE;
  }

  *strbuf = tablename.substr(2, pos - 2) + "." + tablename.substr(pos + 1);

  return HA_EXIT_SUCCESS;
}

/*
  Check to see if the user's original statement includes foreign key
  references
*/
bool ha_rocksdb::contains_foreign_key() {
  bool success;

  THD *thd = my_core::thd_get_current_thd();
  const char *str = thd_query_unsafe(thd).str;

  DBUG_ASSERT(str != nullptr);

  while (*str != '\0') {
    // Scan from our current pos looking for 'FOREIGN'
    str = rdb_find_in_string(str, "FOREIGN", &success);
    if (!success) {
      return false;
    }

    // Skip past the found "FOREIGN'
    str = rdb_check_next_token(&my_charset_bin, str, "FOREIGN", &success);
    DBUG_ASSERT(success);

    if (!my_isspace(&my_charset_bin, *str)) {
      return false;
    }

    // See if the next token is 'KEY'
    str = rdb_check_next_token(&my_charset_bin, str, "KEY", &success);
    if (!success) {
      continue;
    }

    // See if the next token is '('
    str = rdb_check_next_token(&my_charset_bin, str, "(", &success);
    if (!success) {
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
  @return HA_EXIT_SUCCESS on success, non-zero on failure to split
*/
int rdb_split_normalized_tablename(const std::string &fullname,
                                   std::string *const db,
                                   std::string *const table,
                                   std::string *const partition) {
  DBUG_ASSERT(!fullname.empty());

#define RDB_PARTITION_STR "#P#"

  /* Normalize returns dbname.tablename. */
  size_t dotpos = fullname.find('.');

  /* Invalid table name? */
  if (dotpos == std::string::npos) {
    return HA_ERR_ROCKSDB_INVALID_TABLE;
  }

  // Table must have a database name associated with it.
  DBUG_ASSERT(dotpos > 0);

  if (db != nullptr) {
    *db = fullname.substr(0, dotpos);
  }

  dotpos++;

  const size_t partpos =
      fullname.find(RDB_PARTITION_STR, dotpos, strlen(RDB_PARTITION_STR));

  if (partpos != std::string::npos) {
    DBUG_ASSERT(partpos >= dotpos);

    if (table != nullptr) {
      *table = fullname.substr(dotpos, partpos - dotpos);
    }

    if (partition != nullptr) {
      *partition = fullname.substr(partpos + strlen(RDB_PARTITION_STR));
    }
  } else if (table != nullptr) {
    *table = fullname.substr(dotpos);
  }

  return HA_EXIT_SUCCESS;
}

/*
  Generates the normalized tablename using as many of the given arguments as
  possible. Any of the three arguments to <db>.<table>#P#<partition> can be
  null/empty, but return string will only ever be of the form
  <db>
  <db>.<table>
  <db>.<table>#P#<partition>
  <table>
  <table>#P#<partition>
*/
void rdb_gen_normalized_tablename(const std::string *db,
                                  const std::string *table,
                                  const std::string *partition,
                                  std::string *fullname) {
  if (!fullname) return;
  fullname->clear();
  if (db && db->length() > 0) *fullname += *db;

  /* If table was not passed in, the partition will be ignored too */
  if (!table || table->length() == 0) return;

  if (fullname->length() > 0) *fullname += ".";
  *fullname += *table;

  if (partition && partition->length() > 0) {
    *fullname += std::string(RDB_PARTITION_STR) + *partition;
  }
}

/*
 Create a table's Rdb_tbl_def and its Rdb_key_defs and store table information
 into MyRocks Data Dictionary
 The method is called during create table/partition, truncate table/partition

 @param table_name             IN      table's name formated as
 'dbname.tablename'
 @param actual_user_table_name IN      actual table name in case
 of alter table copy algorithm
 @param table_arg              IN      sql table
 @param auto_increment_value   IN      specified table's auto increment value

  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::create_table(const std::string &table_name,
                             const std::string &actual_user_table_name,
                             const TABLE *table_arg,
                             ulonglong auto_increment_value,
                             dd::Table *table_def MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  int err;

  const std::unique_ptr<rocksdb::WriteBatch> wb = dict_manager.begin();
  rocksdb::WriteBatch *const batch = wb.get();

  /* Create table/key descriptions and put them into the data dictionary */
  m_tbl_def = new Rdb_tbl_def(table_name);

  uint n_keys = table_arg->s->keys;

  /*
    If no primary key found, create a hidden PK and place it inside table
    definition
  */
  if (has_hidden_pk(table_arg)) {
    n_keys += 1;
    // reset hidden pk id
    // the starting valid value for hidden pk is 1
    m_tbl_def->m_hidden_pk_val = 1;
  }

  m_key_descr_arr = new std::shared_ptr<Rdb_key_def>[n_keys];
  m_tbl_def->m_key_count = n_keys;
  m_tbl_def->m_pk_index = table_arg->s->primary_key;
  m_tbl_def->m_key_descr_arr = m_key_descr_arr;

  err = create_key_defs(table_arg, m_tbl_def, actual_user_table_name);
  if (err != HA_EXIT_SUCCESS) {
    goto error;
  }

  m_pk_descr = m_key_descr_arr[pk_index(table_arg, m_tbl_def)];

  if (auto_increment_value) {
    bool autoinc_upgrade_test = false;
    m_tbl_def->m_auto_incr_val = auto_increment_value;
    DBUG_EXECUTE_IF("myrocks_autoinc_upgrade", autoinc_upgrade_test = true;);
    if (!autoinc_upgrade_test) {
      auto s = dict_manager.put_auto_incr_val(
          batch, m_tbl_def->get_autoincr_gl_index_id(),
          m_tbl_def->m_auto_incr_val);
      if (!s.ok()) {
        goto error;
      }
    }
  }

  DBUG_EXECUTE_IF("rocksdb_create_table", {
    THD *const thd = my_core::thd_get_current_thd();
    const char act[] =
        "now signal ready_to_mark_cf_dropped_in_create_table "
        "wait_for mark_cf_dropped_done_in_create_table";
    DBUG_ASSERT(!debug_sync_set_action(thd, STRING_WITH_LEN(act)));
  });

  {
    std::lock_guard<Rdb_dict_manager> dm_lock(dict_manager);
    err = ddl_manager.put_and_write(m_tbl_def, batch);
    if (err != HA_EXIT_SUCCESS) {
      goto error;
    }

    err = dict_manager.commit(batch);
    if (err != HA_EXIT_SUCCESS) {
      goto error;
    }
  }

  DBUG_RETURN(HA_EXIT_SUCCESS);

error:
  /* Delete what we have allocated so far */
  delete m_tbl_def;
  m_tbl_def = nullptr;
  m_key_descr_arr = nullptr;

  DBUG_RETURN(err);
}

/**
  @brief
  create() is called to create a table. The variable name will have the name
  of the table.

  @details
  When create() is called you do not need to worry about
  opening the table. Also, the DD table have already been
  created so adjusting create_info is not necessary.

  Called from handle.cc by ha_create_table().

  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)

  @see
  ha_create_table() in handle.cc
*/

int ha_rocksdb::create(const char *const name, TABLE *const table_arg,
                       HA_CREATE_INFO *const create_info,
                       dd::Table *table_def) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(table_arg != nullptr);
  DBUG_ASSERT(create_info != nullptr);

  if (create_info->data_file_name) {
    // DATA DIRECTORY is used to create tables under a specific location
    // outside the MySQL data directory. We don't support this for MyRocks.
    // The `rocksdb_datadir` setting should be used to configure RocksDB data
    // directory.
    DBUG_RETURN(HA_ERR_ROCKSDB_TABLE_DATA_DIRECTORY_NOT_SUPPORTED);
  }

  if (create_info->index_file_name) {
    // Similar check for INDEX DIRECTORY as well.
    DBUG_RETURN(HA_ERR_ROCKSDB_TABLE_INDEX_DIRECTORY_NOT_SUPPORTED);
  }

  if (unlikely(create_info->options & HA_LEX_CREATE_TMP_TABLE)) {
    my_error(ER_ILLEGAL_HA_CREATE_OPTION, MYF(0),
             ha_resolve_storage_engine_name(create_info->db_type), "TEMPORARY");
    DBUG_RETURN(HA_ERR_ROCKSDB_INVALID_TABLE);
  }

  int err;
  /*
    Construct dbname.tablename ourselves, because parititioning
    passes strings like "./test/t14#P#p0" for individual partitions,
    while table_arg->s->table_name has none of that.
  */
  std::string str;
  err = rdb_normalize_tablename(name, &str);
  if (err != HA_EXIT_SUCCESS) {
    DBUG_RETURN(err);
  }

  // FOREIGN KEY isn't supported yet
  if (contains_foreign_key()) {
    my_error(ER_NOT_SUPPORTED_YET, MYF(0),
             "FOREIGN KEY for the RocksDB storage engine");
    DBUG_RETURN(HA_ERR_UNSUPPORTED);
  }

  // Check whether Data Dictionary contain information
  THD *const thd = my_core::thd_get_current_thd();
  Rdb_tbl_def *old_tbl = ddl_manager.find(str);
  if (old_tbl != nullptr) {
    if (thd->lex->sql_command == SQLCOM_TRUNCATE) {
      DBUG_RETURN(truncate_table(old_tbl, create_info->actual_user_table_name,
                                 table_arg, create_info->auto_increment_value,
                                 table_def));
    } else {
      my_error(ER_METADATA_INCONSISTENCY, MYF(0), str.c_str());
      DBUG_RETURN(HA_ERR_ROCKSDB_CORRUPT_DATA);
    }
  }
  DBUG_RETURN(create_table(str, create_info->actual_user_table_name, table_arg,
                           create_info->auto_increment_value, table_def));
}

/*
  Fast truncates a table by renaming the old table, creating a new one and
  restoring or deleting the old table based on the results from creation.

  @param tbl_def               IN      MyRocks's table structure
  @param actual_user_table_name IN     actual table name in case
  @param table_arg             IN      sql table
  @param auto_increment_value  IN      specified table's auto increment value

  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::truncate_table(Rdb_tbl_def *tbl_def_arg,
                               const std::string &actual_user_table_name,
                               TABLE *table_arg, ulonglong auto_increment_value,
                               dd::Table *table_def) {
  DBUG_ENTER_FUNC();

  /*
    Fast table truncation involves deleting the table and then recreating
    it. However, it is possible recreating the table fails. In this case, a
    table inconsistency might result between SQL and MyRocks where MyRocks is
    missing a table. Since table creation involves modifying keys with the
    original table name, renaming the original table first, and then renaming
    it back in case of creation failure can help restore the pre-truncation
    state.

    If the server were to crash during truncation, the system will end up with
    an inconsistency. Future changes for atomic ddl will resolve this. For now,
    if there are any truncation renamed tables found during startup, MyRocks
    will automatically remove them.
  */
  std::string orig_tablename = tbl_def_arg->full_tablename();
  std::string dbname, tblname, partition;

  /*
    Rename the table in the data dictionary. Since this thread should be
    holding the MDL for this tablename, it is safe to perform these renames
    should be locked via MDL, no other process thread be able to access this
    table.
  */
  int err = rdb_split_normalized_tablename(orig_tablename, &dbname, &tblname,
                                           &partition);
  DBUG_ASSERT(err == 0);
  if (err != HA_EXIT_SUCCESS) DBUG_RETURN(err);
  tblname = std::string(TRUNCATE_TABLE_PREFIX) + tblname;

  std::string tmp_tablename;
  rdb_gen_normalized_tablename(&dbname, &tblname, &partition, &tmp_tablename);

  err = rename_table(orig_tablename.c_str(), tmp_tablename.c_str(), table_def,
                     table_def);
  if (err != HA_EXIT_SUCCESS) DBUG_RETURN(err);

  /*
    Attempt to create the table. If this succeeds, then drop the old table.
    Otherwise, try to restore it.
  */
  err = create_table(orig_tablename, actual_user_table_name, table_arg,
                     auto_increment_value, table_def);
  bool should_remove_old_table = true;

  /* Restore the old table being truncated if creating the new table failed */
  if (err != HA_EXIT_SUCCESS) {
    int rename_err = rename_table(tmp_tablename.c_str(), orig_tablename.c_str(),
                                  table_def, table_def);

    /*
      If the rename also fails, we are out of options, but at least try to drop
      the old table contents.
    */
    if (rename_err == HA_EXIT_SUCCESS) {
      should_remove_old_table = false;
    } else {
      // NO_LINT_DEBUG
      sql_print_error(
          "MyRocks: Failure during truncation of table %s "
          "being renamed from %s",
          orig_tablename.c_str(), tmp_tablename.c_str());
      err = rename_err;
    }
  }

  /*
    Since the table was successfully truncated or the name restore failed, no
    error should be returned at this point from trying to delete the old
    table. If the delete_table fails, log it instead.
  */
  Rdb_tbl_def *old_tbl_def = ddl_manager.find(tmp_tablename);
  if (should_remove_old_table && old_tbl_def) {
    m_tbl_def = old_tbl_def;
    if (delete_table(old_tbl_def) != HA_EXIT_SUCCESS) {
      // NO_LINT_DEBUG
      sql_print_error(
          "Failure when trying to drop table %s during "
          "truncation of table %s",
          tmp_tablename.c_str(), orig_tablename.c_str());
    }
  }

  /* Update the local m_tbl_def reference */
  m_tbl_def = ddl_manager.find(orig_tablename);
  m_converter.reset(new Rdb_converter(ha_thd(), m_tbl_def, table_arg));
  DBUG_RETURN(err);
}

/**
  @note
  This function is used only when the table has not yet been opened, and
  keyread_allowed bitmap doesn't have the correct values yet.

  See comment in ha_rocksdb::index_flags() for details.
*/

bool ha_rocksdb::check_keyread_allowed(bool &pk_can_be_decoded,
                                       const TABLE_SHARE *, uint, uint, bool) {
  /*
    (TODO) Remove this function and index version checks, Also remove return
    code from Rdb_field_packing:setup() and remove the changes for '[mysql80]
    Issue #108: Index-only scans do not work for partitioned tables and
    extended keys'.

    For the cases of `create table t1 (a varchar(64), key (a(32)))`
    and text/blob key columns, the sql layer seems to handle them
    correctly regarding using index. InnoDB also seems to return
    HA_KEYREAD_ONLY for these cases.
  */
  pk_can_be_decoded = true;
  return true;
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
    all_parts=true. Essentially, we're asked to provide flags for reading
    keyparts whose datatype is not yet known.

    We walk around this problem by using check_keyread_allowed(), which uses
    table_share object and is careful not to step on unitialized data.

    When we get a call with all_parts=true, we try to analyze all parts but
    ignore those that have key_part->field==nullptr (these are not initialized
    yet).
*/

ulong ha_rocksdb::index_flags(bool &pk_can_be_decoded,
                              const TABLE_SHARE *table_arg, uint inx, uint part,
                              bool all_parts) {
  DBUG_ENTER_FUNC();

  ulong base_flags = HA_READ_NEXT |  // doesn't seem to be used
                     HA_READ_ORDER | HA_READ_RANGE | HA_READ_PREV;

  DBUG_EXECUTE_IF("myrocks_verify_tbl_share_primary_idx",
                  { DBUG_ASSERT(table_arg->primary_key == MAX_INDEXES); };);

  if (check_keyread_allowed(pk_can_be_decoded, table_arg, inx, part,
                            all_parts)) {
    base_flags |= HA_KEYREAD_ONLY;
  }

  if (inx == table_arg->primary_key) {
    /*
      Index-only reads on primary key are the same as table scan for us. Still,
      we need to explicitly "allow" them, otherwise SQL layer will miss some
      plans.
    */
    base_flags |= HA_KEYREAD_ONLY;
  } else {
    /*
      We can Index Condition Pushdown any key except the primary. With primary
      key, we get (pk, record) pair immediately, there is no place to put the
      ICP check.
    */
    base_flags |= HA_DO_INDEX_COND_PUSHDOWN;
  }

  DBUG_RETURN(base_flags);
}

ulong ha_rocksdb::index_flags(uint inx, uint part, bool all_parts) const {
  return index_flags(m_pk_can_be_decoded, table_share, inx, part, all_parts);
}

/**
  @brief
  Read from primary key if secondary key is not covering.

  @details
  m_scan_it points at the index key-value pair that we should read the (pk,row)
  pair for.
*/
int ha_rocksdb::secondary_index_read(const int keyno, uchar *const buf,
                                     const rocksdb::Slice *value,
                                     bool *skip_row) {
  DBUG_ASSERT(table != nullptr);

  int rc = 0;

#ifndef DBUG_OFF
  bool save_keyread_only = m_keyread_only;
  DBUG_EXECUTE_IF("dbug.rocksdb.HA_EXTRA_KEYREAD", { m_keyread_only = true; });
#endif
  bool covered_lookup =
      (m_keyread_only && m_key_descr_arr[keyno]->can_cover_lookup()) ||
      m_key_descr_arr[keyno]->covers_lookup(value,
                                            m_converter->get_lookup_bitmap());
#ifndef DBUG_OFF
  m_keyread_only = save_keyread_only;
#endif

  if (covered_lookup && m_lock_rows == RDB_LOCK_NONE) {
    inc_covered_sk_lookup();
  } else {
    DEBUG_SYNC(ha_thd(), "rocksdb_concurrent_delete_sk");
    rc = get_row_by_rowid(buf, m_last_rowkey.ptr(), m_last_rowkey.length(),
                          skip_row);
  }

  return rc;
}

/*
   See storage/rocksdb/rocksdb-range-access.txt for description of how MySQL
   index navigation commands are converted into RocksDB lookup commands.

   MyRocks needs to decide whether prefix bloom filter can be used or not.
   To decide to use prefix bloom filter or not, calculating equal condition
   length is needed. On equal lookups (find_flag == HA_READ_KEY_EXACT), equal
   condition length is the same as rocksdb::Slice.size() of the start key.
   On range scan, equal condition length is MIN(start_key, end_key) of the
   rocksdb::Slice expression, where end_key is taken from end_range.

   @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::index_read_intern(uchar *const buf, const uchar *const key,
                                  key_part_map keypart_map,
                                  enum ha_rkey_function find_flag) {
  DBUG_ENTER_FUNC();

  check_build_decoder();

  DBUG_EXECUTE_IF(
      "myrocks_busy_loop_on_row_read", int debug_i = 0;
      while (1) { debug_i++; });

  int rc = 0;
  table->m_status = STATUS_NOT_FOUND;

  THD *thd = ha_thd();
  DEBUG_SYNC(thd, "rocksdb.check_flags_iri");
  if (thd && thd->killed) {
    rc = HA_ERR_QUERY_INTERRUPTED;
    DBUG_RETURN(rc);
  }

  switch (find_flag) {
    case HA_READ_KEY_EXACT:
    case HA_READ_AFTER_KEY:
    case HA_READ_KEY_OR_NEXT:
    case HA_READ_BEFORE_KEY:
    case HA_READ_PREFIX_LAST:
    case HA_READ_PREFIX_LAST_OR_PREV:
      break;
    default:
      DBUG_RETURN(HA_ERR_UNSUPPORTED);
  }

  const Rdb_key_def &kd = *m_key_descr_arr[active_index_pos()];
  bool using_full_key = false;
  m_full_key_lookup = false;

  uint packed_size;

  if (!key) {
    // If no key is passed in, then we are doing a full index scan.
    //
    // Just use current index id as the search key.
    kd.get_infimum_key(m_sk_packed_tuple, &packed_size);
  } else {
    const uint actual_key_parts = kd.get_key_parts();
    using_full_key = is_using_full_key(keypart_map, actual_key_parts);
    /*
      Handle some special cases when we do exact key lookups.
    */
    if (find_flag == HA_READ_KEY_EXACT && using_full_key) {
      if (active_index == table->s->primary_key) {
        /*
          Equality lookup over primary key, using full tuple.
          This is a special case, use DB::Get.
          */
        const uint size = kd.pack_index_tuple(
            table, m_pack_buffer, m_pk_packed_tuple, key, keypart_map);
        bool skip_lookup = is_blind_delete_enabled();
        /* TODO(yzha) - row stats are gone in 8.0
        stats.rows_requested++; */

        rc = get_row_by_rowid(buf, m_pk_packed_tuple, size, nullptr,
                              skip_lookup, false);
        if (!rc && !skip_lookup) {
          /* TODO(yzha) - row stats are gone in 8.0
            stats.rows_read++;
            stats.rows_index_first++; */
          update_row_stats(ROWS_READ);
        }
        /*
          If the SQL layer calls index_read_map, it expects the iterator to be
          positioned accordingly, so that next/prev can work as expected. In
          this case, we calling DB::Get directly without positioning an
          iterator, so it is incorrect for the SQL layer to be calling
          next/prev anyway. To avoid correctness issues, just free the
          iterator.
        */
        m_full_key_lookup = true;
        m_iterator->reset();
        DBUG_RETURN(rc);
      } else {
        /*
          The SQL layer sometimes sets HA_WHOLE_KEY for secondary keys lookups,
          even though it may not include extended keys.

          Adjust keypart_map so that we get the correct packing.
        */
        if (keypart_map == HA_WHOLE_KEY) {
          uint avail_key_parts = 0;
          calculate_key_len(table, active_index, keypart_map, &avail_key_parts);
          keypart_map = make_prev_keypart_map(avail_key_parts);
          using_full_key = is_using_full_key(keypart_map, actual_key_parts);
        }

        if (table->key_info[active_index].flags & HA_NOSAME &&
            m_insert_with_update && m_dup_key_found &&
            active_index == m_dupp_errkey) {
          /*
            We are in INSERT ... ON DUPLICATE KEY UPDATE, and this is a read
            that SQL layer does to read the duplicate key.
            Its rowid is saved in m_last_rowkey. Get the full record and
            return it.
          */

#ifndef DBUG_OFF
          packed_size = kd.pack_index_tuple(
              table, m_pack_buffer, m_sk_packed_tuple, key, keypart_map);
          DBUG_ASSERT(m_dup_key_tuple.length() >= packed_size);
          DBUG_ASSERT(memcmp(m_dup_key_tuple.ptr(), m_sk_packed_tuple,
                             packed_size) == 0);
#endif

          rc = get_row_by_rowid(buf, m_last_rowkey.ptr(),
                                m_last_rowkey.length());
          m_iterator->reset();
          DBUG_RETURN(rc);
        }

        if (using_full_key) {
          packed_size = kd.pack_index_tuple(
              table, m_pack_buffer, m_sk_packed_tuple, key, keypart_map);
          rocksdb::Slice key_slice(
              reinterpret_cast<const char *>(m_sk_packed_tuple), packed_size);

          rc = get_row_by_sk(buf, kd, &key_slice);

          if (!rc) {
            /* TODO(yzha) - row stats are gone in 8.0
              stats.rows_read++;
              stats.rows_index_first++; */
            update_row_stats(ROWS_READ);
          }

          m_full_key_lookup = true;
          m_iterator->reset();
          DBUG_RETURN(rc);
        }
      }
    }

    packed_size = kd.pack_index_tuple(table, m_pack_buffer, m_sk_packed_tuple,
                                      key, keypart_map);
  }

  rocksdb::Slice slice(reinterpret_cast<const char *>(m_sk_packed_tuple),
                       packed_size);

  rocksdb::Slice end_slice;
  if (end_range && find_flag != HA_READ_KEY_EXACT &&
      find_flag != HA_READ_PREFIX_LAST) {
    uint end_key_packed_size = 0;
    end_key_packed_size =
        kd.pack_index_tuple(table, m_pack_buffer, m_end_key_packed_tuple,
                            end_range->key, end_range->keypart_map);
    end_slice =
        rocksdb::Slice((char *)m_end_key_packed_tuple, end_key_packed_size);
  }

  Rdb_transaction *const tx = get_or_create_tx(table->in_use);

  bool use_locking_iter= false;

  if ((rc = set_range_lock(tx, kd, find_flag, slice, end_range, 
                           &use_locking_iter)))
    DBUG_RETURN(rc);
  if (use_locking_iter)
    m_iterator->set_use_locking();

  const bool is_new_snapshot = !tx->has_snapshot();

  // Loop as long as we get a deadlock error AND we end up creating the
  // snapshot here (i.e. it did not exist prior to this)
  for (;;) {
    DEBUG_SYNC(thd, "rocksdb.check_flags_iri_scan");
    if (thd && thd->killed) {
      rc = HA_ERR_QUERY_INTERRUPTED;
      break;
    }
    /*
      This will open the iterator and position it at a record that's equal or
      greater than the lookup tuple.
    */
    rc = m_iterator->seek(find_flag, slice, using_full_key, end_slice);

    if (rc) {
      break;
    }

    /*
      Now get the data for the row into 'buf'.  If we were using a primary key
      then we have all the rows we need.  For a secondary key we now need to
      lookup the primary key.
    */
    bool direction = (find_flag == HA_READ_KEY_EXACT) ||
                     (find_flag == HA_READ_AFTER_KEY) ||
                     (find_flag == HA_READ_KEY_OR_NEXT);
    rc = index_next_with_direction_intern(buf, direction, true);

    if (!should_recreate_snapshot(rc, is_new_snapshot)) {
      break; /* Exit the loop */
    }

    // release the snapshot and iterator so they will be regenerated
    tx->release_snapshot();
    m_iterator->reset();
  }

  if (!rc) {
    /* TODO(yzha) - row stats are gone in 8.0
    stats.rows_index_first++;
    stats.rows_index_next--; */
  }

  DBUG_RETURN(rc);
}


/*
  @brief
    Compute the range lock endpoints and set the range lock, if necessary
   
  @param  use_locking_iter OUT  If true, locks are not set and LockingIterator
                                should be used instead

  @detail
    If the scanned range doesn't have the endpoint we're scanning towards,
    don't set the lock, it will be too coarse. Indicate that LockingIterator
    should be used, instead.

    == RangeFlagsShouldBeFlippedForRevCF ==
    When using reverse column families, the value of Endpoint::inf_suffix has
    the reverse meaning.

    Let's consider a forward-ordered CF and some keys and endpoints in it:

      key=a, inf_suffix=false
      key=ab
      key=az
      key=a, inf_suffix=true

    Now, let's put the same data and endpoints into a reverse-ordered CF. The
    physical order of the data will be the reverse of the above:

      key=a, inf_suffix=true
      key=az
      key=ab
      key=a, inf_suffix=false

    Note that inf_suffix=false comes *before* any values with the same prefix.
    And inf_suffix=true comes *after* all values with the same prefix.

    The Endpoint comparison function in RocksDB doesn't "know" if the CF is
    reverse-ordered or not. It uses the Key Comparator for key values, and
    then it assumes that Endpoint(key=$VAL, inf_suffix=false) comes before
    the row with key=$VAL.

    The only way to achieve the required ordering is to flip the endpoint
    flag value before passing class Endpoint to RocksDB. This function does
    it before the lock_range() call.

  @return
     0      Ok
     Other  Error acquiring the lock (wait timeout, deadlock, etc)
*/

int ha_rocksdb::set_range_lock(Rdb_transaction *tx,
                               const Rdb_key_def &kd,
                               const enum ha_rkey_function &find_flag,
                               const rocksdb::Slice &slice_arg,
                               const key_range *const end_key,
                               bool *use_locking_iterator)
{
  rocksdb::Slice end_slice;
  uchar end_slice_buf[MAX_KEY_LENGTH];
  bool start_has_inf_suffix = false, end_has_inf_suffix = false;
  rocksdb::Slice slice(slice_arg);
  *use_locking_iterator= false;

  if (!m_use_range_locking) {
    return 0;
  }
  bool big_range= false;

  /*
    The 'slice' has the left endpoint of the range to lock.
    Figure out the right endpoint.
  */

  if (find_flag == HA_READ_KEY_EXACT ||
      find_flag == HA_READ_PREFIX_LAST) {
    if (slice.size() == Rdb_key_def::INDEX_NUMBER_SIZE) {
      // This is a full table/index scan
      // (in case of HA_READ_PREFIX_LAST, a reverse-ordered one)
      start_has_inf_suffix= false;
      big_range = true;
    } else {
      /*
        HA_READ_KEY_EXACT: 
          This is "key_part= const" interval. We need to lock this range:
          (lookup_value, -inf) < key < (lookup_value, +inf)
        HA_READ_PREFIX_LAST:
          We get here for queries like:

            select * from t1 where pk1=const order by pk1 desc for update

          assuming this uses an index on (pk1, ...).
          We get end_key=nullptr.
      */
      start_has_inf_suffix= false;
      end_has_inf_suffix= true;
      end_slice= slice;
    }
  } else if (find_flag == HA_READ_PREFIX_LAST_OR_PREV) {
    /*
       We get here for queries like:

         select * from t1 where pk1=const1 and pk2 between const2 and const3
         order by pk1 desc
         for update

       assuming this uses an index on (pk1, pk2).
       The slice has the right endpoint: {const1, const3}
       the end_key has the left endpoint: {const1, const2}.
    */

    // Move the right endpoint from slice to end_slice
    end_slice= slice;

    // Pack the left endpoint and make "slice" point to it
    uchar pack_buffer[MAX_KEY_LENGTH];
    uint end_slice_size=
        kd.pack_index_tuple(table, pack_buffer, end_slice_buf,
                            end_key->key, end_key->keypart_map);
    slice= rocksdb::Slice(reinterpret_cast<char *>(end_slice_buf),
                              end_slice_size);
    start_has_inf_suffix= false;
    end_has_inf_suffix= true;
  } else if (find_flag == HA_READ_BEFORE_KEY) {
    /*
      We get here for queries like
        select * from t1
        where              pk <1007 order by pk desc limit 2 for update
        select * from t1
        where pk >=800 and pk <1007 order by pk desc limit 2 for update
    */

    // Move the right endpoint from slice to end_slice
    end_slice= slice;

    if (end_key) {
      uchar pack_buffer[MAX_KEY_LENGTH];
      uint end_slice_size=
          kd.pack_index_tuple(table, pack_buffer, end_slice_buf,
                              end_key->key, end_key->keypart_map);

      slice= rocksdb::Slice(reinterpret_cast<char *>(end_slice_buf),
                            end_slice_size);

      // end_has_inf_suffix is false, because we're looking key<const
    } else {
      uint end_slice_size;
      kd.get_infimum_key(end_slice_buf, &end_slice_size);
      slice= rocksdb::Slice((char*)end_slice_buf, end_slice_size);

     big_range= true;
    }
  } else if (end_key) {
    // Known start range bounds: HA_READ_KEY_OR_NEXT, HA_READ_AFTER_KEY
    if (find_flag == HA_READ_KEY_OR_NEXT)
      start_has_inf_suffix= false;
    else if (find_flag == HA_READ_AFTER_KEY)
      start_has_inf_suffix= true;
    else {
      // Unknown type of range, shouldn't happen
      DBUG_ASSERT(0);
      big_range = true;
    }

    // Known end range bounds: HA_READ_AFTER_KEY, HA_READ_BEFORE_KEY
    if (end_key->flag == HA_READ_AFTER_KEY) {
      // this is "key_part <= const".
      end_has_inf_suffix= true;
    } else if (end_key->flag == HA_READ_BEFORE_KEY) {
      // this is "key_part < const", non-inclusive.
      end_has_inf_suffix= false;
    } else {
      // Unknown type of range, shouldn't happen
      DBUG_ASSERT(0);
      big_range = true;
    }

    uchar pack_buffer[MAX_KEY_LENGTH];
    uint end_slice_size= kd.pack_index_tuple(table, pack_buffer, end_slice_buf,
                                             end_key->key,
                                             end_key->keypart_map);

    end_slice= rocksdb::Slice(reinterpret_cast<char *>(end_slice_buf),
                              end_slice_size);
  } else {
    big_range= true;
#if 0
    // The below is code to handle this without LockingIterator:
    // No end key
    // Known start range bounds: HA_READ_KEY_OR_NEXT, HA_READ_AFTER_KEY
    if (find_flag == HA_READ_KEY_OR_NEXT)
      start_has_inf_suffix= false;
    else if (find_flag == HA_READ_AFTER_KEY)
      start_has_inf_suffix= true;
    else
      DBUG_ASSERT(0);

    uint end_slice_size;
    kd.get_infimum_key(end_slice_buf, &end_slice_size);
    end_slice= rocksdb::Slice((char*)end_slice_buf, end_slice_size);
    end_has_inf_suffix= true;
#endif
  }

  if (big_range) {
    *use_locking_iterator= true;
    return 0;
  }

  rocksdb::Endpoint start_endp;
  rocksdb::Endpoint end_endp;

  if (kd.m_is_reverse_cf) {
    // Flip the endpoint flag values, as explained in the
    // RangeFlagsShouldBeFlippedForRevCF comment above.
    start_endp =rocksdb::Endpoint(end_slice, !end_has_inf_suffix);
    end_endp  = rocksdb::Endpoint(slice, !start_has_inf_suffix);
  } else {
    start_endp= rocksdb::Endpoint(slice, start_has_inf_suffix);
    end_endp=   rocksdb::Endpoint(end_slice, end_has_inf_suffix);
  }

  /*
    RocksDB's iterator is reading the snapshot of the data that was taken at
    the time the iterator was created.

    After we've got a lock on the range, we'll need to refresh the iterator
    to read the latest contents. (If we use the iterator created before the
    lock_range() call, we may miss the changes that were made/committed after
    the iterator was created but before the lock_range() call was made).

    RocksDB has Iterator::Refresh() method, but alas, it is not implemented for
    the iterator returned by Transaction object (Transaction object returns
    BaseDeltaIterator which allows one to see the transactions's own changes).

    Our solution to this is to release the iterator and create the new one.
    We release it here, it will be created as soon as there's a need to read
    records.
  */
  m_iterator->reset();

  auto s= tx->lock_range(kd.get_cf(), start_endp, end_endp);
  if (!s.ok()) {
    return (tx->set_status_error(table->in_use, s, kd, m_tbl_def,
                                 m_table_handler));
  }
  return 0;
}

/*
   See storage/rocksdb/rocksdb-range-access.txt for description of how MySQL
   index navigation commands are converted into RocksDB lookup commands.

   MyRocks needs to decide whether prefix bloom filter can be used or not.
   To decide to use prefix bloom filter or not, calculating equal condition
   length is needed. On equal lookups (find_flag == HA_READ_KEY_EXACT), equal
   condition length is the same as rocksdb::Slice.size() of the start key.
   On range scan, equal condition length is MIN(start_key, end_key) of the
   rocksdb::Slice expression, where end_key is taken from end_range.

   @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::index_read_map(uchar *const buf, const uchar *const key,
                               key_part_map keypart_map,
                               enum ha_rkey_function find_flag) {
  DBUG_ENTER_FUNC();
  ha_statistic_increment(&System_status_var::ha_read_key_count);

  int rc = index_read_intern(buf, key, keypart_map, find_flag);

  // The SQL layer generally expects HA_ERR_KEY_NOT_FOUND for this call.
  if (rc == HA_ERR_END_OF_FILE) {
    rc = HA_ERR_KEY_NOT_FOUND;
  }

  DBUG_RETURN(rc);
}

/**
   @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::index_read_last_map(uchar *const buf, const uchar *const key,
                                    key_part_map keypart_map) {
  DBUG_ENTER_FUNC();

  DBUG_RETURN(index_read_map(buf, key, keypart_map, HA_READ_PREFIX_LAST));
}

/**
   @return
    HA_ADMIN_OK      OK
    other            HA_ADMIN error code
*/
int ha_rocksdb::check(THD *const thd MY_ATTRIBUTE((__unused__)),
                      HA_CHECK_OPT *const check_opt
                          MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(thd != nullptr);
  DBUG_ASSERT(check_opt != nullptr);

  const uint pk = table->s->primary_key;
  String rowkey_copy;
  String sec_key_copy;
  const char *const table_name = table->s->table_name.str;

  // Only when debugging: don't use snapshot when reading
  // Rdb_transaction *tx= get_or_create_tx(table->in_use);
  // tx->snapshot= nullptr;

  bool save_verify_row_debug_checksums =
      m_converter->get_verify_row_debug_checksums();
  m_converter->set_verify_row_debug_checksums(true);
  /* For each secondary index, check that we can get a PK value from it */
  // NO_LINT_DEBUG
  sql_print_information("CHECKTABLE %s: Checking table %s", table_name,
                        table_name);
  ha_rows row_checksums_at_start =
      ha_rows(0);  // set/used iff first_index==true
  ha_rows row_checksums = ha_rows(-1);
  bool first_index = true;

  for (uint keyno = 0; keyno < table->s->keys; keyno++) {
    if (keyno != pk) {
      extra(HA_EXTRA_KEYREAD);
      ha_index_init(keyno, true);
      ha_rows rows = 0;
      m_validated_checksums = 0;
      if (first_index) {
        row_checksums_at_start = m_converter->get_row_checksums_checked();
      }
      int res;
      // NO_LINT_DEBUG
      sql_print_information("CHECKTABLE %s:   Checking index %s", table_name,
                            table->key_info[keyno].name);
      while (1) {
        if (!rows) {
          res = index_first(table->record[0]);
        } else {
          res = index_next(table->record[0]);
        }

        if (res == HA_ERR_END_OF_FILE) break;
        if (res) {
          // error
          // NO_LINT_DEBUG
          sql_print_error("CHECKTABLE %s:   .. row %lld: index scan error %d",
                          table_name, rows, res);
          goto error;
        }
        rocksdb::Slice key = m_iterator->key();
        sec_key_copy.copy(key.data(), key.size(), &my_charset_bin);
        rowkey_copy.copy(m_last_rowkey.ptr(), m_last_rowkey.length(),
                         &my_charset_bin);

        if ((res = get_row_by_rowid(table->record[0], rowkey_copy.ptr(),
                                    rowkey_copy.length()))) {
          // NO_LINT_DEBUG
          sql_print_error(
              "CHECKTABLE %s:   .. row %lld: "
              "failed to fetch row by rowid",
              table_name, rows);
          goto error;
        }

        longlong hidden_pk_id = 0;
        if (has_hidden_pk(table) &&
            read_hidden_pk_id_from_rowkey(&hidden_pk_id)) {
          goto error;
        }

        /* Check if we get the same PK value */
        uint packed_size = m_pk_descr->pack_record(
            table, m_pack_buffer, table->record[0], m_pk_packed_tuple, nullptr,
            false, hidden_pk_id);
        if (packed_size != rowkey_copy.length() ||
            memcmp(m_pk_packed_tuple, rowkey_copy.ptr(), packed_size)) {
          // NO_LINT_DEBUG
          sql_print_error("CHECKTABLE %s:   .. row %lld: PK value mismatch",
                          table_name, rows);
          goto print_and_error;
        }

        /* Check if we get the same secondary key value */
        packed_size = m_key_descr_arr[keyno]->pack_record(
            table, m_pack_buffer, table->record[0], m_sk_packed_tuple,
            &m_sk_tails, false, hidden_pk_id);
        if (packed_size != sec_key_copy.length() ||
            memcmp(m_sk_packed_tuple, sec_key_copy.ptr(), packed_size)) {
          // NO_LINT_DEBUG
          sql_print_error(
              "CHECKTABLE %s:   .. row %lld: "
              "secondary index value mismatch",
              table_name, rows);
          goto print_and_error;
        }
        rows++;
        continue;

      print_and_error : {
        std::string buf;
        buf = rdb_hexdump(rowkey_copy.ptr(), rowkey_copy.length(),
                          RDB_MAX_HEXDUMP_LEN);
        // NO_LINT_DEBUG
        sql_print_error("CHECKTABLE %s:   rowkey: %s", table_name, buf.c_str());

        buf = rdb_hexdump(m_retrieved_record.data(), m_retrieved_record.size(),
                          RDB_MAX_HEXDUMP_LEN);
        // NO_LINT_DEBUG
        sql_print_error("CHECKTABLE %s:   record: %s", table_name, buf.c_str());

        buf = rdb_hexdump(sec_key_copy.ptr(), sec_key_copy.length(),
                          RDB_MAX_HEXDUMP_LEN);
        // NO_LINT_DEBUG
        sql_print_error("CHECKTABLE %s:   index: %s", table_name, buf.c_str());

        goto error;
      }
      }
      // NO_LINT_DEBUG
      sql_print_information(
          "CHECKTABLE %s:   ... %lld index entries checked "
          "(%lld had checksums)",
          table_name, rows, m_validated_checksums);

      if (first_index) {
        row_checksums =
            m_converter->get_row_checksums_checked() - row_checksums_at_start;
        first_index = false;
      }
      ha_index_end();
    }
  }
  if (row_checksums != ha_rows(-1)) {
    // NO_LINT_DEBUG
    sql_print_information("CHECKTABLE %s:   %lld table records had checksums",
                          table_name, row_checksums);
  }
  extra(HA_EXTRA_NO_KEYREAD);

  m_converter->set_verify_row_debug_checksums(save_verify_row_debug_checksums);
  /*
    TODO(sergiy): we should check also for PK records that are missing in
    the secondary indexes.
    For that, need to walk through the PK and check that every PK record has a
    proper counterpart in each secondary index.
  */
  DBUG_RETURN(HA_ADMIN_OK);
error:
  m_converter->set_verify_row_debug_checksums(save_verify_row_debug_checksums);
  ha_index_or_rnd_end();
  extra(HA_EXTRA_NO_KEYREAD);

  DBUG_RETURN(HA_ADMIN_CORRUPT);
}

static void dbug_dump_str(FILE *const out, const char *const str, int len) {
  fprintf(out, "\"");
  for (int i = 0; i < len; i++) {
    if (str[i] > 32) {
      fprintf(out, "%c", str[i]);
    } else {
      fprintf(out, "\\%d", str[i]);
    }
  }
  fprintf(out, "\"");
}

/*
  Debugging help: dump the whole database into a human-readable file.
  Usage:
    dbug_dump_database(rdb);
*/

void dbug_dump_database(rocksdb::DB *const db) {
  FILE *const out = fopen("/tmp/rocksdb.dump", "wt");
  if (!out) return;

  rocksdb::Iterator *it = db->NewIterator(rocksdb::ReadOptions());
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    rocksdb::Slice key = it->key();
    rocksdb::Slice val = it->value();
    dbug_dump_str(out, key.data(), key.size());
    fprintf(out, " -> ");
    dbug_dump_str(out, val.data(), val.size());
    fprintf(out, "\n");
  }

  delete it;
  fclose(out);
}

rocksdb::Status ha_rocksdb::get_for_update(Rdb_transaction *const tx,
                                           const Rdb_key_def &key_descr,
                                           const rocksdb::Slice &key) const {
  DBUG_ASSERT(m_lock_rows != RDB_LOCK_NONE);
  bool exclusive = m_lock_rows != RDB_LOCK_READ;
  bool skip_wait =
      m_locked_row_action == THR_NOWAIT || m_locked_row_action == THR_SKIP;
  return rdb_tx_get_for_update(tx, key_descr, key, nullptr, exclusive,
                               skip_wait);
}

bool ha_rocksdb::is_blind_delete_enabled() {
  THD *thd = ha_thd();
  return (THDVAR(thd, blind_delete_primary_key) &&
          thd->lex->sql_command == SQLCOM_DELETE &&
          thd->lex->table_count == 1 && table->s->keys == 1 &&
          !has_hidden_pk(table) && !thd->rli_slave);
}

/*
  Given a rowid (i.e. packed PK) as a parameter, get the record.

  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/

int ha_rocksdb::get_row_by_rowid(uchar *const buf, const char *const rowid,
                                 const uint rowid_size, bool *skip_row,
                                 const bool skip_lookup,
                                 const bool skip_ttl_check) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(table != nullptr);

  int rc;

  if (skip_row) {
    *skip_row = false;
  }

  rocksdb::Slice key_slice(rowid, rowid_size);

  Rdb_transaction *const tx = get_or_create_tx(table->in_use);
  DBUG_ASSERT(tx != nullptr);

  DEBUG_SYNC(ha_thd(), "rocksdb.get_row_by_rowid");
  DBUG_EXECUTE_IF("dbug.rocksdb.get_row_by_rowid", {
    THD *thd = ha_thd();
    const char act[] =
        "now signal Reached "
        "wait_for signal.rocksdb.get_row_by_rowid_let_running";
    DBUG_ASSERT(opt_debug_sync_timeout > 0);
    DBUG_ASSERT(!debug_sync_set_action(thd, STRING_WITH_LEN(act)));
  };);

  /* Pretend row found without looking up */
  if (skip_lookup) {
    /* TODO(yzha) - rows stas are gone in 8.0
    stats.rows_deleted_blind++; */
    update_row_stats(ROWS_DELETED_BLIND);
    m_last_rowkey.copy((const char *)rowid, rowid_size, &my_charset_bin);
    table->m_status = 0;
    DBUG_RETURN(0);
  }

  if (m_insert_with_update && m_dup_key_found &&
      m_pk_descr->get_keyno() == m_dupp_errkey) {
    DBUG_ASSERT(m_lock_rows == RDB_LOCK_WRITE);
    DBUG_ASSERT(m_dup_key_tuple.length() == key_slice.size());
    DBUG_ASSERT(
        memcmp(m_dup_key_tuple.ptr(), key_slice.data(), key_slice.size()) == 0);

    // We have stored the record with duplicate key in
    // m_dup_key_retrieved_record during write_row already, so just move it
    // over.
    m_retrieved_record = std::move(m_dup_key_retrieved_record);
    rc = HA_EXIT_SUCCESS;
  } else {
    tx->acquire_snapshot(false);
    bool skip_wait =
        m_locked_row_action == THR_NOWAIT || m_locked_row_action == THR_SKIP;
    rc = get_pk_iterator()->get(&key_slice, &m_retrieved_record, m_lock_rows,
                                skip_ttl_check, skip_wait);
  }

  if (!rc) {
    m_last_rowkey.copy((const char *)rowid, rowid_size, &my_charset_bin);
    rc = convert_record_from_storage_format(&key_slice, buf);

    if (!rc) {
      table->m_status = 0;
    }
  } else if (should_skip_locked_record(rc)) {
    if (skip_row) {
      *skip_row = true;
    }
    DBUG_RETURN(HA_ERR_KEY_NOT_FOUND);
  }

  /*
    Note: we don't need to unlock the row. It is intentional that we keep
    locks on rows that don't exist.
  */
  DBUG_RETURN(rc);
}

int ha_rocksdb::records(ha_rows *num_rows) {
  if (m_lock_rows == RDB_LOCK_NONE) {
    // SELECT COUNT(*) without locking, fast path
    m_iteration_only = true;
    auto iteration_guard =
        create_scope_guard([this]() { m_iteration_only = false; });
    return handler::records(num_rows);
  } else {
    // SELECT COUNT(*) with locking, slow path
    return handler::records(num_rows);
  }
}

int ha_rocksdb::records_from_index(ha_rows *num_rows, uint index) {
  if (m_lock_rows == RDB_LOCK_NONE) {
    // SELECT COUNT(*) without locking, fast path
    m_iteration_only = true;
    auto iteration_guard =
        create_scope_guard([this]() { m_iteration_only = false; });
    return handler::records_from_index(num_rows, index);
  } else {
    // SELECT COUNT(*) with locking, slow path
    return handler::records_from_index(num_rows, index);
  }
}

/*
  The analagous function to ha_rocksdb::get_row_by_rowid for performing
  secondary key lookups.

  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::get_row_by_sk(uchar *buf, const Rdb_key_def &kd,
                              const rocksdb::Slice *key) {
  DBUG_ENTER_FUNC();

  int rc = m_iterator->get(key, &m_retrieved_record, RDB_LOCK_NONE);
  if (rc) DBUG_RETURN(rc);

  const uint size =
      kd.get_primary_key_tuple(*m_pk_descr, key, m_pk_packed_tuple);
  if (size == RDB_INVALID_KEY_LEN) {
    DBUG_RETURN(HA_ERR_ROCKSDB_CORRUPT_DATA);
  }

  m_last_rowkey.copy((const char *)m_pk_packed_tuple, size, &my_charset_bin);

  rc = secondary_index_read(active_index, buf, &m_retrieved_record, nullptr);
  if (!rc) {
    table->m_status = 0;
  }
  DBUG_RETURN(rc);
}

/**
  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::index_next(uchar *const buf) {
  DBUG_ENTER_FUNC();

  check_build_decoder();

  ha_statistic_increment(&System_status_var::ha_read_next_count);
  DBUG_RETURN(index_next_with_direction_intern(buf, true, false));
}

/**
  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::index_next_same(uchar *const buf,
                                const uchar *key MY_ATTRIBUTE((unused)),
                                uint keylen MY_ATTRIBUTE((unused))) {
  DBUG_ENTER_FUNC();

  if (m_full_key_lookup) {
#ifndef DBUG_OFF
    uint len = calculate_key_len(table, active_index, HA_WHOLE_KEY);
    DBUG_ASSERT(len == keylen);
#endif
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }
  DBUG_RETURN(index_next(buf));
}

/**
  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::index_prev(uchar *const buf) {
  DBUG_ENTER_FUNC();

  check_build_decoder();

  ha_statistic_increment(&System_status_var::ha_read_prev_count);
  DBUG_RETURN(index_next_with_direction_intern(buf, false, false));
}

/**
  This function handles all the logic for reading out the next row given a valid
  iterator, and populates buf with the next row on success. This includes
  filtering logic (eg. ICP, TTL, etc.), locking and reading from the PK to
  obtain full row if necessary.

  The skip_next parameter determines whether we need to advance the iterator at
  least once. Generally, callers that are seeking (eg.
  index_first/index_read_map/etc.) will set skip_next to true, since the
  iterator is already at the correct position and may not need advancement.
  Callers that are iterating to the next key (eg. index_next/index_prev/etc.)
  will set skip_next to false since the iterator needs to be advanced at least
  once.

  The move_forward parameter controls the direction of iteration.

  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::index_next_with_direction_intern(uchar *const buf,
                                                 bool move_forward,
                                                 bool skip_next) {
  DBUG_ENTER_FUNC();

  THD *thd = ha_thd();
  int rc = 0;
  const Rdb_key_def &kd = *m_key_descr_arr[active_index_pos()];

  table->m_status = STATUS_NOT_FOUND;
  /* TODO(yzha) - row stats are gone in 8.0
  stats.rows_requested++; */

  for (;;) {
    DEBUG_SYNC(thd, "rocksdb.check_flags_inwdi");
    if (thd && thd->killed) {
      rc = HA_ERR_QUERY_INTERRUPTED;
      break;
    }

    DBUG_ASSERT(m_iterator != nullptr);
    if (m_iterator == nullptr) {
      rc = HA_ERR_INTERNAL_ERROR;
      break;
    }

    if (skip_next) {
      skip_next = false;
    } else {
      if (move_forward) {
        rc = m_iterator->next();
      } else {
        rc = m_iterator->prev();
      }
    }

    if (rc != HA_EXIT_SUCCESS) {
      break;
    }

    const rocksdb::Slice &key = m_iterator->key();
    const rocksdb::Slice &value = m_iterator->value();

    if (m_iteration_only) {
      table->m_status = 0;
      rc = 0;
    } else if (active_index == table->s->primary_key) {
      if (m_lock_rows != RDB_LOCK_NONE && !m_use_range_locking) {
        DEBUG_SYNC(ha_thd(), "rocksdb_concurrent_delete");
        /* We need to put a lock and re-read */
        bool skip_row = false;
        rc = get_row_by_rowid(buf, key.data(), key.size(), &skip_row);
        if (rc != HA_EXIT_SUCCESS && skip_row) {
          // We are asked to skip locked rows
          continue;
        }
      } else {
        /* Unpack from the row we've read */
        m_last_rowkey.copy(key.data(), key.size(), &my_charset_bin);
        rc = convert_record_from_storage_format(&key, &value, buf);
      }
    } else {
      rc = kd.unpack_record(table, buf, &key, &value,
                            m_converter->get_verify_row_debug_checksums());
      if (rc != HA_EXIT_SUCCESS) {
        break;
      }

      // Record did not satisfy ICP, move to next record
      if (pushed_idx_cond && pushed_idx_cond_keyno == active_index) {
        const enum icp_result icp_status = check_index_cond();
        if (icp_status == ICP_NO_MATCH) {
          continue;
        } else if (icp_status == ICP_OUT_OF_RANGE) {
          rc = HA_ERR_END_OF_FILE;
          break;
        }
        DBUG_ASSERT(icp_status == ICP_MATCH);
      }

      const uint size =
          kd.get_primary_key_tuple(*m_pk_descr, &key, m_pk_packed_tuple);
      if (size == RDB_INVALID_KEY_LEN) {
        rc = HA_ERR_ROCKSDB_CORRUPT_DATA;
        break;
      }

      m_last_rowkey.copy((const char *)m_pk_packed_tuple, size,
                         &my_charset_bin);

      bool skip_row = false;
      rc = secondary_index_read(active_index, buf, &value, &skip_row);
      if (!rc && skip_row) {
        // SKIP LOCKED
        continue;
      }
    }

    if (!should_skip_invalidated_record(rc)) {
      break;
    }
  }

  if (!rc) {
    /* TODO(yzha) - row stats are gone in 8.0
    stats.rows_read++;
    stats.rows_index_next++; */
    update_row_stats(ROWS_READ);
    table->m_status = 0;
  }

  // skip_next is false when called from functions that are trying to iterate
  // through keys such as index_next/rnd_next/etc. and these functions
  // typically expect HA_ERR_END_OF_FILE if no next key is found.
  if (!skip_next) {
    if (rc == HA_ERR_KEY_NOT_FOUND) {
      rc = HA_ERR_END_OF_FILE;
    }
  }

  DBUG_RETURN(rc);
}

Rdb_iterator_base *ha_rocksdb::get_pk_iterator() {
  if (!m_pk_iterator) {
    m_pk_iterator.reset(
        new Rdb_iterator_base(ha_thd(), m_pk_descr, m_pk_descr, m_tbl_def));
  }
  return m_pk_iterator.get();
}

/**
  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::index_first(uchar *const buf) {
  DBUG_ENTER_FUNC();

  check_build_decoder();

  ha_statistic_increment(&System_status_var::ha_read_first_count);
  DBUG_RETURN(index_read_intern(buf, true /* first */));
}

/**
  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::index_last(uchar *const buf) {
  DBUG_ENTER_FUNC();

  check_build_decoder();

  ha_statistic_increment(&System_status_var::ha_read_last_count);
  DBUG_RETURN(index_read_intern(buf, false /* first */));
}

/*
  index_read_intern is called by handler methods doing full table/index scans
  (eg. index_first/index_last).

  'first' refers to the logical seek direction in the table.

  The implementation simply calls index_read_intern with nullptr as the key
  (which will default to using just the index id as the key) with either
  HA_READ_KEY_EXACT or HA_READ_PREFIX_LAST, for ascending and descending
  respectively. This will automatically give us the correct semantics as
  HA_READ_KEY_EXACT will return the first key with the given prefix, whereas
  HA_READ_PREFIX_LAST will return the last key with the given prefix.

  At a lower level, there are four cases being handled in this function.

  1. Seeking to first row of table with forwards cf:

     An picture of a forward-ordered keyspace (remember, the keys have form
     'indexnr-keyval'. Suppose the index we are at has number n)

     (n-1) - ...
     ( n )          <--- 1. (n) doesn't exist in the db but it would be here.
     ( n ) - aaa       <--- 2. Seek("n") will put us here on the first index
     ( n ) - bbb               record.
     ( n ) - cc

     So, need to do: Seek(n);

  2. Seeking to last row of table with reverse cf:

     (n+1) - bbb
     (n+1) - aaa
     (n+1)        <--- (n+1) doesn't exist in the db but would be here.
     ( n ) - ccc       <--- 1. We need to be here.
     ( n ) - bbb
     ( n ) - aaa
     ( n )

     So, need to: Seek(n+1);

  3. Seeking to last row of table with forwards cf:

     An picture of a forward-ordered keyspace (remember, the keys have form
    'indexnr-keyval'. Suppose the we are at a key that has number n)

     (n-1)-something
     ( n )-aaa
     ( n )-bbb
     ( n )-ccc            <----------- Need to seek to here.
     (n+1)      <---- Doesn't exist, but would be here.
     (n+1)-smth, or no value at all

     RocksDB's Iterator::SeekForPrev($val) seeks to "at $val or last value
     that's smaller". We can't seek to "(n)-ccc" directly, because we don't know
     what is the value of 'ccc' (the biggest record with prefix (n)). Instead, we
     seek to "(n+1)", which is the least possible value that's greater than any
     value in index #n.

     So, need to:  it->SeekForPrev(n+1)

  4. Seeking to first row of table with reverse cf:

     (n+1)-something
     ( n ) - ccc
     ( n ) - bbb
     ( n ) - aaa       <---------------- (*) Need to seek here.
     ( n ) <--- Doesn't exist, but would be here.
     (n-1)-smth, or no value at all

     So, need to:  it->SeekForPrev(n)
*/
int ha_rocksdb::index_read_intern(uchar *const buf, bool first) {
  DBUG_ENTER_FUNC();
  int rc = index_read_intern(buf, nullptr, 0,
                             first ? HA_READ_KEY_EXACT : HA_READ_PREFIX_LAST);

  if (rc == HA_ERR_KEY_NOT_FOUND) rc = HA_ERR_END_OF_FILE;

  DBUG_RETURN(rc);
}

void ha_rocksdb::unlock_row() {
  DBUG_ENTER_FUNC();

  // Don't release the lock when using range locking.
  // This breaks m_row_lock_count
  if (m_lock_rows != RDB_LOCK_NONE && !m_use_range_locking) {
    Rdb_transaction *const tx = get_or_create_tx(table->in_use);
    tx->release_lock(*m_pk_descr,
                     std::string(m_last_rowkey.ptr(), m_last_rowkey.length()));
  }

  DBUG_VOID_RETURN;
}

/*
  Returning true if SingleDelete can be used.
  - Secondary Indexes can always use SingleDelete.
  - If the index is PRIMARY KEY, and if all of the columns of the table
    are covered by the PRIMARY KEY, SingleDelete can be used.
*/
bool ha_rocksdb::can_use_single_delete(const uint index) const {
  return (index != pk_index(table, m_tbl_def) ||
          (!has_hidden_pk(table) &&
           table->key_info[index].actual_key_parts == table->s->fields));
}

bool ha_rocksdb::skip_unique_check() const {
  /*
    We want to skip unique checks if:
      1) bulk_load is on
      2) the user set unique_checks option to 0, and the table does not have
         any indexes. If the table has secondary keys, then those might becomes
         inconsisted/corrupted
      3) We're using read-free replication
  */
  return THDVAR(table->in_use, bulk_load) ||
         (my_core::thd_test_options(table->in_use,
                                    OPTION_RELAXED_UNIQUE_CHECKS) &&
          m_tbl_def->m_key_count == 1) ||
         use_read_free_rpl();
}

bool commit_in_the_middle(THD *thd) {
  // It does not make sense to use write unprepared with commit in the middle,
  // since both handle large transactions by flushing the write batches onto
  // disk.
  //
  // For the two to work together, we would need to assign a new xid after
  // committing.
  return (THDVAR(thd, bulk_load) || THDVAR(thd, commit_in_the_middle)) &&
         rocksdb_write_policy != rocksdb::TxnDBWritePolicy::WRITE_UNPREPARED;
}

/*
  Executing bulk commit if it should.
  @retval true if bulk commit failed
  @retval false if bulk commit was skipped or succeeded
*/
bool ha_rocksdb::do_bulk_commit(Rdb_transaction *const tx) {
  return commit_in_the_middle(table->in_use) &&
         tx->get_write_count() >= THDVAR(table->in_use, bulk_load_size) &&
         tx->flush_batch();
}

/*
  If table was created without primary key, SQL layer represents the primary
  key number as MAX_INDEXES.  Hence, this function returns true if the table
  does not contain a primary key. (In which case we generate a hidden
  'auto-incremented' pk.)
*/
bool ha_rocksdb::has_hidden_pk(const TABLE *const tabl) const {
  return Rdb_key_def::table_has_hidden_pk(tabl);
}

/*
  Returns true if given index number is a hidden_pk.
  - This is used when a table is created with no primary key.
*/
bool ha_rocksdb::is_hidden_pk(const uint index, const TABLE *const table_arg,
                              const Rdb_tbl_def *const tbl_def_arg) {
  DBUG_ASSERT(table_arg->s != nullptr);

  return (table_arg->s->primary_key == MAX_INDEXES &&
          index == tbl_def_arg->m_key_count - 1);
}

/* Returns index of primary key */
uint ha_rocksdb::pk_index(const TABLE *const table_arg,
                          const Rdb_tbl_def *const tbl_def_arg) {
  DBUG_ASSERT(table_arg->s != nullptr);

  return table_arg->s->primary_key == MAX_INDEXES ? tbl_def_arg->m_key_count - 1
                                                  : table_arg->s->primary_key;
}

/* Returns the index into m_key_descr_arr array based on active_index */
uint ha_rocksdb::active_index_pos() {
  return active_index == MAX_KEY ? m_tbl_def->m_key_count - 1 : active_index;
}

/* Returns true if given index number is a primary key */
bool ha_rocksdb::is_pk(const uint index, const TABLE *const table_arg,
                       const Rdb_tbl_def *const tbl_def_arg) {
  DBUG_ASSERT(table_arg->s != nullptr);

  return index == table_arg->s->primary_key ||
         is_hidden_pk(index, table_arg, tbl_def_arg);
}

uint ha_rocksdb::max_supported_key_part_length(
    HA_CREATE_INFO *create_info MY_ATTRIBUTE((unused))) const {
  DBUG_ENTER_FUNC();
  DBUG_RETURN(rocksdb_large_prefix ? MAX_INDEX_COL_LEN_LARGE
                                   : MAX_INDEX_COL_LEN_SMALL);
}

const char *ha_rocksdb::get_key_name(const uint index,
                                     const TABLE *const table_arg,
                                     const Rdb_tbl_def *const tbl_def_arg) {
  if (is_hidden_pk(index, table_arg, tbl_def_arg)) {
    return HIDDEN_PK_NAME;
  }

  DBUG_ASSERT(table_arg->key_info != nullptr);
  DBUG_ASSERT(table_arg->key_info[index].name != nullptr);

  return table_arg->key_info[index].name;
}

const char *ha_rocksdb::get_key_comment(const uint index,
                                        const TABLE *const table_arg,
                                        const Rdb_tbl_def *const tbl_def_arg) {
  if (is_hidden_pk(index, table_arg, tbl_def_arg)) {
    return nullptr;
  }

  DBUG_ASSERT(table_arg->key_info != nullptr);

  return table_arg->key_info[index].comment.str;
}

const std::string ha_rocksdb::generate_cf_name(
    const uint index, const TABLE *const table_arg,
    const Rdb_tbl_def *const tbl_def_arg, bool *per_part_match_found) {
  DBUG_ASSERT(table_arg != nullptr);
  DBUG_ASSERT(tbl_def_arg != nullptr);
  DBUG_ASSERT(per_part_match_found != nullptr);

  // When creating CF-s the caller needs to know if there was a custom CF name
  // specified for a given paritition.
  *per_part_match_found = false;

  // Index comment is used to define the column family name specification(s).
  // If there was no comment, we get an emptry string, and it means "use the
  // default column family".
  const char *const comment = get_key_comment(index, table_arg, tbl_def_arg);

  // `get_key_comment` can return `nullptr`, that's why this.
  std::string key_comment = comment ? comment : "";

  std::string cf_name = Rdb_key_def::parse_comment_for_qualifier(
      key_comment, table_arg, tbl_def_arg, per_part_match_found,
      RDB_CF_NAME_QUALIFIER);

  if (table_arg->part_info != nullptr && !*per_part_match_found) {
    // At this point we tried to search for a custom CF name for a partition,
    // but none was specified. Therefore default one will be used.
    return "";
  }

  // If we didn't find any partitioned/non-partitioned qualifiers, return the
  // comment itself.  NOTE: this currently handles returning the cf name
  // specified in the index comment in the case of no partitions, which doesn't
  // use any qualifiers at the moment. (aka its a special case)
  if (cf_name.empty() && !key_comment.empty()) {
    return key_comment;
  }

  return cf_name;
}

const std::string ha_rocksdb::get_table_comment(const TABLE *const table_arg) {
  DBUG_ASSERT(table_arg->s != nullptr);

  return table_arg->s->comment.str;
}

/**
  Write a new row

  @param[in] buf                new row data to write
  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::write_row(uchar *const buf) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(buf != nullptr);
  DBUG_ASSERT(buf == table->record[0]);
  DBUG_ASSERT(m_lock_rows == RDB_LOCK_WRITE);

  ha_statistic_increment(&System_status_var::ha_write_count);
  /*
    Note: "buf == table->record[0]" is copied from innodb. I am not aware of
    any use cases where this condition is not true.
  */
  if (table->next_number_field && buf == table->record[0]) {
    int err;
    if ((err = update_auto_increment())) {
      DBUG_RETURN(err);
    }
  }

  // clear cache at beginning of write for INSERT ON DUPLICATE
  // we may get multiple write->fail->read->update if there are multiple
  // values from INSERT
  m_dup_key_found = false;

  const int rv = update_write_row(nullptr, buf, skip_unique_check());

  if (rv == 0) {
    /* TODO(yzha) - row stats are gone in 8.0
    stats.rows_inserted++; */

    // Not protected by ddl_manger lock for performance
    // reasons. This is an estimate value anyway.
    inc_table_n_rows();
    update_table_stats_if_needed();

    update_row_stats(ROWS_INSERTED);
  }

  DBUG_RETURN(rv);
}

// Increment the number of rows in the table by one.
// This operation is not protected by ddl manager lock.
// The number is estimated.
void ha_rocksdb::inc_table_n_rows() {
  if (!rocksdb_table_stats_use_table_scan) {
    return;
  }

  uint64 n_rows = m_tbl_def->m_tbl_stats.m_stat_n_rows;
  if (n_rows < std::numeric_limits<ulonglong>::max()) {
    m_tbl_def->m_tbl_stats.m_stat_n_rows = n_rows + 1;
  }
}

// Decrement the number of rows in the table by one.
// This operation is not protected by ddl manager lock.
// The number is estimated.
void ha_rocksdb::dec_table_n_rows() {
  if (!rocksdb_table_stats_use_table_scan) {
    return;
  }

  uint64 n_rows = m_tbl_def->m_tbl_stats.m_stat_n_rows;
  if (n_rows > 0) {
    m_tbl_def->m_tbl_stats.m_stat_n_rows = n_rows - 1;
  }
}

/**
  Constructing m_last_rowkey (MyRocks key expression) from
  before_update|delete image (MySQL row expression).
  m_last_rowkey is normally set during lookup phase, such as
  rnd_next() and rnd_pos(). With Read Free Replication,
  these read functions are skipped and update_rows(), delete_rows() are
  called without setting m_last_rowkey. This function sets m_last_rowkey
  for Read Free Replication.
*/
void ha_rocksdb::set_last_rowkey(
    const uchar *const old_data MY_ATTRIBUTE((__unused__))) {
  if (old_data && use_read_free_rpl()) {
    const int old_pk_size = m_pk_descr->pack_record(
        table, m_pack_buffer, old_data, m_pk_packed_tuple, nullptr, false);
    m_last_rowkey.copy((const char *)m_pk_packed_tuple, old_pk_size,
                       &my_charset_bin);
  }
}

void ha_rocksdb::set_last_rowkey(const char *str, size_t len) {
  m_last_rowkey.copy(str, len, &my_charset_bin);
}

/**
  Collect update data for primary key

  @param[in, out] row_info            hold all data for update row, such as
                                      new row data/old row data
  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::get_pk_for_update(struct update_row_info *const row_info) {
  int size;

  /*
    Get new row key for any insert, and any update where the pk is not hidden.
    Row key for updates with hidden pk is handled below.
  */
  if (!has_hidden_pk(table)) {
    row_info->hidden_pk_id = 0;

    row_info->new_pk_unpack_info = &m_pk_unpack_info;

    size = m_pk_descr->pack_record(
        table, m_pack_buffer, row_info->new_data, m_pk_packed_tuple,
        row_info->new_pk_unpack_info, false, 0, 0, nullptr);
  } else if (row_info->old_data == nullptr) {
    row_info->hidden_pk_id = update_hidden_pk_val();
    size =
        m_pk_descr->pack_hidden_pk(row_info->hidden_pk_id, m_pk_packed_tuple);
  } else {
    /*
      If hidden primary key, rowkey for new record will always be the same as
      before
    */
    size = row_info->old_pk_slice.size();
    memcpy(m_pk_packed_tuple, row_info->old_pk_slice.data(), size);
    int err = read_hidden_pk_id_from_rowkey(&row_info->hidden_pk_id);
    if (err) {
      return err;
    }
  }

  row_info->new_pk_slice =
      rocksdb::Slice((const char *)m_pk_packed_tuple, size);

  return HA_EXIT_SUCCESS;
}

/**
   Check the specified primary key value is unique and also lock the row

  @param[in] row_info         hold all data for update row, such as old row
                              data and new row data
  @param[out] found           whether the primary key exists before.
  @param[out] pk_changed      whether primary key is changed
  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::check_and_lock_unique_pk(const struct update_row_info &row_info,
                                         bool *const found,
                                         const bool skip_unique_check) {
  DBUG_ASSERT(found != nullptr);

  DBUG_ASSERT(row_info.old_pk_slice.size() == 0 ||
              row_info.new_pk_slice.compare(row_info.old_pk_slice) != 0);

  /* Ignore PK violations if this is a optimized 'replace into' */
  const bool ignore_pk_unique_check =
      ha_thd()->lex->blind_replace_into || skip_unique_check;
  rocksdb::PinnableSlice value;
  rocksdb::PinnableSlice *pslice =
      m_insert_with_update ? &m_dup_key_retrieved_record : &value;

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
  int rc = get_pk_iterator()->get(&row_info.new_pk_slice,
                                  ignore_pk_unique_check ? nullptr : pslice,
                                  m_lock_rows);

  if (rc && rc != HA_ERR_KEY_NOT_FOUND) {
    return rc;
  }

  bool key_found = ignore_pk_unique_check ? false : (rc == HA_EXIT_SUCCESS);

  if (key_found && row_info.old_data == nullptr && m_insert_with_update) {
    // In INSERT ON DUPLICATE KEY UPDATE ... case, if the insert failed
    // due to a duplicate key, remember the last key and skip the check
    // next time
    m_dup_key_found = true;
#ifndef DBUG_OFF
    // save it for sanity checking later
    m_dup_key_tuple.copy(row_info.new_pk_slice.data(),
                         row_info.new_pk_slice.size(), &my_charset_bin);
#endif
  }

  *found = key_found;

  return HA_EXIT_SUCCESS;
}

int ha_rocksdb::acquire_prefix_lock(const Rdb_key_def &kd, Rdb_transaction *tx,
                                    const uchar *data) {
  DBUG_ASSERT(kd.is_partial_index());
  // Obtain shared lock on prefix.
  uint size = kd.pack_record(table, m_pack_buffer, data, m_sk_packed_tuple,
                             nullptr, false, 0, kd.partial_index_keyparts());
  const rocksdb::Slice prefix_slice =
      rocksdb::Slice((const char *)m_sk_packed_tuple, size);

  const rocksdb::Status s =
      tx->get_for_update(kd, prefix_slice, nullptr, false /* exclusive */,
                         false /* do validate */, false /* no_wait */);
  if (!s.ok()) {
    return tx->set_status_error(table->in_use, s, kd, m_tbl_def,
                                m_table_handler);
  }

  return HA_EXIT_SUCCESS;
}

/**
   Check the specified secondary key value is unique and also lock the row

  @param[in] key_id           key index
  @param[in] row_info         hold all data for update row, such as old row
                              data and new row data
  @param[out] found           whether specified key value exists before.
  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::check_and_lock_sk(
    const uint key_id, const struct update_row_info &row_info,
    bool *const found,
    const bool skip_unique_check MY_ATTRIBUTE((__unused__))) {
  DBUG_ASSERT(found != nullptr);
  *found = false;

  /*
    Can skip checking this key if none of the key fields have changed.
  */
  if (row_info.old_data != nullptr && !m_update_scope.is_set(key_id)) {
    return HA_EXIT_SUCCESS;
  }

  KEY *key_info = nullptr;
  uint n_null_fields = 0;
  uint user_defined_key_parts = 1;
  int rc = HA_EXIT_SUCCESS;

  key_info = &table->key_info[key_id];
  user_defined_key_parts = key_info->user_defined_key_parts;
  const Rdb_key_def &kd = *m_key_descr_arr[key_id];

  if (kd.is_partial_index()) {
    // Obtain shared lock on new/old prefix.
    if (row_info.old_data != nullptr) {
      rc = acquire_prefix_lock(kd, row_info.tx, row_info.old_data);
      if (rc) return rc;
    }

    DBUG_ASSERT(row_info.new_data != nullptr);
    rc = acquire_prefix_lock(kd, row_info.tx, row_info.new_data);
    if (rc) return rc;
  }

  /*
    If there are no uniqueness requirements, there's no need to obtain a
    lock for this key.
  */
  if (!(key_info->flags & HA_NOSAME)) {
    if (rocksdb_use_range_locking)
      return check_and_lock_non_unique_sk(key_id, row_info);
    return HA_EXIT_SUCCESS;
  }

  /*
    Calculate the new key for obtaining the lock

    For unique secondary indexes, the key used for locking does not
    include the extended fields.
  */
  int size =
      kd.pack_record(table, m_pack_buffer, row_info.new_data, m_sk_packed_tuple,
                     nullptr, false, 0, user_defined_key_parts, &n_null_fields);
  if (n_null_fields > 0) {
    /*
      If any fields are marked as NULL this will never match another row as
      to NULL never matches anything else including another NULL.
     */
    return HA_EXIT_SUCCESS;
  }

  const rocksdb::Slice new_slice =
      rocksdb::Slice((const char *)m_sk_packed_tuple, size);

  /*
     Acquire lock on the old key in case of UPDATE
  */
  if (row_info.old_data != nullptr) {
    size = kd.pack_record(table, m_pack_buffer, row_info.old_data,
                          m_sk_packed_tuple_old, nullptr, false, 0,
                          user_defined_key_parts);
    const rocksdb::Slice old_slice =
        rocksdb::Slice((const char *)m_sk_packed_tuple_old, size);

    const rocksdb::Status s = get_for_update(row_info.tx, kd, old_slice);
    if (!s.ok()) {
      return row_info.tx->set_status_error(table->in_use, s, kd, m_tbl_def,
                                           m_table_handler);
    }

    /*
      If the old and new keys are the same we're done since we've already taken
      the lock on the old key
    */
    if (!new_slice.compare(old_slice)) {
      return HA_EXIT_SUCCESS;
    }
  }

  /*
    Perform a read to determine if a duplicate entry exists - since this is
    a secondary indexes a range scan is needed.

    note: we intentionally don't set options.snapshot here. We want to read
    the latest committed data.
  */

  const bool all_parts_used = (user_defined_key_parts == kd.get_key_parts());

  /*
    This iterator seems expensive since we need to allocate and free
    memory for each unique index.

    If this needs to be optimized, for keys without NULL fields, the
    extended primary key fields can be migrated to the value portion of the
    key. This enables using Get() instead of Seek() as in the primary key
    case.

    The bloom filter may need to be disabled for this lookup.
  */
  DBUG_ASSERT(!m_key_descr_arr[key_id]->is_partial_index());
  Rdb_iterator_base iter(ha_thd(), m_key_descr_arr[key_id], m_pk_descr,
                         m_tbl_def);

  /*
    If all_parts_used is true, then PK uniqueness check/lock would already
    guarantee SK uniqueness. We could optimize this by calling non-locking
    get instead.
  */
  rc = iter.get(&new_slice, all_parts_used ? &m_retrieved_record : nullptr,
                m_lock_rows);
  if (rc && rc != HA_ERR_KEY_NOT_FOUND) {
    return rc;
  }

  if (!all_parts_used) {
    rc = iter.seek(HA_READ_KEY_EXACT, new_slice, false /* full_key_match */,
                   new_slice, true /* read current */);

    if (rc && rc != HA_ERR_END_OF_FILE) {
      return rc;
    }
  }

  *found = (rc == HA_EXIT_SUCCESS);

  rc = HA_EXIT_SUCCESS;
  if (*found && m_insert_with_update) {
    const rocksdb::Slice &rkey = all_parts_used ? new_slice : iter.key();
    uint pk_size =
        kd.get_primary_key_tuple(*m_pk_descr, &rkey, m_pk_packed_tuple);
    if (pk_size == RDB_INVALID_KEY_LEN) {
      rc = HA_ERR_ROCKSDB_CORRUPT_DATA;
    } else {
      m_dup_key_found = true;
      m_last_rowkey.copy((const char *)m_pk_packed_tuple, pk_size,
                         &my_charset_bin);
#ifndef DBUG_OFF
      // save it for sanity checking later
      m_dup_key_tuple.copy(rkey.data(), rkey.size(), &my_charset_bin);
#endif
    }
  }

  return rc;
}


/**
  @brief
    Lock the non-unique sk for range locking
*/
int ha_rocksdb::check_and_lock_non_unique_sk(
    const uint key_id, const struct update_row_info &row_info) {

  DBUG_ASSERT(rocksdb_use_range_locking);
  const Rdb_key_def &kd = *m_key_descr_arr[key_id];
  bool store_row_debug_checksums = should_store_row_debug_checksums();

  if (row_info.old_data != nullptr) {
    rocksdb::Slice old_key_slice;
    int old_packed_size;

    old_packed_size = kd.pack_record(
        table, m_pack_buffer, row_info.old_data, m_sk_packed_tuple_old,
        &m_sk_tails_old, store_row_debug_checksums, row_info.hidden_pk_id, 0,
        nullptr, m_ttl_bytes);

    old_key_slice = rocksdb::Slice(
        reinterpret_cast<const char *>(m_sk_packed_tuple_old), old_packed_size);

    auto s= row_info.tx->lock_singlepoint_range(kd.get_cf(), old_key_slice);
    if (!s.ok()) {
      return (row_info.tx->set_status_error(table->in_use, s, kd,
                                            m_tbl_def, m_table_handler));
    }
  }

  int new_packed_size;
  rocksdb::Slice new_key_slice;
  rocksdb::Slice new_value_slice;
  new_packed_size =
      kd.pack_record(table, m_pack_buffer, row_info.new_data,
                     m_sk_packed_tuple, &m_sk_tails, 0,
                     row_info.hidden_pk_id, 0, nullptr, m_ttl_bytes);
  new_key_slice = rocksdb::Slice(
      reinterpret_cast<const char *>(m_sk_packed_tuple), new_packed_size);

  auto s= row_info.tx->lock_singlepoint_range(kd.get_cf(), new_key_slice);
  if (!s.ok()) {
    return (row_info.tx->set_status_error(table->in_use, s, kd,
                                          m_tbl_def, m_table_handler));
  }

  return HA_EXIT_SUCCESS;
}


/**
   Enumerate all keys to check their uniquess and also lock it

  @param[in] row_info         hold all data for update row, such as old row
                              data and new row data
  @param[out] pk_changed      whether primary key is changed
  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::check_uniqueness_and_lock(
    const struct update_row_info &row_info, bool pk_changed,
    bool skip_unique_check) {
  Rdb_transaction *const tx = get_or_create_tx(ha_thd());
  tx->acquire_snapshot(false);

  /*
    Go through each index and determine if the index has uniqueness
    requirements. If it does, then try to obtain a row lock on the new values.
    Once all locks have been obtained, then perform the changes needed to
    update/insert the row.
  */
  for (uint key_id = 0; key_id < m_tbl_def->m_key_count; key_id++) {
    bool found;
    int rc;

    DBUG_EXECUTE_IF("rocksdb_blob_crash",
                    DBUG_SET("+d,rocksdb_check_uniqueness"););
    if (is_pk(key_id, table, m_tbl_def)) {
      if (row_info.old_pk_slice.size() > 0 && !pk_changed) {
        found = false;
        rc = HA_EXIT_SUCCESS;
      } else {
        rc = check_and_lock_unique_pk(row_info, &found, skip_unique_check);
        DEBUG_SYNC(ha_thd(), "rocksdb.after_unique_pk_check");
      }
    } else {
      rc = check_and_lock_sk(key_id, row_info, &found, skip_unique_check);
      DEBUG_SYNC(ha_thd(), "rocksdb.after_unique_sk_check");
    }
    DBUG_EXECUTE_IF("rocksdb_blob_crash",
                    DBUG_SET("-d,rocksdb_check_uniqueness"););

    if (rc != HA_EXIT_SUCCESS) {
      return rc;
    }

    if (found) {
      /* There is a row with this key already, so error out. */
      errkey = key_id;
      m_dupp_errkey = errkey;

      return HA_ERR_FOUND_DUPP_KEY;
    }
  }

  return HA_EXIT_SUCCESS;
}

int ha_rocksdb::bulk_load_key(Rdb_transaction *const tx, const Rdb_key_def &kd,
                              const rocksdb::Slice &key,
                              const rocksdb::Slice &value, bool sort) {
  DBUG_ENTER_FUNC();
  int res;
  THD *thd = ha_thd();
  if (thd && thd->killed) {
    DBUG_RETURN(HA_ERR_QUERY_INTERRUPTED);
  }

  rocksdb::ColumnFamilyHandle *cf = kd.get_cf();

  // In the case of unsorted inserts, m_sst_info allocated here is not
  // used to store the keys. It is still used to indicate when tables
  // are switched.
  if (m_sst_info == nullptr || m_sst_info->is_done()) {
    m_sst_info.reset(new Rdb_sst_info(rdb, m_table_handler->m_table_name,
                                      kd.get_name(), cf, *rocksdb_db_options,
                                      THDVAR(ha_thd(), trace_sst_api)));
    res = tx->start_bulk_load(this, m_sst_info);
    if (res != HA_EXIT_SUCCESS) {
      DBUG_RETURN(res);
    }
  }
  DBUG_ASSERT(m_sst_info);

  if (sort) {
    Rdb_index_merge *key_merge;
    DBUG_ASSERT(cf != nullptr);

    res = tx->get_key_merge(kd.get_gl_index_id(), cf, &key_merge);
    if (res == HA_EXIT_SUCCESS) {
      res = key_merge->add(key, value);
    }
  } else {
    res = m_sst_info->put(key, value);
  }

  DBUG_RETURN(res);
}

int ha_rocksdb::finalize_bulk_load(bool print_client_error) {
  DBUG_ENTER_FUNC();

  int res = HA_EXIT_SUCCESS;

  /* Skip if there are no possible ongoing bulk loads */
  if (m_sst_info) {
    if (m_sst_info->is_done()) {
      m_sst_info.reset();
      DBUG_RETURN(res);
    }

    Rdb_sst_info::Rdb_sst_commit_info commit_info;

    // Wrap up the current work in m_sst_info and get ready to commit
    // This transfer the responsibility of commit over to commit_info
    res = m_sst_info->finish(&commit_info, print_client_error);
    if (res == 0) {
      // Make sure we have work to do - under race condition we could lose
      // to another thread and end up with no work
      if (commit_info.has_work()) {
        rocksdb::IngestExternalFileOptions opts;
        opts.move_files = true;
        opts.snapshot_consistency = false;
        opts.allow_global_seqno = false;
        opts.allow_blocking_flush = false;

        const rocksdb::Status s = rdb->IngestExternalFile(
            commit_info.get_cf(), commit_info.get_committed_files(), opts);
        if (!s.ok()) {
          if (print_client_error) {
            Rdb_sst_info::report_error_msg(s, nullptr);
          }
          res = HA_ERR_ROCKSDB_BULK_LOAD;
        } else {
          // Mark the list of SST files as committed, otherwise they'll get
          // cleaned up when commit_info destructs
          commit_info.commit();
        }
      }
    }
    m_sst_info.reset();
  }
  DBUG_RETURN(res);
}

/**
  Update an existing primary key record or write a new primary key record

  @param[in] kd                the primary key is being update/write
  @param[in] update_row_info   hold all row data, such as old row data and
                               new row data
  @param[in] pk_changed        whether primary key is changed
  @return
    HA_EXIT_SUCCESS OK
    Other           HA_ERR error code (can be SE-specific)
 */
int ha_rocksdb::update_write_pk(const Rdb_key_def &kd,
                                const struct update_row_info &row_info,
                                bool pk_changed) {
  uint key_id = kd.get_keyno();
  bool hidden_pk = is_hidden_pk(key_id, table, m_tbl_def);
  ulonglong bytes_written = 0;

  /*
    If the PK has changed, or if this PK uses single deletes and this is an
    update, the old key needs to be deleted. In the single delete case, it
    might be possible to have this sequence of keys: PUT(X), PUT(X), SD(X),
    resulting in the first PUT(X) showing up.
  */
  if (!hidden_pk && (pk_changed || ((row_info.old_pk_slice.size() > 0) &&
                                    can_use_single_delete(key_id)))) {
    const rocksdb::Status s = delete_or_singledelete(
        key_id, row_info.tx, kd.get_cf(), row_info.old_pk_slice);
    if (!s.ok()) {
      return row_info.tx->set_status_error(table->in_use, s, kd, m_tbl_def,
                                           m_table_handler);
    } else {
      bytes_written = row_info.old_pk_slice.size();
    }
  }

  if (table->found_next_number_field) {
    update_auto_incr_val_from_field();
  }

  int rc = HA_EXIT_SUCCESS;
  rocksdb::Slice value_slice;
  /* Prepare the new record to be written into RocksDB */
  if ((rc = m_converter->encode_value_slice(
           m_pk_descr, row_info.new_pk_slice, row_info.new_pk_unpack_info,
           !row_info.old_pk_slice.empty(), should_store_row_debug_checksums(),
           m_ttl_bytes, &m_ttl_bytes_updated, &value_slice))) {
    return rc;
  }

  const auto cf = m_pk_descr->get_cf();
  if (rocksdb_enable_bulk_load_api && THDVAR(table->in_use, bulk_load) &&
      !hidden_pk) {
    /*
      Write the primary key directly to an SST file using an SstFileWriter
     */
    rc = bulk_load_key(row_info.tx, kd, row_info.new_pk_slice, value_slice,
                       THDVAR(table->in_use, bulk_load_allow_unsorted));
  } else if (row_info.skip_unique_check || row_info.tx->m_ddl_transaction) {
    /*
      It is responsibility of the user to make sure that the data being
      inserted doesn't violate any unique keys.
    */
    row_info.tx->get_indexed_write_batch()->Put(cf, row_info.new_pk_slice,
                                                value_slice);
  } else {
    const bool assume_tracked = can_assume_tracked(ha_thd());
    const auto s = row_info.tx->put(cf, row_info.new_pk_slice, value_slice,
                                    assume_tracked);
    if (!s.ok()) {
      if (s.IsBusy()) {
        errkey = table->s->primary_key;
        m_dupp_errkey = errkey;
        rc = HA_ERR_FOUND_DUPP_KEY;
      } else {
        rc = row_info.tx->set_status_error(table->in_use, s, *m_pk_descr,
                                           m_tbl_def, m_table_handler);
      }
    }
  }

  if (rc == HA_EXIT_SUCCESS) {
    row_info.tx->update_bytes_written(
        bytes_written + row_info.new_pk_slice.size() + value_slice.size());
  }
  return rc;
}

/**
  update an existing secondary key record or write a new secondary key record

  @param[in] table_arg    Table we're working on
  @param[in] kd           The secondary key being update/write
  @param[in] row_info     data structure contains old row data and new row data
  @param[in] bulk_load_sk whether support bulk load. Currently it is only
                          support for write
  @return
    HA_EXIT_SUCCESS OK
    Other           HA_ERR error code (can be SE-specific)
 */
int ha_rocksdb::update_write_sk(const TABLE *const table_arg,
                                const Rdb_key_def &kd,
                                const struct update_row_info &row_info,
                                const bool bulk_load_sk) {
  int new_packed_size;
  int old_packed_size;
  int rc = HA_EXIT_SUCCESS;

  rocksdb::Slice new_key_slice;
  rocksdb::Slice new_value_slice;
  rocksdb::Slice old_key_slice;

  const uint key_id = kd.get_keyno();

  ulonglong bytes_written = 0;

  /*
    Can skip updating this key if none of the key fields have changed and, if
    this table has TTL, the TTL timestamp has not changed.
  */
  if (row_info.old_data != nullptr && !m_update_scope.is_set(key_id) &&
      (!kd.has_ttl() || !m_ttl_bytes_updated)) {
    return HA_EXIT_SUCCESS;
  }

  bool store_row_debug_checksums = should_store_row_debug_checksums();
  new_packed_size =
      kd.pack_record(table_arg, m_pack_buffer, row_info.new_data,
                     m_sk_packed_tuple, &m_sk_tails, store_row_debug_checksums,
                     row_info.hidden_pk_id, 0, nullptr, m_ttl_bytes);

  if (row_info.old_data != nullptr) {
    // The old value
    old_packed_size = kd.pack_record(
        table_arg, m_pack_buffer, row_info.old_data, m_sk_packed_tuple_old,
        &m_sk_tails_old, store_row_debug_checksums, row_info.hidden_pk_id, 0,
        nullptr, m_ttl_bytes);

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
        !(kd.has_ttl() && m_ttl_bytes_updated) &&
        memcmp(m_sk_packed_tuple_old, m_sk_packed_tuple, old_packed_size) ==
            0 &&
        memcmp(m_sk_tails_old.ptr(), m_sk_tails.ptr(),
               m_sk_tails.get_current_pos()) == 0) {
      return HA_EXIT_SUCCESS;
    }

    /*
      Deleting entries from secondary index should skip locking, but
      be visible to the transaction.
      (also note that DDL statements do not delete rows, so this is not a DDL
       statement)
    */
    old_key_slice = rocksdb::Slice(
        reinterpret_cast<const char *>(m_sk_packed_tuple_old), old_packed_size);

    // TODO(mung) - If the new_data and old_data below to the same partial index
    // group (ie. have the same prefix), we can make use of the read below to
    // determine whether to issue SingleDelete or not.
    //
    // Currently we unconditionally issue SingleDelete, meaning that it is
    // possible for SD to be compacted out without meeting any Puts. This bumps
    // the num_single_delete_fallthrough counter in rocksdb, but is otherwise
    // fine as the SD markers just get compacted out. We will have to revisit
    // this if we want stronger consistency checks though.
    row_info.tx->get_indexed_write_batch()->SingleDelete(kd.get_cf(),
                                                         old_key_slice);

    bytes_written = old_key_slice.size();
  }

  if (kd.is_partial_index() && !bulk_load_sk) {
    // TODO(mung) - We've already calculated prefix len when locking. If we
    // cache that value, we can avoid recalculating here.
    uint size = kd.pack_record(table, m_pack_buffer, row_info.new_data,
                               m_sk_packed_tuple, nullptr, false, 0,
                               kd.partial_index_keyparts());
    rocksdb::Slice prefix_slice(
        reinterpret_cast<const char *>(m_sk_packed_tuple), size);
    // Shared lock on prefix should have already been acquired in
    // check_and_lock_sk. Check if this prefix has been materialized.
    Rdb_iterator_base iter(ha_thd(), m_key_descr_arr[kd.get_keyno()],
                           m_pk_descr, m_tbl_def);
    rc = iter.seek(kd.m_is_reverse_cf ? HA_READ_PREFIX_LAST : HA_READ_KEY_EXACT,
                   prefix_slice, false, prefix_slice, true /* read current */);

    // We can skip updating the index, if the prefix is not materialized.
    if (rc == HA_ERR_END_OF_FILE || rc == HA_ERR_KEY_NOT_FOUND) {
      return 0;
    }
  }

  new_key_slice = rocksdb::Slice(
      reinterpret_cast<const char *>(m_sk_packed_tuple), new_packed_size);
  new_value_slice =
      rocksdb::Slice(reinterpret_cast<const char *>(m_sk_tails.ptr()),
                     m_sk_tails.get_current_pos());

  if (bulk_load_sk && row_info.old_data == nullptr) {
    rc = bulk_load_key(row_info.tx, kd, new_key_slice, new_value_slice, true);
  } else {
    row_info.tx->get_indexed_write_batch()->Put(kd.get_cf(), new_key_slice,
                                                new_value_slice);
  }

  row_info.tx->update_bytes_written(bytes_written + new_key_slice.size() +
                                    new_value_slice.size());

  return rc;
}

/**
   Update existing indexes(PK/SKs) or write new indexes(PK/SKs)

   @param[in] row_info    hold all row data, such as old key/new key
   @param[in] pk_changed  whether primary key is changed
   @return
     HA_EXIT_SUCCESS OK
     Other           HA_ERR error code (can be SE-specific)
 */
int ha_rocksdb::update_write_indexes(const struct update_row_info &row_info,
                                     const bool pk_changed) {
  int rc;
  bool bulk_load_sk;

  // The PK must be updated first to pull out the TTL value.
  rc = update_write_pk(*m_pk_descr, row_info, pk_changed);
  if (rc != HA_EXIT_SUCCESS) {
    return rc;
  }

  // Update the remaining indexes. Allow bulk loading only if
  // allow_sk is enabled
  bulk_load_sk = rocksdb_enable_bulk_load_api &&
                 THDVAR(table->in_use, bulk_load) &&
                 THDVAR(table->in_use, bulk_load_allow_sk);
  for (uint key_id = 0; key_id < m_tbl_def->m_key_count; key_id++) {
    if (is_pk(key_id, table, m_tbl_def)) {
      continue;
    }

    rc = update_write_sk(table, *m_key_descr_arr[key_id], row_info,
                         bulk_load_sk);
    if (rc != HA_EXIT_SUCCESS) {
      return rc;
    }
  }

  return HA_EXIT_SUCCESS;
}

/**
  Update an existing row or write a new row

  @param[in] old_data           nullptr for write, non-null for update
  @param[in] new_data           non-null for write/update
  @param[in] skip_unique_check  whether to check uniqueness
  @return
    HA_EXIT_SUCCESS OK
    Other           HA_ERR error code (can be SE-specific)
 */
int ha_rocksdb::update_write_row(const uchar *const old_data,
                                 const uchar *const new_data,
                                 const bool skip_unique_check) {
  DBUG_ENTER_FUNC();

  THD *thd = ha_thd();
  if (thd && thd->killed) {
    DBUG_RETURN(HA_ERR_QUERY_INTERRUPTED);
  }

  bool pk_changed = false;
  struct update_row_info row_info;

  row_info.old_data = old_data;
  row_info.new_data = new_data;
  row_info.skip_unique_check = skip_unique_check;
  row_info.new_pk_unpack_info = nullptr;
  set_last_rowkey(old_data);

  row_info.tx = get_or_create_tx(table->in_use);

  if (old_data != nullptr) {
    row_info.old_pk_slice =
        rocksdb::Slice(m_last_rowkey.ptr(), m_last_rowkey.length());

    /* Determine which indexes need updating. */
    calc_updated_indexes();
  }

  /*
    Get the new row key into row_info.new_pk_slice
   */
  int rc = get_pk_for_update(&row_info);
  if (rc != HA_EXIT_SUCCESS) {
    DBUG_RETURN(rc);
  }

  /*
    For UPDATEs, if the key has changed, we need to obtain a lock. INSERTs
    always require locking.
  */
  if (row_info.old_pk_slice.size() > 0) {
    pk_changed = row_info.new_pk_slice.compare(row_info.old_pk_slice) != 0;
  }

  // Case: We skip both unique checks and rows locks only when bulk load is
  // enabled or if rocksdb_skip_locks_if_skip_unique_check is ON
  if (!THDVAR(table->in_use, bulk_load) &&
      (!rocksdb_skip_locks_if_skip_unique_check || !skip_unique_check)) {
    /*
      Check to see if we are going to have failures because of unique
      keys.  Also lock the appropriate key values.
    */
    rc = check_uniqueness_and_lock(row_info, pk_changed, skip_unique_check);
    if (rc != HA_EXIT_SUCCESS) {
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
  rc = update_write_indexes(row_info, pk_changed);
  if (rc != HA_EXIT_SUCCESS) {
    DBUG_RETURN(rc);
  }

  if (old_data != nullptr) {
    row_info.tx->incr_update_count();
  } else {
    row_info.tx->incr_insert_count();
  }

  row_info.tx->log_table_write_op(m_tbl_def);

  if (do_bulk_commit(row_info.tx)) {
    DBUG_RETURN(HA_ERR_ROCKSDB_BULK_LOAD);
  }

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

/*
 Setting iterator upper/lower bounds for Seek/SeekForPrev.
 This makes RocksDB to avoid scanning tombstones outside of
 the given key ranges, when prefix_same_as_start=true was not passed
 (when prefix bloom filter can not be used).
 Inversing upper/lower bound is necessary on reverse order CF.
 This covers HA_READ_PREFIX_LAST* case as well. For example,
 if given query eq condition was 12 bytes and condition was
 0x0000b3eb003f65c5e78858b8, and if doing HA_READ_PREFIX_LAST,
 eq_cond_len was 11 (see calc_eq_cond_len() for details).
 If the index was reverse order, upper bound would be
 0x0000b3eb003f65c5e78857, and lower bound would be
 0x0000b3eb003f65c5e78859. These cover given eq condition range.
*/
static void setup_iterator_bounds(const Rdb_key_def &kd,
                                  const rocksdb::Slice &eq_cond,
                                  size_t bound_len, uchar *const lower_bound,
                                  uchar *const upper_bound,
                                  rocksdb::Slice *lower_bound_slice,
                                  rocksdb::Slice *upper_bound_slice) {
  // If eq_cond is shorter than Rdb_key_def::INDEX_NUMBER_SIZE, we should be
  // able to get better bounds just by using index id directly.
  if (eq_cond.size() <= Rdb_key_def::INDEX_NUMBER_SIZE) {
    DBUG_ASSERT(bound_len == Rdb_key_def::INDEX_NUMBER_SIZE);
    uint size;
    kd.get_infimum_key(lower_bound, &size);
    DBUG_ASSERT(size == Rdb_key_def::INDEX_NUMBER_SIZE);
    kd.get_supremum_key(upper_bound, &size);
    DBUG_ASSERT(size == Rdb_key_def::INDEX_NUMBER_SIZE);
  } else {
    DBUG_ASSERT(bound_len <= eq_cond.size());
    memcpy(upper_bound, eq_cond.data(), bound_len);
    kd.successor(upper_bound, bound_len);
    memcpy(lower_bound, eq_cond.data(), bound_len);
    kd.predecessor(lower_bound, bound_len);
  }

  if (kd.m_is_reverse_cf) {
    *upper_bound_slice = rocksdb::Slice((const char *)lower_bound, bound_len);
    *lower_bound_slice = rocksdb::Slice((const char *)upper_bound, bound_len);
  } else {
    *upper_bound_slice = rocksdb::Slice((const char *)upper_bound, bound_len);
    *lower_bound_slice = rocksdb::Slice((const char *)lower_bound, bound_len);
  }
}

/**
  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::rnd_init(bool scan MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  m_need_build_decoder = true;
  m_rnd_scan_started = false;
  DBUG_RETURN(
      index_init(has_hidden_pk(table) ? MAX_KEY : pk_index(table, m_tbl_def),
                 false /* sorted */));
}

/**
  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::rnd_next(uchar *const buf) {
  DBUG_ENTER_FUNC();

  check_build_decoder();

  int rc;
  ha_statistic_increment(&System_status_var::ha_read_rnd_next_count);

  /*
    Since order does not matter, the scan will occur go with natural index
    order.
  */
  bool is_reverse_cf = m_key_descr_arr[active_index_pos()]->m_is_reverse_cf;
  if (!m_rnd_scan_started) {
    rc = index_read_intern(buf, !is_reverse_cf /* first */);
    m_rnd_scan_started = true;
  } else {
    if (is_reverse_cf) {
      rc = index_next_with_direction_intern(buf, false, false);
    } else {
      rc = index_next_with_direction_intern(buf, true, false);
    }
  }

  DBUG_RETURN(rc);
}

int ha_rocksdb::rnd_end() {
  DBUG_ENTER_FUNC();
  DBUG_RETURN(index_end());
}

void ha_rocksdb::build_decoder() {
  m_converter->setup_field_decoders(table->read_set, active_index,
                                    m_keyread_only,
                                    m_lock_rows == RDB_LOCK_WRITE);
}

void ha_rocksdb::check_build_decoder() {
  if (m_need_build_decoder) {
    build_decoder();
    m_need_build_decoder = false;
  }
}

/**
  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::index_init(uint idx, bool sorted MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  m_need_build_decoder = true;
  active_index = idx;

  THD *thd = ha_thd();
  if (thd && thd->killed) {
    DBUG_RETURN(HA_ERR_QUERY_INTERRUPTED);
  }

  Rdb_transaction *const tx = get_or_create_tx(thd);
  DBUG_ASSERT(tx != nullptr);

  active_index = idx;
  if (idx != table->s->primary_key &&
      m_key_descr_arr[idx]->is_partial_index()) {
    m_iterator.reset(
        new Rdb_iterator_partial(thd, m_key_descr_arr[active_index_pos()],
                                 m_pk_descr, m_tbl_def, table));
  } else {
    m_iterator.reset(new Rdb_iterator_base(
        thd, m_key_descr_arr[active_index_pos()], m_pk_descr, m_tbl_def));
  }

  // If m_lock_rows is not RDB_LOCK_NONE then we will be doing a get_for_update
  // when accessing the index, so don't acquire the snapshot right away.
  // Otherwise acquire the snapshot immediately.
  tx->acquire_snapshot(m_lock_rows == RDB_LOCK_NONE);

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

/**
  @return
    HA_EXIT_SUCCESS      OK
*/
int ha_rocksdb::index_end() {
  DBUG_ENTER_FUNC();

  m_need_build_decoder = false;

  m_iterator = nullptr;

  active_index = MAX_KEY;
  in_range_check_pushed_down = false;

  if (mrr_rowid_reader) {
    mrr_free();
  }

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

/**
  Called by the partition manager for truncating tables.

  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::truncate(dd::Table *table_def) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(m_tbl_def != nullptr);

  // Reset auto_increment_value to 1 if auto-increment feature is enabled
  // By default, the starting valid value for auto_increment_value is 1
  DBUG_RETURN(truncate_table(
      m_tbl_def, "" /* actual_user_table_name */, table,
      table->found_next_number_field ? 1 : 0 /* auto_increment_value */,
      table_def));
}

int ha_rocksdb::reset() {
  DBUG_ENTER_FUNC();

  /* Free blob data */
  m_retrieved_record.Reset();
  m_dup_key_retrieved_record.Reset();
  release_blob_buffer();
  m_iterator.reset(nullptr);
  m_pk_iterator.reset(nullptr);
  DBUG_RETURN(HA_EXIT_SUCCESS);
}

/*
  Delete the row we've last read. The row is also passed as parameter.

  @detail
    The caller guarantees table buf points to the row that was just read.
    The row is either table->record[0] or table->record[1].
    (Check out InnoDB: row_update_for_mysql() has "UT_NOT_USED(mysql_rec)"

  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::delete_row(const uchar *const buf) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(buf != nullptr);

  ha_statistic_increment(&System_status_var::ha_delete_count);
  set_last_rowkey(buf);

  rocksdb::Slice key_slice(m_last_rowkey.ptr(), m_last_rowkey.length());
  Rdb_transaction *const tx = get_or_create_tx(table->in_use);
  ulonglong bytes_written = 0;

  const uint index = pk_index(table, m_tbl_def);
  rocksdb::Status s =
      delete_or_singledelete(index, tx, m_pk_descr->get_cf(), key_slice);
  if (!s.ok()) {
    DBUG_RETURN(tx->set_status_error(table->in_use, s, *m_pk_descr, m_tbl_def,
                                     m_table_handler));
  } else {
    bytes_written = key_slice.size();
  }

  longlong hidden_pk_id = 0;
  if (m_tbl_def->m_key_count > 1 && has_hidden_pk(table)) {
    int err = read_hidden_pk_id_from_rowkey(&hidden_pk_id);
    if (err) {
      DBUG_RETURN(err);
    }
  }

  // Delete the record for every secondary index
  for (uint i = 0; i < m_tbl_def->m_key_count; i++) {
    if (!is_pk(i, table, m_tbl_def)) {
      int packed_size;
      const Rdb_key_def &kd = *m_key_descr_arr[i];

      // The unique key should be locked so that behavior is
      // similar to InnoDB and reduce conflicts. The key
      // used for locking does not include the extended fields.
      const KEY *key_info = &table->key_info[i];
      if (key_info->flags & HA_NOSAME) {
        uint user_defined_key_parts = key_info->user_defined_key_parts;
        uint n_null_fields = 0;

        packed_size = kd.pack_record(table, m_pack_buffer, buf,
                                     m_sk_packed_tuple, nullptr, false, 0,
                                     user_defined_key_parts, &n_null_fields);

        // NULL fields are considered unique, so no lock is needed
        if (n_null_fields == 0) {
          rocksdb::Slice sk_slice(
              reinterpret_cast<const char *>(m_sk_packed_tuple), packed_size);
          const rocksdb::Status s = get_for_update(tx, kd, sk_slice);
          if (!s.ok()) {
            DBUG_RETURN(tx->set_status_error(table->in_use, s, kd, m_tbl_def,
                                             m_table_handler));
          }
        }
      }

      if (kd.is_partial_index()) {
        // Obtain shared lock on prefix.
        int rc = acquire_prefix_lock(kd, tx, buf);
        if (rc) DBUG_RETURN(rc);
      }

      packed_size = kd.pack_record(table, m_pack_buffer, buf, m_sk_packed_tuple,
                                   nullptr, false, hidden_pk_id);
      rocksdb::Slice secondary_key_slice(
          reinterpret_cast<const char *>(m_sk_packed_tuple), packed_size);

      /*
        For point locking, Deleting on secondary key doesn't need any locks.
        Range locking must get a lock.
      */
      if (rocksdb_use_range_locking) {
        auto s= tx->lock_singlepoint_range(kd.get_cf(), secondary_key_slice);
        if (!s.ok()) {
          DBUG_RETURN(tx->set_status_error(table->in_use, s, kd, m_tbl_def,
                                       m_table_handler));
        }
      }

      tx->get_indexed_write_batch()->SingleDelete(kd.get_cf(),
                                                  secondary_key_slice);
      bytes_written += secondary_key_slice.size();
    }
  }

  tx->incr_delete_count();
  tx->log_table_write_op(m_tbl_def);

  if (do_bulk_commit(tx)) {
    DBUG_RETURN(HA_ERR_ROCKSDB_BULK_LOAD);
  }
  /* TODO(yzha) - row stats are gone in 8.0
  stats.rows_deleted++; */

  // Not protected by ddl_manger lock for performance
  // reasons. This is an estimate value anyway.
  dec_table_n_rows();
  update_table_stats_if_needed();
  update_row_stats(ROWS_DELETED);
  tx->update_bytes_written(bytes_written);

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

rocksdb::Status ha_rocksdb::delete_or_singledelete(
    uint index, Rdb_transaction *const tx,
    rocksdb::ColumnFamilyHandle *const column_family,
    const rocksdb::Slice &key) {
  const bool assume_tracked = can_assume_tracked(ha_thd());
  if (can_use_single_delete(index)) {
    return tx->single_delete(column_family, key, assume_tracked);
  }
  return tx->delete_key(column_family, key, assume_tracked);
}

int ha_rocksdb::adjust_handler_stats_table_scan(ha_statistics *ha_stats,
                                                Rdb_tbl_def *tbl_def) {
  DBUG_ENTER_FUNC();

  bool should_recalc_stats = false;
  if (static_cast<longlong>(ha_stats->data_file_length) < 0) {
    ha_stats->data_file_length = 0;
    should_recalc_stats = true;
  }

  if (static_cast<longlong>(ha_stats->index_file_length) < 0) {
    ha_stats->index_file_length = 0;
    should_recalc_stats = true;
  }

  if (static_cast<longlong>(ha_stats->records) < 0) {
    ha_stats->records = 1;
    should_recalc_stats = true;
  }

  if (should_recalc_stats) {
    // If any of the stats is corrupt, add the table to the index stats
    // recalc queue.
    rdb_is_thread.add_index_stats_request(tbl_def->full_tablename());
  }
  DBUG_RETURN(HA_EXIT_SUCCESS);
}

int ha_rocksdb::update_stats(ha_statistics *ha_stats, Rdb_tbl_def *tbl_def,
                             bool from_handler) {
  /*
    Test only to simulate corrupted stats
  */
  DBUG_EXECUTE_IF("myrocks_simulate_negative_stats", {
    auto pk_def = tbl_def->get_pk_def();
    pk_def->m_stats.m_actual_disk_size = -pk_def->m_stats.m_actual_disk_size;
  });

  rocksdb_get_stats(ha_stats, tbl_def);
  if (rocksdb_table_stats_use_table_scan) {
    int ret = adjust_handler_stats_table_scan(ha_stats, tbl_def);
    if (ret != HA_EXIT_SUCCESS) {
      return ret;
    }
  } else {
    int ret = adjust_handler_stats_sst_and_memtable(ha_stats, tbl_def);
    if (ret != HA_EXIT_SUCCESS) {
      return ret;
    }
  }

  if (rocksdb_debug_optimizer_n_rows > 0) {
    ha_stats->records = rocksdb_debug_optimizer_n_rows;
  }

  if (ha_stats->records != 0) {
    ha_stats->mean_rec_length = ha_stats->data_file_length / ha_stats->records;
  }

  // Skip the rest if we are called from ha_rocksdb::info which will perform
  // following updates based on flag. Alawys update auto_inc_val is expensive
  // from ha_rocksdb::info as it involves a DBImpl::Get so we don't want to
  // do that in steady state. When called from rocksdb_get_table_statistics
  // it is fine as it is much less frequent
  if (!from_handler) {
    // HA_STATUS_TIME
    ha_stats->update_time = tbl_def->m_update_time;

    // HA_STATUS_AUTO
    // Because we haven't opened the table yet, we need to load auto incr
    // value here. Note we won't know if the table actually has auto incr
    // without opening the table, so there is a corner case it'll end up
    // being NULL if the table has just been created and haven't been
    // opened yet - InnoDB has the same issue
    if (tbl_def->m_auto_incr_val == 0) {
      // Unfortunately in this case we don't know if we actually have auto
      // increment without opening the table, so we'd have to load the value
      // always even if the table doesn't have auto increment
      if (!dict_manager.get_auto_incr_val(tbl_def->get_autoincr_gl_index_id(),
                                          &ha_stats->auto_increment_value)) {
        ha_stats->auto_increment_value = 0;
      }
    } else {
      ha_stats->auto_increment_value = tbl_def->m_auto_incr_val;
    }
  }

  return HA_EXIT_SUCCESS;
}

/**
  @return
    HA_EXIT_SUCCESS  OK
    HA_EXIT_FAILURE  Error
*/
int ha_rocksdb::info(uint flag) {
  DBUG_ENTER_FUNC();

  if (!table) {
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  if (flag & HA_STATUS_VARIABLE) {
    int ret = update_stats(&stats, m_tbl_def, /* from_handler */ true);
    if (ret != HA_EXIT_SUCCESS) {
      return ret;
    }
    stats.mrr_length_per_rec = mrr_get_length_per_rec();
  }

  if (flag & HA_STATUS_CONST) {
    ref_length = m_pk_descr->max_storage_fmt_length();

    for (uint i = 0; i < m_tbl_def->m_key_count; i++) {
      if (is_hidden_pk(i, table, m_tbl_def)) {
        continue;
      }
      KEY *const k = &table->key_info[i];
      auto records = stats.records;
      for (uint j = 0; j < k->actual_key_parts; j++) {
        const Rdb_index_stats &k_stats = m_key_descr_arr[i]->m_stats;
        rec_per_key_t x = REC_PER_KEY_UNKNOWN;

        // Doesn't make sense to calculate cardinality if there are no records
        if (records > 0) {
          if (k_stats.m_distinct_keys_per_prefix.size() > j &&
              k_stats.m_distinct_keys_per_prefix[j] > 0) {
            x = (rec_per_key_t)k_stats.m_rows /
                k_stats.m_distinct_keys_per_prefix[j];

            /*
              If the number of rows is less than the number of prefixes (due to
              sampling), the average number of rows with the same prefix is 1.
             */
            if (x < 1) {
              x = 1;
            }
          }

          if ((x == REC_PER_KEY_UNKNOWN &&
               rocksdb_debug_optimizer_no_zero_cardinality) ||
              rocksdb_debug_optimizer_n_rows > 0) {
            // Fake cardinality implementation. For example, (idx1, idx2, idx3)
            // index
            // will have rec_per_key for (idx1)=4, (idx1,2)=2, and (idx1,2,3)=1.
            // rec_per_key for the whole index is 1, and multiplied by 2^n if
            // n suffix columns of the index are not used.
            if (rocksdb_debug_cardinality_multiplier == 2) {
              x = 1 << (k->actual_key_parts - j - 1);
            } else {
              x = 1;
              for (uint kp = 1; kp <= k->actual_key_parts - j - 1; kp++) {
                x *= rocksdb_debug_cardinality_multiplier;
              }
            }
          }

          if (x > records) x = records;
        }

        // 1 <= x <= records, or x = REC_PER_KEY_UNKNOWN
        k->set_records_per_key(j, x);
      }
    }
  }

  if (flag & HA_STATUS_TIME) {
    stats.update_time = m_tbl_def->m_update_time;
  }

  if (flag & HA_STATUS_ERRKEY) {
    /*
      Currently we support only primary keys so we know which key had a
      uniqueness violation.
    */
    errkey = m_dupp_errkey;
  }

  if (flag & HA_STATUS_AUTO) {
    stats.auto_increment_value = m_tbl_def->m_auto_incr_val;
  }

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

void ha_rocksdb::position(const uchar *const record) {
  DBUG_ENTER_FUNC();

  longlong hidden_pk_id = 0;
  if (has_hidden_pk(table) && read_hidden_pk_id_from_rowkey(&hidden_pk_id)) {
    DBUG_ASSERT(false);  // should never reach here
  }

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
  const uint packed_size = m_pk_descr->pack_record(
      table, m_pack_buffer, record, ref, nullptr, false, hidden_pk_id);

  /*
    It could be that mem-comparable form of PK occupies less than ref_length
    bytes. Fill the remainder with zeros.
  */
  if (ref_length > packed_size) {
    memset(ref + packed_size, 0, ref_length - packed_size);
  }

  DBUG_VOID_RETURN;
}

/**
  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::rnd_pos(uchar *const buf, uchar *const pos) {
  DBUG_ENTER_FUNC();

  check_build_decoder();

  int rc;
  size_t len;

  ha_statistic_increment(&System_status_var::ha_read_rnd_count);

  /* TODO(yzha) - row stats are gone in 8.0
  stats.rows_requested++; */
  len = m_pk_descr->key_length(table,
                               rocksdb::Slice((const char *)pos, ref_length));
  if (len == size_t(-1)) {
    DBUG_RETURN(HA_ERR_ROCKSDB_CORRUPT_DATA); /* Data corruption? */
  }

  rc = get_row_by_rowid(buf, pos, len);

  if (!rc) {
    /* TODO(yzha) - row stats are gone in 8.0
    stats.rows_read++; */
    update_row_stats(ROWS_READ);
  }

  DBUG_RETURN(rc);
}

/*
  @brief
    Calculate (if needed) the bitmap of indexes that are modified by the
    current query

  @detail
    The calculation done by checking index definitions against the
  table->write_set
    column bitmap.
*/

void ha_rocksdb::calc_updated_indexes() {
  m_update_scope.clear_all();

  for (uint keynr = 0; keynr < table->s->keys; keynr++) {
    const Rdb_key_def &kd = *m_key_descr_arr[keynr];
    /* Walk over all key parts, including the "extended key" suffix */
    const uint key_parts = kd.get_key_parts();
    for (uint kp = 0; kp < key_parts; kp++) {
      if (has_hidden_pk(table) && kp + 1 == key_parts) break;

      Field *const field = kd.get_table_field_for_part_no(table, kp);
      if (bitmap_is_set(table->write_set, field->field_index)) {
        m_update_scope.set_bit(keynr);
        break;
      }
    }
  }
}

/**
  Update an existing row
  @param[in] old_data           nullptr for write, non-null for update
  @param[in] new_data           non-null for write/update
  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::update_row(const uchar *const old_data, uchar *const new_data) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(old_data != nullptr);
  DBUG_ASSERT(new_data != nullptr);
  DBUG_ASSERT(m_lock_rows == RDB_LOCK_WRITE);
  /*
    old_data points to record we're updating. It is the same as the record
    we've just read (for multi-table UPDATE, too, because SQL layer will make
    an rnd_pos() call to re-read the record before calling update_row())
  */
  DBUG_ASSERT(new_data == table->record[0]);

  ha_statistic_increment(&System_status_var::ha_update_count);
  const int rv = update_write_row(old_data, new_data, skip_unique_check());

  if (rv == 0) {
    /* TODO(yzha) - row stats are gone in 8.0
    stats.rows_updated++; */
    update_table_stats_if_needed();
    update_row_stats(ROWS_UPDATED);
  }

  DBUG_RETURN(rv);
}

void ha_rocksdb::update_table_stats_if_needed() {
  DBUG_ENTER_FUNC();

  if (!rocksdb_table_stats_use_table_scan) {
    DBUG_VOID_RETURN;
  }

  /*
    InnoDB performs a similar operation to update counters during query
    processing. Because the changes in MyRocks are made to a write batch,
    it is possible for the table scan cardinality calculation to trigger
    before the transaction performing the update commits. Hence the
    cardinality scan might miss the keys for these pending transactions.
  */
  uint64 counter = m_tbl_def->m_tbl_stats.m_stat_modified_counter++;
  uint64 n_rows = m_tbl_def->m_tbl_stats.m_stat_n_rows;

  if (counter > std::max(rocksdb_table_stats_recalc_threshold_count,
                         static_cast<unsigned long long>(
                             n_rows * rocksdb_table_stats_recalc_threshold_pct /
                             100.0))) {
    // Add the table to the recalc queue
    rdb_is_thread.add_index_stats_request(m_tbl_def->full_tablename());
    m_tbl_def->m_tbl_stats.m_stat_modified_counter = 0;
  }

  DBUG_VOID_RETURN;
}

/* The following function was copied from ha_blackhole::store_lock: */
THR_LOCK_DATA **ha_rocksdb::store_lock(THD *const thd, THR_LOCK_DATA **to,
                                       enum thr_lock_type lock_type) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(thd != nullptr);
  DBUG_ASSERT(to != nullptr);

  bool in_lock_tables = my_core::thd_in_lock_tables(thd);

  /* First, make a decision about MyRocks's internal locking */
  if (lock_type >= TL_WRITE_ALLOW_WRITE) {
    m_lock_rows = RDB_LOCK_WRITE;
  } else if (lock_type == TL_READ_WITH_SHARED_LOCKS) {
    m_lock_rows = RDB_LOCK_READ;
  } else if (lock_type != TL_IGNORE) {
    m_lock_rows = RDB_LOCK_NONE;
    if (THDVAR(thd, lock_scanned_rows)) {
      /*
        The following logic was copied directly from
        ha_innobase::store_lock_with_x_type() in
        storage/innobase/handler/ha_innodb.cc and causes MyRocks to leave
        locks in place on rows that are in a table that is not being updated.
      */
      const uint sql_command = my_core::thd_sql_command(thd);
      if ((lock_type == TL_READ && in_lock_tables) ||
          (lock_type == TL_READ_HIGH_PRIORITY && in_lock_tables) ||
          can_hold_read_locks_on_select(thd, lock_type)) {
        ulong tx_isolation = my_core::thd_tx_isolation(thd);
        if (sql_command != SQLCOM_CHECKSUM &&
            ((my_core::thd_test_options(thd, OPTION_BIN_LOG) &&
              tx_isolation > ISO_READ_COMMITTED) ||
             tx_isolation == ISO_SERIALIZABLE ||
             (lock_type != TL_READ && lock_type != TL_READ_NO_INSERT) ||
             (sql_command != SQLCOM_INSERT_SELECT &&
              sql_command != SQLCOM_REPLACE_SELECT &&
              sql_command != SQLCOM_UPDATE && sql_command != SQLCOM_DELETE &&
              sql_command != SQLCOM_CREATE_TABLE))) {
          m_lock_rows = RDB_LOCK_READ;
        }
      }
    }
  }

  /* Then, tell the SQL layer what kind of locking it should use: */
  if (lock_type != TL_IGNORE && m_db_lock.type == TL_UNLOCK) {
    /*
      Here is where we get into the guts of a row level lock.
      If TL_UNLOCK is set
      If we are not doing a LOCK TABLE or DISCARD/IMPORT
      TABLESPACE, then allow multiple writers
    */

    if ((lock_type >= TL_WRITE_CONCURRENT_INSERT && lock_type <= TL_WRITE) &&
        !in_lock_tables && !my_core::thd_tablespace_op(thd)) {
      lock_type = TL_WRITE_ALLOW_WRITE;
    }

    /*
      In queries of type INSERT INTO t1 SELECT ... FROM t2 ...
      MySQL would use the lock TL_READ_NO_INSERT on t2, and that
      would conflict with TL_WRITE_ALLOW_WRITE, blocking all inserts
      to t2. Convert the lock to a normal read lock to allow
      concurrent inserts to t2.
    */

    if (lock_type == TL_READ_NO_INSERT && !in_lock_tables) {
      lock_type = TL_READ;
    }

    m_db_lock.type = lock_type;
  }

  m_locked_row_action = THR_WAIT;
  if (lock_type != TL_IGNORE) {
    auto action = table->pos_in_table_list->lock_descriptor().action;
    switch (action) {
      case THR_SKIP:
        m_locked_row_action = THR_SKIP;
        break;
      case THR_NOWAIT:
        m_locked_row_action = THR_NOWAIT;
        break;
      default:
        break;
    }
  }

  *to++ = &m_db_lock;

  DBUG_RETURN(to);
}

void ha_rocksdb::read_thd_vars(THD *const thd) {
  m_store_row_debug_checksums = THDVAR(thd, store_row_debug_checksums);
  m_converter->set_verify_row_debug_checksums(
      THDVAR(thd, verify_row_debug_checksums));
  m_checksums_pct = THDVAR(thd, checksums_pct);
}

/**
  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (cannot be SE-specific)
*/
int ha_rocksdb::external_lock(THD *const thd, int lock_type) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(thd != nullptr);

  int res = HA_EXIT_SUCCESS;
  int binlog_format = my_core::thd_binlog_format(thd);
  bool unsafe_for_binlog = THDVAR(ha_thd(), unsafe_for_binlog);
  if (lock_type == F_WRLCK && !thd->rli_slave && !unsafe_for_binlog &&
      binlog_format != BINLOG_FORMAT_ROW &&
      binlog_format != BINLOG_FORMAT_UNSPEC &&
      my_core::thd_binlog_filter_ok(thd)) {
    my_error(ER_REQUIRE_ROW_BINLOG_FORMAT, MYF(0));
    DBUG_RETURN(HA_ERR_UNSUPPORTED);
  }

  if (lock_type == F_UNLCK) {
    Rdb_transaction *const tx = get_tx_from_thd(thd);

    if (tx) {
      tx->io_perf_end_and_record(&m_io_perf);
      DBUG_ASSERT(tx->m_n_mysql_tables_in_use > 0);
      tx->m_n_mysql_tables_in_use--;
      if (tx->m_n_mysql_tables_in_use == 0 &&
          !my_core::thd_test_options(thd,
                                     OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN)) {
        /*
          Do like InnoDB: when we get here, it's time to commit a
          single-statement transaction.

          If the statement involved multiple tables, this code will be executed
          for each of them, but that's ok because non-first tx->commit() calls
          will be no-ops.
        */
        if (tx->commit_or_rollback()) {
          res = HA_ERR_INTERNAL_ERROR;
        }
      }
    }
  } else {
    if (my_core::thd_tx_isolation(thd) < ISO_READ_COMMITTED ||
        my_core::thd_tx_isolation(thd) > ISO_REPEATABLE_READ) {
      my_error(ER_ISOLATION_MODE_NOT_SUPPORTED, MYF(0),
               tx_isolation_names[my_core::thd_tx_isolation(thd)]);
      DBUG_RETURN(HA_ERR_UNSUPPORTED);
    }

    /*
      It's nice to do the following on start of every statement. The problem
      is, handler->start_stmt() is not called for INSERTs.
      So, we put this code here.
    */
    Rdb_transaction *const tx = get_or_create_tx(thd);
    read_thd_vars(thd);

    if (skip_unique_check()) {
      if ((thd->lex->sql_command == SQLCOM_INSERT ||
           thd->lex->sql_command == SQLCOM_LOAD ||
           thd->lex->sql_command == SQLCOM_REPLACE) &&
          (thd->lex->duplicates == DUP_REPLACE ||
           thd->lex->duplicates == DUP_UPDATE)) {
        my_error(ER_ON_DUPLICATE_DISABLED, MYF(0), thd->query());
        DBUG_RETURN(HA_ERR_UNSUPPORTED);
      }
    }

    if (lock_type == F_WRLCK) {
      if (tx->is_tx_read_only()) {
        my_error(ER_UPDATES_WITH_CONSISTENT_SNAPSHOT, MYF(0));
        DBUG_RETURN(HA_ERR_UNSUPPORTED);
      }

      if (thd->get_explicit_snapshot()) {
        my_error(ER_UPDATES_WITH_EXPLICIT_SNAPSHOT, MYF(0));
        DBUG_RETURN(HA_ERR_UNSUPPORTED);
      }

      /*
        SQL layer signals us to take a write lock. It does so when starting DML
        statement. We should put locks on the rows we're reading.

        Note: sometimes, external_lock() can be called without a prior
        ::store_lock call.  That's why we need to set lock_* members here, too.
      */
      m_lock_rows = RDB_LOCK_WRITE;

      if (thd->lex->sql_command == SQLCOM_CREATE_INDEX ||
          thd->lex->sql_command == SQLCOM_DROP_INDEX ||
          thd->lex->sql_command == SQLCOM_ALTER_TABLE) {
        tx->m_ddl_transaction = true;
      }
    }
    tx->m_n_mysql_tables_in_use++;
    rocksdb_register_tx(rocksdb_hton, thd, tx, (m_lock_rows != RDB_LOCK_NONE));
    tx->io_perf_start(&m_io_perf);

    m_use_range_locking= false;
    if (rocksdb_use_range_locking &&
        m_lock_rows != RDB_LOCK_NONE &&
        my_core::thd_tx_isolation(thd) >= ISO_REPEATABLE_READ) {
      m_use_range_locking= true;
    }
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

  @return
    HA_EXIT_SUCCESS  OK
*/

int ha_rocksdb::start_stmt(THD *const thd,
                           thr_lock_type lock_type MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(thd != nullptr);

  Rdb_transaction *const tx = get_or_create_tx(thd);
  read_thd_vars(thd);
  rocksdb_register_tx(ht, thd, tx, (m_lock_rows != RDB_LOCK_NONE));
  tx->io_perf_start(&m_io_perf);

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

rocksdb::Range get_range(uint32_t i,
                         uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE * 2],
                         int offset1, int offset2) {
  uchar *buf_begin = buf;
  uchar *buf_end = buf + Rdb_key_def::INDEX_NUMBER_SIZE;
  rdb_netbuf_store_index(buf_begin, i + offset1);
  rdb_netbuf_store_index(buf_end, i + offset2);

  return rocksdb::Range(
      rocksdb::Slice((const char *)buf_begin, Rdb_key_def::INDEX_NUMBER_SIZE),
      rocksdb::Slice((const char *)buf_end, Rdb_key_def::INDEX_NUMBER_SIZE));
}

static rocksdb::Range get_range(const Rdb_key_def &kd,
                                uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE * 2],
                                int offset1, int offset2) {
  return get_range(kd.get_index_number(), buf, offset1, offset2);
}

rocksdb::Range ha_rocksdb::get_range(
    const Rdb_key_def &kd, uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE * 2]) {
  if (kd.m_is_reverse_cf) {
    return myrocks::get_range(kd, buf, 1, 0);
  } else {
    return myrocks::get_range(kd, buf, 0, 1);
  }
}

rocksdb::Range ha_rocksdb::get_range(
    const int i, uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE * 2]) const {
  return get_range(*m_key_descr_arr[i], buf);
}

/*
 This function is called with total_order_seek=true, but
 upper/lower bound setting is not necessary.
 Boundary set is useful when there is no matching key,
 but in drop_index_thread's case, it means index is marked as removed,
 so no further seek will happen for the index id.
*/
static bool is_myrocks_index_empty(rocksdb::ColumnFamilyHandle *cfh,
                                   const bool is_reverse_cf,
                                   const rocksdb::ReadOptions &read_opts,
                                   const uint index_id) {
  bool index_removed = false;
  uchar key_buf[Rdb_key_def::INDEX_NUMBER_SIZE] = {0};
  rdb_netbuf_store_uint32(key_buf, index_id);
  const rocksdb::Slice key =
      rocksdb::Slice(reinterpret_cast<char *>(key_buf), sizeof(key_buf));
  std::unique_ptr<rocksdb::Iterator> it(rdb->NewIterator(read_opts, cfh));
  rocksdb_smart_seek(is_reverse_cf, it.get(), key);
  if (!it->Valid()) {
    index_removed = true;
  } else {
    if (memcmp(it->key().data(), key_buf, Rdb_key_def::INDEX_NUMBER_SIZE)) {
      // Key does not have same prefix
      index_removed = true;
    }
  }
  return index_removed;
}

/*
  Drop index thread's main logic
*/

void Rdb_drop_index_thread::run() {
  RDB_MUTEX_LOCK_CHECK(m_signal_mutex);

  for (;;) {
    // The stop flag might be set by shutdown command
    // after drop_index_thread releases signal_mutex
    // (i.e. while executing expensive Seek()). To prevent drop_index_thread
    // from entering long cond_timedwait, checking if stop flag
    // is true or not is needed, with drop_index_interrupt_mutex held.
    if (m_killed) {
      break;
    }

    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += dict_manager.is_drop_index_empty()
                     ? 24 * 60 * 60  // no filtering
                     : 60;           // filtering

    const auto ret MY_ATTRIBUTE((__unused__)) =
        mysql_cond_timedwait(&m_signal_cond, &m_signal_mutex, &ts);
    if (m_killed) {
      break;
    }
    // make sure, no program error is returned
    DBUG_ASSERT(ret == 0 || ret == ETIMEDOUT);
    RDB_MUTEX_UNLOCK_CHECK(m_signal_mutex);

    std::unordered_set<GL_INDEX_ID> indices;
    dict_manager.get_ongoing_drop_indexes(&indices);
    if (!indices.empty()) {
      std::unordered_set<GL_INDEX_ID> finished;
      rocksdb::ReadOptions read_opts;
      read_opts.total_order_seek = true;  // disable bloom filter

      for (const auto d : indices) {
        uint32 cf_flags = 0;
        if (!dict_manager.get_cf_flags(d.cf_id, &cf_flags)) {
          // NO_LINT_DEBUG
          sql_print_error(
              "RocksDB: Failed to get column family flags "
              "from cf id %u. MyRocks data dictionary may "
              "get corrupted.",
              d.cf_id);
          abort();
        }

        std::shared_ptr<rocksdb::ColumnFamilyHandle> cfh =
            cf_manager.get_cf(d.cf_id);
        DBUG_ASSERT(cfh);

        if (dict_manager.get_dropped_cf(d.cf_id)) {
          finished.insert(d);
          continue;
        }

        const bool is_reverse_cf = cf_flags & Rdb_key_def::REVERSE_CF_FLAG;

        uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE * 2];
        rocksdb::Range range = get_range(d.index_id, buf, is_reverse_cf ? 1 : 0,
                                         is_reverse_cf ? 0 : 1);

        rocksdb::Status status = DeleteFilesInRange(rdb->GetBaseDB(), cfh.get(),
                                                    &range.start, &range.limit);
        if (!status.ok()) {
          if (status.IsIncomplete()) {
            continue;
          } else if (status.IsShutdownInProgress()) {
            break;
          }
          rdb_handle_io_error(status, RDB_IO_ERROR_BG_THREAD);
        }

        status = rdb->CompactRange(getCompactRangeOptions(), cfh.get(),
                                   &range.start, &range.limit);
        if (!status.ok()) {
          if (status.IsIncomplete()) {
            continue;
          } else if (status.IsShutdownInProgress()) {
            break;
          }
          rdb_handle_io_error(status, RDB_IO_ERROR_BG_THREAD);
        }
        if (is_myrocks_index_empty(cfh.get(), is_reverse_cf, read_opts,
                                   d.index_id)) {
          finished.insert(d);
        }
      }

      if (!finished.empty()) {
        dict_manager.finish_drop_indexes(finished);
      }
    }

    DBUG_EXECUTE_IF("rocksdb_drop_cf", {
      THD *thd = new THD();
      thd->thread_stack = reinterpret_cast<char *>(&(thd));
      thd->store_globals();

      const char act[] = "now wait_for ready_to_drop_cf";
      DBUG_ASSERT(!debug_sync_set_action(thd, STRING_WITH_LEN(act)));

      thd->restore_globals();
      delete thd;
    });

    // Remove dropped column family
    // 1. Get all cf ids from ongoing_index_drop.
    // 2. Get all cf ids for cfs marked as dropped.
    // 3. If a cf id is in the list of ongoing_index_drop
    // , skip removing this cf. It will be removed later.
    // 4. If it is not, proceed to remove the cf.
    //
    // This should be under dict_manager lock

    {
      std::lock_guard<Rdb_dict_manager> dm_lock(dict_manager);
      std::unordered_set<uint32> dropped_cf_ids;
      dict_manager.get_all_dropped_cfs(&dropped_cf_ids);

      if (!dropped_cf_ids.empty()) {
        std::unordered_set<GL_INDEX_ID> ongoing_drop_indices;
        dict_manager.get_ongoing_drop_indexes(&ongoing_drop_indices);

        std::unordered_set<uint32> ongoing_drop_cf_ids;
        for (const auto index : ongoing_drop_indices) {
          ongoing_drop_cf_ids.insert(index.cf_id);
        }

        for (const auto cf_id : dropped_cf_ids) {
          if (ongoing_drop_cf_ids.find(cf_id) == ongoing_drop_cf_ids.end()) {
            cf_manager.remove_dropped_cf(&dict_manager, rdb, cf_id);
          }
        }
      }
    }

    DBUG_EXECUTE_IF("rocksdb_drop_cf", {
      std::lock_guard<Rdb_dict_manager> dm_lock(dict_manager);
      std::unordered_set<uint32> dropped_cf_ids;
      dict_manager.get_all_dropped_cfs(&dropped_cf_ids);
      if (dropped_cf_ids.empty()) {
        THD *thd = new THD();
        thd->thread_stack = reinterpret_cast<char *>(&(thd));
        thd->store_globals();

        const char act[] = "now signal drop_cf_done";
        DBUG_ASSERT(!debug_sync_set_action(thd, STRING_WITH_LEN(act)));

        thd->restore_globals();
        delete thd;
      }
    });
    RDB_MUTEX_LOCK_CHECK(m_signal_mutex);
  }

  RDB_MUTEX_UNLOCK_CHECK(m_signal_mutex);
}

Rdb_tbl_def *ha_rocksdb::get_table_if_exists(const char *const tablename) {
  std::string str;
  if (rdb_normalize_tablename(tablename, &str) != HA_EXIT_SUCCESS) {
    // We were not passed table name?
    DBUG_ASSERT(0);
    return nullptr;
  }

  return ddl_manager.find(str);
}

/*
  Overload func for delete table ---it deletes table meta data in data
  dictionary immediately and delete real data in background thread(async)

  @param tbl       IN      MyRocks table definition

  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/
int ha_rocksdb::delete_table(Rdb_tbl_def *const tbl) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(tbl != nullptr);
  DBUG_ASSERT(m_tbl_def == nullptr || m_tbl_def == tbl);

  const std::unique_ptr<rocksdb::WriteBatch> wb = dict_manager.begin();
  rocksdb::WriteBatch *const batch = wb.get();

  DBUG_EXECUTE_IF("rocksdb_before_delete_table", {
    const char act[] =
        "now signal ready_to_mark_cf_dropped_before_delete_table wait_for "
        "mark_cf_dropped_done_before_delete_table";
    DBUG_ASSERT(!debug_sync_set_action(ha_thd(), STRING_WITH_LEN(act)));
  });

  {
    std::lock_guard<Rdb_dict_manager> dm_lock(dict_manager);
    dict_manager.add_drop_table(tbl->m_key_descr_arr, tbl->m_key_count, batch);

    /*
      Remove the table entry in data dictionary (this will also remove it from
      the persistent data dictionary).
    */
    ddl_manager.remove(tbl, batch, true);

    int err = dict_manager.commit(batch);
    if (err) {
      DBUG_RETURN(err);
    }
  }

  DBUG_EXECUTE_IF("rocksdb_after_delete_table", {
    const char act[] =
        "now signal ready_to_mark_cf_dropped_after_delete_table "
        "wait_for mark_cf_dropped_done_after_delete_table";
    DBUG_ASSERT(!debug_sync_set_action(ha_thd(), STRING_WITH_LEN(act)));
  });

  rdb_drop_idx_thread.signal();
  // avoid dangling pointer
  m_tbl_def = nullptr;
  DBUG_RETURN(HA_EXIT_SUCCESS);
}

/*
  Note: the following function is called when the table is not open. That is,
  this->table==nullptr, pk_key_descr==nullptr, etc.

  tablename points to line in form "./dbname/tablename".

  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (can be SE-specific)
*/

int ha_rocksdb::delete_table(const char *const tablename,
                             const dd::Table *table_def
                                 MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(tablename != nullptr);

  /* Find the table in the hash */
  Rdb_tbl_def *const tbl = get_table_if_exists(tablename);
  if (!tbl) {
    DBUG_RETURN(HA_ERR_NO_SUCH_TABLE);
  }

  DBUG_RETURN(delete_table(tbl));
}

/**
  @return
    HA_EXIT_SUCCESS  OK
    other            HA_ERR error code (cannot be SE-specific)
*/
int ha_rocksdb::rename_table(
    const char *const from, const char *const to,
    const dd::Table *from_table_def MY_ATTRIBUTE((__unused__)),
    dd::Table *to_table_def MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  std::string from_str;
  std::string to_str;
  std::string from_db;
  std::string to_db;
  int rc;

  if (rdb_is_tablename_normalized(from)) {
    from_str = from;
  } else {
    rc = rdb_normalize_tablename(from, &from_str);
    if (rc != HA_EXIT_SUCCESS) {
      DBUG_RETURN(rc);
    }
  }

  rc = rdb_split_normalized_tablename(from_str, &from_db);
  if (rc != HA_EXIT_SUCCESS) {
    DBUG_RETURN(rc);
  }

  if (rdb_is_tablename_normalized(to)) {
    to_str = to;
  } else {
    rc = rdb_normalize_tablename(to, &to_str);
    if (rc != HA_EXIT_SUCCESS) {
      DBUG_RETURN(rc);
    }
  }

  rc = rdb_split_normalized_tablename(to_str, &to_db);
  if (rc != HA_EXIT_SUCCESS) {
    DBUG_RETURN(rc);
  }

  // If the user changed the database part of the name then validate that the
  // 'to' database exists.
  if (from_db != to_db && !rdb_database_exists(to_db)) {
    // If we return a RocksDB specific error code here we get
    // "error: 206 - Unknown error 206".  InnoDB gets
    // "error -1 - Unknown error -1" so let's match them.
    DBUG_RETURN(-1);
  }

  DBUG_EXECUTE_IF("gen_sql_table_name", to_str = to_str + "_rdb_only";);

  const std::unique_ptr<rocksdb::WriteBatch> wb = dict_manager.begin();
  rocksdb::WriteBatch *const batch = wb.get();

  // rename table is under dict_manager lock, and the cfs used
  // by indices of this table cannot be dropped during the process.
  dict_manager.lock();

  if (ddl_manager.rename(from_str, to_str, batch)) {
    rc = HA_ERR_NO_SUCH_TABLE;
  } else {
    rc = dict_manager.commit(batch);
  }
  dict_manager.unlock();

  DBUG_RETURN(rc);
}

/**
  check_if_incompatible_data() called if ALTER TABLE can't detect otherwise
  if new and old definition are compatible

  @details If there are no other explicit signs like changed number of
  fields this function will be called by compare_tables()
  (sql/sql_tables.cc) to decide should we rewrite whole table or only .frm
  file.

*/

bool ha_rocksdb::check_if_incompatible_data(
    HA_CREATE_INFO *const info MY_ATTRIBUTE((__unused__)),
    uint table_changes MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(info != nullptr);

  // this function is needed only for online alter-table
  DBUG_RETURN(COMPATIBLE_DATA_NO);
}

/**
  @return
    HA_EXIT_SUCCESS  OK
*/
int ha_rocksdb::extra(enum ha_extra_function operation) {
  DBUG_ENTER_FUNC();

  switch (operation) {
    case HA_EXTRA_KEYREAD:
      m_keyread_only = true;
      break;
    case HA_EXTRA_NO_KEYREAD:
      m_keyread_only = false;
      break;
    case HA_EXTRA_FLUSH:
      /*
        If the table has blobs, then they are part of m_retrieved_record.
        This call invalidates them.
      */
      m_retrieved_record.Reset();
      m_dup_key_retrieved_record.Reset();
      break;
    case HA_EXTRA_INSERT_WITH_UPDATE:
      // INSERT ON DUPLICATE KEY UPDATE
      if (rocksdb_enable_insert_with_update_caching) {
        m_insert_with_update = true;
      }
      break;
    case HA_EXTRA_NO_IGNORE_DUP_KEY:
      // PAIRED with HA_EXTRA_INSERT_WITH_UPDATE or HA_EXTRA_WRITE_CAN_REPLACE
      // that indicates the end of REPLACE / INSERT ON DUPLICATE KEY
      m_insert_with_update = false;
      break;

    default:
      break;
  }

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

/*
  Given a starting key and an ending key, estimate the number of rows that
  will exist between the two keys.
*/
ha_rows ha_rocksdb::records_in_range(uint inx, key_range *const min_key,
                                     key_range *const max_key) {
  DBUG_ENTER_FUNC();

  ha_rows ret = THDVAR(ha_thd(), records_in_range);
  if (ret) {
    DBUG_RETURN(ret);
  }
  if (table->force_index) {
    const ha_rows force_rows = THDVAR(ha_thd(), force_index_records_in_range);
    if (force_rows) {
      DBUG_RETURN(force_rows);
    }
  }

  const Rdb_key_def &kd = *m_key_descr_arr[inx];
  auto disk_size = kd.m_stats.m_actual_disk_size;
  if (disk_size == 0) disk_size = kd.m_stats.m_data_size;
  auto rows = kd.m_stats.m_rows;
  if (rows == 0 || disk_size == 0) {
    rows = 1;
    disk_size = ROCKSDB_ASSUMED_KEY_VALUE_DISK_SIZE;
  }
  ulonglong total_size = 0;
  ulonglong total_row = 0;
  records_in_range_internal(inx, min_key, max_key, disk_size, rows, &total_size,
                            &total_row);
  ret = total_row;
  /*
    GetApproximateSizes() gives estimates so ret might exceed stats.records.
    MySQL then decides to use full index scan rather than range scan, which
    is not efficient for most cases.
    To prevent this, changing estimated records slightly smaller than
    stats.records.
  */
  if (ret >= stats.records) {
    ret = stats.records * 0.99;
  }

  if (rocksdb_debug_optimizer_n_rows > 0) {
    ret = rocksdb_debug_optimizer_n_rows;
  } else if (ret == 0) {
    ret = 1;
  }

  DBUG_RETURN(ret);
}

/*
  Given a starting key and an ending key, estimate the total size of rows that
  will exist between the two keys.
*/
ulonglong ha_rocksdb::records_size_in_range(uint inx, key_range *const min_key,
                                            key_range *const max_key) {
  DBUG_ENTER_FUNC();
  ulonglong total_size = 0;
  ulonglong total_row = 0;
  records_in_range_internal(inx, min_key, max_key,
                            ROCKSDB_ASSUMED_KEY_VALUE_DISK_SIZE, 1, &total_size,
                            &total_row);
  DBUG_RETURN(total_size);
}

void ha_rocksdb::records_in_range_internal(uint inx, key_range *const min_key,
                                           key_range *const max_key,
                                           int64 disk_size, int64 rows,
                                           ulonglong *total_size,
                                           ulonglong *row_count) {
  DBUG_ENTER_FUNC();

  const Rdb_key_def &kd = *m_key_descr_arr[inx];

  uint size1 = 0;
  if (min_key) {
    size1 = kd.pack_index_tuple(table, m_pack_buffer, m_sk_packed_tuple,
                                min_key->key, min_key->keypart_map);
    if (min_key->flag == HA_READ_PREFIX_LAST_OR_PREV ||
        min_key->flag == HA_READ_PREFIX_LAST ||
        min_key->flag == HA_READ_AFTER_KEY) {
      kd.successor(m_sk_packed_tuple, size1);
    }
  } else {
    kd.get_infimum_key(m_sk_packed_tuple, &size1);
  }

  uint size2 = 0;
  if (max_key) {
    size2 = kd.pack_index_tuple(table, m_pack_buffer, m_sk_packed_tuple_old,
                                max_key->key, max_key->keypart_map);
    if (max_key->flag == HA_READ_PREFIX_LAST_OR_PREV ||
        max_key->flag == HA_READ_PREFIX_LAST ||
        max_key->flag == HA_READ_AFTER_KEY) {
      kd.successor(m_sk_packed_tuple_old, size2);
    }
  } else {
    kd.get_supremum_key(m_sk_packed_tuple_old, &size2);
  }

  const rocksdb::Slice slice1((const char *)m_sk_packed_tuple, size1);
  const rocksdb::Slice slice2((const char *)m_sk_packed_tuple_old, size2);

  // It's possible to get slice1 == slice2 for a non-inclusive range with the
  // right bound being successor() of the left one, e.g. "t.key>10 AND
  // t.key<11"
  if (slice1.compare(slice2) >= 0) {
    // It's not possible to get slice2 > slice1
    DBUG_ASSERT(!min_key || !max_key || slice1.compare(slice2) == 0);
    DBUG_VOID_RETURN;
  }

  rocksdb::Range r(kd.m_is_reverse_cf ? slice2 : slice1,
                   kd.m_is_reverse_cf ? slice1 : slice2);

  uint64_t sz = 0;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
  // Getting statistics, including from Memtables
  uint8_t include_flags = rocksdb::DB::INCLUDE_FILES;
  rdb->GetApproximateSizes(kd.get_cf(), &r, 1, &sz, include_flags);
  *row_count = rows * ((double)sz / (double)disk_size);
  *total_size = sz;
  uint64_t memTableCount;
  rdb->GetApproximateMemTableStats(kd.get_cf(), r, &memTableCount, &sz);
  *row_count += memTableCount;
  *total_size += sz;
  DBUG_VOID_RETURN;
}

void ha_rocksdb::update_create_info(HA_CREATE_INFO *const create_info) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(create_info != nullptr);

  if (!(create_info->used_fields & HA_CREATE_USED_AUTO)) {
    create_info->auto_increment_value = m_tbl_def->m_auto_incr_val;
  }

  DBUG_VOID_RETURN;
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

  @return
    HA_ADMIN_OK      OK
    other            HA_ADMIN error code
*/
int ha_rocksdb::optimize(THD *const thd MY_ATTRIBUTE((__unused__)),
                         HA_CHECK_OPT *const check_opt
                             MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(thd != nullptr);
  DBUG_ASSERT(check_opt != nullptr);

  for (uint i = 0; i < table->s->keys; i++) {
    uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE * 2];
    auto range = get_range(i, buf);
    const rocksdb::Status s = rdb->CompactRange(getCompactRangeOptions(),
                                                m_key_descr_arr[i]->get_cf(),
                                                &range.start, &range.limit);
    if (!s.ok()) {
      DBUG_RETURN(rdb_error_to_mysql(s));
    }
  }

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

static void init_stats(
    const std::unordered_map<GL_INDEX_ID, std::shared_ptr<const Rdb_key_def>>
        &to_recalc,
    std::unordered_map<GL_INDEX_ID, Rdb_index_stats> *stats) {
  for (const auto &it : to_recalc) {
    const GL_INDEX_ID index_id = it.first;
    auto &kd = it.second;

    (*stats).emplace(index_id, Rdb_index_stats(index_id));
    DBUG_ASSERT(kd->get_key_parts() > 0);
    (*stats)[index_id].m_distinct_keys_per_prefix.resize(kd->get_key_parts());
  }
}

/**
  Calculate the following index stats for all indexes of a table:
  number of rows, file size, and cardinality. It adopts an index
  scan approach using rocksdb::Iterator. Sampling is used to
  accelerate the scan.
**/
static int calculate_cardinality_table_scan(
    const std::unordered_map<GL_INDEX_ID, std::shared_ptr<const Rdb_key_def>>
        &to_recalc,
    std::unordered_map<GL_INDEX_ID, Rdb_index_stats> *stats,
    table_cardinality_scan_type scan_type, uint64_t max_num_rows_scanned,
    std::atomic<THD::killed_state> *killed) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(scan_type != SCAN_TYPE_NONE);
  init_stats(to_recalc, stats);

  auto read_opts = rocksdb::ReadOptions();
  read_opts.fill_cache = false;
  if (scan_type == SCAN_TYPE_MEMTABLE_ONLY) {
    read_opts.read_tier = rocksdb::ReadTier::kMemtableTier;
  } else {
    read_opts.total_order_seek = true;
  }

  Rdb_tbl_card_coll cardinality_collector(rocksdb_table_stats_sampling_pct);

  for (const auto &it_kd : to_recalc) {
    const GL_INDEX_ID index_id = it_kd.first;

    if (!ddl_manager.safe_find(index_id)) {
      // If index id is not in ddl manager, then it has been dropped.
      // Skip scanning index
      continue;
    }

    const std::shared_ptr<const Rdb_key_def> &kd = it_kd.second;
    DBUG_ASSERT(index_id == kd->get_gl_index_id());
    Rdb_index_stats &stat = (*stats)[kd->get_gl_index_id()];

    uchar r_buf[Rdb_key_def::INDEX_NUMBER_SIZE * 2];
    auto r = ha_rocksdb::get_range(*kd, r_buf);
    uint64_t memtableCount;
    uint64_t memtableSize;
    rdb->GetApproximateMemTableStats(kd->get_cf(), r, &memtableCount,
                                     &memtableSize);

    if (scan_type == SCAN_TYPE_MEMTABLE_ONLY &&
        memtableCount < (uint64_t)stat.m_rows / 10) {
      // skip tables that already have enough stats from SST files to reduce
      // overhead and avoid degradation of big tables stats by sampling from
      // relatively tiny (less than 10% of full data set) memtable dataset
      continue;
    }

    // Set memtable count to row count
    stat.m_rows = memtableCount;

    if (scan_type == SCAN_TYPE_FULL_TABLE) {
      // Set memtable size to file size
      stat.m_actual_disk_size = memtableSize;
    }

    std::unique_ptr<rocksdb::Iterator> it = std::unique_ptr<rocksdb::Iterator>(
        rdb->NewIterator(read_opts, kd->get_cf()));
    rocksdb::Slice first_index_key((const char *)r_buf,
                                   Rdb_key_def::INDEX_NUMBER_SIZE);

    // Reset m_last_key for new index
    cardinality_collector.Reset();
    uint64_t rows_scanned = 0ul;
    cardinality_collector
        .Reset(); /* reset m_last_key for each key definition */
    for (it->Seek(first_index_key); is_valid_iterator(it.get()); it->Next()) {
      if (killed && *killed) {
        // NO_LINT_DEBUG
        sql_print_information(
            "Index stats calculation for index %s with id (%u,%u) is "
            "terminated",
            kd->get_name().c_str(), stat.m_gl_index_id.cf_id,
            stat.m_gl_index_id.index_id);
        DBUG_RETURN(HA_EXIT_FAILURE);
      }

      const rocksdb::Slice key = it->key();

      if ((scan_type == SCAN_TYPE_FULL_TABLE && max_num_rows_scanned > 0 &&
           rows_scanned >= max_num_rows_scanned) ||
          !kd->covers_key(key)) {
        break;  // end of this index
      }

      cardinality_collector.ProcessKey(key, kd.get(), &stat);
      rows_scanned++;
    }

    cardinality_collector.SetCardinality(&stat);
    cardinality_collector.AdjustStats(&stat);

    DBUG_EXECUTE_IF("rocksdb_calculate_stats", {
      if (kd->get_name() == "secondary_key") {
        THD *thd = new THD();
        thd->thread_stack = reinterpret_cast<char *>(&thd);
        thd->store_globals();

        const char act[] =
            "now signal ready_to_drop_index wait_for ready_to_save_index_stats";
        DBUG_ASSERT(!debug_sync_set_action(thd, STRING_WITH_LEN(act)));

        thd->restore_globals();
        delete thd;
      }
    });
  }

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

static void reset_cardinality(
    std::unordered_map<GL_INDEX_ID, Rdb_index_stats> *stats) {
  for (auto &src : *stats) {
    Rdb_index_stats &stat = src.second;
    stat.reset_cardinality();
  }
}

static void merge_stats(
    const std::unordered_map<GL_INDEX_ID, std::shared_ptr<const Rdb_key_def>>
        &to_recalc,
    std::unordered_map<GL_INDEX_ID, Rdb_index_stats> *stats,
    const std::unordered_map<GL_INDEX_ID, Rdb_index_stats> &card_stats) {
  DBUG_ASSERT(stats->size() == card_stats.size());

  for (auto &src : *stats) {
    auto index_id = src.first;
    Rdb_index_stats &stat = src.second;
    auto it = card_stats.find(index_id);
    DBUG_ASSERT(it != card_stats.end());

    auto it_index = to_recalc.find(index_id);
    DBUG_ASSERT(it_index != to_recalc.end());
    stat.merge(it->second, true, it_index->second->max_storage_fmt_length());
  }
}

static void adjust_cardinality(
    std::unordered_map<GL_INDEX_ID, Rdb_index_stats> *stats,
    table_cardinality_scan_type scan_type MY_ATTRIBUTE((__unused__)),
    uint64_t max_num_rows_scanned) {
  DBUG_ASSERT(scan_type == SCAN_TYPE_FULL_TABLE);
  DBUG_ASSERT(max_num_rows_scanned > 0);

  for (auto &src : *stats) {
    Rdb_index_stats &stat = src.second;
    if ((uint64_t)stat.m_rows > max_num_rows_scanned) {
      stat.adjust_cardinality(stat.m_rows / max_num_rows_scanned);
    }
#ifndef DBUG_OFF
    for (size_t i = 0; i < stat.m_distinct_keys_per_prefix.size(); i++) {
      DBUG_ASSERT(stat.m_distinct_keys_per_prefix[i] <= stat.m_rows);
    }
#endif
  }
}

static int read_stats_from_ssts(
    const std::unordered_map<GL_INDEX_ID, std::shared_ptr<const Rdb_key_def>>
        &to_recalc,
    std::unordered_map<GL_INDEX_ID, Rdb_index_stats> *stats) {
  DBUG_ENTER_FUNC();

  init_stats(to_recalc, stats);

  // find per column family key ranges which need to be queried
  std::unordered_map<rocksdb::ColumnFamilyHandle *, std::vector<rocksdb::Range>>
      ranges;
  std::vector<uchar> buf(to_recalc.size() * 2 * Rdb_key_def::INDEX_NUMBER_SIZE);

  uchar *bufp = buf.data();
  for (const auto &it : to_recalc) {
    auto &kd = it.second;
    ranges[kd->get_cf()].push_back(ha_rocksdb::get_range(*kd, bufp));
    bufp += 2 * Rdb_key_def::INDEX_NUMBER_SIZE;
  }

  // get RocksDB table properties for these ranges
  rocksdb::TablePropertiesCollection props;
  for (const auto &it : ranges) {
    const auto old_size MY_ATTRIBUTE((__unused__)) = props.size();
    const auto status = rdb->GetPropertiesOfTablesInRange(
        it.first, &it.second[0], it.second.size(), &props);
    DBUG_ASSERT(props.size() >= old_size);
    if (!status.ok()) {
      DBUG_RETURN(ha_rocksdb::rdb_error_to_mysql(
          status, "Could not access RocksDB properties"));
    }
  }

  int num_sst = 0;
  for (const auto &it : props) {
    std::vector<Rdb_index_stats> sst_stats;
    Rdb_tbl_prop_coll::read_stats_from_tbl_props(it.second, &sst_stats);
    /*
      sst_stats is a list of index statistics for indexes that have entries
      in the current SST file.
    */
    for (const auto &it1 : sst_stats) {
      /*
        Only update statistics for indexes that belong to this SQL table.

        The reason is: We are walking through all SST files that have
        entries from this table (and so can compute good statistics). For
        other SQL tables, it can be that we're only seeing a small fraction
        of table's entries (and so we can't update statistics based on that).
      */
      if (stats->find(it1.m_gl_index_id) == stats->end()) {
        continue;
      }

      auto it_index = to_recalc.find(it1.m_gl_index_id);
      DBUG_ASSERT(it_index != to_recalc.end());
      if (it_index == to_recalc.end()) {
        continue;
      }

      (*stats)[it1.m_gl_index_id].merge(
          it1, true, it_index->second->max_storage_fmt_length());
    }
    num_sst++;
  }

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

static int calculate_stats(
    const std::unordered_map<GL_INDEX_ID, std::shared_ptr<const Rdb_key_def>>
        &to_recalc,
    table_cardinality_scan_type scan_type,
    std::atomic<THD::killed_state> *killed) {
  DBUG_ENTER_FUNC();

  std::unordered_map<GL_INDEX_ID, Rdb_index_stats> stats;
  int ret = read_stats_from_ssts(to_recalc, &stats);
  if (ret != HA_EXIT_SUCCESS) {
    DBUG_RETURN(ret);
  }

  if (scan_type != SCAN_TYPE_NONE) {
    std::unordered_map<GL_INDEX_ID, Rdb_index_stats> card_stats;
    uint64_t max_num_rows_scanned = rocksdb_table_stats_max_num_rows_scanned;
    ret = calculate_cardinality_table_scan(to_recalc, &card_stats, scan_type,
                                           max_num_rows_scanned, killed);
    if (ret != HA_EXIT_SUCCESS) {
      DBUG_RETURN(ret);
    }

    if (scan_type == SCAN_TYPE_FULL_TABLE) {
      reset_cardinality(&stats);
    }

    merge_stats(to_recalc, &stats, card_stats);
    if (scan_type == SCAN_TYPE_FULL_TABLE && max_num_rows_scanned > 0) {
      adjust_cardinality(&stats, scan_type, max_num_rows_scanned);
    }
  }

  // set and persist new stats
  ddl_manager.set_stats(stats);
  ddl_manager.persist_stats(true);

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

static int calculate_stats_for_table(
    const std::string &tbl_name, table_cardinality_scan_type scan_type,
    std::atomic<THD::killed_state> *killed = nullptr) {
  DBUG_ENTER_FUNC();
  std::unordered_map<GL_INDEX_ID, std::shared_ptr<const Rdb_key_def>> to_recalc;
  std::vector<GL_INDEX_ID> indexes;
  ddl_manager.find_indexes(tbl_name, &indexes);

  for (const auto &index : indexes) {
    std::shared_ptr<const Rdb_key_def> keydef = ddl_manager.safe_find(index);

    if (keydef) {
      to_recalc.insert(std::make_pair(keydef->get_gl_index_id(), keydef));
    }
  }

  if (to_recalc.empty()) {
    DBUG_RETURN(HA_EXIT_FAILURE);
  }

  DBUG_EXECUTE_IF("rocksdb_is_bg_thread_drop_table", {
    if (tbl_name == "test.t") {
      THD *thd = new THD();
      thd->thread_stack = reinterpret_cast<char *>(&thd);
      thd->store_globals();

      const char act[] = "now signal ready_to_drop_table";
      DBUG_ASSERT(!debug_sync_set_action(thd, STRING_WITH_LEN(act)));

      thd->restore_globals();
      delete thd;
    }
  });

  int err = calculate_stats(to_recalc, scan_type, killed);
  if (err != HA_EXIT_SUCCESS) {
    DBUG_RETURN(err);
  }

  DBUG_EXECUTE_IF("rocksdb_is_bg_thread_drop_table", {
    if (tbl_name == "test.t") {
      THD *thd = new THD();
      thd->thread_stack = reinterpret_cast<char *>(&thd);
      thd->store_globals();

      const char act[] = "now wait_for ready_to_save_table_stats";
      DBUG_ASSERT(!debug_sync_set_action(thd, STRING_WITH_LEN(act)));

      thd->restore_globals();
      delete thd;
    }
  });

  if (scan_type == SCAN_TYPE_FULL_TABLE) {
    // Save table stats including number of rows
    // and modified counter
    ddl_manager.set_table_stats(tbl_name);
  }

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

/*
  @return
    HA_ADMIN_OK      OK
    other            HA_ADMIN error code
*/
int ha_rocksdb::analyze(
    THD *const thd, HA_CHECK_OPT *const check_opt MY_ATTRIBUTE((__unused__))) {
  DBUG_ENTER_FUNC();

  if (table) {
    table_cardinality_scan_type scan_type = rocksdb_table_stats_use_table_scan
                                                ? SCAN_TYPE_FULL_TABLE
                                                : SCAN_TYPE_MEMTABLE_ONLY;

    if (calculate_stats_for_table(m_tbl_def->full_tablename(), scan_type,
                                  &(thd->killed)) != HA_EXIT_SUCCESS) {
      DBUG_RETURN(HA_ADMIN_FAILED);
    }
  }

  // A call to ::info is needed to repopulate some SQL level structs. This is
  // necessary for online analyze because we cannot rely on another ::open
  // call to call info for us.
  if (info(HA_STATUS_CONST | HA_STATUS_VARIABLE) != HA_EXIT_SUCCESS) {
    DBUG_RETURN(HA_ADMIN_FAILED);
  }

  DBUG_RETURN(HA_ADMIN_OK);
}

int ha_rocksdb::adjust_handler_stats_sst_and_memtable(ha_statistics *ha_stats,
                                                      Rdb_tbl_def *tbl_def) {
  DBUG_ENTER_FUNC();

  /*
    If any stats are negative due to bad cached stats, re-run analyze table
    and re-retrieve the stats.
  */
  if (static_cast<longlong>(ha_stats->data_file_length) < 0 ||
      static_cast<longlong>(ha_stats->index_file_length) < 0 ||
      static_cast<longlong>(ha_stats->records) < 0) {
    if (calculate_stats_for_table(tbl_def->full_tablename(), SCAN_TYPE_NONE)) {
      DBUG_RETURN(HA_EXIT_FAILURE);
    }

    rocksdb_get_stats(ha_stats, tbl_def);
  }

  // if number of records is hardcoded, we do not want to force computation
  // of memtable cardinalities
  if (ha_stats->records == 0 || (rocksdb_force_compute_memtable_stats &&
                                 rocksdb_debug_optimizer_n_rows == 0)) {
    // First, compute SST files stats
    uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE * 2];
    std::shared_ptr<Rdb_key_def> pk_def = tbl_def->get_pk_def();
    auto r = ha_rocksdb::get_range(*pk_def, buf);
    uint64_t sz = 0;

    uint8_t include_flags = rocksdb::DB::INCLUDE_FILES;

    // recompute SST files stats only if records count is 0
    if (ha_stats->records == 0) {
      rdb->GetApproximateSizes(pk_def->get_cf(), &r, 1, &sz, include_flags);
      ha_stats->records += sz / ROCKSDB_ASSUMED_KEY_VALUE_DISK_SIZE;
      ha_stats->data_file_length += sz;
    }

    // Second, compute memtable stats. This call is expensive, so cache
    // values computed for some time.
    uint64_t cachetime = rocksdb_force_compute_memtable_stats_cachetime;
    uint64_t time = (cachetime == 0) ? 0 : my_micro_time();
    if (cachetime == 0 || time > tbl_def->m_mtcache_last_update + cachetime) {
      uint64_t memtableCount;
      uint64_t memtableSize;

      // the stats below are calculated from skiplist wich is a probablistic
      // data structure, so the results vary between test runs
      // it also can return 0 for quite a large tables which means that
      // cardinality for memtable only indxes will be reported as 0

      rdb->GetApproximateMemTableStats(pk_def->get_cf(), r, &memtableCount,
                                       &memtableSize);

      // Atomically update all of these fields at the same time
      if (cachetime > 0) {
        if (tbl_def->m_mtcache_lock.fetch_add(1, std::memory_order_acquire) ==
            0) {
          tbl_def->m_mtcache_count = memtableCount;
          tbl_def->m_mtcache_size = memtableSize;
          tbl_def->m_mtcache_last_update = time;
        }
        tbl_def->m_mtcache_lock.fetch_sub(1, std::memory_order_release);
      }

      ha_stats->records += memtableCount;
      ha_stats->data_file_length += memtableSize;
    } else {
      // Cached data is still valid, so use it instead
      ha_stats->records += tbl_def->m_mtcache_count;
      ha_stats->data_file_length += tbl_def->m_mtcache_size;
    }
  }

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

void ha_rocksdb::get_auto_increment(
    ulonglong off, ulonglong inc,
    ulonglong nb_desired_values MY_ATTRIBUTE((__unused__)),
    ulonglong *const first_value, ulonglong *const nb_reserved_values) {
  /*
    MySQL has a somewhat complicated way of handling the auto-increment value.
    The first time get_auto_increment is called for a statement,
    nb_desired_values is the estimate for how many values will be needed.  The
    engine can then reserve some values, and those will be automatically used
    by MySQL, until a hard-coded value shows up in the insert statement, after
    which MySQL again calls this function to reset its starting value.
   *
    For simplicity we will just ignore nb_desired_values - we aren't going to
    reserve any extra values for a multi-insert statement.  Each row will
    simply acquire the next value as needed and we will always tell MySQL that
    we only reserved 1 value.  Since we are using an atomic value for
    m_auto_incr_val this should be safe - if we had to grab a mutex, doing
    an actual reserve of some values might be a better solution.
   */
  DEBUG_SYNC(ha_thd(), "rocksdb.autoinc_vars");

  if (off > inc) {
    off = 1;
  }

  Field *field;
  ulonglong new_val, max_val;
  field = table->key_info[table->s->next_number_index].key_part[0].field;
  max_val = field->get_max_int_value();

  // Local variable reference to simplify code below
  auto &auto_incr = m_tbl_def->m_auto_incr_val;

  if (inc == 1) {
    DBUG_ASSERT(off == 1);
    // Optimization for the standard case where we are always simply
    // incrementing from the last position

    // Use CAS operation in a loop to make sure automically get the next auto
    // increment value while ensuring that we don't wrap around to a negative
    // number.
    //
    // We set auto_incr to the min of max_val and new_val + 1. This means that
    // if we're at the maximum, we should be returning the same value for
    // multiple rows, resulting in duplicate key errors (as expected).
    //
    // If we return values greater than the max, the SQL layer will "truncate"
    // the value anyway, but it means that we store invalid values into
    // auto_incr that will be visible in SHOW CREATE TABLE.
    new_val = auto_incr;
    while (new_val != std::numeric_limits<ulonglong>::max()) {
      if (auto_incr.compare_exchange_weak(new_val,
                                          std::min(new_val + 1, max_val))) {
        break;
      }
    }
  } else {
    // The next value can be more complicated if either 'inc' or 'off' is not 1
    ulonglong last_val = auto_incr;

    if (last_val > max_val) {
      new_val = std::numeric_limits<ulonglong>::max();
    } else {
      // Loop until we can correctly update the atomic value
      do {
        DBUG_ASSERT(last_val > 0);
        // Calculate the next value in the auto increment series: offset
        // + N * increment where N is 0, 1, 2, ...
        //
        // For further information please visit:
        // http://dev.mysql.com/doc/refman/5.7/en/replication-options-master.html
        //
        // The following is confusing so here is an explanation:
        // To get the next number in the sequence above you subtract out the
        // offset, calculate the next sequence (N * increment) and then add the
        // offset back in.
        //
        // The additions are rearranged to avoid overflow.  The following is
        // equivalent to (last_val - 1 + inc - off) / inc. This uses the fact
        // that (a+b)/c = a/c + b/c + (a%c + b%c)/c. To show why:
        //
        // (a+b)/c
        // = (a - a%c + a%c + b - b%c + b%c) / c
        // = (a - a%c) / c + (b - b%c) / c + (a%c + b%c) / c
        // = a/c + b/c + (a%c + b%c) / c
        //
        // Now, substitute a = last_val - 1, b = inc - off, c = inc to get the
        // following statement.
        ulonglong n =
            (last_val - 1) / inc + ((last_val - 1) % inc + inc - off) / inc;

        // Check if n * inc + off will overflow. This can only happen if we have
        // an UNSIGNED BIGINT field.
        if (n > (std::numeric_limits<ulonglong>::max() - off) / inc) {
          DBUG_ASSERT(max_val == std::numeric_limits<ulonglong>::max());
          // The 'last_val' value is already equal to or larger than the largest
          // value in the sequence.  Continuing would wrap around (technically
          // the behavior would be undefined).  What should we do?
          // We could:
          //   1) set the new value to the last possible number in our sequence
          //      as described above.  The problem with this is that this
          //      number could be smaller than a value in an existing row.
          //   2) set the new value to the largest possible number.  This number
          //      may not be in our sequence, but it is guaranteed to be equal
          //      to or larger than any other value already inserted.
          //
          //  For now I'm going to take option 2.
          //
          //  Returning ULLONG_MAX from get_auto_increment will cause the SQL
          //  layer to fail with ER_AUTOINC_READ_FAILED. This means that due to
          //  the SE API for get_auto_increment, inserts will fail with
          //  ER_AUTOINC_READ_FAILED if the column is UNSIGNED BIGINT, but
          //  inserts will fail with ER_DUP_ENTRY for other types (or no failure
          //  if the column is in a non-unique SK).
          new_val = std::numeric_limits<ulonglong>::max();
          auto_incr = new_val;  // Store the largest value into auto_incr
          break;
        }

        new_val = n * inc + off;

        // Attempt to store the new value (plus 1 since m_auto_incr_val contains
        // the next available value) into the atomic value.  If the current
        // value no longer matches what we have in 'last_val' this will fail and
        // we will repeat the loop (`last_val` will automatically get updated
        // with the current value).
        //
        // See above explanation for inc == 1 for why we use std::min.
      } while (!auto_incr.compare_exchange_weak(
          last_val, std::min(new_val + 1, max_val)));
    }
  }

  *first_value = new_val;
  *nb_reserved_values = 1;
}

#ifndef DBUG_OFF

/* Debugger help function */
static char dbug_item_print_buf[512];

const char *dbug_print_item(Item *const item) {
  THD *const thd = my_core::thd_get_current_thd();
  char *const buf = dbug_item_print_buf;
  String str(buf, sizeof(dbug_item_print_buf), &my_charset_bin);
  str.length(0);
  if (!item) return "(Item*)nullptr";
  item->print(thd, &str, QT_ORDINARY);
  if (str.c_ptr() == buf) {
    return buf;
  } else {
    return "Couldn't fit into buffer";
  }
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

class Item *ha_rocksdb::idx_cond_push(uint keyno, class Item *const idx_cond) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(keyno != MAX_KEY);
  DBUG_ASSERT(idx_cond != nullptr);

  pushed_idx_cond = idx_cond;
  pushed_idx_cond_keyno = keyno;
  in_range_check_pushed_down = true;

  /* We will check the whole condition */
  DBUG_RETURN(nullptr);
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

enum icp_result ha_rocksdb::check_index_cond() const {
  DBUG_ASSERT(pushed_idx_cond);
  DBUG_ASSERT(pushed_idx_cond_keyno != MAX_KEY);

  if (end_range && compare_key_icp(end_range) > 0) {
    /* caller should return HA_ERR_END_OF_FILE already */
    return ICP_OUT_OF_RANGE;
  }

  return pushed_idx_cond->val_int() ? ICP_MATCH : ICP_NO_MATCH;
}

/** Operations for altering a table that RocksDB does not care about */
static const Alter_inplace_info::HA_ALTER_FLAGS ROCKSDB_INPLACE_IGNORE =
    Alter_inplace_info::ALTER_COLUMN_DEFAULT |
    Alter_inplace_info::ALTER_COLUMN_COLUMN_FORMAT |
    Alter_inplace_info::ALTER_COLUMN_STORAGE_TYPE |
    Alter_inplace_info::ALTER_RENAME | Alter_inplace_info::CHANGE_INDEX_OPTION |
    Alter_inplace_info::ADD_CHECK_CONSTRAINT |
    Alter_inplace_info::DROP_CHECK_CONSTRAINT |
    Alter_inplace_info::SUSPEND_CHECK_CONSTRAINT;

/*
    Checks if instant alter is supported for a given operation.
*/
ha_rocksdb::Instant_Type ha_rocksdb::rocksdb_support_instant(
    my_core::Alter_inplace_info *const ha_alter_info MY_ATTRIBUTE((unused)),
    const TABLE *old_table MY_ATTRIBUTE((unused)),
    const TABLE *altered_table MY_ATTRIBUTE((unused))) const {
  if (rocksdb_instant_ddl) {
    return Instant_Type::INSTANT_IMPOSSIBLE;
  }

  if (!(ha_alter_info->handler_flags & ~ROCKSDB_INPLACE_IGNORE)) {
    /* after adding support, return (Instant_Type::INSTANT_NO_CHANGE) */
    return (Instant_Type::INSTANT_IMPOSSIBLE);
  }

  Alter_inplace_info::HA_ALTER_FLAGS alter_inplace_flags =
      ha_alter_info->handler_flags & ~ROCKSDB_INPLACE_IGNORE;

  /* If it's only adding and(or) dropping virtual columns */
  if (!(alter_inplace_flags & ~(Alter_inplace_info::ADD_VIRTUAL_COLUMN |
                                Alter_inplace_info::DROP_VIRTUAL_COLUMN))) {
    /* after enable, return (Instant_Type::INSTANT_VIRTUAL_ONLY); */
    return (Instant_Type::INSTANT_IMPOSSIBLE);
  }

  /* If it's an ADD COLUMN without changing existing column orders */
  if (alter_inplace_flags == Alter_inplace_info::ADD_STORED_BASE_COLUMN) {
    /* after add col instant support, return Instant_Type::INSTANT_ADD_COLUMN
     * */
    return (Instant_Type::INSTANT_IMPOSSIBLE);
  }

  // by default, impossible to do instant ddl
  return (Instant_Type::INSTANT_IMPOSSIBLE);
}

/*
  Checks if inplace alter is supported for a given operation.
*/
my_core::enum_alter_inplace_result ha_rocksdb::check_if_supported_inplace_alter(
    TABLE *altered_table, my_core::Alter_inplace_info *const ha_alter_info) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(ha_alter_info != nullptr);

  /* check instant ddl type */
  Instant_Type instant_type =
      rocksdb_support_instant(ha_alter_info, this->table, altered_table);

  /* by default, impossible to do intant ddl */
  ha_alter_info->handler_trivial_ctx =
      static_cast<uint8>(Instant_Type::INSTANT_IMPOSSIBLE);

  switch (instant_type) {
    case Instant_Type::INSTANT_IMPOSSIBLE:
      break;
    case Instant_Type::INSTANT_ADD_COLUMN:
      if (ha_alter_info->alter_info->requested_algorithm ==
          Alter_info::ALTER_TABLE_ALGORITHM_INPLACE) {
        /* Still fall back to INPLACE since the behaviour is different */
        break;
      } else if (ha_alter_info->error_if_not_empty) {
        /* In this case, it can't be instant because the table
        may not be empty. Have to fall back to INPLACE */
        break;
      }
      /* Fall through */
    case Instant_Type::INSTANT_NO_CHANGE:
    case Instant_Type::INSTANT_VIRTUAL_ONLY:
      ha_alter_info->handler_trivial_ctx = static_cast<uint8>(instant_type);
      DBUG_RETURN(HA_ALTER_INPLACE_INSTANT);
  }

  if (ha_alter_info->handler_flags &
      ~(my_core::Alter_inplace_info::DROP_INDEX |
        my_core::Alter_inplace_info::DROP_UNIQUE_INDEX |
        my_core::Alter_inplace_info::ADD_INDEX |
        my_core::Alter_inplace_info::ADD_UNIQUE_INDEX |
        my_core::Alter_inplace_info::CHANGE_CREATE_OPTION |
        my_core::Alter_inplace_info::DROP_PARTITION |
        (rocksdb_alter_column_default_inplace
             ? my_core::Alter_inplace_info::ALTER_COLUMN_DEFAULT
             : 0))) {
    DBUG_RETURN(my_core::HA_ALTER_INPLACE_NOT_SUPPORTED);
  }

  /* We don't support unique keys on table w/ no primary keys */
  if ((ha_alter_info->handler_flags &
       my_core::Alter_inplace_info::ADD_UNIQUE_INDEX) &&
      has_hidden_pk(altered_table)) {
    DBUG_RETURN(my_core::HA_ALTER_INPLACE_NOT_SUPPORTED);
  }

  /* We only support changing auto_increment for table options. */
  if ((ha_alter_info->handler_flags &
       my_core::Alter_inplace_info::CHANGE_CREATE_OPTION) &&
      !(ha_alter_info->create_info->used_fields & HA_CREATE_USED_AUTO)) {
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
    TABLE *const altered_table,
    my_core::Alter_inplace_info *const ha_alter_info,
    const dd::Table *old_table_def MY_ATTRIBUTE((unused)),
    dd::Table *new_table_def MY_ATTRIBUTE((unused))) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(altered_table != nullptr);
  DBUG_ASSERT(ha_alter_info != nullptr);

  Rdb_tbl_def *new_tdef = nullptr;
  std::shared_ptr<Rdb_key_def> *old_key_descr = nullptr;
  std::shared_ptr<Rdb_key_def> *new_key_descr = nullptr;
  uint old_n_keys = m_tbl_def->m_key_count;
  uint new_n_keys = altered_table->s->keys;
  std::unordered_set<std::shared_ptr<Rdb_key_def>> added_indexes;
  std::unordered_set<GL_INDEX_ID> dropped_index_ids;
  uint n_dropped_keys = 0;
  uint n_added_keys = 0;
  ulonglong max_auto_incr = 0;

  if (ha_alter_info->handler_flags &
      (my_core::Alter_inplace_info::DROP_INDEX |
       my_core::Alter_inplace_info::DROP_UNIQUE_INDEX |
       my_core::Alter_inplace_info::ADD_INDEX |
       my_core::Alter_inplace_info::ADD_UNIQUE_INDEX)) {
    if (has_hidden_pk(altered_table)) {
      new_n_keys += 1;
    }

    const TABLE *const old_table = table;
    old_key_descr = m_tbl_def->m_key_descr_arr;
    new_key_descr = new std::shared_ptr<Rdb_key_def>[new_n_keys];

    new_tdef = new Rdb_tbl_def(m_tbl_def->full_tablename());
    new_tdef->m_key_descr_arr = new_key_descr;
    new_tdef->m_key_count = new_n_keys;
    new_tdef->m_pk_index = altered_table->s->primary_key;
    new_tdef->m_auto_incr_val =
        m_tbl_def->m_auto_incr_val.load(std::memory_order_relaxed);
    new_tdef->m_hidden_pk_val =
        m_tbl_def->m_hidden_pk_val.load(std::memory_order_relaxed);

    if (create_key_defs(altered_table, new_tdef, "" /*actual_user_table_name*/,
                        table, m_tbl_def)) {
      /* Delete the new key descriptors */
      delete[] new_key_descr;

      /*
        Explicitly mark as nullptr so we don't accidentally remove entries
        from data dictionary on cleanup (or cause double delete[]).
        */
      new_tdef->m_key_descr_arr = nullptr;
      delete new_tdef;

      my_error(ER_KEY_CREATE_DURING_ALTER, MYF(0));
      DBUG_RETURN(HA_EXIT_FAILURE);
    }

    uint i;
    uint j;

    /* Determine which(if any) key definition(s) need to be dropped */
    for (i = 0; i < ha_alter_info->index_drop_count; i++) {
      const KEY *const dropped_key = ha_alter_info->index_drop_buffer[i];
      for (j = 0; j < old_n_keys; j++) {
        const KEY *const old_key =
            &old_table->key_info[old_key_descr[j]->get_keyno()];

        if (!compare_keys(old_key, dropped_key)) {
          dropped_index_ids.insert(old_key_descr[j]->get_gl_index_id());
          break;
        }
      }
    }

    /* Determine which(if any) key definitions(s) need to be added */
    int identical_indexes_found = 0;
    for (i = 0; i < ha_alter_info->index_add_count; i++) {
      const KEY *const added_key =
          &ha_alter_info->key_info_buffer[ha_alter_info->index_add_buffer[i]];
      for (j = 0; j < new_n_keys; j++) {
        const KEY *const new_key =
            &altered_table->key_info[new_key_descr[j]->get_keyno()];
        if (!compare_keys(new_key, added_key)) {
          /*
            Check for cases where an 'identical' index is being dropped and
            re-added in a single ALTER statement.  Turn this into a no-op as the
            index has not changed.

            E.G. Unique index -> non-unique index requires no change

            Note that cases where the index name remains the same but the
            key-parts are changed is already handled in create_inplace_key_defs.
            In these cases the index needs to be rebuilt.
            */
          if (dropped_index_ids.count(new_key_descr[j]->get_gl_index_id())) {
            dropped_index_ids.erase(new_key_descr[j]->get_gl_index_id());
            identical_indexes_found++;
          } else {
            added_indexes.insert(new_key_descr[j]);
          }

          break;
        }
      }
    }

    n_dropped_keys = ha_alter_info->index_drop_count - identical_indexes_found;
    n_added_keys = ha_alter_info->index_add_count - identical_indexes_found;
    DBUG_ASSERT(dropped_index_ids.size() == n_dropped_keys);
    DBUG_ASSERT(added_indexes.size() == n_added_keys);
    DBUG_ASSERT(new_n_keys == (old_n_keys - n_dropped_keys + n_added_keys));
  }
  if (ha_alter_info->handler_flags &
      my_core::Alter_inplace_info::CHANGE_CREATE_OPTION) {
    if (!new_tdef) {
      new_tdef = m_tbl_def;
    }
    if (table->found_next_number_field) {
      max_auto_incr = load_auto_incr_value_from_index();
    }
  }

  ha_alter_info->handler_ctx = new (*THR_MALLOC)
      Rdb_inplace_alter_ctx(new_tdef, old_key_descr, new_key_descr, old_n_keys,
                            new_n_keys, added_indexes, dropped_index_ids,
                            n_added_keys, n_dropped_keys, max_auto_incr);
  DBUG_RETURN(HA_EXIT_SUCCESS);
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
bool ha_rocksdb::inplace_alter_table(
    TABLE *const altered_table,
    my_core::Alter_inplace_info *const ha_alter_info,
    const dd::Table *old_table_def MY_ATTRIBUTE((unused)),
    dd::Table *new_table_def MY_ATTRIBUTE((unused))) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(altered_table != nullptr);
  DBUG_ASSERT(ha_alter_info != nullptr);
  DBUG_ASSERT(ha_alter_info->handler_ctx != nullptr);

  bool res = HA_EXIT_SUCCESS;
  Rdb_inplace_alter_ctx *const ctx =
      static_cast<Rdb_inplace_alter_ctx *>(ha_alter_info->handler_ctx);

  if (!(ha_alter_info->handler_flags &
        (my_core::Alter_inplace_info::ADD_INDEX |
         my_core::Alter_inplace_info::ADD_UNIQUE_INDEX))) {
    DBUG_RETURN(res);
  }

  /*
    Buffers need to be set up again to account for new, possibly longer
    secondary keys.
  */
  free_key_buffers();

  DBUG_ASSERT(ctx != nullptr);

  /*
    If adding unique index, allocate special buffers for duplicate checking.
  */
  int err;
  if ((err = alloc_key_buffers(altered_table, ctx->m_new_tdef))) {
    my_error(ER_OUT_OF_RESOURCES, MYF(0));
    res = HA_EXIT_FAILURE;
    goto end;
  }

  /* Populate all new secondary keys by scanning the primary key. */
  if ((err = inplace_populate_sk(altered_table, ctx->m_added_indexes))) {
    my_error(ER_SK_POPULATE_DURING_ALTER, MYF(0));
    res = HA_EXIT_FAILURE;
    goto end;
  }

  DBUG_EXECUTE_IF("myrocks_simulate_index_create_rollback", {
    dbug_create_err_inplace_alter();
    res = HA_EXIT_FAILURE;
  };);

end:
  // During inplace_populate_sk, we may perform duplicate key checking which
  // involves calling unpack_record. However, blobs end up being unpacked
  // using the buffer in altered_table->file, instead of the current handler
  // object. This handler does not get cleaned up as it was never
  // opened, so we end up leaking that buffer. To fix, we call free_key_buffers
  // here.
  //
  // In the partitioned case, the altered_table->file is not only unopened, but
  // also unintialized, but we only unpack on duplicate key errors, and the
  // error is not possible on partitioned tables because indexes must include
  // the partitioning key which must include the primary key.
  //
  // A better solution would be to pass in the blob buffer from the correct
  // handler explicitly into the unpack functions, so that
  // Rdb_key_def::unpack_record does not need to derive it from the TABLE
  // object, since the Rdb_key_def class are generally unaware of handler
  // classes anyway.
  if (err == ER_DUP_ENTRY) {
    DBUG_ASSERT(ha_alter_info->group_commit_ctx == nullptr);
    reinterpret_cast<ha_rocksdb *>(altered_table->file)->free_key_buffers();
  }

  // Try to reallocate key buffers from the old schema.
  //
  // A better fix may to save the original buffers to avoid needing to
  // reallocate here.
  if (res == HA_EXIT_FAILURE) {
    free_key_buffers();
    err = alloc_key_buffers(table, m_tbl_def);
  }

  DBUG_RETURN(res);
}

/**
 Scan the Primary Key index entries and populate the new secondary keys.
*/
int ha_rocksdb::inplace_populate_sk(
    TABLE *const new_table_arg,
    const std::unordered_set<std::shared_ptr<Rdb_key_def>> &indexes) {
  DBUG_ENTER_FUNC();
  int res = HA_EXIT_SUCCESS;
  const std::unique_ptr<rocksdb::WriteBatch> wb = dict_manager.begin();
  rocksdb::WriteBatch *const batch = wb.get();

  DBUG_EXECUTE_IF("rocksdb_inplace_populate_sk", {
    const char act[] =
        "now signal ready_to_mark_cf_dropped_in_populate_sk "
        "wait_for mark_cf_dropped_done_in_populate_sk";
    DBUG_ASSERT(!debug_sync_set_action(ha_thd(), STRING_WITH_LEN(act)));
  });

  {
    std::lock_guard<Rdb_dict_manager> dm_lock(dict_manager);
    for (const auto &kd : indexes) {
      const std::string cf_name = kd->get_cf()->GetName();
      std::shared_ptr<rocksdb::ColumnFamilyHandle> cfh =
          cf_manager.get_cf(cf_name);

      if (!cfh || cfh != kd->get_shared_cf()) {
        // The CF has been dropped, i.e., cf_manager.remove_dropped_cf() has
        // been called.
        DBUG_RETURN(HA_EXIT_FAILURE);
      }

      uint32 cf_id = cfh->GetID();
      if (dict_manager.get_dropped_cf(cf_id)) {
        DBUG_RETURN(HA_EXIT_FAILURE);
      }
    }

    /* Update the data dictionary */
    std::unordered_set<GL_INDEX_ID> create_index_ids;
    for (const auto &index : indexes) {
      create_index_ids.insert(index->get_gl_index_id());
    }
    dict_manager.add_create_index(create_index_ids, batch);
    res = dict_manager.commit(batch);
    if (res != HA_EXIT_SUCCESS) {
      return res;
    }

    /*
      Add uncommitted key definitons to ddl_manager.  We need to do this
      so that the property collector can find this keydef when it needs to
      update stats.  The property collector looks for the keydef in the
      data dictionary, but it won't be there yet since this key definition
      is still in the creation process.
    */
    ddl_manager.add_uncommitted_keydefs(indexes);
  }

  const bool hidden_pk_exists = has_hidden_pk(table);

  Rdb_transaction *tx = get_or_create_tx(table->in_use);

  /*
    There is one specific scenario where m_sst_info may not be nullptr. This
    happens if the handler we're using happens to be the handler where the PK
    bulk load was done on. The sequence of events that lead to this is as
    follows (T1 is PK bulk load, T2 is SK alter table):

    T1: Execute last INSERT statement
    T1: Return TABLE and handler object back to Table_cache_manager
    T1: Close connection
    T2: Execute ALTER statement
    T2: Take same TABLE/handler from Table_cache_manager
    T2: Call closefrm which will call finalize_bulk_load on every other open
        table/handler *except* the one it's on.
    T2: Acquire stale snapshot of PK
    T1: Call finalize_bulk_load

    This is rare because usually, closefrm will call the destructor (and thus
    finalize_bulk_load) on the handler where PK bulk load is done. However, if
    the thread ids of the bulk load thread and the alter thread differ by a
    multiple of table_cache_instances (8 by default), then they hash to the
    same bucket in Table_cache_manager and the alter thread will not not call
    the destructor on the handler it is holding. Thus, its m_sst_info will not
    be nullptr.

    At this point, it is safe to refresh the snapshot because we know all other
    open handlers have been closed at this point, and the one we're on is the
    only one left.
  */
  if (m_sst_info) {
    if ((res = finalize_bulk_load())) {
      DBUG_RETURN(res);
    }
    DBUG_ASSERT(tx->get_write_count() == 0);
    tx->release_snapshot();
  }

  for (const auto &index : indexes) {
    // Skip populating partial indexes.
    if (index->is_partial_index() && !THDVAR(ha_thd(), bulk_load_partial_index))
      continue;

    /*
      Note: We use the currently existing table + tbl_def object here,
      as the pk index position may have changed in the case of hidden primary
      keys.
    */
    res = ha_rnd_init(true /* scan */);
    if (res) DBUG_RETURN(res);

    /* Scan each record in the primary key in order */
    for (res = ha_rnd_next(table->record[0]); res == 0;
         res = ha_rnd_next(table->record[0])) {
      longlong hidden_pk_id = 0;
      if (hidden_pk_exists &&
          (res = read_hidden_pk_id_from_rowkey(&hidden_pk_id))) {
        // NO_LINT_DEBUG
        sql_print_error("Error retrieving hidden pk id.");
        ha_rnd_end();
        DBUG_RETURN(res);
      }

      /* Create new secondary index entry */
      const int new_packed_size = index->pack_record(
          new_table_arg, m_pack_buffer, table->record[0], m_sk_packed_tuple,
          &m_sk_tails, should_store_row_debug_checksums(), hidden_pk_id, 0,
          nullptr, m_ttl_bytes);

      const rocksdb::Slice key = rocksdb::Slice(
          reinterpret_cast<const char *>(m_sk_packed_tuple), new_packed_size);
      const rocksdb::Slice val =
          rocksdb::Slice(reinterpret_cast<const char *>(m_sk_tails.ptr()),
                         m_sk_tails.get_current_pos());

      /*
        Add record to offset tree in preparation for writing out to
        disk in sorted chunks.
      */
      if ((res = bulk_load_key(tx, *index, key, val, true))) {
        ha_rnd_end();
        DBUG_RETURN(res);
      }
    }

    if (res != HA_ERR_END_OF_FILE) {
      // NO_LINT_DEBUG
      sql_print_error("Error retrieving index entry from primary key.");
      ha_rnd_end();
      DBUG_RETURN(res);
    }

    ha_rnd_end();

    bool is_critical_error;
    res = tx->finish_bulk_load(&is_critical_error, true, new_table_arg,
                               m_table_handler->m_table_name);

    if (res == ER_DUP_ENTRY) {
      DBUG_ASSERT(new_table_arg->key_info[index->get_keyno()].flags &
                  HA_NOSAME);
      print_keydup_error(new_table_arg,
                         &new_table_arg->key_info[index->get_keyno()], MYF(0),
                         ha_thd());
    }

    if (res && is_critical_error) {
      // NO_LINT_DEBUG
      sql_print_error("Error finishing bulk load.");
      DBUG_RETURN(res);
    }
  }

  /*
    Explicitly tell jemalloc to clean up any unused dirty pages at this point.
    See https://reviews.facebook.net/D63723 for more details.
  */
  purge_all_jemalloc_arenas();

  DBUG_EXECUTE_IF("crash_during_online_index_creation", DBUG_SUICIDE(););
  DBUG_RETURN(res);
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
    my_core::TABLE *const altered_table,
    my_core::Alter_inplace_info *const ha_alter_info, bool commit,
    const dd::Table *old_table_def MY_ATTRIBUTE((unused)),
    dd::Table *new_table_def MY_ATTRIBUTE((unused))) {
  DBUG_ENTER_FUNC();

  DBUG_ASSERT(altered_table != nullptr);
  DBUG_ASSERT(ha_alter_info != nullptr);

  Rdb_inplace_alter_ctx *const ctx0 =
      static_cast<Rdb_inplace_alter_ctx *>(ha_alter_info->handler_ctx);

  DEBUG_SYNC(ha_thd(), "rocksdb.commit_in_place_alter_table");

  /*
    IMPORTANT: When rollback is requested, mysql will abort with
    an assertion failure. That means every failed commit during inplace alter
    table will result in a fatal error on the server. Indexes ongoing creation
    will be detected when the server restarts, and dropped.

    For partitioned tables, a rollback call to this function (commit == false)
    is done for each partition.  A successful commit call only executes once
    for all partitions.
  */
  if (!commit) {
    /* If ctx has not been created yet, nothing to do here */
    if (!ctx0) {
      DBUG_RETURN(HA_EXIT_SUCCESS);
    }

    /*
      Cannot call destructor for Rdb_tbl_def directly because we don't want to
      erase the mappings inside the ddl_manager, as the old_key_descr is still
      using them.
    */
    if (ctx0->m_new_key_descr) {
      /* Delete the new key descriptors */
      for (uint i = 0; i < ctx0->m_new_tdef->m_key_count; i++) {
        ctx0->m_new_key_descr[i] = nullptr;
      }

      delete[] ctx0->m_new_key_descr;
      ctx0->m_new_key_descr = nullptr;
      ctx0->m_new_tdef->m_key_descr_arr = nullptr;

      delete ctx0->m_new_tdef;
    }

    {
      std::lock_guard<Rdb_dict_manager> dm_lock(dict_manager);
      /* Remove uncommitted key definitons from ddl_manager */
      ddl_manager.remove_uncommitted_keydefs(ctx0->m_added_indexes);

      std::unordered_set<GL_INDEX_ID> all_gl_index_ids;
      dict_manager.get_ongoing_create_indexes(&all_gl_index_ids);

      std::unordered_set<GL_INDEX_ID> gl_index_ids;
      for (auto index : ctx0->m_added_indexes) {
        auto gl_index_id = index->get_gl_index_id();
        if (all_gl_index_ids.find(gl_index_id) != all_gl_index_ids.end()) {
          gl_index_ids.insert(gl_index_id);
        }
      }

      if (!gl_index_ids.empty()) {
        /* Rollback any partially created indexes of this table */
        dict_manager.rollback_ongoing_index_creation(gl_index_ids);
      }
    }

    DBUG_RETURN(HA_EXIT_SUCCESS);
  }

  DBUG_ASSERT(ctx0);

  /*
    For partitioned tables, we need to commit all changes to all tables at
    once, unlike in the other inplace alter API methods.
  */
  inplace_alter_handler_ctx **ctx_array;
  inplace_alter_handler_ctx *ctx_single[2];

  if (ha_alter_info->group_commit_ctx) {
    DBUG_EXECUTE_IF("crash_during_index_creation_partition", DBUG_SUICIDE(););
    ctx_array = ha_alter_info->group_commit_ctx;
  } else {
    ctx_single[0] = ctx0;
    ctx_single[1] = nullptr;
    ctx_array = ctx_single;
  }

  DBUG_ASSERT(ctx0 == ctx_array[0]);
  ha_alter_info->group_commit_ctx = nullptr;

  if (ha_alter_info->handler_flags &
      (my_core::Alter_inplace_info::DROP_INDEX |
       my_core::Alter_inplace_info::DROP_UNIQUE_INDEX |
       my_core::Alter_inplace_info::ADD_INDEX |
       my_core::Alter_inplace_info::ADD_UNIQUE_INDEX)) {
    const std::unique_ptr<rocksdb::WriteBatch> wb = dict_manager.begin();
    rocksdb::WriteBatch *const batch = wb.get();
    std::unordered_set<GL_INDEX_ID> create_index_ids;

    m_tbl_def = ctx0->m_new_tdef;
    m_key_descr_arr = m_tbl_def->m_key_descr_arr;
    m_pk_descr = m_key_descr_arr[pk_index(altered_table, m_tbl_def)];

    DBUG_EXECUTE_IF("rocksdb_commit_alter_table", {
      const char act[] =
          "now signal ready_to_mark_cf_dropped_before_commit_alter_table "
          "wait_for mark_cf_dropped_done_before_commit_alter_table";
      DBUG_ASSERT(!debug_sync_set_action(ha_thd(), STRING_WITH_LEN(act)));
    });

    {
      std::lock_guard<Rdb_dict_manager> dm_lock(dict_manager);
      for (inplace_alter_handler_ctx **pctx = ctx_array; *pctx; pctx++) {
        Rdb_inplace_alter_ctx *const ctx =
            static_cast<Rdb_inplace_alter_ctx *>(*pctx);

        /* Mark indexes to be dropped */
        dict_manager.add_drop_index(ctx->m_dropped_index_ids, batch);

        for (const auto &index : ctx->m_added_indexes) {
          create_index_ids.insert(index->get_gl_index_id());
        }

        if (ddl_manager.put_and_write(ctx->m_new_tdef, batch)) {
          /*
            Failed to write new entry into data dictionary, this should never
            happen.
          */
          DBUG_ASSERT(0);
        }

        /*
          Remove uncommitted key definitons from ddl_manager, as they are now
          committed into the data dictionary.
        */
        ddl_manager.remove_uncommitted_keydefs(ctx->m_added_indexes);
      }

      if (dict_manager.commit(batch)) {
        /*
          Should never reach here. We assume MyRocks will abort if commit
          fails.
        */
        DBUG_ASSERT(0);
      }

      /* Mark ongoing create indexes as finished/remove from data dictionary */
      dict_manager.finish_indexes_operation(
          create_index_ids, Rdb_key_def::DDL_CREATE_INDEX_ONGOING);
    }

    DBUG_EXECUTE_IF("rocksdb_delete_index", {
      const char act[] =
          "now signal ready_to_mark_cf_dropped_after_commit_alter_table "
          "wait_for mark_cf_dropped_done_after_commit_alter_table";
      DBUG_ASSERT(!debug_sync_set_action(ha_thd(), STRING_WITH_LEN(act)));
    });

    rdb_drop_idx_thread.signal();

    if (rocksdb_table_stats_use_table_scan && !ctx0->m_added_indexes.empty()) {
      // If new indexes are created, add the table to the recalc queue
      // to calculate stats for new indexes
      rdb_is_thread.add_index_stats_request(m_tbl_def->full_tablename());
    }
  }

  if (ha_alter_info->handler_flags &
      (my_core::Alter_inplace_info::CHANGE_CREATE_OPTION)) {
    const std::unique_ptr<rocksdb::WriteBatch> wb = dict_manager.begin();
    rocksdb::WriteBatch *const batch = wb.get();
    std::unordered_set<GL_INDEX_ID> create_index_ids;

    ulonglong auto_incr_val = ha_alter_info->create_info->auto_increment_value;

    for (inplace_alter_handler_ctx **pctx = ctx_array; *pctx; pctx++) {
      Rdb_inplace_alter_ctx *const ctx =
          static_cast<Rdb_inplace_alter_ctx *>(*pctx);
      auto_incr_val = std::max(auto_incr_val, ctx->m_max_auto_incr);
      dict_manager.put_auto_incr_val(
          batch, ctx->m_new_tdef->get_autoincr_gl_index_id(), auto_incr_val,
          true /* overwrite */);
      ctx->m_new_tdef->m_auto_incr_val = auto_incr_val;
    }

    if (dict_manager.commit(batch)) {
      DBUG_ASSERT(0);
    }
  }

  DBUG_RETURN(HA_EXIT_SUCCESS);
}

#define SHOW_FNAME(name) rocksdb_show_##name

#define DEF_SHOW_FUNC(name, key)                                          \
  static int SHOW_FNAME(name)(MYSQL_THD thd MY_ATTRIBUTE((unused)),       \
                              SHOW_VAR * var,                             \
                              char *buff MY_ATTRIBUTE((unused))) {        \
    rocksdb_status_counters.name =                                        \
        rocksdb_stats->getTickerCount(rocksdb::key);                      \
    var->type = SHOW_LONGLONG;                                            \
    var->value = reinterpret_cast<char *>(&rocksdb_status_counters.name); \
    return HA_EXIT_SUCCESS;                                               \
  }

#define DEF_STATUS_VAR(name) \
  { "rocksdb_" #name, (char *)&SHOW_FNAME(name), SHOW_FUNC, SHOW_SCOPE_GLOBAL }

#define DEF_STATUS_VAR_PTR(name, ptr, option) \
  { "rocksdb_" name, (char *)ptr, option, SHOW_SCOPE_GLOBAL }

#define DEF_STATUS_VAR_FUNC(name, ptr, option) \
  { name, reinterpret_cast<char *>(ptr), option, SHOW_SCOPE_GLOBAL }

struct rocksdb_status_counters_t {
  uint64_t block_cache_miss;
  uint64_t block_cache_hit;
  uint64_t block_cache_add;
  uint64_t block_cache_add_failures;
  uint64_t block_cache_index_miss;
  uint64_t block_cache_index_hit;
  uint64_t block_cache_index_add;
  uint64_t block_cache_index_bytes_insert;
  uint64_t block_cache_index_bytes_evict;
  uint64_t block_cache_filter_miss;
  uint64_t block_cache_filter_hit;
  uint64_t block_cache_filter_add;
  uint64_t block_cache_filter_bytes_insert;
  uint64_t block_cache_filter_bytes_evict;
  uint64_t block_cache_bytes_read;
  uint64_t block_cache_bytes_write;
  uint64_t block_cache_data_bytes_insert;
  uint64_t block_cache_data_miss;
  uint64_t block_cache_data_hit;
  uint64_t block_cache_data_add;
  uint64_t bloom_filter_useful;
  uint64_t bloom_filter_full_positive;
  uint64_t bloom_filter_full_true_positive;
  uint64_t memtable_hit;
  uint64_t memtable_miss;
  uint64_t get_hit_l0;
  uint64_t get_hit_l1;
  uint64_t get_hit_l2_and_up;
  uint64_t compaction_key_drop_new;
  uint64_t compaction_key_drop_obsolete;
  uint64_t compaction_key_drop_user;
  uint64_t number_keys_written;
  uint64_t number_keys_read;
  uint64_t number_keys_updated;
  uint64_t bytes_written;
  uint64_t bytes_read;
  uint64_t number_db_seek;
  uint64_t number_db_seek_found;
  uint64_t number_db_next;
  uint64_t number_db_next_found;
  uint64_t number_db_prev;
  uint64_t number_db_prev_found;
  uint64_t iter_bytes_read;
  uint64_t no_file_closes;
  uint64_t no_file_opens;
  uint64_t no_file_errors;
  uint64_t stall_micros;
  uint64_t num_iterators;
  uint64_t number_multiget_get;
  uint64_t number_multiget_keys_read;
  uint64_t number_multiget_bytes_read;
  uint64_t number_deletes_filtered;
  uint64_t number_merge_failures;
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
DEF_SHOW_FUNC(block_cache_add_failures, BLOCK_CACHE_ADD_FAILURES)
DEF_SHOW_FUNC(block_cache_index_miss, BLOCK_CACHE_INDEX_MISS)
DEF_SHOW_FUNC(block_cache_index_hit, BLOCK_CACHE_INDEX_HIT)
DEF_SHOW_FUNC(block_cache_index_add, BLOCK_CACHE_INDEX_ADD)
DEF_SHOW_FUNC(block_cache_index_bytes_insert, BLOCK_CACHE_INDEX_BYTES_INSERT)
DEF_SHOW_FUNC(block_cache_index_bytes_evict, BLOCK_CACHE_INDEX_BYTES_EVICT)
DEF_SHOW_FUNC(block_cache_filter_miss, BLOCK_CACHE_FILTER_MISS)
DEF_SHOW_FUNC(block_cache_filter_hit, BLOCK_CACHE_FILTER_HIT)
DEF_SHOW_FUNC(block_cache_filter_add, BLOCK_CACHE_FILTER_ADD)
DEF_SHOW_FUNC(block_cache_filter_bytes_insert, BLOCK_CACHE_FILTER_BYTES_INSERT)
DEF_SHOW_FUNC(block_cache_filter_bytes_evict, BLOCK_CACHE_FILTER_BYTES_EVICT)
DEF_SHOW_FUNC(block_cache_bytes_read, BLOCK_CACHE_BYTES_READ)
DEF_SHOW_FUNC(block_cache_bytes_write, BLOCK_CACHE_BYTES_WRITE)
DEF_SHOW_FUNC(block_cache_data_bytes_insert, BLOCK_CACHE_DATA_BYTES_INSERT)
DEF_SHOW_FUNC(block_cache_data_miss, BLOCK_CACHE_DATA_MISS)
DEF_SHOW_FUNC(block_cache_data_hit, BLOCK_CACHE_DATA_HIT)
DEF_SHOW_FUNC(block_cache_data_add, BLOCK_CACHE_DATA_ADD)
DEF_SHOW_FUNC(bloom_filter_useful, BLOOM_FILTER_USEFUL)
DEF_SHOW_FUNC(bloom_filter_full_positive, BLOOM_FILTER_FULL_POSITIVE)
DEF_SHOW_FUNC(bloom_filter_full_true_positive, BLOOM_FILTER_FULL_TRUE_POSITIVE)
DEF_SHOW_FUNC(memtable_hit, MEMTABLE_HIT)
DEF_SHOW_FUNC(memtable_miss, MEMTABLE_MISS)
DEF_SHOW_FUNC(get_hit_l0, GET_HIT_L0)
DEF_SHOW_FUNC(get_hit_l1, GET_HIT_L1)
DEF_SHOW_FUNC(get_hit_l2_and_up, GET_HIT_L2_AND_UP)
DEF_SHOW_FUNC(compaction_key_drop_new, COMPACTION_KEY_DROP_NEWER_ENTRY)
DEF_SHOW_FUNC(compaction_key_drop_obsolete, COMPACTION_KEY_DROP_OBSOLETE)
DEF_SHOW_FUNC(compaction_key_drop_user, COMPACTION_KEY_DROP_USER)
DEF_SHOW_FUNC(number_keys_written, NUMBER_KEYS_WRITTEN)
DEF_SHOW_FUNC(number_keys_read, NUMBER_KEYS_READ)
DEF_SHOW_FUNC(number_keys_updated, NUMBER_KEYS_UPDATED)
DEF_SHOW_FUNC(bytes_written, BYTES_WRITTEN)
DEF_SHOW_FUNC(bytes_read, BYTES_READ)
DEF_SHOW_FUNC(number_db_seek, NUMBER_DB_SEEK)
DEF_SHOW_FUNC(number_db_seek_found, NUMBER_DB_SEEK_FOUND)
DEF_SHOW_FUNC(number_db_next, NUMBER_DB_NEXT)
DEF_SHOW_FUNC(number_db_next_found, NUMBER_DB_NEXT_FOUND)
DEF_SHOW_FUNC(number_db_prev, NUMBER_DB_PREV)
DEF_SHOW_FUNC(number_db_prev_found, NUMBER_DB_PREV_FOUND)
DEF_SHOW_FUNC(iter_bytes_read, ITER_BYTES_READ)
DEF_SHOW_FUNC(no_file_closes, NO_FILE_CLOSES)
DEF_SHOW_FUNC(no_file_opens, NO_FILE_OPENS)
DEF_SHOW_FUNC(no_file_errors, NO_FILE_ERRORS)
DEF_SHOW_FUNC(stall_micros, STALL_MICROS)
DEF_SHOW_FUNC(num_iterators, NO_ITERATORS)
DEF_SHOW_FUNC(number_multiget_get, NUMBER_MULTIGET_CALLS)
DEF_SHOW_FUNC(number_multiget_keys_read, NUMBER_MULTIGET_KEYS_READ)
DEF_SHOW_FUNC(number_multiget_bytes_read, NUMBER_MULTIGET_BYTES_READ)
DEF_SHOW_FUNC(number_deletes_filtered, NUMBER_FILTERED_DELETES)
DEF_SHOW_FUNC(number_merge_failures, NUMBER_MERGE_FAILURES)
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
  export_stats.rows_deleted_blind = global_stats.rows[ROWS_DELETED_BLIND];
  export_stats.rows_expired = global_stats.rows[ROWS_EXPIRED];
  export_stats.rows_filtered = global_stats.rows[ROWS_FILTERED];
  export_stats.rows_unfiltered_no_snapshot =
      global_stats.rows[ROWS_UNFILTERED_NO_SNAPSHOT];

  export_stats.system_rows_deleted = global_stats.system_rows[ROWS_DELETED];
  export_stats.system_rows_inserted = global_stats.system_rows[ROWS_INSERTED];
  export_stats.system_rows_read = global_stats.system_rows[ROWS_READ];
  export_stats.system_rows_updated = global_stats.system_rows[ROWS_UPDATED];

  export_stats.queries_point = global_stats.queries[QUERIES_POINT];
  export_stats.queries_range = global_stats.queries[QUERIES_RANGE];

  export_stats.table_index_stats_success =
      global_stats.table_index_stats_result[TABLE_INDEX_STATS_SUCCESS];
  export_stats.table_index_stats_failure =
      global_stats.table_index_stats_result[TABLE_INDEX_STATS_FAILURE];
  export_stats.table_index_stats_req_queue_length =
      rdb_is_thread.get_request_queue_size();

  export_stats.covered_secondary_key_lookups =
      global_stats.covered_secondary_key_lookups;
}

static void myrocks_update_memory_status() {
  std::vector<rocksdb::DB *> dbs;
  std::unordered_set<const rocksdb::Cache *> cache_set;
  dbs.push_back(rdb);
  std::map<rocksdb::MemoryUtil::UsageType, uint64_t> temp_usage_by_type;
  rocksdb::MemoryUtil::GetApproximateMemoryUsageByType(dbs, cache_set,
                                                       &temp_usage_by_type);
  memory_stats.memtable_total =
      temp_usage_by_type[rocksdb::MemoryUtil::kMemTableTotal];
  memory_stats.memtable_unflushed =
      temp_usage_by_type[rocksdb::MemoryUtil::kMemTableUnFlushed];
}

static SHOW_VAR myrocks_status_variables[] = {
    DEF_STATUS_VAR_FUNC("rows_deleted", &export_stats.rows_deleted,
                        SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("rows_inserted", &export_stats.rows_inserted,
                        SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("rows_read", &export_stats.rows_read, SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("rows_updated", &export_stats.rows_updated,
                        SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("rows_deleted_blind", &export_stats.rows_deleted_blind,
                        SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("rows_expired", &export_stats.rows_expired,
                        SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("rows_filtered", &export_stats.rows_filtered,
                        SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("rows_unfiltered_no_snapshot",
                        &export_stats.rows_unfiltered_no_snapshot,
                        SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("system_rows_deleted",
                        &export_stats.system_rows_deleted, SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("system_rows_inserted",
                        &export_stats.system_rows_inserted, SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("system_rows_read", &export_stats.system_rows_read,
                        SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("system_rows_updated",
                        &export_stats.system_rows_updated, SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("memtable_total", &memory_stats.memtable_total,
                        SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("memtable_unflushed", &memory_stats.memtable_unflushed,
                        SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("queries_point", &export_stats.queries_point,
                        SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("queries_range", &export_stats.queries_range,
                        SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("table_index_stats_success",
                        &export_stats.table_index_stats_success, SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("table_index_stats_failure",
                        &export_stats.table_index_stats_failure, SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("table_index_stats_req_queue_length",
                        &export_stats.table_index_stats_req_queue_length,
                        SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("covered_secondary_key_lookups",
                        &export_stats.covered_secondary_key_lookups,
                        SHOW_LONGLONG),

    {NullS, NullS, SHOW_LONG, SHOW_SCOPE_GLOBAL}};

static int show_myrocks_vars(THD *thd MY_ATTRIBUTE((unused)), SHOW_VAR *var,
                             char *buff MY_ATTRIBUTE((unused))) {
  myrocks_update_status();
  myrocks_update_memory_status();
  var->type = SHOW_ARRAY;
  var->value = reinterpret_cast<char *>(&myrocks_status_variables);
  return 0;
}

static ulonglong io_stall_prop_value(
    const std::map<std::string, std::string> &props, const std::string &key) {
  std::map<std::string, std::string>::const_iterator iter =
      props.find("io_stalls." + key);
  if (iter != props.end()) {
    return std::stoull(iter->second);
  } else {
    DBUG_PRINT("warning",
               ("RocksDB GetMapPropery hasn't returned key=%s", key.c_str()));
    DBUG_ASSERT(0);
    return 0;
  }
}

static void update_rocksdb_stall_status() {
  st_io_stall_stats local_io_stall_stats;
  for (const auto &cf_name : cf_manager.get_cf_names()) {
    std::shared_ptr<rocksdb::ColumnFamilyHandle> cfh =
        cf_manager.get_cf(cf_name);
    if (!cfh) {
      continue;
    }

    // Retrieve information from valid CF handle object. It is safe
    // even if the CF is removed from cf_manager at this point.
    std::map<std::string, std::string> props;
    if (!rdb->GetMapProperty(cfh.get(), "rocksdb.cfstats", &props)) {
      continue;
    }

    local_io_stall_stats.level0_slowdown +=
        io_stall_prop_value(props, "level0_slowdown");
    local_io_stall_stats.level0_slowdown_with_compaction +=
        io_stall_prop_value(props, "level0_slowdown_with_compaction");
    local_io_stall_stats.level0_numfiles +=
        io_stall_prop_value(props, "level0_numfiles");
    local_io_stall_stats.level0_numfiles_with_compaction +=
        io_stall_prop_value(props, "level0_numfiles_with_compaction");
    local_io_stall_stats.stop_for_pending_compaction_bytes +=
        io_stall_prop_value(props, "stop_for_pending_compaction_bytes");
    local_io_stall_stats.slowdown_for_pending_compaction_bytes +=
        io_stall_prop_value(props, "slowdown_for_pending_compaction_bytes");
    local_io_stall_stats.memtable_compaction +=
        io_stall_prop_value(props, "memtable_compaction");
    local_io_stall_stats.memtable_slowdown +=
        io_stall_prop_value(props, "memtable_slowdown");
    local_io_stall_stats.total_stop += io_stall_prop_value(props, "total_stop");
    local_io_stall_stats.total_slowdown +=
        io_stall_prop_value(props, "total_slowdown");
  }
  io_stall_stats = local_io_stall_stats;
}

static SHOW_VAR rocksdb_stall_status_variables[] = {
    DEF_STATUS_VAR_FUNC("l0_file_count_limit_slowdowns",
                        &io_stall_stats.level0_slowdown, SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("locked_l0_file_count_limit_slowdowns",
                        &io_stall_stats.level0_slowdown_with_compaction,
                        SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("l0_file_count_limit_stops",
                        &io_stall_stats.level0_numfiles, SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("locked_l0_file_count_limit_stops",
                        &io_stall_stats.level0_numfiles_with_compaction,
                        SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("pending_compaction_limit_stops",
                        &io_stall_stats.stop_for_pending_compaction_bytes,
                        SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("pending_compaction_limit_slowdowns",
                        &io_stall_stats.slowdown_for_pending_compaction_bytes,
                        SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("memtable_limit_stops",
                        &io_stall_stats.memtable_compaction, SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("memtable_limit_slowdowns",
                        &io_stall_stats.memtable_slowdown, SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("total_stops", &io_stall_stats.total_stop,
                        SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("total_slowdowns", &io_stall_stats.total_slowdown,
                        SHOW_LONGLONG),
    // end of the array marker
    {NullS, NullS, SHOW_LONG, SHOW_SCOPE_GLOBAL}};

static int show_rocksdb_stall_vars(THD *thd MY_ATTRIBUTE((unused)),
                                   SHOW_VAR *var,
                                   char *buff MY_ATTRIBUTE((unused))) {
  update_rocksdb_stall_status();
  var->type = SHOW_ARRAY;
  var->value = reinterpret_cast<char *>(&rocksdb_stall_status_variables);
  return 0;
}

//
// Lock Tree Status variables
//
static longlong rocksdb_locktree_escalation_count=1234;
static longlong rocksdb_locktree_current_lock_memory=0;

static SHOW_VAR rocksdb_locktree_status_variables[] = {
    DEF_STATUS_VAR_FUNC("escalation_count",
                        &rocksdb_locktree_escalation_count, SHOW_LONGLONG),
    DEF_STATUS_VAR_FUNC("current_lock_memory",
                        &rocksdb_locktree_current_lock_memory, SHOW_LONGLONG),
    // end of the array marker
    {NullS, NullS, SHOW_LONG, SHOW_SCOPE_GLOBAL}};

static SHOW_VAR rocksdb_empty_status_variables[] = {
    {NullS, NullS, SHOW_LONG, SHOW_SCOPE_GLOBAL}};

static void show_rocksdb_locktree_vars(THD*, SHOW_VAR *var, char*) {
  var->type = SHOW_ARRAY;
  if (range_lock_mgr)
  {
    auto status = range_lock_mgr->GetStatus();
    rocksdb_locktree_escalation_count = status.escalation_count;
    rocksdb_locktree_current_lock_memory = status.current_lock_memory;
    var->value = reinterpret_cast<char *>(&rocksdb_locktree_status_variables);
  }
  else
    var->value = reinterpret_cast<char *>(&rocksdb_empty_status_variables);
}

static SHOW_VAR rocksdb_status_vars[] = {
    DEF_STATUS_VAR(block_cache_miss),
    DEF_STATUS_VAR(block_cache_hit),
    DEF_STATUS_VAR(block_cache_add),
    DEF_STATUS_VAR(block_cache_add_failures),
    DEF_STATUS_VAR(block_cache_index_miss),
    DEF_STATUS_VAR(block_cache_index_hit),
    DEF_STATUS_VAR(block_cache_index_add),
    DEF_STATUS_VAR(block_cache_index_bytes_insert),
    DEF_STATUS_VAR(block_cache_index_bytes_evict),
    DEF_STATUS_VAR(block_cache_filter_miss),
    DEF_STATUS_VAR(block_cache_filter_hit),
    DEF_STATUS_VAR(block_cache_filter_add),
    DEF_STATUS_VAR(block_cache_filter_bytes_insert),
    DEF_STATUS_VAR(block_cache_filter_bytes_evict),
    DEF_STATUS_VAR(block_cache_bytes_read),
    DEF_STATUS_VAR(block_cache_bytes_write),
    DEF_STATUS_VAR(block_cache_data_bytes_insert),
    DEF_STATUS_VAR(block_cache_data_miss),
    DEF_STATUS_VAR(block_cache_data_hit),
    DEF_STATUS_VAR(block_cache_data_add),
    DEF_STATUS_VAR(bloom_filter_useful),
    DEF_STATUS_VAR(bloom_filter_full_positive),
    DEF_STATUS_VAR(bloom_filter_full_true_positive),
    DEF_STATUS_VAR(memtable_hit),
    DEF_STATUS_VAR(memtable_miss),
    DEF_STATUS_VAR(get_hit_l0),
    DEF_STATUS_VAR(get_hit_l1),
    DEF_STATUS_VAR(get_hit_l2_and_up),
    DEF_STATUS_VAR(compaction_key_drop_new),
    DEF_STATUS_VAR(compaction_key_drop_obsolete),
    DEF_STATUS_VAR(compaction_key_drop_user),
    DEF_STATUS_VAR(number_keys_written),
    DEF_STATUS_VAR(number_keys_read),
    DEF_STATUS_VAR(number_keys_updated),
    DEF_STATUS_VAR(bytes_written),
    DEF_STATUS_VAR(bytes_read),
    DEF_STATUS_VAR(number_db_seek),
    DEF_STATUS_VAR(number_db_seek_found),
    DEF_STATUS_VAR(number_db_next),
    DEF_STATUS_VAR(number_db_next_found),
    DEF_STATUS_VAR(number_db_prev),
    DEF_STATUS_VAR(number_db_prev_found),
    DEF_STATUS_VAR(iter_bytes_read),
    DEF_STATUS_VAR(no_file_closes),
    DEF_STATUS_VAR(no_file_opens),
    DEF_STATUS_VAR(no_file_errors),
    DEF_STATUS_VAR(stall_micros),
    DEF_STATUS_VAR(num_iterators),
    DEF_STATUS_VAR(number_multiget_get),
    DEF_STATUS_VAR(number_multiget_keys_read),
    DEF_STATUS_VAR(number_multiget_bytes_read),
    DEF_STATUS_VAR(number_deletes_filtered),
    DEF_STATUS_VAR(number_merge_failures),
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
    DEF_STATUS_VAR_PTR("row_lock_deadlocks", &rocksdb_row_lock_deadlocks,
                       SHOW_LONGLONG),
    DEF_STATUS_VAR_PTR("row_lock_wait_timeouts",
                       &rocksdb_row_lock_wait_timeouts, SHOW_LONGLONG),
    DEF_STATUS_VAR_PTR("snapshot_conflict_errors",
                       &rocksdb_snapshot_conflict_errors, SHOW_LONGLONG),
    DEF_STATUS_VAR_PTR("wal_group_syncs", &rocksdb_wal_group_syncs,
                       SHOW_LONGLONG),
    DEF_STATUS_VAR_PTR("manual_compactions_processed",
                       &rocksdb_manual_compactions_processed, SHOW_LONGLONG),
    DEF_STATUS_VAR_PTR("manual_compactions_cancelled",
                       &rocksdb_manual_compactions_cancelled, SHOW_LONGLONG),
    DEF_STATUS_VAR_PTR("manual_compactions_running",
                       &rocksdb_manual_compactions_running, SHOW_LONGLONG),
    DEF_STATUS_VAR_PTR("manual_compactions_pending",
                       &rocksdb_manual_compactions_pending, SHOW_LONGLONG),
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
    DEF_STATUS_VAR_PTR("additional_compaction_triggers",
                       &rocksdb_additional_compaction_triggers, SHOW_LONGLONG),
#ifndef DBUG_OFF
    DEF_STATUS_VAR_PTR("num_get_for_update_calls",
                       &rocksdb_num_get_for_update_calls, SHOW_LONGLONG),
#endif
    DEF_STATUS_VAR_PTR("select_bypass_executed",
                       &rocksdb_select_bypass_executed, SHOW_LONGLONG),
    DEF_STATUS_VAR_PTR("select_bypass_rejected",
                       &rocksdb_select_bypass_rejected, SHOW_LONGLONG),
    DEF_STATUS_VAR_PTR("select_bypass_failed", &rocksdb_select_bypass_failed,
                       SHOW_LONGLONG),

    DEF_STATUS_VAR_PTR("partial_index_groups_sorted",
                       &rocksdb_partial_index_groups_sorted, SHOW_LONGLONG),
    DEF_STATUS_VAR_PTR("partial_index_groups_materialized",
                       &rocksdb_partial_index_groups_materialized,
                       SHOW_LONGLONG),
    DEF_STATUS_VAR_PTR("partial_index_rows_sorted",
                       &rocksdb_partial_index_rows_sorted, SHOW_LONGLONG),
    DEF_STATUS_VAR_PTR("partial_index_rows_materialized",
                       &rocksdb_partial_index_rows_materialized, SHOW_LONGLONG),

    // the variables generated by SHOW_FUNC are sorted only by prefix (first
    // arg in the tuple below), so make sure it is unique to make sorting
    // deterministic as quick sort is not stable
    {"rocksdb", reinterpret_cast<char *>(&show_myrocks_vars), SHOW_FUNC,
     SHOW_SCOPE_GLOBAL},
    {"rocksdb_stall", reinterpret_cast<char *>(&show_rocksdb_stall_vars),
     SHOW_FUNC, SHOW_SCOPE_GLOBAL},
    {"rocksdb_locktree", reinterpret_cast<char *>(show_rocksdb_locktree_vars),
     SHOW_FUNC, SHOW_SCOPE_GLOBAL},
    {NullS, NullS, SHOW_LONG, SHOW_SCOPE_GLOBAL}};

/*
  Background thread's main logic
*/

void Rdb_background_thread::run() {
  // How many seconds to wait till flushing the WAL next time.
  const int WAKE_UP_INTERVAL = 1;

  timespec ts_next_sync;
  clock_gettime(CLOCK_REALTIME, &ts_next_sync);
  ts_next_sync.tv_sec += WAKE_UP_INTERVAL;

  for (;;) {
    // Wait until the next timeout or until we receive a signal to stop the
    // thread. Request to stop the thread should only be triggered when the
    // storage engine is being unloaded.
    RDB_MUTEX_LOCK_CHECK(m_signal_mutex);
    const auto ret MY_ATTRIBUTE((__unused__)) =
        mysql_cond_timedwait(&m_signal_cond, &m_signal_mutex, &ts_next_sync);

    // Check that we receive only the expected error codes.
    DBUG_ASSERT(ret == 0 || ret == ETIMEDOUT);
    const std::atomic<THD::killed_state> local_killed(m_killed.load());
    const bool local_save_stats = m_save_stats;
    reset();
    RDB_MUTEX_UNLOCK_CHECK(m_signal_mutex);

    if (local_killed) {
      // If we're here then that's because condition variable was signaled by
      // another thread and we're shutting down. Break out the loop to make
      // sure that shutdown thread can proceed.
      break;
    }

    // This path should be taken only when the timer expired.
    DBUG_ASSERT(ret == ETIMEDOUT);

    if (local_save_stats) {
      ddl_manager.persist_stats();
    }

    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    // Flush the WAL. Sync it for both background and never modes to copy
    // InnoDB's behavior. For mode never, the wal file isn't even written,
    // whereas background writes to the wal file, but issues the syncs in a
    // background thread.
    if (rdb && (rocksdb_flush_log_at_trx_commit != FLUSH_LOG_SYNC) &&
        !rocksdb_db_options->allow_mmap_writes) {
      bool sync = rdb_sync_wal_supported();
      const rocksdb::Status s = rdb->FlushWAL(sync);
      if (!s.ok()) {
        rdb_handle_io_error(s, RDB_IO_ERROR_BG_THREAD);
      }
    }

    // Recalculate statistics for indexes only if
    // rocksdb_table_stats_use_table_scan is disabled.
    //  Otherwise, Rdb_index_stats_thread will do the work
    if (!rocksdb_table_stats_use_table_scan && rocksdb_stats_recalc_rate) {
      std::vector<std::string> to_recalc;
      if (rdb_tables_to_recalc.empty()) {
        struct Rdb_index_collector : public Rdb_tables_scanner {
          int add_table(Rdb_tbl_def *tdef) override {
            rdb_tables_to_recalc.push_back(tdef->full_tablename());
            return HA_EXIT_SUCCESS;
          }
        } collector;
        ddl_manager.scan_for_tables(&collector);
      }

      while (to_recalc.size() < rocksdb_stats_recalc_rate &&
             !rdb_tables_to_recalc.empty()) {
        to_recalc.push_back(rdb_tables_to_recalc.back());
        rdb_tables_to_recalc.pop_back();
      }

      for (const auto &tbl_name : to_recalc) {
        calculate_stats_for_table(tbl_name, SCAN_TYPE_NONE);
      }
    }

    // Set the next timestamp for mysql_cond_timedwait() (which ends up calling
    // pthread_cond_timedwait()) to wait on.
    ts_next_sync.tv_sec = ts.tv_sec + WAKE_UP_INTERVAL;
  }

  // save remaining stats which might've left unsaved
  ddl_manager.persist_stats();
}

void Rdb_index_stats_thread::run() {
  const int WAKE_UP_INTERVAL = 1;
#ifndef _WIN32
  RDB_MUTEX_LOCK_CHECK(m_is_mutex);
  m_tid_set = true;
  m_tid = syscall(SYS_gettid);
  RDB_MUTEX_UNLOCK_CHECK(m_is_mutex);
#endif

  renice(rocksdb_table_stats_background_thread_nice_value);
  for (;;) {
    RDB_MUTEX_LOCK_CHECK(m_signal_mutex);
    if (m_killed) {
      RDB_MUTEX_UNLOCK_CHECK(m_signal_mutex);
      break;
    }

    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    // Wait for 24 hours if the table scan based index calculation
    // is off. When the switch is turned on and any request is added
    // to the recalc queue, this thread will be signaled.
    ts.tv_sec +=
        (rocksdb_table_stats_use_table_scan) ? WAKE_UP_INTERVAL : 24 * 60 * 60;

    const auto ret MY_ATTRIBUTE((__unused__)) =
        mysql_cond_timedwait(&m_signal_cond, &m_signal_mutex, &ts);

    if (m_killed) {
      RDB_MUTEX_UNLOCK_CHECK(m_signal_mutex);
      break;
    }

    // Make sure, no program error is returned
    DBUG_ASSERT(ret == 0 || ret == ETIMEDOUT);
    RDB_MUTEX_UNLOCK_CHECK(m_signal_mutex);

    for (;;) {
      if (!rocksdb_table_stats_use_table_scan) {
        // Clear the recalc queue
        clear_all_index_stats_requests();
        break;
      }

      std::string tbl_name;
      if (!get_index_stats_request(&tbl_name)) {
        // No request in the recalc queue
        break;
      }

      Rdb_table_stats tbl_stats;
      if (ddl_manager.find_table_stats(tbl_name, &tbl_stats) !=
          HA_EXIT_SUCCESS) {
        // The table has been dropped. Skip this table.
        continue;
      }

      clock_gettime(CLOCK_REALTIME, &ts);
      if (difftime(ts.tv_sec, tbl_stats.m_last_recalc) <
          RDB_MIN_RECALC_INTERVAL) {
        /* Stats were (re)calculated not long ago. To avoid
        too frequent stats updates we put back the table on
        the recalc queue and do nothing. */

        add_index_stats_request(tbl_name);
        break;
      }

      DBUG_EXECUTE_IF("rocksdb_is_bg_thread", {
        if (tbl_name == "test.t") {
          THD *thd = new THD();
          thd->thread_stack = reinterpret_cast<char *>(&thd);
          thd->store_globals();

          const char act[] = "now wait_for ready_to_calculate_index_stats";
          DBUG_ASSERT(!debug_sync_set_action(thd, STRING_WITH_LEN(act)));

          thd->restore_globals();
          delete thd;
        }
      });

      int err =
          calculate_stats_for_table(tbl_name, SCAN_TYPE_FULL_TABLE, &m_killed);

      if (err != HA_EXIT_SUCCESS) {
        global_stats.table_index_stats_result[TABLE_INDEX_STATS_FAILURE].inc();
        break;
      }

      global_stats.table_index_stats_result[TABLE_INDEX_STATS_SUCCESS].inc();

      DBUG_EXECUTE_IF("rocksdb_is_bg_thread", {
        if (tbl_name == "test.t") {
          THD *thd = new THD();
          thd->thread_stack = reinterpret_cast<char *>(&thd);
          thd->store_globals();

          const char act[] = "now signal index_stats_calculation_done";
          DBUG_ASSERT(!debug_sync_set_action(thd, STRING_WITH_LEN(act)));

          thd->restore_globals();
          delete thd;
        }
      });
    }
  }

  RDB_MUTEX_LOCK_CHECK(m_is_mutex);
  m_tid_set = false;
  m_tid = 0;
  RDB_MUTEX_UNLOCK_CHECK(m_is_mutex);
}

bool Rdb_index_stats_thread::get_index_stats_request(std::string *tbl_name) {
  RDB_MUTEX_LOCK_CHECK(m_is_mutex);
  if (m_requests.empty()) {
    RDB_MUTEX_UNLOCK_CHECK(m_is_mutex);
    return false;
  }

  *tbl_name = m_requests[0];
  m_requests.pop_front();

  auto count = m_tbl_names.erase(*tbl_name);
  if (count != 1) {
    DBUG_ASSERT(0);
  }

  RDB_MUTEX_UNLOCK_CHECK(m_is_mutex);
  return true;
}

void Rdb_index_stats_thread::add_index_stats_request(
    const std::string &tbl_name) {
  RDB_MUTEX_LOCK_CHECK(m_is_mutex);

  /* Quit if already in the queue */
  auto ret = m_tbl_names.insert(tbl_name);
  if (!ret.second) {
    RDB_MUTEX_UNLOCK_CHECK(m_is_mutex);
    return;
  }

  m_requests.push_back(*ret.first);
  RDB_MUTEX_UNLOCK_CHECK(m_is_mutex);
  signal();
}

void Rdb_index_stats_thread::clear_all_index_stats_requests() {
  RDB_MUTEX_LOCK_CHECK(m_is_mutex);
  m_requests.clear();
  m_tbl_names.clear();
  RDB_MUTEX_UNLOCK_CHECK(m_is_mutex);
}

int Rdb_index_stats_thread::renice(int nice_val) {
  RDB_MUTEX_LOCK_CHECK(m_is_mutex);
  if (!m_tid_set) {
    RDB_MUTEX_UNLOCK_CHECK(m_is_mutex);
    return HA_EXIT_FAILURE;
  }

#ifndef _WIN32
  int ret = setpriority(PRIO_PROCESS, m_tid, nice_val);
  if (ret != 0) {
    // NO_LINT_DEBUG
    sql_print_error("Set index stats thread priority failed due to %s",
                    strerror(errno));
    RDB_MUTEX_UNLOCK_CHECK(m_is_mutex);
    return HA_EXIT_FAILURE;
  }
#endif

  RDB_MUTEX_UNLOCK_CHECK(m_is_mutex);
  return HA_EXIT_SUCCESS;
}

size_t Rdb_index_stats_thread::get_request_queue_size() {
  size_t len = 0;
  RDB_MUTEX_LOCK_CHECK(m_is_mutex);
  len = m_requests.size();
  RDB_MUTEX_UNLOCK_CHECK(m_is_mutex);

  return len;
}

/*
  A background thread to handle manual compactions,
  except for dropping indexes/tables. Every second, it checks
  pending manual compactions, and it calls CompactRange if there is.
*/
void Rdb_manual_compaction_thread::run() {
  RDB_MUTEX_LOCK_CHECK(m_signal_mutex);
  for (;;) {
    if (m_killed) {
      break;
    }
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1;

    const auto ret MY_ATTRIBUTE((__unused__)) =
        mysql_cond_timedwait(&m_signal_cond, &m_signal_mutex, &ts);
    if (m_killed) {
      break;
    }
    // make sure, no program error is returned
    DBUG_ASSERT(ret == 0 || ret == ETIMEDOUT);
    RDB_MUTEX_UNLOCK_CHECK(m_signal_mutex);

    RDB_MUTEX_LOCK_CHECK(m_mc_mutex);
    // Grab the first PENDING state item and proceed, if not empty.
    if (m_requests.empty()) {
      RDB_MUTEX_UNLOCK_CHECK(m_mc_mutex);
      RDB_MUTEX_LOCK_CHECK(m_signal_mutex);
      continue;
    }
    auto it = m_requests.begin();
    auto pending_it = m_requests.end();
    // Remove all items with client_done. client_done means
    // caller no longer uses the mcr object so it is safe to erase.
    // Pick first PENDING state item
    it = m_requests.begin();
    while (it != m_requests.end()) {
      if (it->second.client_done) {
        // If state is PENDING, decrement counter
        if (it->second.state == Manual_compaction_request::PENDING) {
          rocksdb_manual_compactions_pending--;
        }
        m_requests.erase(it++);
      } else if (it->second.state == Manual_compaction_request::PENDING &&
                 pending_it == m_requests.end()) {
        // found
        pending_it = it;
        it++;
      } else {
        it++;
      }
    }
    if (pending_it == m_requests.end()) {
      RDB_MUTEX_UNLOCK_CHECK(m_mc_mutex);
      RDB_MUTEX_LOCK_CHECK(m_signal_mutex);
      continue;
    }

    Manual_compaction_request &mcr = pending_it->second;
    DBUG_ASSERT(mcr.cf);
    DBUG_ASSERT(mcr.state == Manual_compaction_request::PENDING);
    mcr.state = Manual_compaction_request::RUNNING;
    rocksdb_manual_compactions_running++;
    rocksdb_manual_compactions_pending--;
    RDB_MUTEX_UNLOCK_CHECK(m_mc_mutex);

    DBUG_ASSERT(mcr.state == Manual_compaction_request::RUNNING);
    // NO_LINT_DEBUG
    sql_print_information("Manual Compaction id %d cf %s started.", mcr.mc_id,
                          mcr.cf->GetName().c_str());
    if (rocksdb_debug_manual_compaction_delay > 0) {
      uint32 delay = rocksdb_debug_manual_compaction_delay;
      for (uint32 i = 0; i < delay; ++i) {
        // Sleep in 1s increments so that shutdown/abort can happen properly
        my_sleep(1000000);
        if (m_killed) {
          // Let it run through to the end to simplify abort processing
          break;
        }
      }
    }

    DBUG_EXECUTE_IF("rocksdb_manual_compaction", {
      THD *thd = new THD();
      thd->thread_stack = reinterpret_cast<char *>(&(thd));
      thd->store_globals();
      const char act[] =
          "now signal ready_to_mark_cf_dropped_in_manual_compaction wait_for "
          "mark_cf_dropped_done_in_manual_compaction";
      DBUG_ASSERT(!debug_sync_set_action(thd, STRING_WITH_LEN(act)));
      thd->restore_globals();
      delete thd;
    });

    // CompactRange may take a very long time. On clean shutdown,
    // it is cancelled by CancelAllBackgroundWork, then status is
    // set to shutdownInProgress.
    const rocksdb::Status s =
        rdb->CompactRange(mcr.option, mcr.cf.get(), mcr.start, mcr.limit);

    rocksdb_manual_compactions_running--;
    if (s.ok()) {
      rocksdb_manual_compactions_processed++;
      // NO_LINT_DEBUG
      sql_print_information("Manual Compaction id %d cf %s ended.", mcr.mc_id,
                            mcr.cf->GetName().c_str());
      set_state(&mcr, Manual_compaction_request::SUCCESS);
    } else {
      if (!cf_manager.get_cf(mcr.cf->GetID())) {
        // NO_LINT_DEBUG
        sql_print_information("cf %s has been dropped",
                              mcr.cf->GetName().c_str());
        set_state(&mcr, Manual_compaction_request::SUCCESS);
      } else if (s.IsIncomplete()) {
        // NO_LINT_DEBUG
        sql_print_information(
            "Manual Compaction id %d cf %s cancelled. (%d:%d, %s)", mcr.mc_id,
            mcr.cf->GetName().c_str(), s.code(), s.subcode(), s.getState());
        // Cancelled
        set_state(&mcr, Manual_compaction_request::CANCEL);
        rocksdb_manual_compactions_cancelled++;
      } else {
        // NO_LINT_DEBUG
        sql_print_information(
            "Manual Compaction id %d cf %s aborted. (%d:%d, %s)", mcr.mc_id,
            mcr.cf->GetName().c_str(), s.code(), s.subcode(), s.getState());
        set_state(&mcr, Manual_compaction_request::FAILURE);
        if (!s.IsShutdownInProgress()) {
          rdb_handle_io_error(s, RDB_IO_ERROR_BG_THREAD);
        } else {
          DBUG_ASSERT(m_requests.size() == 1);
        }
      }
    }
    RDB_MUTEX_LOCK_CHECK(m_signal_mutex);
  }
  clear_all_manual_compaction_requests();
  DBUG_ASSERT(m_requests.empty());
  RDB_MUTEX_UNLOCK_CHECK(m_signal_mutex);
}

void Rdb_manual_compaction_thread::clear_all_manual_compaction_requests() {
  RDB_MUTEX_LOCK_CHECK(m_mc_mutex);
  DBUG_ASSERT(rocksdb_manual_compactions_pending == 0);
  m_requests.clear();
  RDB_MUTEX_UNLOCK_CHECK(m_mc_mutex);
}

void Rdb_manual_compaction_thread::
    cancel_all_pending_manual_compaction_requests() {
  RDB_MUTEX_LOCK_CHECK(m_mc_mutex);
  auto it = m_requests.begin();
  while (it != m_requests.end()) {
    Manual_compaction_request &mcr = it->second;
    if (mcr.state == Manual_compaction_request::PENDING) {
      mcr.state = Manual_compaction_request::CANCEL;
      rocksdb_manual_compactions_cancelled++;
      rocksdb_manual_compactions_pending--;
    }
    it++;
  }
  DBUG_ASSERT(rocksdb_manual_compactions_pending == 0);
  RDB_MUTEX_UNLOCK_CHECK(m_mc_mutex);
}

/**
 *  Requesting to cancel a Manual Compaction job with mc_id.
 *  Only PENDING or RUNNING states need cancellation.
 *  This function may take a while if state is RUNNING.
 *  Returning true if hitting timeout and state is RUNNING.
 */
bool Rdb_manual_compaction_thread::cancel_manual_compaction_request(
    const int mc_id, const int timeout_100ms) {
  Manual_compaction_request::mc_state state =
      Manual_compaction_request::PENDING;

  RDB_MUTEX_LOCK_CHECK(m_mc_mutex);
  auto it = m_requests.find(mc_id);
  if (it != m_requests.end()) {
    Manual_compaction_request &mcr = it->second;
    if (mcr.state == Manual_compaction_request::PENDING) {
      mcr.state = Manual_compaction_request::CANCEL;
      rocksdb_manual_compactions_cancelled++;
      rocksdb_manual_compactions_pending--;
      RDB_MUTEX_UNLOCK_CHECK(m_mc_mutex);
      return false;
    } else if (mcr.state == Manual_compaction_request::RUNNING) {
      // explicitly requesting to cancel compaction (cancellation happens in
      // background may take time)
      mcr.option.canceled->store(true, std::memory_order_release);
      state = mcr.state;
    }
  }
  RDB_MUTEX_UNLOCK_CHECK(m_mc_mutex);

  // Waiting to get manual compaction to get cancelled.
  // Even if returning timeouts to clients, manual compaction
  // is still running so further compactions can remain
  // in pending state until the compaction completes cancellation.
  uint64_t retry = timeout_100ms;
  while (retry > 0 && state == Manual_compaction_request::RUNNING) {
    my_sleep(100000);
    retry--;
    state = manual_compaction_state(mc_id);
  }

  return retry <= 0 && state == Manual_compaction_request::RUNNING;
}

/**
 * This function is for clients to request for Manual Compaction.
 * This function adds mcr (Manual Compaction Request) in a queue
 * as PENDING state then returns. Worker Thread then later picks it up
 * and processes compaction.
 * Clients should call set_client_done() when the clients are done with
 * the status of the requests.
 */
int Rdb_manual_compaction_thread::request_manual_compaction(
    std::shared_ptr<rocksdb::ColumnFamilyHandle> cf, rocksdb::Slice *start,
    rocksdb::Slice *limit, const uint manual_compaction_threads,
    const rocksdb::BottommostLevelCompaction bottommost_level_compaction) {
  int mc_id = -1;
  RDB_MUTEX_LOCK_CHECK(m_mc_mutex);
  if (m_requests.size() >= rocksdb_max_manual_compactions) {
    RDB_MUTEX_UNLOCK_CHECK(m_mc_mutex);
    return mc_id;
  }
  Manual_compaction_request mcr;
  mc_id = mcr.mc_id = ++m_latest_mc_id;
  mcr.state = Manual_compaction_request::PENDING;
  mcr.cf = cf;
  mcr.start = start;
  mcr.limit = limit;
  mcr.option = getCompactRangeOptions(manual_compaction_threads,
                                      bottommost_level_compaction);
  mcr.canceled =
      std::shared_ptr<std::atomic<bool>>(new std::atomic<bool>(false));
  mcr.option.canceled = mcr.canceled.get();
  mcr.client_done = false;
  rocksdb_manual_compactions_pending++;
  m_requests.insert(std::make_pair(mcr.mc_id, mcr));
  RDB_MUTEX_UNLOCK_CHECK(m_mc_mutex);
  return mc_id;
}

Rdb_manual_compaction_thread::Manual_compaction_request::mc_state
Rdb_manual_compaction_thread::manual_compaction_state(const int mc_id) {
  Manual_compaction_request::mc_state state =
      Manual_compaction_request::SUCCESS;
  RDB_MUTEX_LOCK_CHECK(m_mc_mutex);
  auto it = m_requests.find(mc_id);
  if (it != m_requests.end()) {
    state = it->second.state;
  }
  RDB_MUTEX_UNLOCK_CHECK(m_mc_mutex);
  return state;
}

void Rdb_manual_compaction_thread::set_state(
    Manual_compaction_request *mcr,
    const Manual_compaction_request::mc_state new_state) {
  RDB_MUTEX_LOCK_CHECK(m_mc_mutex);
  mcr->state = new_state;
  RDB_MUTEX_UNLOCK_CHECK(m_mc_mutex);
}

bool Rdb_manual_compaction_thread::set_client_done(const int mc_id) {
  bool rc = false;
  RDB_MUTEX_LOCK_CHECK(m_mc_mutex);
  auto it = m_requests.find(mc_id);
  if (it != m_requests.end()) {
    Manual_compaction_request &mcr = it->second;
    mcr.client_done = true;
    rc = true;
  }
  RDB_MUTEX_UNLOCK_CHECK(m_mc_mutex);
  return rc;
}

/**
 * Locking read + Not Found + Read Committed occurs if we accessed
 * a row by Seek, tried to lock it, failed, released and reacquired the
 * snapshot (because of READ COMMITTED mode) and the row was deleted by
 * someone else in the meantime.
 * If so, we either just skipping the row, or re-creating a snapshot
 * and seek again. In both cases, Read Committed constraint is not broken.
 */
bool ha_rocksdb::should_skip_invalidated_record(const int rc) {
  if ((m_lock_rows != RDB_LOCK_NONE && rc == HA_ERR_KEY_NOT_FOUND &&
       my_core::thd_tx_isolation(ha_thd()) == ISO_READ_COMMITTED)) {
    return true;
  }
  return false;
}

bool ha_rocksdb::should_skip_locked_record(const int rc) {
  if (m_locked_row_action == THR_SKIP) {
    /*
      In the spirit of SKIP LOCKED, Deadlock and Busy are also skipped as the
      goal is to skip conflicting rows and only lock the rows that can be
      updated later
     */
    if (rc == HA_ERR_LOCK_WAIT_TIMEOUT || rc == HA_ERR_LOCK_DEADLOCK ||
        HA_ERR_ROCKSDB_STATUS_BUSY) {
      return true;
    }
  }
  return false;
}

/**
 * Indicating snapshot needs to be re-created and retrying seek again,
 * instead of returning errors or empty set. This is normally applicable
 * when hitting kBusy when locking the first row of the transaction,
 * with Repeatable Read isolation level.
 */
bool ha_rocksdb::should_recreate_snapshot(const int rc,
                                          const bool is_new_snapshot) {
  if (should_skip_invalidated_record(rc) ||
      (rc == HA_ERR_ROCKSDB_STATUS_BUSY && is_new_snapshot)) {
    return true;
  }
  return false;
}

/**
 * If calling put/delete/singledelete without locking the row,
 * it is necessary to pass assume_tracked=false to RocksDB TX API.
 * Read Free Replication and Blind Deletes are the cases when
 * using TX API and skipping row locking.
 */
bool ha_rocksdb::can_assume_tracked(THD *thd) {
  if (use_read_free_rpl() || (THDVAR(thd, blind_delete_primary_key))) {
    return false;
  }
  return true;
}

bool ha_rocksdb::check_bloom_and_set_bounds(
    THD *thd, const Rdb_key_def &kd, const rocksdb::Slice &eq_cond,
    size_t bound_len, uchar *const lower_bound, uchar *const upper_bound,
    rocksdb::Slice *lower_bound_slice, rocksdb::Slice *upper_bound_slice) {
  bool can_use_bloom = can_use_bloom_filter(thd, kd, eq_cond);
  if (!can_use_bloom && (THDVAR(thd, enable_iterate_bounds))) {
    setup_iterator_bounds(kd, eq_cond, bound_len, lower_bound, upper_bound,
                          lower_bound_slice, upper_bound_slice);
  }
  return can_use_bloom;
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
*/
bool ha_rocksdb::can_use_bloom_filter(THD *thd, const Rdb_key_def &kd,
                                      const rocksdb::Slice &eq_cond) {
  bool can_use = false;

  if (THDVAR(thd, skip_bloom_filter_on_read)) {
    return can_use;
  }

  const rocksdb::SliceTransform *prefix_extractor = kd.get_extractor();
  if (prefix_extractor) {
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
    if (prefix_extractor->SameResultWhenAppended(eq_cond)) {
      can_use = true;
    } else {
      can_use = false;
    }
  }

  return can_use;
}

/* For modules that need access to the global data structures */
rocksdb::TransactionDB *rdb_get_rocksdb_db() { return rdb; }

Rdb_cf_manager &rdb_get_cf_manager() { return cf_manager; }

const rocksdb::BlockBasedTableOptions &rdb_get_table_options() {
  return *rocksdb_tbl_options;
}

bool rdb_is_table_scan_index_stats_calculation_enabled() {
  return rocksdb_table_stats_use_table_scan;
}
bool rdb_is_ttl_enabled() { return rocksdb_enable_ttl; }
bool rdb_is_ttl_read_filtering_enabled() {
  return rocksdb_enable_ttl_read_filtering;
}
#ifndef DBUG_OFF
int rdb_dbug_set_ttl_rec_ts() { return rocksdb_debug_ttl_rec_ts; }
int rdb_dbug_set_ttl_snapshot_ts() { return rocksdb_debug_ttl_snapshot_ts; }
int rdb_dbug_set_ttl_read_filter_ts() {
  return rocksdb_debug_ttl_read_filter_ts;
}
bool rdb_dbug_set_ttl_ignore_pk() { return rocksdb_debug_ttl_ignore_pk; }
#endif

void rdb_update_global_stats(const operation_type &type, uint count,
                             Rdb_tbl_def *td) {
  DBUG_ASSERT(type < ROWS_MAX);

  if (count == 0) {
    return;
  }

  if (td && td->m_is_mysql_system_table) {
    global_stats.system_rows[type].add(count);
  } else {
    global_stats.rows[type].add(count);
  }
}

int rdb_get_table_perf_counters(const char *const tablename,
                                Rdb_perf_counters *const counters) {
  DBUG_ASSERT(tablename != nullptr);

  Rdb_table_handler *table_handler;
  table_handler = rdb_open_tables.get_table_handler(tablename);
  if (table_handler == nullptr) {
    return HA_ERR_ROCKSDB_INVALID_TABLE;
  }

  counters->load(table_handler->m_table_perf_context);

  rdb_open_tables.release_table_handler(table_handler);
  return HA_EXIT_SUCCESS;
}

const char *get_rdb_io_error_string(const RDB_IO_ERROR_TYPE err_type) {
  // If this assertion fails then this means that a member has been either added
  // to or removed from RDB_IO_ERROR_TYPE enum and this function needs to be
  // changed to return the appropriate value.
  static_assert(RDB_IO_ERROR_LAST == 4, "Please handle all the error types.");

  switch (err_type) {
    case RDB_IO_ERROR_TYPE::RDB_IO_ERROR_TX_COMMIT:
      return "RDB_IO_ERROR_TX_COMMIT";
    case RDB_IO_ERROR_TYPE::RDB_IO_ERROR_DICT_COMMIT:
      return "RDB_IO_ERROR_DICT_COMMIT";
    case RDB_IO_ERROR_TYPE::RDB_IO_ERROR_BG_THREAD:
      return "RDB_IO_ERROR_BG_THREAD";
    case RDB_IO_ERROR_TYPE::RDB_IO_ERROR_GENERAL:
      return "RDB_IO_ERROR_GENERAL";
    default:
      DBUG_ASSERT(false);
      return "(unknown)";
  }
}

// In case of core dump generation we want this function NOT to be optimized
// so that we can capture as much data as possible to debug the root cause
// more efficiently.
void rdb_handle_io_error(const rocksdb::Status status,
                         const RDB_IO_ERROR_TYPE err_type) {
  if (status.IsIOError()) {
    /* skip dumping core if write failed and we are allowed to do so */
    if (skip_core_dump_on_error) {
      opt_core_file = false;
    }

    switch (err_type) {
      case RDB_IO_ERROR_TX_COMMIT:
      case RDB_IO_ERROR_DICT_COMMIT: {
        rdb_log_status_error(status, "failed to write to WAL");
        /* NO_LINT_DEBUG */
        sql_print_error("MyRocks: aborting on WAL write error.");
        abort();
        break;
      }
      case RDB_IO_ERROR_BG_THREAD: {
        rdb_log_status_error(status, "BG thread failed to write to RocksDB");
        /* NO_LINT_DEBUG */
        sql_print_error("MyRocks: aborting on BG write error.");
        abort();
        break;
      }
      case RDB_IO_ERROR_GENERAL: {
        rdb_log_status_error(status, "failed on I/O");
        /* NO_LINT_DEBUG */
        sql_print_error("MyRocks: aborting on I/O error.");
        abort();
        break;
      }
      default:
        DBUG_ASSERT(0);
        break;
    }
  } else if (status.IsCorruption()) {
    rdb_log_status_error(status, "data corruption detected!");
    rdb_persist_corruption_marker();
    /* NO_LINT_DEBUG */
    sql_print_error("MyRocks: aborting because of data corruption.");
    abort();
  } else if (!status.ok()) {
    switch (err_type) {
      case RDB_IO_ERROR_TX_COMMIT:
      case RDB_IO_ERROR_DICT_COMMIT: {
        rdb_log_status_error(status, "Failed to write to WAL (non kIOError)");
        /* NO_LINT_DEBUG */
        sql_print_error("MyRocks: aborting on WAL write error.");
        abort();
        break;
      }
      default:
        rdb_log_status_error(status, "Failed to read/write in RocksDB");
        break;
    }
  }
}

Rdb_dict_manager *rdb_get_dict_manager(void) { return &dict_manager; }

Rdb_ddl_manager *rdb_get_ddl_manager(void) { return &ddl_manager; }

Rdb_binlog_manager *rdb_get_binlog_manager(void) { return &binlog_manager; }

void rocksdb_set_compaction_options(
    my_core::THD *const thd MY_ATTRIBUTE((__unused__)),
    my_core::SYS_VAR *const var MY_ATTRIBUTE((__unused__)), void *const var_ptr,
    const void *const save) {
  if (var_ptr && save) {
    *(uint64_t *)var_ptr = *(const uint64_t *)save;
  }
  const Rdb_compact_params params = {
      (uint64_t)rocksdb_compaction_sequential_deletes,
      (uint64_t)rocksdb_compaction_sequential_deletes_window,
      (uint64_t)rocksdb_compaction_sequential_deletes_file_size};
  if (properties_collector_factory) {
    properties_collector_factory->SetCompactionParams(params);
  }
}

void rocksdb_set_table_stats_sampling_pct(
    my_core::THD *const thd MY_ATTRIBUTE((__unused__)),
    my_core::SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)), const void *const save) {
  RDB_MUTEX_LOCK_CHECK(rdb_sysvars_mutex);

  const uint32_t new_val = *static_cast<const uint32_t *>(save);

  if (new_val != rocksdb_table_stats_sampling_pct) {
    rocksdb_table_stats_sampling_pct = new_val;

    if (properties_collector_factory) {
      properties_collector_factory->SetTableStatsSamplingPct(
          rocksdb_table_stats_sampling_pct);
    }
  }

  RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
}

void rocksdb_update_table_stats_use_table_scan(THD *const /* thd */,
                                               struct SYS_VAR *const /* var */,
                                               void *const var_ptr,
                                               const void *const save) {
  RDB_MUTEX_LOCK_CHECK(rdb_sysvars_mutex);
  bool old_val = *static_cast<const bool *>(var_ptr);
  bool new_val = *static_cast<const bool *>(save);

  if (old_val == new_val) {
    RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
    return;
  }

  if (new_val) {
    struct Rdb_table_collector : public Rdb_tables_scanner {
      int add_table(Rdb_tbl_def *tdef) override {
        DBUG_ASSERT(tdef->m_key_count > 0);
        tdef->m_tbl_stats.set(tdef->m_key_count > 0
                                  ? tdef->m_key_descr_arr[0]->m_stats.m_rows
                                  : 0,
                              0, 0);
        return HA_EXIT_SUCCESS;
      }
    } collector;
    ddl_manager.scan_for_tables(&collector);

    // We do not add all tables to the index stats recalculation queue
    // to avoid index stats calculation workload spike.
  } else {
    rdb_is_thread.clear_all_index_stats_requests();
  }

  *static_cast<bool *>(var_ptr) = *static_cast<const bool *>(save);
  RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
}

int rocksdb_index_stats_thread_renice(THD *const /* thd */,
                                      struct SYS_VAR *const /* var */,
                                      void *const save,
                                      struct st_mysql_value *const value) {
  long long nice_val;
  /* value is NULL */
  if (value->val_int(value, &nice_val)) {
    return HA_EXIT_FAILURE;
  }

  if (rdb_is_thread.renice(nice_val) != HA_EXIT_SUCCESS) {
    return HA_EXIT_FAILURE;
  }

  *static_cast<int32_t *>(save) = static_cast<int32_t>(nice_val);
  return HA_EXIT_SUCCESS;
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
void rocksdb_set_rate_limiter_bytes_per_sec(
    my_core::THD *const thd,
    my_core::SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)), const void *const save) {
  const uint64_t new_val = *static_cast<const uint64_t *>(save);
  if (new_val == 0 || rocksdb_rate_limiter_bytes_per_sec == 0) {
    /*
      If a rate_limiter was not enabled at startup we can't change it nor
      can we disable it if one was created at startup
    */
    push_warning_printf(thd, Sql_condition::SL_WARNING, ER_WRONG_ARGUMENTS,
                        "RocksDB: rocksdb_rate_limiter_bytes_per_sec cannot "
                        "be dynamically changed to or from 0.  Do a clean "
                        "shutdown if you want to change it from or to 0.");
  } else if (new_val != rocksdb_rate_limiter_bytes_per_sec) {
    /* Apply the new value to the rate limiter and store it locally */
    DBUG_ASSERT(rocksdb_rate_limiter != nullptr);
    rocksdb_rate_limiter_bytes_per_sec = new_val;
    rocksdb_rate_limiter->SetBytesPerSecond(new_val);
  }
}

void rocksdb_set_sst_mgr_rate_bytes_per_sec(
    my_core::THD *const thd MY_ATTRIBUTE((unused)),
    my_core::SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)), const void *const save) {
  RDB_MUTEX_LOCK_CHECK(rdb_sysvars_mutex);

  const uint64_t new_val = *static_cast<const uint64_t *>(save);

  if (new_val != rocksdb_sst_mgr_rate_bytes_per_sec) {
    rocksdb_sst_mgr_rate_bytes_per_sec = new_val;

    rocksdb_db_options->sst_file_manager->SetDeleteRateBytesPerSecond(
        rocksdb_sst_mgr_rate_bytes_per_sec);
  }

  RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
}

void rocksdb_set_delayed_write_rate(THD *thd MY_ATTRIBUTE((unused)),
                                    struct SYS_VAR *var MY_ATTRIBUTE((unused)),
                                    void *var_ptr MY_ATTRIBUTE((unused)),
                                    const void *save) {
  RDB_MUTEX_LOCK_CHECK(rdb_sysvars_mutex);
  const uint64_t new_val = *static_cast<const uint64_t *>(save);
  if (rocksdb_delayed_write_rate != new_val) {
    rocksdb_delayed_write_rate = new_val;
    rocksdb::Status s =
        rdb->SetDBOptions({{"delayed_write_rate", std::to_string(new_val)}});

    if (!s.ok()) {
      /* NO_LINT_DEBUG */
      sql_print_warning(
          "MyRocks: failed to update delayed_write_rate. "
          "status code = %d, status = %s",
          s.code(), s.ToString().c_str());
    }
  }
  RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
}

void rocksdb_set_max_lock_memory(THD *thd, struct SYS_VAR*,
                                 void* /*var_ptr*/, const void *save) {
  const uint64_t new_val = *static_cast<const uint64_t *>(save);
  if (rocksdb_max_lock_memory != new_val) {
    if (range_lock_mgr->SetMaxLockMemory(new_val)) {
      /* NO_LINT_DEBUG */
      sql_print_warning("MyRocks: failed to set max_lock_memory");
      push_warning_printf(thd, Sql_condition::SL_WARNING,
                          ER_ERROR_WHEN_EXECUTING_COMMAND,
                          "Cannot set max_lock_memory to size below currently used");
    } else {
      // Succeeded
      rocksdb_max_lock_memory = new_val;
    }
  }
}

void rocksdb_set_max_latest_deadlocks(
    THD *thd MY_ATTRIBUTE((unused)), struct SYS_VAR *var MY_ATTRIBUTE((unused)),
    void *var_ptr MY_ATTRIBUTE((unused)), const void *save) {
  RDB_MUTEX_LOCK_CHECK(rdb_sysvars_mutex);
  const uint32_t new_val = *static_cast<const uint32_t *>(save);
  if (rocksdb_max_latest_deadlocks != new_val) {
    rocksdb_max_latest_deadlocks = new_val;
    if (range_lock_mgr) {
      auto n= rocksdb_max_latest_deadlocks;
      range_lock_mgr->SetRangeDeadlockInfoBufferSize(n);
    }
    else
      rdb->SetDeadlockInfoBufferSize(rocksdb_max_latest_deadlocks);

  }
  RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
}

void rdb_set_collation_exception_list(const char *const exception_list) {
  DBUG_ASSERT(rdb_collation_exceptions != nullptr);

  if (!rdb_collation_exceptions->set_patterns(exception_list)) {
    my_core::warn_about_bad_patterns(rdb_collation_exceptions,
                                     "strict_collation_exceptions");
  }
}

void rocksdb_set_collation_exception_list(
    THD *const thd MY_ATTRIBUTE((unused)),
    struct SYS_VAR *const var MY_ATTRIBUTE((unused)), void *const var_ptr,
    const void *const save) {
  const char *const val = *static_cast<const char *const *>(save);

  rdb_set_collation_exception_list(val == nullptr ? "" : val);

  *static_cast<const char **>(var_ptr) = val;
}

int mysql_value_to_bool(struct st_mysql_value *value, bool *return_value) {
  int new_value_type = value->value_type(value);
  if (new_value_type == MYSQL_VALUE_TYPE_STRING) {
    char buf[16];
    int len = sizeof(buf);
    const char *str = value->val_str(value, buf, &len);
    if (str && (my_strcasecmp(system_charset_info, "true", str) == 0 ||
                my_strcasecmp(system_charset_info, "on", str) == 0)) {
      *return_value = true;
    } else if (str && (my_strcasecmp(system_charset_info, "false", str) == 0 ||
                       my_strcasecmp(system_charset_info, "off", str) == 0)) {
      *return_value = false;
    } else {
      return 1;
    }
  } else if (new_value_type == MYSQL_VALUE_TYPE_INT) {
    long long intbuf;
    value->val_int(value, &intbuf);
    if (intbuf > 1) return 1;
    *return_value = intbuf > 0 ? true : false;
  } else {
    return 1;
  }

  return 0;
}

int rocksdb_check_bulk_load(THD *const thd,
                            struct SYS_VAR *var MY_ATTRIBUTE((__unused__)),
                            void *save, struct st_mysql_value *value) {
  bool new_value;
  if (mysql_value_to_bool(value, &new_value) != 0) {
    return 1;
  }

  Rdb_transaction *tx = get_tx_from_thd(thd);
  if (tx != nullptr) {
    bool is_critical_error;
    const int rc = tx->finish_bulk_load(&is_critical_error);
    if (rc != 0 && is_critical_error) {
      // NO_LINT_DEBUG
      sql_print_error(
          "RocksDB: Error %d finalizing last SST file while "
          "setting bulk loading variable",
          rc);
      THDVAR(thd, bulk_load) = 0;
      return 1;
    }
  }

  *static_cast<bool *>(save) = new_value;
  return 0;
}

int rocksdb_check_bulk_load_allow_unsorted(
    THD *const thd, struct SYS_VAR *var MY_ATTRIBUTE((__unused__)), void *save,
    struct st_mysql_value *value) {
  bool new_value;
  if (mysql_value_to_bool(value, &new_value) != 0) {
    return 1;
  }

  if (THDVAR(thd, bulk_load)) {
    my_error(ER_ERROR_WHEN_EXECUTING_COMMAND, MYF(0), "SET",
             "Cannot change this setting while bulk load is enabled");

    return 1;
  }

  *static_cast<bool *>(save) = new_value;
  return 0;
}

static void rocksdb_set_max_background_jobs(
    THD *thd MY_ATTRIBUTE((unused)),
    struct SYS_VAR *const var MY_ATTRIBUTE((unused)),
    void *const var_ptr MY_ATTRIBUTE((unused)), const void *const save) {
  DBUG_ASSERT(save != nullptr);
  DBUG_ASSERT(rocksdb_db_options != nullptr);
  DBUG_ASSERT(rocksdb_db_options->env != nullptr);

  RDB_MUTEX_LOCK_CHECK(rdb_sysvars_mutex);

  const int new_val = *static_cast<const int *>(save);

  if (rocksdb_db_options->max_background_jobs != new_val) {
    rocksdb_db_options->max_background_jobs = new_val;
    rocksdb::Status s =
        rdb->SetDBOptions({{"max_background_jobs", std::to_string(new_val)}});

    if (!s.ok()) {
      /* NO_LINT_DEBUG */
      sql_print_warning(
          "MyRocks: failed to update max_background_jobs. "
          "Status code = %d, status = %s.",
          s.code(), s.ToString().c_str());
    }
  }

  RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
}

static void rocksdb_set_max_background_compactions(
    THD *thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)), const void *const save) {
  DBUG_ASSERT(save != nullptr);
  DBUG_ASSERT(rocksdb_db_options != nullptr);
  DBUG_ASSERT(rocksdb_db_options->env != nullptr);

  RDB_MUTEX_LOCK_CHECK(rdb_sysvars_mutex);

  const int new_val = *static_cast<const int *>(save);

  if (rocksdb_db_options->max_background_compactions != new_val) {
    rocksdb_db_options->max_background_compactions = new_val;
    rocksdb::Status s = rdb->SetDBOptions(
        {{"max_background_compactions", std::to_string(new_val)}});

    if (!s.ok()) {
      /* NO_LINT_DEBUG */
      sql_print_warning(
          "MyRocks: failed to update max_background_compactions. "
          "Status code = %d, status = %s.",
          s.code(), s.ToString().c_str());
    }
  }

  RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
}

/**
   rocksdb_set_max_bottom_pri_background_compactions_internal() changes
   the number of rocksdb background threads.
   Creating new threads may take up to a few seconds, so instead of
   calling the function at sys_var::update path where global mutex is held,
   doing at sys_var::check path so that other queries are not blocked.
   Same optimization is done for rocksdb_block_cache_size too.
*/
static int rocksdb_validate_max_bottom_pri_background_compactions(
    THD *thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)), void *var_ptr,
    struct st_mysql_value *value) {
  DBUG_ASSERT(value != nullptr);

  long long new_value;

  /* value is NULL */
  if (value->val_int(value, &new_value)) {
    return HA_EXIT_FAILURE;
  }
  if (new_value < 0 ||
      new_value > ROCKSDB_MAX_BOTTOM_PRI_BACKGROUND_COMPACTIONS) {
    return HA_EXIT_FAILURE;
  }
  RDB_MUTEX_LOCK_CHECK(rdb_bottom_pri_background_compactions_resize_mutex);
  if (rocksdb_max_bottom_pri_background_compactions != new_value) {
    if (new_value == 0) {
      my_error(ER_ERROR_WHEN_EXECUTING_COMMAND, MYF(0), "SET",
               "max_bottom_pri_background_compactions can't be changed to 0 "
               "online.");
      RDB_MUTEX_UNLOCK_CHECK(
          rdb_bottom_pri_background_compactions_resize_mutex);
      return HA_EXIT_FAILURE;
    }
    rocksdb_set_max_bottom_pri_background_compactions_internal(new_value);
  }
  *static_cast<int64_t *>(var_ptr) = static_cast<int64_t>(new_value);
  RDB_MUTEX_UNLOCK_CHECK(rdb_bottom_pri_background_compactions_resize_mutex);
  return HA_EXIT_SUCCESS;
}

static void rocksdb_set_bytes_per_sync(
    THD *thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)), const void *const save) {
  DBUG_ASSERT(save != nullptr);
  DBUG_ASSERT(rocksdb_db_options != nullptr);
  DBUG_ASSERT(rocksdb_db_options->env != nullptr);

  RDB_MUTEX_LOCK_CHECK(rdb_sysvars_mutex);

  const ulonglong new_val = *static_cast<const ulonglong *>(save);

  if (rocksdb_db_options->bytes_per_sync != new_val) {
    rocksdb_db_options->bytes_per_sync = new_val;
    rocksdb::Status s =
        rdb->SetDBOptions({{"bytes_per_sync", std::to_string(new_val)}});

    if (!s.ok()) {
      /* NO_LINT_DEBUG */
      sql_print_warning(
          "MyRocks: failed to update max_background_jobs. "
          "Status code = %d, status = %s.",
          s.code(), s.ToString().c_str());
    }
  }

  RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
}

static void rocksdb_set_wal_bytes_per_sync(
    THD *thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)),
    void *const var_ptr MY_ATTRIBUTE((__unused__)), const void *const save) {
  DBUG_ASSERT(save != nullptr);
  DBUG_ASSERT(rocksdb_db_options != nullptr);
  DBUG_ASSERT(rocksdb_db_options->env != nullptr);

  RDB_MUTEX_LOCK_CHECK(rdb_sysvars_mutex);

  const ulonglong new_val = *static_cast<const ulonglong *>(save);

  if (rocksdb_db_options->wal_bytes_per_sync != new_val) {
    rocksdb_db_options->wal_bytes_per_sync = new_val;
    rocksdb::Status s =
        rdb->SetDBOptions({{"wal_bytes_per_sync", std::to_string(new_val)}});

    if (!s.ok()) {
      /* NO_LINT_DEBUG */
      sql_print_warning(
          "MyRocks: failed to update max_background_jobs. "
          "Status code = %d, status = %s.",
          s.code(), s.ToString().c_str());
    }
  }

  RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
}

/*
  Validating and updating block cache size via sys_var::check path.
  SetCapacity may take seconds when reducing block cache, and
  sys_var::update holds LOCK_global_system_variables mutex, so
  updating block cache size is done at check path instead.
*/
static int rocksdb_validate_set_block_cache_size(
    THD *thd MY_ATTRIBUTE((__unused__)),
    struct SYS_VAR *const var MY_ATTRIBUTE((__unused__)), void *var_ptr,
    struct st_mysql_value *value) {
  DBUG_ASSERT(value != nullptr);

  long long new_value;

  /* value is NULL */
  if (value->val_int(value, &new_value)) {
    return HA_EXIT_FAILURE;
  }

  if (new_value < RDB_MIN_BLOCK_CACHE_SIZE ||
      (uint64_t)new_value > (uint64_t)LLONG_MAX) {
    return HA_EXIT_FAILURE;
  }

  RDB_MUTEX_LOCK_CHECK(rdb_block_cache_resize_mutex);
  const rocksdb::BlockBasedTableOptions &table_options =
      rdb_get_table_options();

  if (rocksdb_block_cache_size != new_value && table_options.block_cache) {
    table_options.block_cache->SetCapacity(new_value);
  }
  *static_cast<int64_t *>(var_ptr) = static_cast<int64_t>(new_value);
  RDB_MUTEX_UNLOCK_CHECK(rdb_block_cache_resize_mutex);
  return HA_EXIT_SUCCESS;
}

static int rocksdb_validate_update_cf_options(THD * /* unused */,
                                              struct SYS_VAR * /*unused*/,
                                              void *save,
                                              struct st_mysql_value *value) {
  int length = 0;
  const char *str = value->val_str(value, nullptr, &length);

  *(const char **)save = nullptr;
  if (str == nullptr) {
    return HA_EXIT_SUCCESS;
  }

  std::stringstream output;
  Rdb_cf_options::Name_to_config_t option_map;

  // Basic sanity checking and parsing the options into a map. If this fails
  // then there's no point to proceed.
  if (!Rdb_cf_options::parse_cf_options(str, &option_map, &output)) {
    my_printf_error(ER_WRONG_VALUE_FOR_VAR, "%s", MYF(0), output.str().c_str());
    return HA_EXIT_FAILURE;
  }

  // Loop through option_map and create missing column families
  for (Rdb_cf_options::Name_to_config_t::iterator it = option_map.begin();
       it != option_map.end(); ++it) {
    // If the CF is removed at this point, i.e., cf_manager.drop_cf() has
    // been called, it is OK to create a new CF.

    const auto &cf_name = it->first;
    {
      std::lock_guard<Rdb_dict_manager> dm_lock(dict_manager);
      auto cfh = cf_manager.get_or_create_cf(rdb, cf_name);

      if (!cfh) {
        return HA_EXIT_FAILURE;
      }

      if (cf_manager.create_cf_flags_if_needed(&dict_manager, cfh->GetID(),
                                               cf_name)) {
        return HA_EXIT_FAILURE;
      }
    }
  }

  *(const char **)save = str;

  return HA_EXIT_SUCCESS;
}

static void rocksdb_set_update_cf_options(THD *const /* unused */,
                                          struct SYS_VAR *const /* unused */,
                                          void *const var_ptr,
                                          const void *const save) {
  const char *const val = *static_cast<const char *const *>(save);

  RDB_MUTEX_LOCK_CHECK(rdb_sysvars_mutex);

  if (!val) {
    *reinterpret_cast<char **>(var_ptr) = nullptr;
    RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
    return;
  }

  DBUG_ASSERT(val != nullptr);

  // Reset the pointers regardless of how much success we had with updating
  // the CF options. This will results in consistent behavior and avoids
  // dealing with cases when only a subset of CF-s was successfully updated.
  *reinterpret_cast<const char **>(var_ptr) = val;

  // Do the real work of applying the changes.
  Rdb_cf_options::Name_to_config_t option_map;

  // This should never fail, because of rocksdb_validate_update_cf_options
  if (!Rdb_cf_options::parse_cf_options(val, &option_map)) {
    RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
    return;
  }

  // For each CF we have, see if we need to update any settings.
  for (const auto &cf_name : cf_manager.get_cf_names()) {
    DBUG_ASSERT(!cf_name.empty());

    std::shared_ptr<rocksdb::ColumnFamilyHandle> cfh =
        cf_manager.get_cf(cf_name);

    if (!cfh) {
      // NO_LINT_DEBUG
      sql_print_information(
          "Skip updating options for cf %s because the cf has been dropped.",
          cf_name.c_str());
      continue;
    }

    const auto it = option_map.find(cf_name);
    std::string per_cf_options = (it != option_map.end()) ? it->second : "";

    if (!per_cf_options.empty()) {
      Rdb_cf_options::Name_to_config_t opt_map;
      rocksdb::Status s = rocksdb::StringToMap(per_cf_options, &opt_map);

      if (s != rocksdb::Status::OK()) {
        // NO_LINT_DEBUG
        sql_print_warning(
            "MyRocks: failed to convert the options for column "
            "family '%s' to a map. %s",
            cf_name.c_str(), s.ToString().c_str());
      } else {
        DBUG_ASSERT(rdb != nullptr);

        // Finally we can apply the options.
        // If cf_manager.drop_cf() has been called at this point, SetOptions()
        // will still succeed. The options data will only be cleared when
        // the CF handle object is destroyed.
        s = rdb->SetOptions(cfh.get(), opt_map);

        if (s != rocksdb::Status::OK()) {
          // NO_LINT_DEBUG
          sql_print_warning(
              "MyRocks: failed to apply the options for column "
              "family '%s'. %s",
              cf_name.c_str(), s.ToString().c_str());
        } else {
          // NO_LINT_DEBUG
          sql_print_information(
              "MyRocks: options for column family '%s' "
              "have been successfully updated.",
              cf_name.c_str());

          // Make sure that data is internally consistent as well and update
          // the CF options. This is necessary also to make sure that the CF
          // options will be correctly reflected in the relevant table:
          // ROCKSDB_CF_OPTIONS in INFORMATION_SCHEMA.
          rocksdb::ColumnFamilyOptions cf_options = rdb->GetOptions(cfh.get());
          std::string updated_options;

          s = rocksdb::GetStringFromColumnFamilyOptions(&updated_options,
                                                        cf_options);

          DBUG_ASSERT(s == rocksdb::Status::OK());
          DBUG_ASSERT(!updated_options.empty());

          cf_manager.update_options_map(cf_name, updated_options);
        }
      }
    }
  }

  // Our caller (`plugin_var_memalloc_global_update`) will call `my_free` to
  // free up resources used before.

  RDB_MUTEX_UNLOCK_CHECK(rdb_sysvars_mutex);
}

void rdb_queue_save_stats_request() { rdb_bg_thread.request_save_stats(); }

void ha_rocksdb::rpl_before_delete_rows() {
  DBUG_ENTER_FUNC();

  m_in_rpl_delete_rows = true;

  DBUG_VOID_RETURN;
}

void ha_rocksdb::rpl_after_delete_rows() {
  DBUG_ENTER_FUNC();

  m_in_rpl_delete_rows = false;

  DBUG_VOID_RETURN;
}

void ha_rocksdb::rpl_before_update_rows() {
  DBUG_ENTER_FUNC();

  m_in_rpl_update_rows = true;

  DBUG_VOID_RETURN;
}

void ha_rocksdb::rpl_after_update_rows() {
  DBUG_ENTER_FUNC();

  m_in_rpl_update_rows = false;

  DBUG_VOID_RETURN;
}

bool ha_rocksdb::is_read_free_rpl_table() const {
  return table->s && m_tbl_def->m_is_read_free_rpl_table;
}

/**
  @brief
  Read Free Replication can be used or not. Returning true means
  Read Free Replication can be used.
*/
bool ha_rocksdb::use_read_free_rpl() const {
  DBUG_ENTER_FUNC();

  if (!ha_thd()->rli_slave || table->triggers || !is_read_free_rpl_table()) {
    DBUG_RETURN(false);
  }

  switch (rocksdb_read_free_rpl) {
    case read_free_rpl_type::OFF:
      DBUG_RETURN(false);
    case read_free_rpl_type::PK_ONLY:
      DBUG_RETURN(!has_hidden_pk(table) && table->s->keys == 1);
    case read_free_rpl_type::PK_SK:
      DBUG_RETURN(!has_hidden_pk(table));
  }

  DBUG_ASSERT(false);
  DBUG_RETURN(false);
}

/**
  @brief
  Whether the table or last access partition has TTL column
  Only used in replication error checking
*/
bool ha_rocksdb::last_part_has_ttl_column() const {
  return m_tbl_def->has_ttl_col();
}

uchar *ha_rocksdb::get_blob_buffer(uint current_size) {
  auto output = m_blob_buffer_current;
  m_blob_buffer_current = m_blob_buffer_current + current_size;
  DBUG_ASSERT((m_blob_buffer_current - m_blob_buffer_start) <=
              m_total_blob_buffer_allocated);
  return output;
}

bool ha_rocksdb::reset_blob_buffer(uint total_size) {
  if (m_blob_buffer_start == nullptr) {
    m_blob_buffer_start = reinterpret_cast<uchar *>(
        my_malloc(PSI_NOT_INSTRUMENTED, total_size, MYF(0)));
    m_total_blob_buffer_allocated = total_size;
  } else if (m_total_blob_buffer_allocated < total_size) {
    my_free(m_blob_buffer_start);
    m_blob_buffer_start = reinterpret_cast<uchar *>(
        my_malloc(PSI_NOT_INSTRUMENTED, total_size, MYF(0)));
    m_total_blob_buffer_allocated = total_size;
  }
  if (!m_blob_buffer_start) return true;
  m_blob_buffer_current = m_blob_buffer_start;
  return false;
}

void ha_rocksdb::release_blob_buffer() {
  if (m_blob_buffer_start != nullptr) {
    my_free(m_blob_buffer_start);
    m_blob_buffer_start = nullptr;
  }
}

double ha_rocksdb::read_time(uint index, uint ranges, ha_rows rows) {
  DBUG_ENTER_FUNC();

  if (index != table->s->primary_key) {
    /* Non covering index range scan */
    DBUG_RETURN(handler::read_time(index, ranges, rows));
  }

  DBUG_RETURN((rows / 20.0) + 1);
}

void ha_rocksdb::print_error(int error, myf errflag) {
  if (error == HA_ERR_ROCKSDB_STATUS_BUSY) {
    error = HA_ERR_LOCK_DEADLOCK;
  }
  handler::print_error(error, errflag);
}

std::string rdb_corruption_marker_file_name() {
  std::string ret(rocksdb_datadir);
  ret.append("/ROCKSDB_CORRUPTED");
  return ret;
}

rocksdb::DBOptions *get_rocksdb_db_options() {
  return rocksdb_db_options.get();
}

static void rocksdb_select_bypass_rejected_query_history_size_update(
    my_core::THD *const /* unused */, my_core::SYS_VAR *const /* unused */,
    void *const var_ptr, const void *const save) {
  DBUG_ASSERT(rdb != nullptr);

  uint32_t val = *static_cast<uint32_t *>(var_ptr) =
      *static_cast<const uint32_t *>(save);

  const std::lock_guard<std::mutex> lock(myrocks::rejected_bypass_query_lock);
  if (myrocks::rejected_bypass_queries.size() > val) {
    myrocks::rejected_bypass_queries.resize(val);
  }
}

static void rocksdb_max_compaction_history_update(
    my_core::THD *const /* unused */, my_core::SYS_VAR *const /* unused */,
    void *const var_ptr, const void *const save) {
  DBUG_ASSERT(rdb != nullptr);

  uint64_t val = *static_cast<uint64_t *>(var_ptr) =
      *static_cast<const uint64_t *>(save);
  compaction_stats.resize_history(val);
}

void Rdb_compaction_stats::resize_history(size_t max_history_len) {
  std::lock_guard<std::mutex> guard(m_mutex);
  m_max_history_len = max_history_len;
  if (m_history.size() > m_max_history_len) {
    m_history.erase(m_history.begin(),
                    m_history.begin() + (m_history.size() - m_max_history_len));
  }
  DBUG_ASSERT(m_history.size() <= m_max_history_len);
}

std::vector<Rdb_compaction_stats_record>
Rdb_compaction_stats::get_current_stats() {
  std::lock_guard<std::mutex> guard(m_mutex);
  std::vector<Rdb_compaction_stats_record> res;
  res.reserve(m_tid_to_pending_compaction.size());
  for (const auto &tid_and_pending_compaction : m_tid_to_pending_compaction) {
    res.push_back(tid_and_pending_compaction.second);
  }
  return res;
}

std::vector<Rdb_compaction_stats_record>
Rdb_compaction_stats::get_recent_history() {
  std::lock_guard<std::mutex> guard(m_mutex);
  std::vector<Rdb_compaction_stats_record> res;
  res.reserve(m_history.size());
  for (const auto &record : m_history) {
    res.push_back(record);
  }
  return res;
}

void Rdb_compaction_stats::record_start(rocksdb::CompactionJobInfo info) {
  std::lock_guard<std::mutex> guard(m_mutex);
  time_t start_timestamp = time(nullptr /* tloc */);
  DBUG_ASSERT(start_timestamp != static_cast<time_t>(-1));

  const auto id = info.thread_id;
  m_tid_to_pending_compaction[id] = Rdb_compaction_stats_record{
      start_timestamp, static_cast<time_t>(-1) /* end_timestamp */,
      std::move(info)};
}

void Rdb_compaction_stats::record_end(rocksdb::CompactionJobInfo info) {
  std::lock_guard<std::mutex> guard(m_mutex);
  auto tid_to_pending_compaction_iter =
      m_tid_to_pending_compaction.find(info.thread_id);
  DBUG_ASSERT(tid_to_pending_compaction_iter !=
              m_tid_to_pending_compaction.end());
  Rdb_compaction_stats_record record;
  if (tid_to_pending_compaction_iter != m_tid_to_pending_compaction.end()) {
    record.start_timestamp =
        tid_to_pending_compaction_iter->second.start_timestamp;
    m_tid_to_pending_compaction.erase(tid_to_pending_compaction_iter);
  } else {
    record.start_timestamp = static_cast<time_t>(-1);
  }
  if (m_max_history_len == 0) {
    return;
  }
  if (m_history.size() == m_max_history_len) {
    m_history.pop_front();
  }
  DBUG_ASSERT(m_history.size() < m_max_history_len);
  record.end_timestamp = time(nullptr /* tloc */);
  DBUG_ASSERT(record.end_timestamp != static_cast<time_t>(-1));
  record.info = std::move(info);
  m_history.emplace_back(std::move(record));
}

select_bypass_policy_type get_select_bypass_policy() {
  return static_cast<select_bypass_policy_type>(rocksdb_select_bypass_policy);
}

bool should_fail_unsupported_select_bypass() {
  return rocksdb_select_bypass_fail_unsupported;
}

bool should_log_rejected_select_bypass() {
  return rocksdb_select_bypass_log_rejected;
}

bool should_log_failed_select_bypass() {
  return rocksdb_select_bypass_log_failed;
}

bool should_allow_filters_select_bypass() {
  return rocksdb_select_bypass_allow_filters;
}

uint32_t get_select_bypass_rejected_query_history_size() {
  return rocksdb_select_bypass_rejected_query_history_size;
}

uint32_t get_select_bypass_debug_row_delay() {
  return rocksdb_select_bypass_debug_row_delay;
}

unsigned long long  // NOLINT(runtime/int)
get_select_bypass_multiget_min() {
  return rocksdb_select_bypass_multiget_min;
}

unsigned long long get_partial_index_sort_max_mem(THD *thd) {
  return THDVAR(thd, partial_index_sort_max_mem);
}

const rocksdb::ReadOptions &rdb_tx_acquire_snapshot(Rdb_transaction *tx) {
  tx->acquire_snapshot(true);
  return tx->m_read_opts;
}

rocksdb::Iterator *rdb_tx_get_iterator(
   Rdb_transaction *tx, rocksdb::ColumnFamilyHandle *const column_family,
    const std::shared_ptr<Rdb_key_def> &kd,
    bool skip_bloom_filter, const rocksdb::Slice &lower_bound_slice,
    const rocksdb::Slice &upper_bound_slice, bool read_current,
    bool create_snapshot) {
  return tx->get_iterator(column_family, kd, skip_bloom_filter, lower_bound_slice,
                          upper_bound_slice, read_current, create_snapshot);
}


rocksdb::Iterator *rdb_tx_get_iterator(
    THD *thd, rocksdb::ColumnFamilyHandle *const cf, 
    const std::shared_ptr<Rdb_key_def> &kd, 
    bool skip_bloom_filter,
    const rocksdb::Slice &eq_cond_lower_bound,
    const rocksdb::Slice &eq_cond_upper_bound,
    const rocksdb::Snapshot **snapshot, bool read_current,
    bool create_snapshot, bool use_locking_iter) {
  if (commit_in_the_middle(thd)) {
    DBUG_ASSERT(snapshot && *snapshot == nullptr);
    if (snapshot) {
      *snapshot = rdb->GetSnapshot();
      auto read_opts = rocksdb::ReadOptions();
      // TODO(mung): set based on WHERE conditions
      read_opts.total_order_seek = true;
      read_opts.snapshot = *snapshot;
      return rdb->NewIterator(read_opts, cf);
    } else {
      return nullptr;
    }
  } else {
    Rdb_transaction *tx = get_tx_from_thd(thd);
    return tx->get_iterator(cf, kd, skip_bloom_filter, eq_cond_lower_bound,
                            eq_cond_upper_bound, read_current, create_snapshot, use_locking_iter);
  }
}

bool rdb_tx_started(Rdb_transaction *tx) { return tx->is_tx_started(); }

rocksdb::Status rdb_tx_get(Rdb_transaction *tx,
                           rocksdb::ColumnFamilyHandle *const column_family,
                           const rocksdb::Slice &key,
                           rocksdb::PinnableSlice *const value) {
  return tx->get(column_family, key, value);
}

rocksdb::Status rdb_tx_get_for_update(Rdb_transaction *tx,
                                      const Rdb_key_def &kd,
                                      const rocksdb::Slice &key,
                                      rocksdb::PinnableSlice *const value,
                                      bool exclusive, bool skip_wait) {
  bool do_validate =
      my_core::thd_tx_isolation(tx->get_thd()) > ISO_READ_COMMITTED;
  rocksdb::Status s =
      tx->get_for_update(kd, key, value, exclusive, do_validate, skip_wait);

#ifndef DBUG_OFF
  ++rocksdb_num_get_for_update_calls;
#endif
  return s;
}

void rdb_tx_release_lock(Rdb_transaction *tx, const Rdb_key_def &kd,
                         const rocksdb::Slice &key, bool force) {
  tx->release_lock(kd, std::string(key.data(), key.size()), force);
}


rocksdb::Status rdb_tx_lock_range(Rdb_transaction *tx,
                                  const Rdb_key_def &kd,
                                  const rocksdb::Endpoint &start_key,
                                  const rocksdb::Endpoint &end_key)
{
  return tx->lock_range(kd.get_cf(), start_key, end_key);
}

void rdb_tx_multi_get(Rdb_transaction *tx,
                      rocksdb::ColumnFamilyHandle *const column_family,
                      const size_t num_keys, const rocksdb::Slice *keys,
                      rocksdb::PinnableSlice *values, rocksdb::Status *statuses,
                      const bool sorted_input) {
  tx->multi_get(column_family, num_keys, keys, values, statuses, sorted_input);
}

int rdb_tx_set_status_error(Rdb_transaction *tx, const rocksdb::Status &s,
                            const Rdb_key_def &kd,
                            const Rdb_tbl_def *const tbl_def) {
  return tx->set_status_error(tx->get_thd(), s, kd, tbl_def, nullptr);
}

/****************************************************************************
 * Multi-Range-Read implementation based on RocksDB's MultiGet() call
 ***************************************************************************/

/*
  Check if MultiGet-MRR can be used to scan given list of ranges.

  @param  seq            List of ranges to scan
  @param  bufsz   INOUT  IN: Size of the buffer available for use
                         OUT: How much buffer space will be required
  @param  flags   INOUT  Properties of the scan to be done

  @return
     HA_POS_ERROR - The scan cannot be done at all
     Other value  - Number of expected output rows
*/
ha_rows ha_rocksdb::multi_range_read_info_const(uint keyno, RANGE_SEQ_IF *seq,
                                                void *seq_init_param,
                                                uint n_ranges, uint *bufsz,
                                                uint *flags,
                                                Cost_estimate *cost) {
  ha_rows res;
  THD *thd = table->in_use;

  // We allow MultiGet-MRR only with these settings:
  //   optimizer_switch='mrr=on,mrr_cost_based=off'
  // mrr_cost_based is not supported
  bool mrr_enabled =
      thd->optimizer_switch_flag(OPTIMIZER_SWITCH_MRR) &&
      !thd->optimizer_switch_flag(OPTIMIZER_SWITCH_MRR_COST_BASED);

  uint def_bufsz = *bufsz;
  res = handler::multi_range_read_info_const(keyno, seq, seq_init_param,
                                             n_ranges, &def_bufsz, flags, cost);

  if (res == HA_POS_ERROR) {
    return res;  // Not possible to do the scan
  }

  // Use the default MRR implementation if @@optimizer_switch value tells us
  // to, or if the query needs to do a locking read.
  if (!mrr_enabled || m_lock_rows != RDB_LOCK_NONE) {
    return res;
  }

  // How many buffer required to store all requried keys
  uint calculated_buf = mrr_get_length_per_rec() * res * 10 + 1;
  // How many buffer required to store maximum number of keys per MRR
  ssize_t elements_limit = THDVAR(thd, mrr_batch_size);
  uint mrr_batch_size_buff =
      mrr_get_length_per_rec() * elements_limit * 1.1 + 1;
  // The final bufsz value should be minimum among these three values:
  // 1. The passed in bufsz: contains maximum available buff size --- by
  // default, its value is specify by session variable read_rnd_buff_size,
  // 2. calculated_buf, specify buffer required to store all required keys
  // 3. mrr_batch_size_buffer, specify the maximun number of keys to fetch
  // during each MRR
  uint mrr_bufsz =
      std::min(std::min(*bufsz, calculated_buf), mrr_batch_size_buff);

  if (keyno == table->s->primary_key) {
    // We need all ranges to be single-point lookups using full PK values.
    // (Range scans, like "pk BETWEEN 10 and 20" or restrictions on PK prefix
    //  cannot be used)
    bool all_eq_ranges = true;
    KEY_MULTI_RANGE range{};
    range_seq_t seq_it;
    seq_it = seq->init(seq_init_param, n_ranges, *flags);
    while (!seq->next(seq_it, &range)) {
      if ((range.range_flag & UNIQUE_RANGE) == 0) {
        all_eq_ranges = false;
        break;
      }
      if (table->in_use->killed) {
        return HA_POS_ERROR;
      }
    }

    if (all_eq_ranges) {
      // Indicate that we will use MultiGet MRR
      *flags &= ~HA_MRR_USE_DEFAULT_IMPL;
      *flags |= HA_MRR_SUPPORT_SORTED;
      *bufsz = mrr_bufsz;
    }
  } else {
    // For scans on secondary keys, we use MultiGet when we read the PK values.
    // We only need PK values when the scan is non-index-only.
    if ((*flags & HA_MRR_INDEX_ONLY) == 0) {
      *flags &= ~HA_MRR_USE_DEFAULT_IMPL;
      *flags |= HA_MRR_SUPPORT_SORTED;
      *flags |= HA_MRR_CONVERT_REF_TO_RANGE;
      *bufsz = mrr_bufsz;
    }
  }

  return res;
}

ha_rows ha_rocksdb::multi_range_read_info(uint keyno, uint n_ranges, uint keys,
                                          uint *bufsz, uint *flags,
                                          Cost_estimate *cost) {
  ha_rows res;
  THD *thd = table->in_use;
  bool mrr_enabled =
      thd->optimizer_switch_flag(OPTIMIZER_SWITCH_MRR) &&
      !thd->optimizer_switch_flag(OPTIMIZER_SWITCH_MRR_COST_BASED);

  res =
      handler::multi_range_read_info(keyno, n_ranges, keys, bufsz, flags, cost);
  if (res || m_lock_rows != RDB_LOCK_NONE || !mrr_enabled) {
    return res;
  }

  if (keyno == table->s->primary_key &&
      (*flags & HA_MRR_FULL_EXTENDED_KEYS) != 0) {
    *flags &= ~HA_MRR_USE_DEFAULT_IMPL;
    *flags |= HA_MRR_CONVERT_REF_TO_RANGE;
    *flags |= HA_MRR_SUPPORT_SORTED;
  }

  if (keyno != table->s->primary_key && (*flags & HA_MRR_INDEX_ONLY) == 0) {
    *flags &= ~HA_MRR_USE_DEFAULT_IMPL;
    *flags |= HA_MRR_CONVERT_REF_TO_RANGE;
    *flags &= ~HA_MRR_SUPPORT_SORTED;  // Non-sorted mode
  }

  return 0;  // "0" means ok, despite the ha_rows return type.
}

//
// Source of Rowids for the MRR scan
//
class Mrr_rowid_source {
 public:
  // Get the next rowid, in the on-disk mem-comparable form. Also, get the
  // "range pointer" associated with the rowid (it is returned in *range_ptr).
  virtual int get_next_rowid(uchar *buf, int *size, char **range_ptr) = 0;
  virtual bool eof() = 0;
  virtual ~Mrr_rowid_source() = default;
};

//
// Rowid source that produces rowids by enumerating a sequence of ranges
//
class Mrr_pk_scan_rowid_source : public Mrr_rowid_source {
  bool mrr_ranges_eof;  // true means we've got eof when enumerating the ranges.
  ha_rocksdb *self;

 public:
  Mrr_pk_scan_rowid_source(ha_rocksdb *self_arg, void *seq_init_param,
                           uint n_ranges, uint mode)
      : mrr_ranges_eof(false), self(self_arg) {
    self->mrr_iter = self->mrr_funcs.init(seq_init_param, n_ranges, mode);
  }

  int get_next_rowid(uchar *buf, int *size, char **range_ptr) override {
    if (mrr_ranges_eof) {
      return HA_ERR_END_OF_FILE;  //  At eof already
    }

    KEY_MULTI_RANGE range{};
    if ((mrr_ranges_eof = self->mrr_funcs.next(self->mrr_iter, &range))) {
      return HA_ERR_END_OF_FILE;  //  Got eof now
    }

    key_part_map all_parts_map =
        (key_part_map(1) << self->m_pk_descr->get_key_parts()) - 1;
    DBUG_ASSERT(range.start_key.keypart_map == all_parts_map);
    DBUG_ASSERT(range.end_key.keypart_map == all_parts_map);
    DBUG_ASSERT(range.start_key.flag == HA_READ_KEY_EXACT);
    DBUG_ASSERT(range.end_key.flag == HA_READ_AFTER_KEY);

    *range_ptr = range.ptr;
    *size = self->m_pk_descr->pack_index_tuple(self->table, self->m_pack_buffer,
                                               buf, range.start_key.key,
                                               all_parts_map);
    return 0;
  }

  bool eof() override { return mrr_ranges_eof; }
};

//
// Rowid source that produces rowids by doing an index-only scan on a
// secondary index and returning rowids from the index records
//
class Mrr_sec_key_rowid_source : public Mrr_rowid_source {
  ha_rocksdb *self;
  int err;

 public:
  explicit Mrr_sec_key_rowid_source(ha_rocksdb *self_arg)
      : self(self_arg), err(0) {}

  int init(RANGE_SEQ_IF *seq, void *seq_init_param, uint n_ranges, uint mode) {
    self->m_keyread_only = true;
    self->mrr_enabled_keyread = true;
    return self->handler::multi_range_read_init(seq, seq_init_param, n_ranges,
                                                mode, nullptr);
  }

  int get_next_rowid(uchar *buf, int *size, char **range_ptr) override {
    if (err) {
      return err;
    }

    while ((err = self->handler::multi_range_read_next(range_ptr)) == 0) {
      if (self->mrr_funcs.skip_index_tuple != nullptr &&
          self->mrr_funcs.skip_index_tuple(self->mrr_iter, *range_ptr)) {
        // BKA's variant of "Index Condition Pushdown" check failed
        continue;
      }

      if (self->mrr_funcs.skip_record != nullptr &&
          self->mrr_funcs.skip_record(self->mrr_iter, *range_ptr,
                                      (uchar *)self->m_last_rowkey.ptr())) {
        continue;
      }

      memcpy(buf, self->m_last_rowkey.ptr(), self->m_last_rowkey.length());
      *size = self->m_last_rowkey.length();
      break;
    }
    return err;
  }

  bool eof() override { return err != 0; }
};

// Initialize an MRR scan
int ha_rocksdb::multi_range_read_init(RANGE_SEQ_IF *seq, void *seq_init_param,
                                      uint n_ranges, uint mode,
                                      HANDLER_BUFFER *buf) {
  m_need_build_decoder = true;

  int res;

  if (!current_thd->optimizer_switch_flag(OPTIMIZER_SWITCH_MRR) ||
      (mode & HA_MRR_USE_DEFAULT_IMPL) != 0 ||
      (buf->buffer_end - buf->buffer < mrr_get_length_per_rec()) ||
      (THDVAR(current_thd, mrr_batch_size) == 0)) {
    mrr_uses_default_impl = true;
    res = handler::multi_range_read_init(seq, seq_init_param, n_ranges, mode,
                                         buf);
    return res;
  }

  // Ok, using a non-default MRR implementation, MultiGet-MRR

  mrr_uses_default_impl = false;
  mrr_n_elements = 0;  // nothing to cleanup, yet.
  mrr_enabled_keyread = false;
  mrr_rowid_reader = nullptr;

  mrr_funcs = *seq;
  mrr_buf = *buf;

  bool is_mrr_assoc = !(mode & HA_MRR_NO_ASSOCIATION);
  if (is_mrr_assoc) {
    ++table->in_use->status_var.ha_multi_range_read_init_count;
  }

  // HA_MRR_NO_ASSOCIATION is only set for range plans, and not for BKA. We
  // make use of this fact to determine whether the keys we obtain are sorted
  // or not.
  //
  // TODO: For reverse cf, we just have to pass in the keys to MultiGet in
  // reverse order. Doing this allows to save an std::sort on the rocksdb side.
  mrr_sorted_mode = (mode & HA_MRR_NO_ASSOCIATION) &&
                    active_index == table->s->primary_key &&
                    !m_pk_descr->m_is_reverse_cf;

  if (active_index == table->s->primary_key) {
    // ICP is not supported for PK, so we don't expect that BKA's variant
    // of ICP would be used:
    DBUG_ASSERT(!mrr_funcs.skip_index_tuple);
    mrr_used_cpk = true;
    mrr_rowid_reader =
        new Mrr_pk_scan_rowid_source(this, seq_init_param, n_ranges, mode);
  } else {
    mrr_used_cpk = false;
    auto reader = new Mrr_sec_key_rowid_source(this);
    reader->init(seq, seq_init_param, n_ranges, mode);
    mrr_rowid_reader = reader;
  }

  res = mrr_fill_buffer();

  // note: here, we must NOT return HA_ERR_END_OF_FILE even if we know there
  // are no matches. We should return 0 here and return HA_ERR_END_OF_FILE
  // from the first multi_range_read_next() call.
  if (res == HA_ERR_END_OF_FILE) {
    res = 0;
  }

  return res;
}

// Return the amount of buffer space that MRR scan requires for each record
// returned
uint ha_rocksdb::mrr_get_length_per_rec() {
  return sizeof(rocksdb::Slice) + sizeof(rocksdb::Status) +
         sizeof(rocksdb::PinnableSlice) +
         sizeof(char *) +  // this for KEY_MULTI_RANGE::ptr
         m_pk_descr->max_storage_fmt_length();
}

template <typename T>
void align_ptr(char **p) {
  if ((reinterpret_cast<std::uintptr_t>(*p)) % alignof(T)) {
    *p += alignof(T) - (reinterpret_cast<std::uintptr_t>(*p)) % alignof(T);
  }
}

/*
  We've got a buffer in mrr_buf, and in order to call RocksDB's MultiGet, we
  need to use this space to construct several arrays of the same size N:

    rocksdb::Slice[N]         - lookup keys
    rocksdb::Status[N]        - return statuses
    rocksdb::PinnableSlice[N] - return rows (*)
    char*[N]                  - "ptr" value of KEY_MULTI_RANGE. This tells the
                                SQL layer which lookup key the returned record
                                matches with (**)
    {PK lookup value}[N]      - The rowid (Primary Key) to lookup. The
                                corresponding rocksdb::Slice object points to
                                this key.

  (*) The memory for rows is allocated somewhere inside RocksDB, there's no
      way to make it use the user-supplied buffer.
  (**) The engine could specify HA_MRR_NO_ASSOCIATION which would mean "we
      cannot tell which key the returned records match" we don't do this.

  The PK lookup value is in mem-comparable encoding. It may have variable
  length (this is the case when table's PRIMARY KEY has VARCHAR() columns).
  Currently, we optimize for fixed-size primary keys and consume
  m_pk_descr->max_storage_fmt_length() bytes for each lookup value. One can
  develop a solution for variable-length PKs but this is not a priority.

  Note that the buffer may be much larger than necessary. For range scans,
  @@rnd_buffer_size=256K is passed, even if there will be only a few lookup
  values.
*/
int ha_rocksdb::mrr_fill_buffer() {
  mrr_free_rows();
  mrr_read_index = 0;

  // This should agree with the code in mrr_get_length_per_rec():
  ssize_t element_size = sizeof(rocksdb::Slice) + sizeof(rocksdb::Status) +
                         sizeof(rocksdb::PinnableSlice) +
                         sizeof(char *) +  // this for KEY_MULTI_RANGE::ptr
                         m_pk_descr->max_storage_fmt_length();

  // The buffer has space for this many elements:
  ssize_t n_elements = (mrr_buf.buffer_end - mrr_buf.buffer) / element_size;

  THD *thd = table->in_use;
  ssize_t elements_limit = THDVAR(thd, mrr_batch_size);
  n_elements = std::min(n_elements, elements_limit);

  if (n_elements < 1) {
    // We shouldn't get here as multi_range_read_init() has logic to fall back
    // to the default MRR implementation in this case.
    DBUG_ASSERT(0);
    return HA_ERR_INTERNAL_ERROR;
  }

  char *buf = (char *)mrr_buf.buffer;

  align_ptr<rocksdb::Slice>(&buf);
  mrr_keys = (rocksdb::Slice *)buf;
  buf += sizeof(rocksdb::Slice) * n_elements;

  align_ptr<rocksdb::Status>(&buf);
  mrr_statuses = (rocksdb::Status *)buf;
  buf += sizeof(rocksdb::Status) * n_elements;

  align_ptr<rocksdb::PinnableSlice>(&buf);
  mrr_values = (rocksdb::PinnableSlice *)buf;
  buf += sizeof(rocksdb::PinnableSlice) * n_elements;

  align_ptr<char *>(&buf);
  mrr_range_ptrs = (char **)buf;
  buf += sizeof(char *) * n_elements;

  if (buf + m_pk_descr->max_storage_fmt_length() >=
      (char *)mrr_buf.buffer_end) {
    // a VERY unlikely scenario:  we were given a really small buffer,
    // (probably for just one rowid), and also we had to use some bytes for
    // alignment. As a result, there's no buffer space left to hold even one
    // rowid. Return an error immediately to avoid looping.
    DBUG_ASSERT(0);
    return HA_ERR_INTERNAL_ERROR;  // error
  }

  ssize_t elem = 0;

  mrr_n_elements = elem;
  int key_size;
  char *range_ptr;
  int err;
  while ((err = mrr_rowid_reader->get_next_rowid((uchar *)buf, &key_size,
                                                 &range_ptr)) == 0) {
    DEBUG_SYNC(table->in_use, "rocksdb.mrr_fill_buffer.loop");
    if (table->in_use->killed) {
      return HA_ERR_QUERY_INTERRUPTED;
    }

    new (&mrr_keys[elem]) rocksdb::Slice(buf, key_size);
    new (&mrr_statuses[elem]) rocksdb::Status;
    new (&mrr_values[elem]) rocksdb::PinnableSlice;
    mrr_range_ptrs[elem] = range_ptr;
    buf += key_size;

    elem++;
    mrr_n_elements = elem;

    if ((elem == n_elements) || (buf + m_pk_descr->max_storage_fmt_length() >=
                                 (char *)mrr_buf.buffer_end)) {
      // No more buffer space
      break;
    }
  }

  if (err && err != HA_ERR_END_OF_FILE) {
    return err;
  }

  if (mrr_n_elements == 0) {
    return HA_ERR_END_OF_FILE;  // nothing to scan
  }

  Rdb_transaction *const tx = get_or_create_tx(table->in_use);

  /* TODO - row stats are gone in 8.0
  if (active_index == table->s->primary_key) {
    stats.rows_requested += mrr_n_elements;
  }
  */

  tx->multi_get(m_pk_descr->get_cf(), mrr_n_elements, mrr_keys, mrr_values,
                mrr_statuses, mrr_sorted_mode);

  return 0;
}

void ha_rocksdb::mrr_free() {
  // Free everything
  if (mrr_enabled_keyread) {
    m_keyread_only = false;
    mrr_enabled_keyread = false;
  }
  mrr_free_rows();
  delete mrr_rowid_reader;
  mrr_rowid_reader = nullptr;
}

void ha_rocksdb::mrr_free_rows() {
  for (ssize_t i = 0; i < mrr_n_elements; i++) {
    mrr_values[i].~PinnableSlice();
    mrr_statuses[i].~Status();
    // no need to free mrr_keys
  }

  // There could be rows that MultiGet has returned but MyRocks hasn't
  // returned to the SQL layer (typically due to LIMIT clause)
  // Count them in in "rows_read" anyway. (This is only necessary when using
  // clustered PK. When using a secondary key, the index-only part of the scan
  // that collects the rowids has caused all counters to be incremented)

  /* TODO - row stats are gone in 8.0
  if (mrr_used_cpk && mrr_n_elements) {
    stats.rows_read += mrr_n_elements - mrr_read_index;
  }
  */

  mrr_n_elements = 0;
  // We can't rely on the data from HANDLER_BUFFER once the scan is over, so:
  mrr_values = nullptr;
}

int ha_rocksdb::multi_range_read_next(char **range_info) {
  check_build_decoder();

  if (mrr_uses_default_impl) {
    return handler::multi_range_read_next(range_info);
  }

  Rdb_transaction *tx = get_tx_from_thd(table->in_use);
  int rc;

  while (true) {
    while (true) {
      if (table->in_use->killed) return HA_ERR_QUERY_INTERRUPTED;

      if (mrr_read_index >= mrr_n_elements) {
        if (mrr_rowid_reader->eof() || mrr_n_elements == 0) {
          table->m_status = STATUS_NOT_FOUND;  // not sure if this is necessary?
          mrr_free_rows();
          return HA_ERR_END_OF_FILE;
        }

        if ((rc = mrr_fill_buffer())) {
          if (rc == HA_ERR_END_OF_FILE) table->m_status = STATUS_NOT_FOUND;
          return rc;
        }
      }
      // If we found a status that has a row, leave the loop
      if (mrr_statuses[mrr_read_index].ok()) break;

      // Skip the NotFound errors, return any other error to the SQL layer
      if (!mrr_statuses[mrr_read_index].IsNotFound())
        return rdb_error_to_mysql(mrr_statuses[mrr_read_index]);

      mrr_read_index++;
    }
    size_t cur_key = mrr_read_index++;

    const rocksdb::Slice &rowkey = mrr_keys[cur_key];

    if (mrr_funcs.skip_record &&
        mrr_funcs.skip_record(mrr_iter, mrr_range_ptrs[cur_key],
                              (uchar *)rowkey.data())) {
      rc = HA_ERR_END_OF_FILE;
      continue;
    }

    m_last_rowkey.copy((const char *)rowkey.data(), rowkey.size(),
                       &my_charset_bin);

    *range_info = mrr_range_ptrs[cur_key];

    m_retrieved_record.Reset();
    m_retrieved_record.PinSlice(mrr_values[cur_key], &mrr_values[cur_key]);

    /* If we found the record, but it's expired, pretend we didn't find it.  */
    if (m_pk_descr->has_ttl() &&
        rdb_should_hide_ttl_rec(*m_pk_descr, m_retrieved_record, tx)) {
      continue;
    }

    rc = convert_record_from_storage_format(&rowkey, table->record[0]);

    // When using a secondary index, the scan on secondary index increments the
    // count
    if (active_index == table->s->primary_key) {
      /* TODO - row stats are gone in 8.0
      stats.rows_read++; */
      update_row_stats(ROWS_READ);
    }
    break;
  }
  table->m_status = rc ? STATUS_NOT_FOUND : 0;
  return rc;
}

static bool parse_fault_injection_file_type(const std::string &type_str,
                                            rocksdb::FileType *type) {
  if (type_str == "kWalFile") {
    *type = rocksdb::FileType::kWalFile;
    return false;
  } else if (type_str == "kTableFile") {
    *type = rocksdb::FileType::kTableFile;
    return false;
  } else if (type_str == "kDescriptorFile") {
    *type = rocksdb::FileType::kDescriptorFile;
    return false;
  }
  return true;
}

static bool parse_fault_injection_params(
    bool *retryable, uint32_t *failure_ratio,
    std::vector<rocksdb::FileType> *types) {
  rapidjson::Document doc;
  rapidjson::ParseResult ok = doc.Parse(opt_rocksdb_fault_injection_options);
  if (!ok) {
    // NO_LINT_DEBUG
    sql_print_error(
        "RocksDB: Parse error (errcode=%d offset=%d)  "
        "rocksdb_fault_injection_options=%s",
        ok.Code(), ok.Offset(), opt_rocksdb_fault_injection_options);
    return true;
  }

  auto retry_it = doc.FindMember("retry");
  auto fr_it = doc.FindMember("failure_ratio");
  auto ft_it = doc.FindMember("filetypes");

  if (retry_it == doc.MemberEnd() || fr_it == doc.MemberEnd() ||
      ft_it == doc.MemberEnd()) {
    // NO_LINT_DEBUG
    sql_print_error(
        "RocksDB: rocksdb_fault_injection_options=%s schema not valid",
        opt_rocksdb_fault_injection_options);
    return true;
  }

  if (!retry_it->value.IsBool() || !fr_it->value.IsInt() ||
      !ft_it->value.IsArray()) {
    // NO_LINT_DEBUG
    sql_print_error(
        "RocksDB: rocksdb_fault_injection_options=%s schema not valid (wrong "
        "types)",
        opt_rocksdb_fault_injection_options);
    return true;
  }

  *retryable = retry_it->value.GetBool();
  *failure_ratio = fr_it->value.GetInt();
  for (rapidjson::SizeType i = 0; i < ft_it->value.Size(); i++) {
    rocksdb::FileType type;
    if (!ft_it->value[i].IsString() ||
        parse_fault_injection_file_type(ft_it->value[i].GetString(), &type)) {
      // NO_LINT_DEBUG
      sql_print_error(
          "RocksDB: Wrong filetype = %s to  "
          "rocksdb_fault_injection_options=%s",
          ft_it->value[i].IsString() ? ft_it->value[i].GetString()
                                     : "(wrong type)",
          opt_rocksdb_fault_injection_options);
      return true;
    }
    types->push_back(type);
  }

  return false;
}

}  // namespace myrocks

/*
  Register the storage engine plugin outside of myrocks namespace
  so that mysql_declare_plugin does not get confused when it does
  its name generation.
*/

struct st_mysql_storage_engine rocksdb_storage_engine = {
    MYSQL_HANDLERTON_INTERFACE_VERSION};

mysql_declare_plugin(rocksdb_se){
    MYSQL_STORAGE_ENGINE_PLUGIN,       /* Plugin Type */
    &rocksdb_storage_engine,           /* Plugin Descriptor */
    "ROCKSDB",                         /* Plugin Name */
    "Monty Program Ab",                /* Plugin Author */
    "RocksDB storage engine",          /* Plugin Description */
    PLUGIN_LICENSE_GPL,                /* Plugin Licence */
    myrocks::rocksdb_init_func,        /* Plugin Entry Point */
    nullptr,                           /* Plugin Uninstall */
    myrocks::rocksdb_done_func,        /* Plugin Deinitializer */
    0x0001,                            /* version number (0.1) */
    myrocks::rocksdb_status_vars,      /* status variables */
    myrocks::rocksdb_system_variables, /* system variables */
    nullptr,                           /* config options */
    0,                                 /* flags */
},
    myrocks::rdb_i_s_cfstats, myrocks::rdb_i_s_dbstats,
    myrocks::rdb_i_s_perf_context, myrocks::rdb_i_s_perf_context_global,
    myrocks::rdb_i_s_cfoptions, myrocks::rdb_i_s_compact_stats,
    myrocks::rdb_i_s_active_compact_stats, myrocks::rdb_i_s_compact_history,
    myrocks::rdb_i_s_global_info, myrocks::rdb_i_s_ddl,
    myrocks::rdb_i_s_sst_props, myrocks::rdb_i_s_index_file_map,
    myrocks::rdb_i_s_lock_info, myrocks::rdb_i_s_trx_info,
    myrocks::rdb_i_s_deadlock_info,
    myrocks::rdb_i_s_bypass_rejected_query_history mysql_declare_plugin_end;
