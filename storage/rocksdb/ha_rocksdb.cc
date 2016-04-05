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
#include "./rdb_utils.h"

#ifdef TARGET_OS_LINUX
extern my_bool cachedev_enabled;
#endif /* TARGET_OS_LINUX */

///////////////////////////////////////////////////////////
// Internal MySQL APIs not exposed in any header.
///////////////////////////////////////////////////////////

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

///////////////////////////////////////////////////////////
// Singleton manager objects
///////////////////////////////////////////////////////////

static Rdb_dict_manager    rdb_dict_manager;
static Rdb_cf_manager      rdb_cf_manager;
static Rdb_ddl_manager     rdb_ddl_manager;
static Rdb_binlog_manager  rdb_binlog_manager;


///////////////////////////////////////////////////////////
// Utility functions
///////////////////////////////////////////////////////////

static std::vector<std::string> rdb_split_string(const std::string& input,
                                                 char delimiter);
#ifndef NDEBUG
void rdb_dbug_dump_database(rocksdb::DB *db);
#endif  // NDEBUG
static bool rdb_can_use_bloom_filter(Rdb_key_def *keydef,
                                     const rocksdb::Slice &eq_cond,
                                     const bool use_all_keys,
                                     bool is_ascending);
static rocksdb::DBOptions rdb_init_db_options()
{
  rocksdb::DBOptions o;

  o.create_if_missing = true;
  o.listeners.push_back(std::make_shared<Rdb_event_listener>(&rdb_ddl_manager));
  o.info_log_level = rocksdb::InfoLogLevel::INFO_LEVEL;
  o.max_subcompactions = RDB_DEFAULT_SUBCOMPACTIONS;

  return o;
}


///////////////////////////////////////////////////////////
// Parameters and settings
///////////////////////////////////////////////////////////

extern const longlong RDB_WRITE_BUFFER_SIZE_DEFAULT;
static char * rdb_default_cf_options;
static char * rdb_override_cf_options;
static Rdb_cf_opt_registry rdb_cf_registry;

static rocksdb::DBOptions rdb_db_options = rdb_init_db_options();
static rocksdb::BlockBasedTableOptions rdb_table_options;
static std::shared_ptr<rocksdb::RateLimiter> rdb_rate_limiter;


///////////////////////////////////////////////////////////
// Rdb_handlerton is a wrapper around my_core::handlerton
// encapsulating the C-style virtual function table that
// my_core::handlerton requires initializing.
// NB: The memory for this type of objects gets allocated by
//     MySQL plugin management so don't add any extra member
//     variables pr virtual funtions, or anyting that changes
//     the sizeof for this class (see static_assert below).
///////////////////////////////////////////////////////////

namespace  // anonymous namespace = not visible outside this source file
{
class Rdb_handlerton : public my_core::handlerton
{
 public:
  // Initialize the C-style virtual function table required to override
  // base class my_core::handlerton abstract behavior.
  void init(void)
  {
    // Validate the assumption that this class can safely be mapped to
    // memory allocated by MySQL as a my_core::handlerton.
    static_assert(sizeof(Rdb_handlerton) == sizeof(my_core::handlerton),
                  "Assuming that Rdb_handlerton has no extra fields.");

    this->state=                      SHOW_OPTION_YES;
    this->create=                     hton_create_handler;
    this->close_connection=           hton_close_connection;
    this->prepare=                    hton_prepare;
    this->commit_by_xid=              hton_commit_by_xid;
    this->rollback_by_xid=            hton_rollback_by_xid;
    this->recover=                    hton_recover;
    this->commit=                     hton_commit;
    this->rollback=                   hton_rollback;
    this->db_type=                    DB_TYPE_ROCKSDB;
    this->show_status=                hton_show_status;
    this->start_consistent_snapshot=  hton_start_tx_and_assign_read_view;
    this->savepoint_set=              hton_savepoint;
    this->savepoint_rollback=         hton_rollback_to_savepoint;
    this->savepoint_rollback_can_release_mdl=
                                    hton_rollback_to_savepoint_can_release_mdl;
    this->update_table_stats=         hton_update_table_stats;

    this->flags= HTON_TEMPORARY_NOT_SUPPORTED |
                 HTON_SUPPORTS_EXTENDED_KEYS  |
                 HTON_CAN_RECREATE;
  }

  // Deleting the methods we don't want to allow.
  Rdb_handlerton(Rdb_handlerton const&) = delete;
  void operator=(Rdb_handlerton const&) = delete;

 private:
  // Make sure that this class cannot be instantiated. The only way to get one
  // is via a type-cast from base class.
  Rdb_handlerton() {}

  // Implement the C-style virtual methods required to override
  // base class my_core::handlerton abstract behavior.
  static my_core::handler *hton_create_handler(my_core::handlerton *hton,
                                               my_core::TABLE_SHARE *table,
                                               my_core::MEM_ROOT *mem_root);
  static int hton_close_connection(handlerton* hton __attribute__((__unused__)),
                                   my_core::THD* thd);
  static int hton_prepare(handlerton* hton __attribute__((__unused__)),
                          my_core::THD* thd, bool prepare_tx,
                          bool async __attribute__((__unused__)));
  static int hton_commit_by_xid(handlerton* hton __attribute__((__unused__)),
                                my_core::XID* xid __attribute__((__unused__)));
  static int hton_rollback_by_xid(
    handlerton* hton __attribute__((__unused__)),
    my_core::XID* xid __attribute__((__unused__)));
  static int hton_recover(handlerton* hton __attribute__((__unused__)),
                          XID* xid_list __attribute__((__unused__)),
                          uint len __attribute__((__unused__)),
                          char* binlog_file, my_off_t* binlog_pos);
  static int hton_commit(handlerton* hton __attribute__((__unused__)),
                         my_core::THD* thd, bool commit_tx, bool);
  static int hton_rollback(handlerton* hton __attribute__((__unused__)),
                           my_core::THD* thd, bool rollback_tx);
  static bool hton_show_status(handlerton*       hton,
                               my_core::THD*     thd,
                               stat_print_fn*    stat_print_fn,
                               enum ha_stat_type stat_type);
  static int hton_start_tx_and_assign_read_view(
    my_core::handlerton*  hton,
    my_core::THD*         thd,
    char*                 binlog_file,
    ulonglong*            binlog_pos,
    char**                gtids_executed,
    int*                  gtids_length);
  static int hton_savepoint(handlerton *hton __attribute__((__unused__)),
                            my_core::THD *thd __attribute__((__unused__)),
                            void *savepoint __attribute__((__unused__)));
  static int hton_rollback_to_savepoint(
    handlerton *hton __attribute__((__unused__)),
    my_core::THD *thd, void *savepoint);
  static bool hton_rollback_to_savepoint_can_release_mdl(
    handlerton *hton __attribute__((__unused__)),
    my_core::THD *thd __attribute__((__unused__)));
  static void hton_update_table_stats(
    /* per-table stats callback */
    void (*callback_fn)(const char* db, const char* tbl, bool is_partition,
      my_io_perf_t* r, my_io_perf_t* w, my_io_perf_t* r_blob,
      my_io_perf_t* r_primary, my_io_perf_t* r_secondary,
      page_stats_t *page_stats, comp_stats_t *comp_stats,
      int n_lock_wait, int n_lock_wait_timeout,
      const char* engine));
};
}  // anonymous namespace

///////////////////////////////////////////////////////////
// MyRocks Stoarge Engine Globals
///////////////////////////////////////////////////////////

static Rdb_handlerton *rdb_handlerton= nullptr;
static rocksdb::TransactionDB *rdb_rocksdb_db= nullptr;
static Rdb_global_stats rdb_global_stats;
static Rdb_export_stats rdb_export_stats;
static std::shared_ptr<rocksdb::Statistics> rdb_rocksdb_stats;
static Rdb_perf_context_shared rdb_global_perf_context;
static std::unique_ptr<rocksdb::Env> rdb_flashcache_env;
static mysql_mutex_t rdb_sysvar_mutex;
static std::shared_ptr<Rdb_tbl_prop_coll_factory> rdb_prop_coll_factory;
static mysql_mutex_t rdb_collation_ex_list_mutex;
// List of table names (using regex) that are exceptions to the strict
// collation check requirement.
static std::vector<std::string> rdb_collation_exception_list;


///////////////////////////////////////////////////////////
// Singleton accessors
///////////////////////////////////////////////////////////

rocksdb::DB *rdb_get_rocksdb_db()
{
  return rdb_rocksdb_db;
}

Rdb_cf_manager& rdb_get_cf_manager()
{
  return rdb_cf_manager;
}

rocksdb::BlockBasedTableOptions& rdb_get_table_options()
{
  return rdb_table_options;
}

Rdb_dict_manager* rdb_get_dict_manager(void)
{
  return &rdb_dict_manager;
}

Rdb_ddl_manager* rdb_get_ddl_manager(void)
{
  return &rdb_ddl_manager;
}

Rdb_binlog_manager* rdb_get_binlog_manager(void)
{
  return &rdb_binlog_manager;
}


///////////////////////////////////////////////////////////
// Hash map of table_name to open table handler
///////////////////////////////////////////////////////////

namespace  // anonymous namespace = not visible outside this source file
{
struct Rdb_open_tables
{
  /* Hash used to track the handlers of open tables */
  my_core::HASH m_hash;
  /* The mutex used to protect the hash */
  mutable mysql_mutex_t m_mutex;

  void init_hash(void)
  {
    (void) my_hash_init(&m_hash, system_charset_info, 32, 0, 0,
                        (my_hash_get_key) Rdb_open_tables::get_hash_key,
                        0, 0);
  }

  Rdb_table_handler* get_table_handler(const char *table_name);
  std::vector<std::string> get_table_names(void) const;
  int free_table_handler(Rdb_table_handler *table_handler);

  static uchar* get_hash_key(Rdb_table_handler *table_handler,
                             size_t *length,
                             my_bool not_used __attribute__((__unused__)));

  // Deleting the methods we don't want to allow.
  void operator=(Rdb_open_tables const&) = delete;
};
}  // anonymous namespace

static Rdb_open_tables rdb_open_tables;


///////////////////////////////////////////////////////////
// Background thread implementation
///////////////////////////////////////////////////////////

namespace  // anonymous namespace = not visible outside this source file
{
struct Rdb_bg_thread_control
{
  bool m_stop= false;
  bool m_save_stats= false;
};

struct Rdb_background_thread
{
  pthread_t             m_thread_handle;
  mysql_mutex_t         m_mutex;
  mysql_mutex_t         m_stop_cond_mutex;
  mysql_cond_t          m_stop_cond;
  Rdb_bg_thread_control m_control;
  my_bool               m_pause_work_sysvar= 0;

  static void* thread_fn(void* ptr __attribute__((__unused__)));
};
}  // anonymous namespace

static Rdb_background_thread rdb_bg_thd;


///////////////////////////////////////////////////////////
// Drop index thread implementation
///////////////////////////////////////////////////////////

namespace  // anonymous namespace = not visible outside this source file
{
struct Rdb_drop_index_thread
{
  pthread_t     m_thread_handle;
  mysql_mutex_t m_mutex;
  mysql_mutex_t m_interrupt_mutex;
  mysql_cond_t  m_interrupt_cond;
  bool          m_stop;

  static void* thread_fn(void* ptr __attribute__((__unused__)));
  static void wakeup(my_core::THD* thd,
                     struct st_mysql_sys_var* var,
                     void* var_ptr,
                     const void* save);

  void signal(bool stop_thread= false);
};
}  // anonymous namespace

static Rdb_drop_index_thread rdb_drop_index_thd;

void Rdb_drop_index_thread::wakeup(
  my_core::THD* thd __attribute__((__unused__)),
  struct st_mysql_sys_var* var __attribute__((__unused__)),
  void* var_ptr __attribute__((__unused__)),
  const void* save)
{
  if (*static_cast<const bool*>(save)) {
    rdb_drop_index_thd.signal();
  }
}


static const char* const RDB_ERRSTR_ROLLBACK_ONLY
  = "This transaction was rolled back and cannot be "
    "committed. Only supported operation is to roll it back, "
    "so all pending changes will be discarded. "
    "Please restart another transaction.";


///////////////////////////////////////////////////////////
// Utility functions implementation
///////////////////////////////////////////////////////////

static void rdb_flush_all_memtables()
{
  Rdb_cf_manager& rdb_cf_manager = rdb_get_cf_manager();
  for (auto cf_handle : rdb_cf_manager.get_all_cf()) {
    rdb_rocksdb_db->Flush(rocksdb::FlushOptions(), cf_handle);
  }
}


static std::string rdb_normalize_dir(std::string dir)
{
  while (dir.size() > 0 &&
          dir.back() == '/') {
    dir.resize(dir.size() - 1);
  }
  return dir;
}

static int
rdb_create_checkpoint(my_core::THD* thd __attribute__((__unused__)),
                      struct st_mysql_sys_var* var __attribute__((__unused__)),
                      void* save __attribute__((__unused__)),
                      struct st_mysql_value* value)
{
  char buf[512];
  int len = sizeof(buf);
  const char* checkpoint_dir_raw = value->val_str(value, buf, &len);
  if (checkpoint_dir_raw) {
    if (rdb_rocksdb_db != nullptr) {
      std::string checkpoint_dir = rdb_normalize_dir(checkpoint_dir_raw);
      // NO_LINT_DEBUG
      sql_print_information("RocksDB: creating checkpoint in directory : %s\n",
          checkpoint_dir.c_str());
      rocksdb::Checkpoint* checkpoint;
      auto status = rocksdb::Checkpoint::Create(rdb_rocksdb_db, &checkpoint);
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
rdb_create_checkpoint_stub(
  my_core::THD* thd __attribute__((__unused__)),
  struct st_mysql_sys_var* var __attribute__((__unused__)),
  void* var_ptr __attribute__((__unused__)),
  const void* save __attribute__((__unused__)))
{
}

static void
rdb_set_force_flush_memtable_sysvar(
  my_core::THD* thd __attribute__((__unused__)),
  struct st_mysql_sys_var* var __attribute__((__unused__)),
  void* var_ptr __attribute__((__unused__)),
  const void* save __attribute__((__unused__)))
{
  sql_print_information("RocksDB: Manual memtable flush\n");
  rdb_flush_all_memtables();
}

static void
rdb_set_compaction_options_sysvar(my_core::THD* thd,
                                  struct st_mysql_sys_var* var,
                                  void* var_ptr,
                                  const void* save);

static void
rdb_set_table_stats_sampling_pct_sysvar(my_core::THD* thd,
                                        struct st_mysql_sys_var* var,
                                        void* var_ptr,
                                        const void* save);

static void
rdb_set_rate_limiter_bps_sysvar(my_core::THD*            thd,
                                struct st_mysql_sys_var* var,
                                void*                    var_ptr,
                                const void*              save);

static void
rdb_set_collation_exception_list(my_core::THD*            thd,
                                 struct st_mysql_sys_var* var,
                                 void*                    var_ptr,
                                 const void*              save);

static
void rdb_set_pause_bg_work_sysvar(my_core::THD*            thd,
                                  struct st_mysql_sys_var* var,
                                  void*                    var_ptr,
                                  const void*              save);


//////////////////////////////////////////////////////////////////////////////
// Storage Engine options definitions & sysvars
//////////////////////////////////////////////////////////////////////////////

static long long  // NOLINT(runtime/int)
    rdb_block_cache_size_sysvar;
/* Use unsigned long long instead of uint64_t because of MySQL compatibility */
static unsigned long long  // NOLINT(runtime/int)
    rdb_rate_limiter_bps_sysvar;
static uint64_t rdb_info_log_level_sysvar;
static char * rdb_wal_dir_sysvar;
static uint64_t rdb_index_type_sysvar;
static char rdb_background_sync_sysvar;
static uint32_t rdb_debug_optimizer_n_rows_sysvar;
static my_bool rdb_dbg_optimizer_no_zero_cardinality_sysvar;
static uint32_t rdb_perf_context_level_sysvar;
static uint32_t rdb_wal_recovery_mode_sysvar;
static uint32_t rdb_access_hint_on_compact_start_sysvar;
static char * rdb_compact_cf_name_sysvar;
static char * rdb_checkpoint_name_sysvar;
static my_bool rdb_signal_drop_idx_thd_sysvar;
static my_bool rdb_strict_collation_check_sysvar= 1;
static char * rdb_strict_collation_exceptions_sysvar;
static my_bool rdb_collect_sst_properties_sysvar= 1;
static my_bool rdb_force_flush_memtable_sysvar= 0;
static uint64_t rdb_num_stat_computes_showvar= 0;
static uint32_t rdb_secs_between_stat_computes_sysvar= 3600;
static long long  // NOLINT(runtime/int)
    rdb_compact_seq_del_count_sysvar= 0l;
static long long  // NOLINT(runtime/int)
    rdb_compact_seq_del_window_sysvar= 0l;
static long long  // NOLINT(runtime/int)
    rdb_compact_seq_del_file_size_sysvar= 0l;
static uint32_t rdb_validate_tables_sysvar= 1;
static char * rdb_datadir_sysvar;
static uint32_t rdb_table_stats_sampling_pct_sysvar;

static const char* rdb_info_log_level_names[] =
{
  "debug_level",
  "info_level",
  "warn_level",
  "error_level",
  "fatal_level",
  NullS
};
/* This enum needs to be kept up to date with rocksdb::InfoLogLevel */
static_assert(
  array_elements(rdb_info_log_level_names) == rocksdb::NUM_INFO_LOG_LEVELS,
  "Assuming that longlong is 8 bytes.");

static TYPELIB rdb_info_log_level_typelib = {
  array_elements(rdb_info_log_level_names) - 1,
  "rdb_info_log_level_typelib",
  rdb_info_log_level_names,
  nullptr
};


static void rdb_set_compact_column_family_sysvar(
  my_core::THD* thd __attribute__((__unused__)),
  struct st_mysql_sys_var* var __attribute__((__unused__)),
  void* var_ptr __attribute__((__unused__)),
  const void* save)
{
  if (const char* cf = *static_cast<const char*const*>(save)) {
    bool is_automatic;
    auto cfh = rdb_cf_manager.get_cf(cf, nullptr, nullptr, &is_automatic);
    if (cfh != nullptr && rdb_rocksdb_db != nullptr) {
      sql_print_information("RocksDB: Manual compaction of column family: %s\n",
                            cf);
      rdb_rocksdb_db->CompactRange(rocksdb::CompactRangeOptions(), cfh,
                                   nullptr, nullptr);
    }
  }
}


static void rdb_set_compaction_options_sysvar(
  my_core::THD* thd __attribute__((__unused__)),
  struct st_mysql_sys_var* var __attribute__((__unused__)),
  void* var_ptr,
  const void* save)
{
  if (var_ptr && save) {
    *reinterpret_cast<uint64_t*>(var_ptr) = *(const uint64_t*) save;
  }
  Rdb_compact_params params = {
    (uint64_t)rdb_compact_seq_del_count_sysvar,
    (uint64_t)rdb_compact_seq_del_window_sysvar,
    (uint64_t)rdb_compact_seq_del_file_size_sysvar
  };
  if (rdb_prop_coll_factory) {
    rdb_prop_coll_factory->SetCompactionParams(params);
  }
}


static void rdb_set_table_stats_sampling_pct_sysvar(
  my_core::THD* thd __attribute__((__unused__)),
  struct st_mysql_sys_var* var __attribute__((__unused__)),
  void* var_ptr __attribute__((__unused__)),
  const void* save)
{
  mysql_mutex_lock(&rdb_sysvar_mutex);

  uint32_t new_val = *static_cast<const uint32_t*>(save);

  if (new_val != rdb_table_stats_sampling_pct_sysvar) {
    rdb_table_stats_sampling_pct_sysvar = new_val;

    if (rdb_prop_coll_factory) {
      rdb_prop_coll_factory->SetTableStatsSamplingPct(
        rdb_table_stats_sampling_pct_sysvar);
    }
  }

  mysql_mutex_unlock(&rdb_sysvar_mutex);
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

static void rdb_set_rate_limiter_bps_sysvar(
  my_core::THD* thd __attribute__((__unused__)),
  struct st_mysql_sys_var* var __attribute__((__unused__)),
  void* var_ptr __attribute__((__unused__)),
  const void* save)
{
  uint64_t new_val = *static_cast<const uint64_t*>(save);
  if (new_val == 0 || rdb_rate_limiter_bps_sysvar == 0)
  {
    /*
      If a rdb_rate_limiter was not enabled at startup we can't change it nor
      can we disable it if one was created at startup
    */
    push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                        ER_WRONG_ARGUMENTS,
                        "RocksDB: rocksdb_rate_limiter_bytes_per_sec cannot "
                        "be dynamically changed to or from 0.  Do a clean "
                        "shutdown if you want to change it from or to 0.");
  }
  else if (new_val != rdb_rate_limiter_bps_sysvar)
  {
    /* Apply the new value to the rate limiter and store it locally */
    DBUG_ASSERT(rdb_rate_limiter != nullptr);
    rdb_rate_limiter_bps_sysvar = new_val;
    rdb_rate_limiter->SetBytesPerSecond(new_val);
  }
}


static void rdb_set_info_log_level_sysvar(
  my_core::THD* thd __attribute__((__unused__)),
  struct st_mysql_sys_var* var __attribute__((__unused__)),
  void* var_ptr __attribute__((__unused__)),
  const void* save)
{
  mysql_mutex_lock(&rdb_sysvar_mutex);
  rdb_info_log_level_sysvar = *static_cast<const uint64_t*>(save);
  rdb_db_options.info_log->SetInfoLogLevel(
      static_cast<const rocksdb::InfoLogLevel>(rdb_info_log_level_sysvar));
  mysql_mutex_unlock(&rdb_sysvar_mutex);
}


static const char* rdb_index_type_names[] = {
  "kBinarySearch",
  "kHashSearch",
  NullS
};

static TYPELIB rdb_index_type_typelib = {
  array_elements(rdb_index_type_names) - 1,
  "index_type_typelib",
  rdb_index_type_names,
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

static MYSQL_THDVAR_BOOL(rpl_lookup_rows, PLUGIN_VAR_RQCMDARG,
  "Lookup a row on replication slave", nullptr, nullptr, TRUE);

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
  *reinterpret_cast<my_bool*>(&rdb_db_options.create_if_missing),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::create_if_missing for RocksDB",
  nullptr, nullptr, rdb_db_options.create_if_missing);

static MYSQL_SYSVAR_BOOL(create_missing_column_families,
  *reinterpret_cast<my_bool*>(&rdb_db_options.create_missing_column_families),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::create_missing_column_families for RocksDB",
  nullptr, nullptr, rdb_db_options.create_missing_column_families);

static MYSQL_SYSVAR_BOOL(error_if_exists,
  *reinterpret_cast<my_bool*>(&rdb_db_options.error_if_exists),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::error_if_exists for RocksDB",
  nullptr, nullptr, rdb_db_options.error_if_exists);

static MYSQL_SYSVAR_BOOL(paranoid_checks,
  *reinterpret_cast<my_bool*>(&rdb_db_options.paranoid_checks),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::paranoid_checks for RocksDB",
  nullptr, nullptr, rdb_db_options.paranoid_checks);

static MYSQL_SYSVAR_ULONGLONG(rate_limiter_bytes_per_sec,
  rdb_rate_limiter_bps_sysvar,
  PLUGIN_VAR_RQCMDARG,
  "DBOptions::rate_limiter bytes_per_sec for RocksDB",
  nullptr, rdb_set_rate_limiter_bps_sysvar, /* default */ 0L,
  /* min */ 0L, /* max */ RDB_MAX_RATE_LIMITER_BPS, 0);

static MYSQL_SYSVAR_ENUM(info_log_level,
  rdb_info_log_level_sysvar,
  PLUGIN_VAR_RQCMDARG,
  "Filter level for info logs to be written mysqld error log. "
  "Valid values include 'debug_level', 'info_level', 'warn_level'"
  "'error_level' and 'fatal_level'.",
  nullptr, rdb_set_info_log_level_sysvar,
  rocksdb::InfoLogLevel::ERROR_LEVEL, &rdb_info_log_level_typelib);

static MYSQL_SYSVAR_UINT(perf_context_level,
  rdb_perf_context_level_sysvar,
  PLUGIN_VAR_RQCMDARG,
  "Perf Context Level for rocksdb internal timer stat collection",
  nullptr, nullptr, rocksdb::kDisable,
  /* min */ 0L, /* max */ UINT_MAX, 0);

static MYSQL_SYSVAR_UINT(wal_recovery_mode,
  rdb_wal_recovery_mode_sysvar,
  PLUGIN_VAR_RQCMDARG,
  "DBOptions::wal_recovery_mode for RocksDB",
  nullptr, nullptr, 2,
  /* min */ 0L, /* max */ 3, 0);

static MYSQL_SYSVAR_ULONG(compaction_readahead_size,
  rdb_db_options.compaction_readahead_size,
  PLUGIN_VAR_RQCMDARG,
  "DBOptions::compaction_readahead_size for RocksDB",
  nullptr, nullptr, rdb_db_options.compaction_readahead_size,
  /* min */ 0L, /* max */ ULONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(new_table_reader_for_compaction_inputs,
  *reinterpret_cast<my_bool*>
    (&rdb_db_options.new_table_reader_for_compaction_inputs),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::new_table_reader_for_compaction_inputs for RocksDB",
  nullptr, nullptr, rdb_db_options.new_table_reader_for_compaction_inputs);

static MYSQL_SYSVAR_UINT(access_hint_on_compaction_start,
  rdb_access_hint_on_compact_start_sysvar,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::access_hint_on_compaction_start for RocksDB",
  nullptr, nullptr, 1,
  /* min */ 0L, /* max */ 3, 0);

static MYSQL_SYSVAR_BOOL(allow_concurrent_memtable_write,
  *reinterpret_cast<my_bool*>(&rdb_db_options.allow_concurrent_memtable_write),
  PLUGIN_VAR_RQCMDARG,
  "DBOptions::allow_concurrent_memtable_write for RocksDB",
  nullptr, nullptr, rdb_db_options.allow_concurrent_memtable_write);

static MYSQL_SYSVAR_BOOL(enable_write_thread_adaptive_yield,
  *reinterpret_cast<my_bool*>(
    &rdb_db_options.enable_write_thread_adaptive_yield),
  PLUGIN_VAR_RQCMDARG,
  "DBOptions::enable_write_thread_adaptive_yield for RocksDB",
  nullptr, nullptr, rdb_db_options.enable_write_thread_adaptive_yield);

static MYSQL_SYSVAR_INT(max_open_files,
  rdb_db_options.max_open_files,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_open_files for RocksDB",
  nullptr, nullptr, rdb_db_options.max_open_files,
  /* min */ -1, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_ULONG(max_total_wal_size,
  rdb_db_options.max_total_wal_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_total_wal_size for RocksDB",
  nullptr, nullptr, rdb_db_options.max_total_wal_size,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(disabledatasync,
  *reinterpret_cast<my_bool*>(&rdb_db_options.disableDataSync),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::disableDataSync for RocksDB",
  nullptr, nullptr, rdb_db_options.disableDataSync);

static MYSQL_SYSVAR_BOOL(use_fsync,
  *reinterpret_cast<my_bool*>(&rdb_db_options.use_fsync),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::use_fsync for RocksDB",
  nullptr, nullptr, rdb_db_options.use_fsync);

static MYSQL_SYSVAR_STR(wal_dir, rdb_wal_dir_sysvar,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::wal_dir for RocksDB",
  nullptr, nullptr, rdb_db_options.wal_dir.c_str());

static MYSQL_SYSVAR_ULONG(delete_obsolete_files_period_micros,
  rdb_db_options.delete_obsolete_files_period_micros,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::delete_obsolete_files_period_micros for RocksDB",
  nullptr, nullptr, rdb_db_options.delete_obsolete_files_period_micros,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_INT(base_background_compactions,
  rdb_db_options.base_background_compactions,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::base_background_compactions for RocksDB",
  nullptr, nullptr, rdb_db_options.base_background_compactions,
  /* min */ -1, /* max */ RDB_MAX_BACKGROUND_COMPACTIONS, 0);

static MYSQL_SYSVAR_INT(max_background_compactions,
  rdb_db_options.max_background_compactions,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_background_compactions for RocksDB",
  nullptr, nullptr, rdb_db_options.max_background_compactions,
  /* min */ 1, /* max */ RDB_MAX_BACKGROUND_COMPACTIONS, 0);

static MYSQL_SYSVAR_INT(max_background_flushes,
  rdb_db_options.max_background_flushes,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_background_flushes for RocksDB",
  nullptr, nullptr, rdb_db_options.max_background_flushes,
  /* min */ 1, /* max */ RDB_MAX_BACKGROUND_FLUSHES, 0);

static MYSQL_SYSVAR_UINT(max_subcompactions,
  rdb_db_options.max_subcompactions,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_subcompactions for RocksDB",
  nullptr, nullptr, rdb_db_options.max_subcompactions,
  /* min */ 1, /* max */ RDB_MAX_SUBCOMPACTIONS, 0);

static MYSQL_SYSVAR_ULONG(max_log_file_size,
  rdb_db_options.max_log_file_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_log_file_size for RocksDB",
  nullptr, nullptr, rdb_db_options.max_log_file_size,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(log_file_time_to_roll,
  rdb_db_options.log_file_time_to_roll,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::log_file_time_to_roll for RocksDB",
  nullptr, nullptr, rdb_db_options.log_file_time_to_roll,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(keep_log_file_num,
  rdb_db_options.keep_log_file_num,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::keep_log_file_num for RocksDB",
  nullptr, nullptr, rdb_db_options.keep_log_file_num,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(max_manifest_file_size,
  rdb_db_options.max_manifest_file_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::max_manifest_file_size for RocksDB",
  nullptr, nullptr, rdb_db_options.max_manifest_file_size,
  /* min */ 0L, /* max */ ULONG_MAX, 0);

static MYSQL_SYSVAR_INT(table_cache_numshardbits,
  rdb_db_options.table_cache_numshardbits,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::table_cache_numshardbits for RocksDB",
  nullptr, nullptr, rdb_db_options.table_cache_numshardbits,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_ULONG(wal_ttl_seconds,
  rdb_db_options.WAL_ttl_seconds,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::WAL_ttl_seconds for RocksDB",
  nullptr, nullptr, rdb_db_options.WAL_ttl_seconds,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(wal_size_limit_mb,
  rdb_db_options.WAL_size_limit_MB,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::WAL_size_limit_MB for RocksDB",
  nullptr, nullptr, rdb_db_options.WAL_size_limit_MB,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(manifest_preallocation_size,
  rdb_db_options.manifest_preallocation_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::manifest_preallocation_size for RocksDB",
  nullptr, nullptr, rdb_db_options.manifest_preallocation_size,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(allow_os_buffer,
  *reinterpret_cast<my_bool*>(&rdb_db_options.allow_os_buffer),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::allow_os_buffer for RocksDB",
  nullptr, nullptr, rdb_db_options.allow_os_buffer);

static MYSQL_SYSVAR_BOOL(allow_mmap_reads,
  *reinterpret_cast<my_bool*>(&rdb_db_options.allow_mmap_reads),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::allow_mmap_reads for RocksDB",
  nullptr, nullptr, rdb_db_options.allow_mmap_reads);

static MYSQL_SYSVAR_BOOL(allow_mmap_writes,
  *reinterpret_cast<my_bool*>(&rdb_db_options.allow_mmap_writes),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::allow_mmap_writes for RocksDB",
  nullptr, nullptr, rdb_db_options.allow_mmap_writes);

static MYSQL_SYSVAR_BOOL(is_fd_close_on_exec,
  *reinterpret_cast<my_bool*>(&rdb_db_options.is_fd_close_on_exec),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::is_fd_close_on_exec for RocksDB",
  nullptr, nullptr, rdb_db_options.is_fd_close_on_exec);

static MYSQL_SYSVAR_UINT(stats_dump_period_sec,
  rdb_db_options.stats_dump_period_sec,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::stats_dump_period_sec for RocksDB",
  nullptr, nullptr, rdb_db_options.stats_dump_period_sec,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_BOOL(advise_random_on_open,
  *reinterpret_cast<my_bool*>(&rdb_db_options.advise_random_on_open),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::advise_random_on_open for RocksDB",
  nullptr, nullptr, rdb_db_options.advise_random_on_open);

static MYSQL_SYSVAR_ULONG(db_write_buffer_size,
  rdb_db_options.db_write_buffer_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::db_write_buffer_size for RocksDB",
  nullptr, nullptr, rdb_db_options.db_write_buffer_size,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(use_adaptive_mutex,
  *reinterpret_cast<my_bool*>(&rdb_db_options.use_adaptive_mutex),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::use_adaptive_mutex for RocksDB",
  nullptr, nullptr, rdb_db_options.use_adaptive_mutex);

static MYSQL_SYSVAR_ULONG(bytes_per_sync,
  rdb_db_options.bytes_per_sync,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::bytes_per_sync for RocksDB",
  nullptr, nullptr, rdb_db_options.bytes_per_sync,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_ULONG(wal_bytes_per_sync,
  rdb_db_options.wal_bytes_per_sync,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::wal_bytes_per_sync for RocksDB",
  nullptr, nullptr, rdb_db_options.wal_bytes_per_sync,
  /* min */ 0L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(enable_thread_tracking,
  *reinterpret_cast<my_bool*>(&rdb_db_options.enable_thread_tracking),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "DBOptions::enable_thread_tracking for RocksDB",
  nullptr, nullptr, rdb_db_options.enable_thread_tracking);

static MYSQL_SYSVAR_LONGLONG(block_cache_size, rdb_block_cache_size_sysvar,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "block_cache size for RocksDB",
  nullptr, nullptr, /* RocksDB's default is 8 MB: */ 8*1024*1024L,
  /* min */ 1024L, /* max */ LONGLONG_MAX, /* Block size */1024L);

static MYSQL_SYSVAR_BOOL(cache_index_and_filter_blocks,
  *reinterpret_cast<my_bool*>(&rdb_table_options.cache_index_and_filter_blocks),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::cache_index_and_filter_blocks for RocksDB",
  nullptr, nullptr, true);

static MYSQL_SYSVAR_ENUM(index_type,
  rdb_index_type_sysvar,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::index_type for RocksDB",
  nullptr, nullptr, (uint64_t)rdb_table_options.index_type,
  &rdb_index_type_typelib);

static MYSQL_SYSVAR_BOOL(hash_index_allow_collision,
  *reinterpret_cast<my_bool*>(&rdb_table_options.hash_index_allow_collision),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::hash_index_allow_collision for RocksDB",
  nullptr, nullptr, rdb_table_options.hash_index_allow_collision);

static MYSQL_SYSVAR_BOOL(no_block_cache,
  *reinterpret_cast<my_bool*>(&rdb_table_options.no_block_cache),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::no_block_cache for RocksDB",
  nullptr, nullptr, rdb_table_options.no_block_cache);

static MYSQL_SYSVAR_ULONG(block_size,
  rdb_table_options.block_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::block_size for RocksDB",
  nullptr, nullptr, rdb_table_options.block_size,
  /* min */ 1L, /* max */ LONG_MAX, 0);

static MYSQL_SYSVAR_INT(block_size_deviation,
  rdb_table_options.block_size_deviation,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::block_size_deviation for RocksDB",
  nullptr, nullptr, rdb_table_options.block_size_deviation,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_INT(block_restart_interval,
  rdb_table_options.block_restart_interval,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::block_restart_interval for RocksDB",
  nullptr, nullptr, rdb_table_options.block_restart_interval,
  /* min */ 1, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_BOOL(whole_key_filtering,
  *reinterpret_cast<my_bool*>(&rdb_table_options.whole_key_filtering),
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "BlockBasedTableOptions::whole_key_filtering for RocksDB",
  nullptr, nullptr, rdb_table_options.whole_key_filtering);

static MYSQL_SYSVAR_STR(default_cf_options, rdb_default_cf_options,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "default CF options for RocksDB",
  nullptr, nullptr, "");

static MYSQL_SYSVAR_STR(override_cf_options, rdb_override_cf_options,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "option overrides per cf for RocksDB",
  nullptr, nullptr, "");

static MYSQL_SYSVAR_BOOL(background_sync,
  rdb_background_sync_sysvar,
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
  "Used to override the result of records_in_range()."
  " Set to a positive number to override",
  nullptr, nullptr, 0,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_THDVAR_UINT(force_index_records_in_range,
  PLUGIN_VAR_RQCMDARG,
  "Used to override the result of records_in_range() when FORCE INDEX is used.",
  nullptr, nullptr, 0,
  /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_UINT(debug_optimizer_n_rows,
  rdb_debug_optimizer_n_rows_sysvar,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY | PLUGIN_VAR_NOSYSVAR,
  "Test only to override rocksdb estimates of table size in a memtable",
  nullptr, nullptr, 0, /* min */ 0, /* max */ INT_MAX, 0);

static MYSQL_SYSVAR_BOOL(debug_optimizer_no_zero_cardinality,
  rdb_dbg_optimizer_no_zero_cardinality_sysvar,
  PLUGIN_VAR_RQCMDARG,
  "In case if cardinality is zero, overrides it with some value",
  nullptr, nullptr, TRUE);

static MYSQL_SYSVAR_STR(compact_cf, rdb_compact_cf_name_sysvar,
  PLUGIN_VAR_RQCMDARG,
  "Compact column family",
  nullptr, rdb_set_compact_column_family_sysvar, "");

static MYSQL_SYSVAR_STR(create_checkpoint, rdb_checkpoint_name_sysvar,
  PLUGIN_VAR_RQCMDARG,
  "Checkpoint directory",
  rdb_create_checkpoint, rdb_create_checkpoint_stub, "");

static MYSQL_SYSVAR_BOOL(signal_drop_index_thread,
  rdb_signal_drop_idx_thd_sysvar,
  PLUGIN_VAR_RQCMDARG,
  "Wake up drop index thread",
  nullptr, Rdb_drop_index_thread::wakeup, FALSE);

static MYSQL_SYSVAR_BOOL(pause_background_work,
  rdb_bg_thd.m_pause_work_sysvar,
  PLUGIN_VAR_RQCMDARG,
  "Disable all rocksdb background operations",
  nullptr, rdb_set_pause_bg_work_sysvar, FALSE);

static MYSQL_SYSVAR_BOOL(strict_collation_check,
  rdb_strict_collation_check_sysvar,
  PLUGIN_VAR_RQCMDARG,
  "Enforce case sensitive collation for MyRocks indexes",
  nullptr, nullptr, TRUE);

static MYSQL_SYSVAR_STR(strict_collation_exceptions,
  rdb_strict_collation_exceptions_sysvar,
  PLUGIN_VAR_RQCMDARG|PLUGIN_VAR_MEMALLOC,
  "List of tables (using regex) that are excluded "
  "from the case sensitive collation enforcement",
  nullptr, rdb_set_collation_exception_list, "");

static MYSQL_SYSVAR_BOOL(collect_sst_properties,
  rdb_collect_sst_properties_sysvar,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "Enables collecting SST file properties on each flush",
  nullptr, nullptr, rdb_collect_sst_properties_sysvar);

static MYSQL_SYSVAR_BOOL(
  force_flush_memtable_now,
  rdb_force_flush_memtable_sysvar,
  PLUGIN_VAR_RQCMDARG,
  "Forces memstore flush which may block all write requests so be careful",
  nullptr, rdb_set_force_flush_memtable_sysvar, FALSE);

static MYSQL_THDVAR_BOOL(
  flush_memtable_on_analyze,
  PLUGIN_VAR_RQCMDARG,
  "Forces memtable flush on ANALZYE table to get accurate cardinality",
  nullptr, nullptr, true);

static MYSQL_SYSVAR_UINT(seconds_between_stat_computes,
  rdb_secs_between_stat_computes_sysvar,
  PLUGIN_VAR_RQCMDARG,
  "Sets a number of seconds to wait between optimizer stats recomputation. "
  "Only changed indexes will be refreshed.",
  nullptr, nullptr, rdb_secs_between_stat_computes_sysvar,
  /* min */ 0L, /* max */ UINT_MAX, 0);

static MYSQL_SYSVAR_LONGLONG(
  compaction_sequential_deletes,
  rdb_compact_seq_del_count_sysvar,
  PLUGIN_VAR_RQCMDARG,
  "RocksDB will trigger compaction for the file if it has more than this"
  " number of sequential deletes per window",
  nullptr, rdb_set_compaction_options_sysvar,
  RDB_DEF_COMPACT_SEQUENTIAL_DELETES,
  /* min */ 0L, /* max */ RDB_MAX_COMPACT_SEQUENTIAL_DELETES, 0);

static MYSQL_SYSVAR_LONGLONG(
  compaction_sequential_deletes_window,
  rdb_compact_seq_del_window_sysvar,
  PLUGIN_VAR_RQCMDARG,
  "Size of the window for counting rocksdb_compaction_sequential_deletes",
  nullptr, rdb_set_compaction_options_sysvar,
  RDB_DEF_COMPACT_SEQUENTIAL_DELETES_WINDOW,
  /* min */ 0L, /* max */ RDB_MAX_COMPACT_SEQUENTIAL_DELETES_WINDOW, 0);

static MYSQL_SYSVAR_LONGLONG(
  compaction_sequential_deletes_file_size,
  rdb_compact_seq_del_file_size_sysvar,
  PLUGIN_VAR_RQCMDARG,
  "Minimum file size required for rocksdb_compaction_sequential_deletes",
  nullptr, rdb_set_compaction_options_sysvar, 0L,
  /* min */ -1L, /* max */ LONGLONG_MAX, 0);

static MYSQL_SYSVAR_BOOL(compaction_sequential_deletes_count_sd,
  rdb_compact_seqdelete_count_sd_sysvar,
  PLUGIN_VAR_RQCMDARG,
  "Counting SingleDelete as rocksdb_compaction_sequential_deletes_count_sd",
  nullptr, nullptr, rdb_compact_seqdelete_count_sd_sysvar);

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
  rdb_validate_tables_sysvar,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "Verify all .frm files match all RocksDB tables (0 means no verification, "
  "1 means verify and fail on error, and 2 means verify but continue",
  nullptr, nullptr, 1 /* default value */, 0 /* min value */,
  2 /* max value */, 0);

static MYSQL_SYSVAR_STR(datadir,
  rdb_datadir_sysvar,
  PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_READONLY,
  "RocksDB data directory",
  nullptr, nullptr, "./.rocksdb");

static MYSQL_SYSVAR_UINT(table_stats_sampling_pct,
  rdb_table_stats_sampling_pct_sysvar,
  PLUGIN_VAR_RQCMDARG,
  "Percentage of entries to sample when collecting statistics about table "
  "properties. Specify either 0 to sample everything or percentage ["
  STRINGIFY_ARG(RDB_SAMPLE_PCT_MIN) ".."
  STRINGIFY_ARG(RDB_SAMPLE_PCT_MAX) "]. " "By default "
  STRINGIFY_ARG(RDB_DEFAULT_SAMPLE_PCT) "% of entries are "
  "sampled.",
  nullptr, rdb_set_table_stats_sampling_pct_sysvar, /* default */
  RDB_DEFAULT_SAMPLE_PCT, /* everything */ 0,
  /* max */ RDB_SAMPLE_PCT_MAX, 0);

const longlong RDB_WRITE_BUFFER_SIZE_DEFAULT= 4194304;
static const int RDB_ASSUMED_KEY_VALUE_DISK_SIZE= 100;

static struct st_mysql_sys_var* rdb_system_variables[]=
{
  MYSQL_SYSVAR(lock_wait_timeout),
  MYSQL_SYSVAR(max_row_locks),
  MYSQL_SYSVAR(lock_scanned_rows),
  MYSQL_SYSVAR(bulk_load),
  MYSQL_SYSVAR(skip_unique_check),
  MYSQL_SYSVAR(commit_in_the_middle),
  MYSQL_SYSVAR(rpl_lookup_rows),
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

static rocksdb::WriteOptions rdb_get_write_options(my_core::THD* thd)
{
  rocksdb::WriteOptions opt;

  opt.sync = THDVAR(thd, write_sync);
  opt.disableWAL = THDVAR(thd, write_disable_wal);
  opt.ignore_missing_column_families =
    THDVAR(thd, write_ignore_missing_column_families);

  return opt;
}

////////////////////////////////////////////////////////////////////////////////

/**
  @brief
  Function we use in the creation of our hash to get key.
*/

uchar* Rdb_open_tables::get_hash_key(
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
PSI_stage_info rdb_stage_waiting_on_row_lock= { 0, "Waiting for row lock", 0 };

#ifdef HAVE_PSI_INTERFACE
static PSI_thread_key rdb_key_thread_background;
static PSI_thread_key rdb_key_thread_drop_index;

static PSI_stage_info *rdb_psi_stages[]=
{
  & rdb_stage_waiting_on_row_lock
};

static PSI_mutex_key rdb_psi_open_tbls_mutex,
  rdb_psi_background_mutex, rdb_psi_stop_bg_mutex,
  rdb_psi_drop_index_mutex, rdb_psi_drop_idx_interrupt_mutex,
  rdb_psi_tx_list_mutex, rdb_psi_collation_mutex,
  rdb_psi_sysvar_mutex;

static PSI_mutex_info rdb_psi_mutexes[]=
{
  { &rdb_psi_open_tbls_mutex, "rocksdb", PSI_FLAG_GLOBAL},
  { &rdb_psi_background_mutex, "background", PSI_FLAG_GLOBAL},
  { &rdb_psi_stop_bg_mutex, "stop background", PSI_FLAG_GLOBAL},
  { &rdb_psi_drop_index_mutex, "drop index", PSI_FLAG_GLOBAL},
  { &rdb_psi_drop_idx_interrupt_mutex, "drop index interrupt", PSI_FLAG_GLOBAL},
  { &rdb_psi_tx_list_mutex, "tx_list", PSI_FLAG_GLOBAL},
  { &rdb_psi_collation_mutex, "collation exception list", PSI_FLAG_GLOBAL},
  { &rdb_psi_sysvar_mutex, "setting sysvar", PSI_FLAG_GLOBAL},
};

static PSI_cond_key rdb_psi_stop_bg_cond, rdb_psi_drop_idx_interrupt_cond;

static PSI_cond_info rdb_psi_cond_vars[]=
{
  { &rdb_psi_stop_bg_cond, "cond_stop", PSI_FLAG_GLOBAL},
  { &rdb_psi_drop_idx_interrupt_cond, "cond_stop_drop_index", PSI_FLAG_GLOBAL},
};

static PSI_thread_info rdb_psi_threads[]=
{
  { &rdb_key_thread_background, "background", PSI_FLAG_GLOBAL},
  { &rdb_key_thread_drop_index, "drop index", PSI_FLAG_GLOBAL},
};

static void rdb_init_psi_keys()
{
  const char* category= "rocksdb";
  int count;

  if (PSI_server == nullptr)
    return;

  count= array_elements(rdb_psi_mutexes);
  PSI_server->register_mutex(category, rdb_psi_mutexes, count);

  count= array_elements(rdb_psi_cond_vars);
  // TODO Disabling PFS for conditions due to the bug
  // described at https://github.com/MySQLOnRocksDB/mysql-5.6/issues/92
  // PSI_server->register_cond(category, rdb_psi_cond_vars, count);

  count= array_elements(rdb_psi_stages);
  mysql_stage_register(category, rdb_psi_stages, count);

  count= array_elements(rdb_psi_threads);
  mysql_thread_register(category, rdb_psi_threads, count);
}
#endif  // HAVE_PSI_INTERFACE


// TODO(vasilep): move the classes below to a separate source file

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

  my_core::THD* m_thd= nullptr;

  rocksdb::ReadOptions m_read_opts;

  static std::multiset<Rdb_transaction*> s_tx_list;
  static mysql_mutex_t s_tx_list_mutex;

  std::shared_ptr<Rdb_snapshot_notifier> m_notifier;

public:
  const char* m_mysql_log_file_name;
  my_off_t m_mysql_log_offset;
  const char* m_mysql_gtid;
  String m_detailed_error;

  static void init_mutex()
  {
    mysql_mutex_init(rdb_psi_tx_list_mutex, &s_tx_list_mutex,
                     MY_MUTEX_INIT_FAST);
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

  int set_status_error(my_core::THD *thd, const rocksdb::Status &s,
                       Rdb_key_def *key_descr, Rdb_tbl_def *tbl_def)
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
                                            tbl_def->m_dbname_tablename.c_ptr(),
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

  int m_timeout_sec; /* Cached value of @@rocksdb_lock_wait_timeout */

  /* Maximum number of locks the transaction can have */
  ulonglong m_max_row_locks;

  bool m_ddl_transaction;

  my_core::THD* get_thd() const { return m_thd; }

  void set_params(int timeout_sec_arg, int max_row_locks_arg)
  {
    m_timeout_sec= timeout_sec_arg;
    m_max_row_locks= max_row_locks_arg;
    if (m_rocksdb_tx)
      m_rocksdb_tx->SetLockTimeout(m_timeout_sec * 1000);
  }

  ulonglong get_n_writes() { return m_write_count; }

  void release_lock(rocksdb::ColumnFamilyHandle* column_family,
                    const std::string &rowkey)
  {
    if (!THDVAR(m_thd, lock_scanned_rows))
    {
      m_rocksdb_tx->UndoGetForUpdate(column_family, rocksdb::Slice(rowkey));
    }
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
    if (get_n_writes() == 0)
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
      my_printf_error(ER_UNKNOWN_ERROR, RDB_ERRSTR_ROLLBACK_ONLY, MYF(0));
      rollback();
      return true;
    }
    else
    {
      my_core::thd_binlog_pos(m_thd, &m_mysql_log_file_name,
                              &m_mysql_log_offset, &m_mysql_gtid);
      rdb_binlog_manager.update(m_mysql_log_file_name,
                            m_mysql_log_offset,
                            m_mysql_gtid, m_rocksdb_tx->GetWriteBatch());
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
    rdb_rocksdb_db->GetEnv()->GetCurrentTime(&m_snapshot_timestamp);
    m_is_delayed_snapshot = false;
  }

  void acquire_snapshot(bool acquire_now)
  {
    if (m_read_opts.snapshot == nullptr) {
      if (is_tx_read_only()) {
        snapshot_created(rdb_rocksdb_db->GetSnapshot());
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
        rdb_rocksdb_db->ReleaseSnapshot(m_read_opts.snapshot);
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
    return m_rocksdb_tx->GetIterator(options, column_family);
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

    /*
      If m_rocksdb_reuse_tx is null this will create a new transaction object.
      Otherwise it will reuse the existing one.
    */
    m_rocksdb_tx= rdb_rocksdb_db->BeginTransaction(write_opts, tx_opts,
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
          rdb_rocksdb_db->GetEnv()->GetCurrentTime(&m_snapshot_timestamp);
        else
          m_is_delayed_snapshot = true;
      }
    }
  }

  bool can_prepare()
  {
    if (m_rollback_only)
    {
      my_printf_error(ER_UNKNOWN_ERROR, RDB_ERRSTR_ROLLBACK_ONLY, MYF(0));
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

  void set_tx_read_only(bool val)
  {
    m_tx_read_only= val;
  }

  explicit Rdb_transaction(my_core::THD *thd)
  : m_rocksdb_tx(nullptr), m_thd(thd)
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

static Rdb_transaction* &get_tx_from_thd(my_core::THD *thd)
{
  return *reinterpret_cast<Rdb_transaction**>(
    my_core::thd_ha_data(thd, rdb_handlerton));
}

/*
  TODO: maybe, call this in external_lock() and store in ha_rocksdb..
*/

static Rdb_transaction *rdb_get_or_create_tx(my_core::THD *thd)
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

// TODO(vasilep): move the classes above to a separate source file


int Rdb_handlerton::hton_close_connection(
  handlerton* hton __attribute__((__unused__)),
  my_core::THD* thd)
{
  Rdb_transaction*& tx= get_tx_from_thd(thd);
  delete tx;
  tx= nullptr;
  return 0;
}


/**
  @brief
  Doing nothing at prepare() on master. But defining handler::prepare() is
  needed for Xid binlog event to be written at commit.

  @detail
  For a slave, prepare() updates the slave_gtid_info table which tracks the
  replication progress.
*/

int Rdb_handlerton::hton_prepare(handlerton* hton __attribute__((__unused__)),
                                 my_core::THD* thd, bool prepare_tx,
                                 bool async __attribute__((__unused__)))
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
      rdb_binlog_manager.update_slave_gtid_info(it.id, it.db, it.gtid,
                                                write_batch);
    }
  }

  return 0;
}

/**
 do nothing for prepare/commit by xid
 this is needed to avoid crashes in XA scenarios
*/

int Rdb_handlerton::hton_commit_by_xid(
  handlerton* hton __attribute__((__unused__)),
  my_core::XID* xid __attribute__((__unused__)))
{
  return 0;
}


int Rdb_handlerton::hton_rollback_by_xid(
  handlerton* hton __attribute__((__unused__)),
  my_core::XID* xid __attribute__((__unused__)))
{
  return 0;
}


/**
  @brief
  Reading last committed binary log info from RocksDB system row.
  The info is needed for crash safe slave/master to work.
*/

int Rdb_handlerton::hton_recover(handlerton* hton __attribute__((__unused__)),
                                 XID* xid_list __attribute__((__unused__)),
                                 uint len __attribute__((__unused__)),
                                 char* binlog_file, my_off_t* binlog_pos)
{
  if (binlog_file && binlog_pos)
  {
    char file_buf[FN_REFLEN+1]= {0};
    my_off_t pos;
    char gtid_buf[FN_REFLEN+1]= {0};
    if (rdb_binlog_manager.read(file_buf, &pos, gtid_buf))
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


int Rdb_handlerton::hton_commit(handlerton* hton __attribute__((__unused__)),
                                my_core::THD* thd, bool commit_tx, bool)
{
  DBUG_ENTER("Rdb_handlerton::hton_commit");

  Rdb_perf_context_local local_perf_context;
  RDB_PERF_CONTEXT_GUARD(local_perf_context, nullptr, rdb_global_perf_context,
                         rdb_perf_context_level_sysvar != rocksdb::kDisable);

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


int Rdb_handlerton::hton_rollback(handlerton* hton __attribute__((__unused__)),
                                  my_core::THD* thd, bool rollback_tx)
{
  Rdb_perf_context_local local_perf_context;
  RDB_PERF_CONTEXT_GUARD(local_perf_context, nullptr, rdb_global_perf_context,
                         rdb_perf_context_level_sysvar != rocksdb::kDisable);

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


static bool rdb_print_stats(my_core::THD* thd,
                            std::string const& type,
                            std::string const& name,
                            std::string const& status,
                            my_core::stat_print_fn *stat_print_fn)
{
  return stat_print_fn(thd, type.c_str(), type.size(), name.c_str(),
                       name.size(), status.c_str(), status.size());
}


static std::string rdb_format_string(
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

    return rdb_format_string(format, currtm.tm_year + 1900, currtm.tm_mon + 1,
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
      rdb_rocksdb_db->GetEnv()->GetCurrentTime(&curr_time);

      my_core::THD* thd = tx->get_thd();

      m_data += rdb_format_string("---SNAPSHOT, ACTIVE %lld sec\n"
                                  "MySQL thread id %lu, OS thread handle %p\n",
                                  curr_time - snapshot_timestamp,
                                  my_core::thd_get_thread_id(thd), thd);
    }
  }
};


/* Generate the snapshot status table */
static bool rdb_show_snapshot_status(
    handlerton*    hton __attribute__((__unused__)),
    my_core::THD*  thd,
    stat_print_fn* stat_print_fn)
{
  Rdb_snapshot_status showStatus;
  Rdb_transaction::walk_tx_list(&showStatus);

  // Send the result data back to MySQL */
  return rdb_print_stats(thd, "SNAPSHOTS", "rocksdb", showStatus.getResult(),
                         stat_print_fn);
}

/*
  This is called for SHOW ENGINE ROCKSDB STATUS|LOGS|etc.

  For now, produce info about live files (which gives an imprecise idea about
  what column families are there)
*/

bool Rdb_handlerton::hton_show_status(handlerton*       hton,
                                      my_core::THD*     thd,
                                      stat_print_fn*    stat_print_fn,
                                      enum ha_stat_type stat_type)
{
  bool res= false;
  if (stat_type == HA_ENGINE_STATUS)
  {
    std::string str;

    /* Per DB stats */
    if (rdb_rocksdb_db->GetProperty("rocksdb.dbstats", &str))
    {
      res |= rdb_print_stats(thd, "DBSTATS", "rocksdb", str, stat_print_fn);
    }

    /* Per column family stats */
    for (auto cf_name : rdb_cf_manager.get_cf_names())
    {
      rocksdb::ColumnFamilyHandle* cfh;
      bool is_automatic;

      /*
        Only the cf name is important. Whether it was generated automatically
        does not matter, so is_automatic is ignored.
      */
      cfh= rdb_cf_manager.get_cf(cf_name.c_str(), nullptr, nullptr,
                                 &is_automatic);
      if (cfh == nullptr)
        continue;

      if (!rdb_rocksdb_db->GetProperty(cfh, "rocksdb.cfstats", &str))
      {
        continue;
      }

      res |= rdb_print_stats(thd, "CF_COMPACTION", cf_name, str, stat_print_fn);
    }

    /* Memory Statistics */
    std::vector<rocksdb::DB*> dbs;
    std::unordered_set<const rocksdb::Cache*> cache_set;
    size_t internal_cache_count = 0;
    size_t kDefaultInternalCacheSize = 8 * 1024 * 1024;
    char buf[100];

    dbs.push_back(rdb_rocksdb_db);
    cache_set.insert(rdb_table_options.block_cache.get());
    for (const auto& cf_handle : rdb_cf_manager.get_all_cf())
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
    res |= rdb_print_stats(thd, "Memory_Stats", "rocksdb", str, stat_print_fn);
  }
  else if (stat_type == HA_ENGINE_TRX)
  {
    /* Handle the SHOW ENGINE ROCKSDB TRANSACTION STATUS command */
    res |= rdb_show_snapshot_status(hton, thd, stat_print_fn);
  }

  return res;
}


static inline void rdb_register_tx(handlerton *hton __attribute__((__unused__)),
                                   my_core::THD *thd, Rdb_transaction *tx)
{
  my_core::trans_register_ha(thd, FALSE, rdb_handlerton);
  if (my_core::thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN))
  {
    tx->start_stmt();
    my_core::trans_register_ha(thd, TRUE, rdb_handlerton);
  }
}


/**
  @brief
  Supporting START TRANSACTION WITH CONSISTENT [ROCKSDB] SNAPSHOT

  @details
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

  @param hton            MyRocks handlerton
  @param thd             MySQL thread handle of the user for whom the
                         transaction should be committed
  @param binlog_file     OUT binlog file for last commit
  @param binlog_pos      OUT binlog position for last commit
  @param gtids_executed  OUT Gtid(s) logged until last commit
  @param gtids_length    OUT Length of gtids_executed string
*/
int Rdb_handlerton::hton_start_tx_and_assign_read_view(
  my_core::handlerton*  hton,
  my_core::THD*         thd,
  char*                 binlog_file,
  ulonglong*            binlog_pos,
  char**                gtids_executed,
  int*                  gtids_length)
{
  Rdb_perf_context_local local_perf_context;
  RDB_PERF_CONTEXT_GUARD(local_perf_context, nullptr, rdb_global_perf_context,
                         rdb_perf_context_level_sysvar != rocksdb::kDisable);

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

  Rdb_transaction* tx= rdb_get_or_create_tx(thd);
  DBUG_ASSERT(!tx->has_snapshot());
  tx->set_tx_read_only(true);
  rdb_register_tx(hton, thd, tx);
  tx->acquire_snapshot(true);

  if (binlog_file)
    mysql_bin_log_unlock_commits(binlog_file, binlog_pos, gtids_executed,
                                 gtids_length);

  return 0;
}


/**
  @brief
  Dummy SAVEPOINT support.

  @details
  This is needed for long running transactions
  like mysqldump (https://bugs.mysql.com/bug.php?id=71017).
  Current SAVEPOINT does not correctly handle ROLLBACK and does not return
  errors. This needs to be addressed in future versions (Issue#96).
*/

int Rdb_handlerton::hton_savepoint(
  handlerton *hton __attribute__((__unused__)),
  my_core::THD *thd __attribute__((__unused__)),
  void *savepoint __attribute__((__unused__)))
{
  return 0;
}


int Rdb_handlerton::hton_rollback_to_savepoint(
  handlerton *hton __attribute__((__unused__)),
  my_core::THD *thd, void *savepoint)
{
  Rdb_transaction*& tx= get_tx_from_thd(thd);
  return tx->rollback_to_savepoint(savepoint);
}


bool Rdb_handlerton::hton_rollback_to_savepoint_can_release_mdl(
  handlerton *hton __attribute__((__unused__)),
  my_core::THD *thd __attribute__((__unused__)))
{
  return true;
}


/*
  This is called for INFORMATION_SCHEMA
*/
void Rdb_handlerton::hton_update_table_stats(
  /* per-table stats callback */
  void (*callback_fn)(const char* db, const char* tbl, bool is_partition,
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

  for (auto it : tablenames)
  {
    Rdb_table_handler *table_handler;
    StringBuffer<256> buf, dbname, tablename, partname;
    char dbname_sys[NAME_LEN + 1];
    char tablename_sys[NAME_LEN + 1];
    bool is_partition;

    if (rdb_normalize_tablename(it.c_str(), &buf)) {
      /* Function needs to return void because of the interface and we've
       * detected an error which shouldn't happen. There's no way to let
       * caller know that something failed.
      */
      SHIP_ASSERT(false);
      return;
    }

    if (rdb_split_normalized_tablename(buf.c_ptr(), &dbname, &tablename,
                                       &partname))
    {
      continue;
    }

    is_partition= (partname.length() != 0);

    table_handler= rdb_open_tables.get_table_handler(it.c_str());
    if (!table_handler)
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
    rdb_open_tables.free_table_handler(table_handler);

    /*
      Table stats expects our database and table name to be in system encoding,
      not filename format. Convert before calling callback.
     */
    my_core::filename_to_tablename(dbname.c_ptr(), dbname_sys,
          sizeof(dbname_sys));
    my_core::filename_to_tablename(tablename.c_ptr(), tablename_sys,
          sizeof(tablename_sys));
    (*callback_fn)(dbname_sys, tablename_sys, is_partition, &io_perf_read,
          &io_perf, &io_perf, &io_perf, &io_perf, &page_stats, &comp_stats, 0,
          0, rdb_storage_engine_name);
  }
}


static rocksdb::Status rdb_check_rocksdb_options_compat(
        const char *dbpath,
        const rocksdb::Options& main_opts,
        const std::vector<rocksdb::ColumnFamilyDescriptor>& cf_descr)
{
  DBUG_ASSERT(rdb_datadir_sysvar != nullptr);

  rocksdb::DBOptions loaded_db_opt;
  std::vector<rocksdb::ColumnFamilyDescriptor> loaded_cf_descs;
  rocksdb::Status status = LoadLatestOptions(
    dbpath, rocksdb::Env::Default(), &loaded_db_opt, &loaded_cf_descs);

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

static int rdb_plugin_load_fn(void *hton_arg)
{
  DBUG_ENTER("rdb_plugin_load_fn");

  // Validate the assumption about the size of RDB_SIZEOF_HIDDEN_PK_COLUMN.
  static_assert(sizeof(longlong) == 8, "Assuming that longlong is 8 bytes.");
  static_assert(RDB_SIZEOF_HIDDEN_PK_COLUMN == 8,
                "Assuming that RDB_SIZEOF_HIDDEN_PK_COLUMN is 8 bytes.");

#ifdef HAVE_PSI_INTERFACE
  rdb_init_psi_keys();
#endif

  rdb_handlerton= reinterpret_cast<Rdb_handlerton *>(hton_arg);
  rdb_handlerton->init();

  mysql_mutex_init(rdb_psi_open_tbls_mutex, &rdb_open_tables.m_mutex,
                   MY_MUTEX_INIT_FAST);
  mysql_mutex_init(rdb_psi_background_mutex, &rdb_bg_thd.m_mutex,
                   MY_MUTEX_INIT_FAST);
  mysql_mutex_init(rdb_psi_stop_bg_mutex, &rdb_bg_thd.m_stop_cond_mutex,
                   MY_MUTEX_INIT_FAST);
  mysql_mutex_init(rdb_psi_collation_mutex,
                   &rdb_collation_ex_list_mutex, MY_MUTEX_INIT_FAST);
  mysql_mutex_init(rdb_psi_sysvar_mutex, &rdb_sysvar_mutex, MY_MUTEX_INIT_FAST);
  mysql_cond_init(rdb_psi_stop_bg_cond, &rdb_bg_thd.m_stop_cond, nullptr);
  rdb_open_tables.init_hash();
  Rdb_transaction::init_mutex();
  mysql_mutex_init(rdb_psi_drop_index_mutex, &rdb_drop_index_thd.m_mutex,
                   MY_MUTEX_INIT_FAST);
  mysql_mutex_init(rdb_psi_drop_idx_interrupt_mutex,
                   &rdb_drop_index_thd.m_interrupt_mutex, MY_MUTEX_INIT_FAST);
  mysql_cond_init(rdb_psi_drop_idx_interrupt_cond,
                  &rdb_drop_index_thd.m_interrupt_cond, nullptr);

  DBUG_ASSERT(!my_core::mysqld_embedded);

  rdb_rocksdb_stats= rocksdb::CreateDBStatistics();
  rdb_db_options.statistics = rdb_rocksdb_stats;

  if (rdb_rate_limiter_bps_sysvar != 0)
  {
    rdb_rate_limiter.reset(rocksdb::NewGenericRateLimiter(
          rdb_rate_limiter_bps_sysvar));
    rdb_db_options.rate_limiter = rdb_rate_limiter;
  }

  rocksdb::Status status;
  std::shared_ptr<Rdb_logger> myrocks_logger = std::make_shared<Rdb_logger>();
  status= rocksdb::CreateLoggerFromOptions(rdb_datadir_sysvar, rdb_db_options,
                                           &rdb_db_options.info_log);
  if (status.ok()) {
    myrocks_logger->SetRocksDBLogger(rdb_db_options.info_log);
  }

  rdb_db_options.info_log = myrocks_logger;
  myrocks_logger->SetInfoLogLevel(
    static_cast<rocksdb::InfoLogLevel>(rdb_info_log_level_sysvar));
  rdb_db_options.wal_dir = rdb_wal_dir_sysvar;

  rdb_db_options.wal_recovery_mode=
    static_cast<rocksdb::WALRecoveryMode>(rdb_wal_recovery_mode_sysvar);

  rdb_db_options.access_hint_on_compaction_start=
    static_cast<rocksdb::Options::AccessHint>
      (rdb_access_hint_on_compact_start_sysvar);

  std::vector<std::string> cf_names;
  status= rocksdb::DB::ListColumnFamilies(rdb_db_options, rdb_datadir_sysvar,
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

  rdb_table_options.index_type =
    (rocksdb::BlockBasedTableOptions::IndexType)rdb_index_type_sysvar;

  if (!rdb_table_options.no_block_cache)
  {
    rdb_table_options.block_cache=
        rocksdb::NewLRUCache(rdb_block_cache_size_sysvar);
  }
  // Using newer BlockBasedTable format version for better compression
  // and better memory allocation.
  // See: https://github.com/facebook/rocksdb/commit/9ab5adfc59a621d12357580c94451d9f7320c2dd
  rdb_table_options.format_version= 2;

  if (rdb_collect_sst_properties_sysvar)
  {
    rdb_prop_coll_factory = std::make_shared
      <Rdb_tbl_prop_coll_factory>(
        &rdb_ddl_manager
      );

    rdb_set_compaction_options_sysvar(nullptr, nullptr, nullptr, nullptr);

    mysql_mutex_lock(&rdb_sysvar_mutex);

    DBUG_ASSERT(rdb_table_stats_sampling_pct_sysvar <= RDB_SAMPLE_PCT_MAX);
    rdb_prop_coll_factory->SetTableStatsSamplingPct(
      rdb_table_stats_sampling_pct_sysvar);

    mysql_mutex_unlock(&rdb_sysvar_mutex);
  }

  if (!rdb_cf_registry.init(RDB_WRITE_BUFFER_SIZE_DEFAULT,
                            rdb_table_options,
                            rdb_prop_coll_factory,
                            rdb_default_cf_options,
                            rdb_override_cf_options))
  {
    DBUG_RETURN(1);
  }

  /*
    If there are no column families, we're creating the new database.
    Create one column family named "default".
  */
  if (cf_names.size() == 0)
    cf_names.push_back(RDB_DEFAULT_CF_NAME);

  std::vector<int> compaction_enabled_cf_indices;
  sql_print_information("RocksDB: Column Families at start:");
  for (size_t i = 0; i < cf_names.size(); ++i)
  {
    rocksdb::ColumnFamilyOptions opts;
    rdb_cf_registry.get_cf_options(cf_names[i], &opts);

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

  rocksdb::Options main_opts(rdb_db_options, rdb_cf_registry.get_defaults());

  /*
    Flashcache configuration:
    When running on Flashcache, mysqld opens Flashcache device before
    initializing storage engines, and setting file descriptor at
    cachedev_fd global variable.
    RocksDB has Flashcache-aware configuration. When this is enabled,
    RocksDB adds background threads into Flashcache blacklists, which
    makes sense for Flashcache use cases.
  */
  if (my_core::cachedev_enabled)
  {
    rdb_flashcache_env=
      rocksdb::NewFlashcacheAwareEnv(rocksdb::Env::Default(),
                                     cachedev_fd);
    if (rdb_flashcache_env.get() == nullptr)
    {
      sql_print_error("RocksDB: Failed to open flashcahce device at fd %d",
                      cachedev_fd);
      DBUG_RETURN(1);
    }
    sql_print_information("RocksDB: Disabling flashcache on background "
                          "writer threads, fd %d", cachedev_fd);
    main_opts.env= rdb_flashcache_env.get();
  }

  main_opts.env->SetBackgroundThreads(main_opts.max_background_flushes,
                                      rocksdb::Env::Priority::HIGH);
  main_opts.env->SetBackgroundThreads(main_opts.max_background_compactions,
                                      rocksdb::Env::Priority::LOW);
  rocksdb::TransactionDBOptions tx_db_options;
  tx_db_options.transaction_lock_timeout= 2;  // 2 seconds
  tx_db_options.custom_mutex_factory= std::make_shared<Rdb_mutex_factory>();

  status= rdb_check_rocksdb_options_compat(rdb_datadir_sysvar, main_opts,
                                           cf_descr);

  // We won't start if we'll determine that there's a chance of data corruption
  // because of incompatible options.
  if (!status.ok()) {
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: compatibility check against existing database " \
                    "options failed. %s", status.ToString().c_str());
    DBUG_RETURN(1);
  }

  status= rocksdb::TransactionDB::Open(main_opts, tx_db_options,
                                       rdb_datadir_sysvar, cf_descr,
                                       &cf_handles, &rdb_rocksdb_db);

  if (!status.ok())
  {
    std::string err_text= status.ToString();
    sql_print_error("RocksDB: Error opening instance: %s", err_text.c_str());
    DBUG_RETURN(1);
  }
  rdb_cf_manager.init(&rdb_cf_registry, &cf_handles);

  if (rdb_dict_manager.init(rdb_rocksdb_db, &rdb_cf_manager))
  {
    sql_print_error("RocksDB: Failed to initialize data dictionary.");
    DBUG_RETURN(1);
  }

  if (rdb_binlog_manager.init(&rdb_dict_manager))
  {
    DBUG_RETURN(1);
  }

  if (rdb_ddl_manager.init(&rdb_dict_manager, &rdb_cf_manager,
                           rdb_validate_tables_sysvar))
  {
    DBUG_RETURN(1);
  }

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

  status= rdb_rocksdb_db->EnableAutoCompaction(compaction_enabled_cf_handles);

  if (!status.ok())
  {
    std::string err_text= status.ToString();
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Error enabling compaction: %s", err_text.c_str());
    DBUG_RETURN(1);
  }

  auto err = mysql_thread_create(
    rdb_key_thread_background, &rdb_bg_thd.m_thread_handle,
    nullptr,
    Rdb_background_thread::thread_fn, nullptr
  );
  if (err != 0) {
    sql_print_error("RocksDB: Couldn't start the background thread: (errno=%d)",
                    err);
    DBUG_RETURN(1);
  }

  rdb_drop_index_thd.m_stop = false;
  err = mysql_thread_create(
    rdb_key_thread_drop_index, &rdb_drop_index_thd.m_thread_handle,
    nullptr,
    Rdb_drop_index_thread::thread_fn, nullptr
  );
  if (err != 0) {
    sql_print_error("RocksDB: Couldn't start the drop index thread: (errno=%d)",
                    err);
    DBUG_RETURN(1);
  }

  rdb_collation_exception_list =
      rdb_split_string(rdb_strict_collation_exceptions_sysvar, ',');

  if (rdb_bg_thd.m_pause_work_sysvar)
  {
    rdb_rocksdb_db->PauseBackgroundWork();
  }

  sql_print_information("RocksDB instance opened");
  DBUG_RETURN(0);
}


/*
  Storage Engine deinitialization function, invoked when plugin is unloaded.
*/

static int rdb_plugin_unload_fn(void *ptr_arg __attribute__((__unused__)))
{
  int error= 0;
  DBUG_ENTER("rdb_plugin_unload_fn");

  // signal the drop index thread to stop
  rdb_drop_index_thd.signal(true);

  // Flush all memtables for not lose data, even if WAL is disabled.
  rdb_flush_all_memtables();

  // Stop all rocksdb background work
  CancelAllBackgroundWork(rdb_rocksdb_db->GetBaseDB(), true);

  // Signal the background thread to stop and to persist all stats collected
  // from background flushes and compactions. This will add more keys to a new
  // memtable, but since the memtables were just flushed, it should not trigger
  // a flush that can stall due to background threads being stopped. As long
  // as these keys are stored in a WAL file, they can be retrieved on restart.
  mysql_mutex_lock(&rdb_bg_thd.m_stop_cond_mutex);
  rdb_bg_thd.m_control.m_stop = true;
  mysql_cond_signal(&rdb_bg_thd.m_stop_cond);
  mysql_mutex_unlock(&rdb_bg_thd.m_stop_cond_mutex);

  // Wait for the background thread to finish.
  auto err = my_os::pthread_join(rdb_bg_thd.m_thread_handle, nullptr);
  if (err != 0) {
    // We'lll log the message and continue because we're shutting down and
    // continuation is the optimal strategy.
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Couldn't stop the background thread: (errno=%d)",
                    err);
  }

  // Wait for the drop index thread to finish.
  err = my_os::pthread_join(rdb_drop_index_thd.m_thread_handle, nullptr);
  if (err != 0) {
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Couldn't stop the index thread: (errno=%d)",
                    err);
  }

  if (rdb_open_tables.m_hash.records)
  {
    error= 1;
  }

  my_hash_free(&rdb_open_tables.m_hash);
  mysql_mutex_destroy(&rdb_collation_ex_list_mutex);
  mysql_mutex_destroy(&rdb_open_tables.m_mutex);
  mysql_mutex_destroy(&rdb_sysvar_mutex);
  Rdb_transaction::term_mutex();

  rdb_ddl_manager.cleanup();
  rdb_binlog_manager.cleanup();
  rdb_dict_manager.cleanup();
  rdb_cf_manager.cleanup();

  delete rdb_rocksdb_db;
  rdb_rocksdb_db= nullptr;

  // Disown the cache data since we're shutting down.
  // This results in memory leaks but it improved the shutdown time.
  // Don't disown when running under valgrind
#ifndef HAVE_purify
  if (rdb_table_options.block_cache)
  {
    rdb_table_options.block_cache->DisownData();
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

Rdb_table_handler* Rdb_open_tables::get_table_handler(const char *table_name)
{
  Rdb_table_handler *table_handler;
  uint length;
  char *tmp_name;

  DBUG_ASSERT(table_name != nullptr);
  length=(uint) strlen(table_name);

  // First, look up the table in the hash map.
  mysql_mutex_lock(&rdb_open_tables.m_mutex);
  if (!(table_handler= reinterpret_cast<Rdb_table_handler*>(
          my_hash_search(&rdb_open_tables.m_hash,
                         reinterpret_cast<const uchar*>(table_name),
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
      mysql_mutex_unlock(&rdb_open_tables.m_mutex);
      return nullptr;
    }

    table_handler->m_ref_count= 0;
    table_handler->m_table_name_length= length;
    table_handler->m_table_name= tmp_name;
    strmov(table_handler->m_table_name, table_name);

    if (my_hash_insert(&rdb_open_tables.m_hash,
                       reinterpret_cast<uchar*>(table_handler)))
    {
      // Inserting into the hash map failed.
      mysql_mutex_unlock(&rdb_open_tables.m_mutex);
      my_free(table_handler);
      return nullptr;
    }

    my_io_perf_atomic_init(&table_handler->m_io_perf_read);
  }
  table_handler->m_ref_count++;
  mysql_mutex_unlock(&rdb_open_tables.m_mutex);

  return table_handler;
}


std::vector<std::string> rdb_get_table_names(void)
{
  return rdb_open_tables.get_table_names();
}


std::vector<std::string> Rdb_open_tables::get_table_names(void) const
{
  ulong i;
  const Rdb_table_handler *table_handler;
  std::vector<std::string> names;

  mysql_mutex_lock(&m_mutex);
  for (i = 0;
       (table_handler= reinterpret_cast<const Rdb_table_handler*>(
          my_hash_get_element(&m_hash, i)));
       i++)
  {
    DBUG_ASSERT(table_handler != nullptr);
    names.push_back(table_handler->m_table_name);
  }
  mysql_mutex_unlock(&m_mutex);
  DBUG_ASSERT(i == m_hash.records);

  return names;
}


/*
  Updates row counters based on the table type and operation type.
*/
void ha_rocksdb::update_row_stats(operation_type type) const
{
  DBUG_ASSERT(type < ROWS_MAX);

  // Find if we are modifying system databases.
  if (handler::table->s && m_tbl_def->m_is_mysql_system_table)
  {
    rdb_global_stats.m_system_rows[type].inc();
  }
  else
  {
    rdb_global_stats.m_rows[type].inc();
  }
}


void ha_rocksdb::load_auto_incr_value()
{
  int save_active_index= handler::active_index;
  handler::active_index= handler::table->s->next_number_index;
  uint8 save_table_status= handler::table->status;

  /*
    load_auto_incr_value() may be called by statements that
    do not execute implicit commits (i.e. SHOW CREATE TABLE).
    index_last() creates a snapshot. When a snapshot is created
    here, it has to be released as well. (GitHub issue#189)
  */
  Rdb_transaction *tx= rdb_get_or_create_tx(this->table->in_use);
  bool is_new_snapshot= !tx->has_snapshot();

  // Do a lookup.
  if (!index_last(table->record[0]))
    update_auto_incr_val();

  if (is_new_snapshot)
  {
    tx->release_snapshot();
  }

  table->status= save_table_status;
  handler::active_index= save_active_index;

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

  my_bitmap_map *old_map= my_core::dbug_tmp_use_all_columns(
    handler::table,
    handler::table->read_set);
  new_val= field->val_int() + 1;
  dbug_tmp_restore_column_map(table->read_set, old_map);

  longlong auto_incr_val= m_tbl_def->m_auto_incr_val;
  while (auto_incr_val < new_val &&
         !m_tbl_def->m_auto_incr_val.compare_exchange_weak(auto_incr_val,
                                                           new_val))
  {
    // Do nothing - just loop until auto_incr_val is >= new_val or
    // we successfully set it
  }
}


int ha_rocksdb::load_hidden_pk_value()
{
  int save_active_index= handler::active_index;
  handler::active_index= m_tbl_def->m_key_count - 1;
  uint8 save_table_status= handler::table->status;

  Rdb_transaction *tx= rdb_get_or_create_tx(table->in_use);
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
  handler::active_index= save_active_index;

  release_scan_iterator();

  return 0;
}


/* Get PK value from m_tbl_def->m_hidden_pk_info. */

longlong ha_rocksdb::update_hidden_pk_val()
{
  DBUG_ASSERT(rdb_table_has_hidden_pk(table));
  longlong new_val;
  new_val= m_tbl_def->m_hidden_pk_val++;
  return new_val;
}


/* Get the id of the hidden pk id from m_last_rowkey */

int ha_rocksdb::read_hidden_pk_id_from_rowkey(longlong* hidden_pk_id) const
{
  DBUG_ASSERT(hidden_pk_id != nullptr);
  DBUG_ASSERT(table != nullptr);
  DBUG_ASSERT(rdb_table_has_hidden_pk(table));

  rocksdb::Slice rowkey_slice(m_last_rowkey.ptr(), m_last_rowkey.length());

  // Get hidden primary key from old key slice
  Rdb_string_reader reader(&rowkey_slice);
  if ((!reader.read(Rdb_key_def::INDEX_NUMBER_SIZE)))
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
  the last reference to the table_handler, then we free the memory associated
  with it.
*/

int Rdb_open_tables::free_table_handler(Rdb_table_handler *table_handler)
{
  mysql_mutex_lock(&m_mutex);
  if (!--table_handler->m_ref_count)
  {
    // Last rereference was released. Tear down the hash entry.
    my_hash_delete(&m_hash, reinterpret_cast<uchar*>(table_handler));
    my_free(table_handler);
  }
  mysql_mutex_unlock(&m_mutex);

  return 0;
}


my_core::handler* Rdb_handlerton::hton_create_handler(
  my_core::handlerton *hton,
  my_core::TABLE_SHARE *table,
  my_core::MEM_ROOT *mem_root)
{
  return new (mem_root) ha_rocksdb(hton, table);
}


ha_rocksdb::ha_rocksdb(handlerton *hton, TABLE_SHARE *table_arg)
  : handler(hton, table_arg), m_scan_it(nullptr),
    m_scan_it_skips_bloom(false),
    m_tbl_def(nullptr),
    m_pk_descr(nullptr),
    m_key_descriptors(nullptr),
    m_pk_can_be_decoded(false),
    m_pk_tuple(nullptr), m_pk_packed_tuple(nullptr),
    m_sec_key_packed_tuple(nullptr), m_sec_key_tails(nullptr),
    m_sec_key_match_prefix(nullptr), m_sec_key_match_prefix_buf(nullptr),
    m_sec_key_packed_tuple_old(nullptr), m_sec_key_tails_old(nullptr),
    m_pack_buffer(nullptr),
    m_lock_rows(RDB_LOCK_NONE),
    m_key_is_read_only(false),
    m_field_enc(nullptr),
    m_row_checksums_checked(0),
    m_rand_seed(time(nullptr)),
    m_rpl_delete_rows(false),
    m_rpl_update_rows(false)
{}


static const char *ha_rocksdb_exts[] = {
  NullS
};


const char **ha_rocksdb::bas_ext() const
{
  return ha_rocksdb_exts;
}

bool ha_rocksdb::init_with_fields()
{
  uint pk= this->handler::table_share->primary_key;
  uint key_parts=
    this->handler::table_share->key_info[pk].user_defined_key_parts;
  check_keyread_allowed(pk /*PK*/, key_parts-1, true);
  this->handler::cached_table_flags= table_flags();
  return false; /* Ok */
}

/**
  @brief
  Convert record from table->record[0] form into a form that can be written
  into rocksdb.

  @param pk_packed_tuple      Packed PK tuple. We need it in order to compute
                              and store its CRC.
  @param pk_packed_size       Length of the above
  @param packed_rec      OUT  Data slice with record data.
*/

void ha_rocksdb::convert_record_to_storage_format(size_t pk_packed_size,
                                                  rocksdb::Slice *packed_rec)
{
  m_storage_record.length(0);
  /* All NULL bits are initially 0 */
  m_storage_record.fill(m_null_bytes_in_rec, 0);

  for (uint i=0; i < table->s->fields; i++)
  {
    /* Don't pack decodable PK key parts */
    if (m_field_enc[i].m_dont_store)
    {
      continue;
    }

    Field *field= table->field[i];
    if (m_field_enc[i].maybe_null())
    {
      char *data= m_storage_record.c_ptr();
      if (field->is_null())
      {
        data[m_field_enc[i].m_null_offset] |= m_field_enc[i].m_null_mask;
        /* Don't write anything for NULL values */
        continue;
      }
    }

    if (m_field_enc[i].m_field_type == MYSQL_TYPE_BLOB)
    {
      Field_blob *blob= (Field_blob*)field;
      /* Get the number of bytes needed to store length*/
      uint length_bytes= blob->pack_length() - portable_sizeof_char_ptr;

      /* Store the length of the value */
      m_storage_record.append(reinterpret_cast<char*>(blob->ptr), length_bytes);

      /* Store the blob value itself */
      char *data_ptr;
      memcpy(&data_ptr, blob->ptr + length_bytes, sizeof(uchar**));
      m_storage_record.append(data_ptr, blob->get_length());
    }
    else if (m_field_enc[i].m_field_type == MYSQL_TYPE_VARCHAR)
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
    uint32_t key_crc32= my_core::crc32(
      0, reinterpret_cast<const uchar*>(m_pk_packed_tuple), pk_packed_size);
    uint32_t val_crc32= my_core::crc32(
      0, reinterpret_cast<const uchar*>(m_storage_record.c_ptr()),
      m_storage_record.length());
    uchar key_crc_buf[CHECKSUM_SIZE];
    uchar val_crc_buf[CHECKSUM_SIZE];
    store_big_uint4(key_crc_buf, key_crc32);
    store_big_uint4(val_crc_buf, val_crc32);
    m_storage_record.append((const char*)&CHECKSUM_DATA_TAG, 1);
    m_storage_record.append((const char*)key_crc_buf, CHECKSUM_SIZE);
    m_storage_record.append((const char*)val_crc_buf, CHECKSUM_SIZE);
  }

  *packed_rec= rocksdb::Slice(m_storage_record.ptr(),
                              m_storage_record.length());
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
  DBUG_ASSERT(key != nullptr);
  DBUG_ASSERT(value != nullptr);
  DBUG_ASSERT(buf != nullptr);

  m_retrieved_record.assign(value->data(), value->size());
  return convert_record_from_storage_format(key, buf);
}


#ifndef NDEBUG

static void rdb_dbug_append_garbage_at_end(std::string *on_disk_rec)
{
  on_disk_rec->append("abc");
}

static void rdb_dbug_truncate_record(std::string *on_disk_rec)
{
  on_disk_rec->resize(0);
}

static void rdb_dbug_modify_rec_varchar12(std::string *on_disk_rec)
{
  std::string res;
  // The record is NULL-byte followed by VARCHAR(10).
  // Put the NULL-byte
  res.append("\0", 1);
  // Then, add a valid VARCHAR(12) value.
  res.append("\xC", 1);
  res.append("123456789ab", 12);

  on_disk_rec->assign(res);
}

static void rdb_dbug_modify_key_varchar8(String *on_disk_rec)
{
  std::string res;
  // The key starts with index number
  res.append(on_disk_rec->ptr(), Rdb_key_def::INDEX_NUMBER_SIZE);

  // Then, a mem-comparable form of a varchar(8) value.
  res.append("ABCDE\0\0\0\xFC", 9);
  on_disk_rec->length(0);
  on_disk_rec->append(res.data(), res.size());
}

#endif  // NDEBUG


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

  @return
    0      OK
    other  Error inpacking the data
*/

int ha_rocksdb::convert_record_from_storage_format(const rocksdb::Slice *key,
                                                   uchar * buf)
{
  DBUG_ASSERT(key != nullptr);
  DBUG_ASSERT(buf != nullptr);

  DBUG_EXECUTE_IF("myrocks_simulate_bad_row_read1",
                  rdb_dbug_append_garbage_at_end(&m_retrieved_record););
  DBUG_EXECUTE_IF("myrocks_simulate_bad_row_read2",
                  rdb_dbug_truncate_record(&m_retrieved_record););
  DBUG_EXECUTE_IF("myrocks_simulate_bad_row_read3",
                  rdb_dbug_modify_rec_varchar12(&m_retrieved_record););

  Rdb_string_reader reader(m_retrieved_record);
  my_ptrdiff_t ptr_diff= buf - table->record[0];

  /*
    Decode PK fields from the key
  */
  DBUG_EXECUTE_IF("myrocks_simulate_bad_pk_read1",
                  rdb_dbug_modify_key_varchar8(&m_last_rowkey););
  rocksdb::Slice rowkey_slice(m_last_rowkey.ptr(), m_last_rowkey.length());

  DBUG_ASSERT(m_pk_descr->get_unpack_data_len() == 0);
  rocksdb::Slice pk_unpack_info_slice;
  if (m_pk_descr->unpack_record(this, table, buf, &rowkey_slice,
      &pk_unpack_info_slice)) {
    return HA_ERR_INTERNAL_ERROR;
  }

  /* Other fields are decoded from the value */
  const char * UNINIT_VAR(null_bytes);
  if (m_null_bytes_in_rec && !(null_bytes= reader.read(m_null_bytes_in_rec)))
  {
    return HA_ERR_INTERNAL_ERROR;
  }

  for (uint i=0; i < table->s->fields; i++)
  {
    if (m_field_enc[i].m_dont_store)
    {
      continue;
    }

    Field *field= table->field[i];

    bool isNull= m_field_enc[i].maybe_null() &&
                 ((null_bytes[m_field_enc[i].m_null_offset] &
                    m_field_enc[i].m_null_mask) != 0);
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

    if (m_field_enc[i].m_field_type == MYSQL_TYPE_BLOB)
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
    else if (m_field_enc[i].m_field_type == MYSQL_TYPE_VARCHAR)
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
      if (len > 0)
      {
        if ((data_bytes= reader.read(len)) == nullptr)
        {
          return HA_ERR_INTERNAL_ERROR;
        }
        field->move_field_offset(ptr_diff);
        memcpy(reinterpret_cast<char*>(field->ptr), data_bytes, len);
        field->move_field_offset(-ptr_diff);
      }
    }
  }

  if (reader.remaining_bytes() == CHECKSUM_CHUNK_SIZE)
  {
    if (reader.read(1)[0] == CHECKSUM_DATA_TAG)
    {
      if (m_verify_checksums)
      {
        uint32_t stored_key_chksum;
        uint32_t stored_val_chksum;

        stored_key_chksum= read_big_uint4((const uchar*)reader.read(CHECKSUM_SIZE));
        stored_val_chksum= read_big_uint4((const uchar*)reader.read(CHECKSUM_SIZE));

        uint32_t computed_key_chksum= my_core::crc32(
          0, reinterpret_cast<const uchar*>(key->data()), key->size());
        uint32_t computed_val_chksum= my_core::crc32(
          0, reinterpret_cast<const uchar*>(m_retrieved_record.data()),
          m_retrieved_record.size()-CHECKSUM_CHUNK_SIZE);

        DBUG_EXECUTE_IF("myrocks_simulate_bad_pk_checksum1",
                        stored_key_chksum++;);

        if (stored_key_chksum != computed_key_chksum)
        {
          rdb_report_checksum_mismatch(m_pk_descr, true, key->data(),
                                       key->size());
          return HA_ERR_INTERNAL_ERROR;
        }

        DBUG_EXECUTE_IF("myrocks_simulate_bad_pk_checksum2",
                        stored_val_chksum++;);
        if (stored_val_chksum != computed_val_chksum)
        {
          rdb_report_checksum_mismatch(m_pk_descr, false,
                                       m_retrieved_record.data(),
                                       m_retrieved_record.size());
          return HA_ERR_INTERNAL_ERROR;
        }

        m_row_checksums_checked++;
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


/**
  @brief
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

  DBUG_ASSERT(m_field_enc == nullptr);
  if (!(m_field_enc= reinterpret_cast<Rdb_field_encoder*>(my_malloc(
    handler::table->s->fields * sizeof(Rdb_field_encoder), MYF(0)))))
  {
    return;
  }

  for (i= 0; i < handler::table->s->fields; i++)
  {
    my_core::Field *field= table->field[i];
    m_field_enc[i].m_dont_store= false;

    /*
      Check if this field is
      - a part of primary key, and
      - it can be decoded back from its key image.
      If both hold, we don't need to store this field in the value part of
      RocksDB's key-value pair.

      If hidden pk exists, we skip this check since the field will never be
      part of the hidden pk.
    */
    if (!rdb_table_has_hidden_pk(table) &&
        field->part_of_key.is_set(table->s->primary_key))
    {
      KEY *pk_info= &table->key_info[table->s->primary_key];
      for (uint kp= 0; kp < pk_info->user_defined_key_parts; kp++)
      {
        /* key_part->fieldnr is counted from 1 */
        if (field->field_index + 1 == pk_info->key_part[kp].fieldnr)
        {
          if (m_pk_descr->can_unpack(kp))
          {
            m_field_enc[i].m_dont_store= true;
          }
          break;
        }
      }
    }

    m_field_enc[i].m_field_type= field->real_type();

    if (field->real_maybe_null())
    {
      m_field_enc[i].m_null_mask= cur_null_mask;
      m_field_enc[i].m_null_offset= null_bytes;
      if (cur_null_mask == 0x80)
      {
        cur_null_mask= 0x1;
        null_bytes++;
      }
      else
      {
        cur_null_mask= cur_null_mask << 1;
      }
    }
    else
    {
      m_field_enc[i].m_null_mask= 0;
    }
  }

  /* Count the last, unfinished NULL-bits byte */
  if (cur_null_mask != 0x1)
  {
    null_bytes++;
  }

  m_null_bytes_in_rec= null_bytes;
}


void ha_rocksdb::io_perf_start(void)
{
  rocksdb::PerfLevel perf_level=
    static_cast<rocksdb::PerfLevel>(rdb_perf_context_level_sysvar);

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
    static_cast<rocksdb::PerfLevel>(rdb_perf_context_level_sysvar);

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
                        &m_table_handler->m_table_perf_context,
                        rdb_global_perf_context);
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

    my_io_perf_sum_atomic_helper(&m_table_handler->m_io_perf_read,
                                 &io_perf_read);
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

  if (!(m_table_handler = rdb_open_tables.get_table_handler(name)))
  {
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  my_core::thr_lock_init(&m_thr_lock);
  my_core::thr_lock_data_init(&m_thr_lock, &m_db_lock, nullptr);

  /*
    note: m_pk_descr may be non-NULL here, when using partitions. It seems,
    it's ok to discard it
  */
  StringBuffer<256> fullname;
  if (rdb_normalize_tablename(name, &fullname)) {
    return HA_ERR_INTERNAL_ERROR;
  }

  if (!(m_tbl_def= rdb_ddl_manager.find(
    reinterpret_cast<uchar*>(fullname.c_ptr()), fullname.length())))
  {
    my_error(ER_INTERNAL_ERROR, MYF(0),
             "Attempt to open a table that is not present in RocksDB-SE data dictionary");
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  m_lock_rows= RDB_LOCK_NONE;
  m_key_descriptors= m_tbl_def->m_key_descriptors;

  uint key_len= 0;
  m_pk_descr= m_key_descriptors[pk_index(table, m_tbl_def)];
  if (rdb_table_has_hidden_pk(table))
  {
    m_pk_key_parts= 1;
  }
  else
  {
    m_pk_key_parts=
      handler::table->key_info[table->s->primary_key].user_defined_key_parts;
    key_len= handler::table->key_info[table->s->primary_key].key_length;
  }

  m_pk_descr->setup(table, m_tbl_def);  // move this into get_table_handler() ??

  uint packed_key_len= m_pk_descr->max_storage_fmt_length();

  if (!(m_pk_tuple= reinterpret_cast<uchar*>(my_malloc(key_len, MYF(0)))) ||
      !(m_pk_packed_tuple=
          reinterpret_cast<uchar*>(my_malloc(packed_key_len, MYF(0)))))
  {
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  /*
    Full table scan actually uses primary key
    (UPDATE needs to know this, otherwise it will go into infinite loop on
    queries like "UPDATE tbl SET pk=pk+100")
  */
  key_used_on_scan= table->s->primary_key;

  /* Sometimes, we may use m_sec_key_packed_tuple for storing packed PK */
  uint max_packed_sec_key_len= packed_key_len;
  for (uint i= 0; i < table->s->keys; i++)
  {
    if (i == table->s->primary_key) /* Primary key was processed above */
      continue;

    // move this into get_table_handler() ??
    m_key_descriptors[i]->setup(table, m_tbl_def);

    uint packed_len= m_key_descriptors[i]->max_storage_fmt_length();
    if (packed_len > max_packed_sec_key_len)
      max_packed_sec_key_len= packed_len;
  }

  setup_field_converters();

  size_t tails_size= max_packed_sec_key_len + CHECKSUM_CHUNK_SIZE;

  if (!(m_sec_key_packed_tuple=
         reinterpret_cast<uchar*>(my_malloc(max_packed_sec_key_len, MYF(0)))) ||
      !(m_sec_key_match_prefix_buf=
         reinterpret_cast<uchar*>(my_malloc(max_packed_sec_key_len, MYF(0)))) ||
      !(m_sec_key_packed_tuple_old=
         reinterpret_cast<uchar*>(my_malloc(max_packed_sec_key_len, MYF(0)))) ||
      !(m_end_key_packed_tuple=
         reinterpret_cast<uchar*>(my_malloc(max_packed_sec_key_len, MYF(0)))) ||
      !((m_sec_key_tails=
         reinterpret_cast<uchar*>(my_malloc(tails_size, MYF(0))))) ||
      !((m_sec_key_tails_old=
         reinterpret_cast<uchar*>(my_malloc(tails_size, MYF(0))))) ||
      !((m_pack_buffer=
         reinterpret_cast<uchar*>(my_malloc(max_packed_sec_key_len, MYF(0))))))
  {
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  info(HA_STATUS_NO_LOCK | HA_STATUS_VARIABLE | HA_STATUS_CONST);

  /* TODO: move the following to where TABLE_SHARE is opened: */
  if (handler::table->found_next_number_field)
  {
    load_auto_incr_value();
  }

  if (rdb_table_has_hidden_pk(table) && load_hidden_pk_value())
  {
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  /* Index block size in MyRocks: used by MySQL in query optimization */
  stats.block_size = rdb_table_options.block_size;

  DBUG_RETURN(0);
}


int ha_rocksdb::close(void)
{
  DBUG_ENTER("ha_rocksdb::close");

  m_pk_descr= nullptr;
  m_key_descriptors= nullptr;

  my_free(m_pk_tuple);
  m_pk_tuple= nullptr;

  my_free(m_pk_packed_tuple);
  m_pk_packed_tuple= nullptr;

  my_free(m_sec_key_packed_tuple);
  m_sec_key_packed_tuple= nullptr;

  my_free(m_sec_key_match_prefix_buf);
  m_sec_key_match_prefix_buf= nullptr;

  my_free(m_sec_key_packed_tuple_old);
  m_sec_key_packed_tuple_old= nullptr;

  my_free(m_end_key_packed_tuple);
  m_end_key_packed_tuple= nullptr;

  my_free(m_sec_key_tails);
  m_sec_key_tails= nullptr;

  my_free(m_sec_key_tails_old);
  m_sec_key_tails_old= nullptr;

  my_free(m_pack_buffer);
  m_pack_buffer= nullptr;

  my_free(m_field_enc);
  m_field_enc= nullptr;

  my_core::thr_lock_delete(&m_thr_lock);

  DBUG_RETURN(rdb_open_tables.free_table_handler(m_table_handler));
}


static const int HA_ERR_ROCKSDB_UNIQUE_NOT_SUPPORTED= HA_ERR_LAST+1;
static const int HA_ERR_ROCKSDB_PK_REQUIRED=          HA_ERR_LAST+2;
static const int HA_ERR_ROCKSDB_TOO_MANY_LOCKS=       HA_ERR_LAST+3;

bool ha_rocksdb::get_error_message(const int error, String *buf)
{
  DBUG_ASSERT(error >= HA_ERR_FIRST);
  DBUG_ASSERT(error <= HA_ERR_LAST);
  DBUG_ASSERT(buf != nullptr);

  Rdb_transaction* tx = get_tx_from_thd(handler::ha_thd());

  if (error == HA_ERR_ROCKSDB_PK_REQUIRED)
    buf->append("Table must have a PRIMARY KEY");
  else if (error == HA_ERR_ROCKSDB_UNIQUE_NOT_SUPPORTED)
    buf->append("Unique indexes are not supported");
  else if (error == HA_ERR_ROCKSDB_TOO_MANY_LOCKS)
    buf->append("Number of locks held reached @@rocksdb_max_row_locks");
  else if (error == HA_ERR_LOCK_WAIT_TIMEOUT)
  {
    DBUG_ASSERT(tx != nullptr);
    buf->append(tx->m_detailed_error);
    return TRUE;
  }
  return FALSE; /* not a temporary error */
}


static bool rdb_is_table_collation_excepted(const std::string& table_name)
{
  bool result = false;

  // Make sure no one else changes the list while we are accessing it.
  mysql_mutex_lock(&rdb_collation_ex_list_mutex);

  // See if this table name matches any in the list
  for (const auto& exception_elem : rdb_collation_exception_list)
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
  mysql_mutex_unlock(&rdb_collation_ex_list_mutex);

  return result;
}


/**
  @brief
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

int ha_rocksdb::create_key_defs(my_core::TABLE *table_arg, const char *db_table,
                                const uint len, HA_CREATE_INFO *create_info)
{
  DBUG_ASSERT(table_arg != nullptr);
  DBUG_ASSERT(db_table != nullptr);
  DBUG_ASSERT(create_info != nullptr);
  DBUG_ASSERT(table_arg->s != nullptr);

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
  std::unique_ptr<rocksdb::WriteBatch> wb= rdb_dict_manager.begin();
  rocksdb::WriteBatch *batch= wb.get();

  /* Create table/key descriptions and put them into the data dictionary */
  m_tbl_def= new Rdb_tbl_def;

  /*
    If no primary key found, create a hidden PK and place it inside table
    definition
  */
  if (rdb_table_has_hidden_pk(table_arg))
  {
    n_keys += 1;
  }

  m_key_descriptors= new Rdb_key_def*[n_keys];

  memset(m_key_descriptors, 0, sizeof(Rdb_key_def*) * n_keys);
  m_tbl_def->m_key_count= n_keys;
  m_tbl_def->m_key_descriptors= m_key_descriptors;

  /*
     The first loop checks the index parameters and creates
     column families if necessary.
  */
  for (i= 0; i < m_tbl_def->m_key_count; i++)
  {
    rocksdb::ColumnFamilyHandle* cf_handle;

    if (rdb_strict_collation_check_sysvar &&
        !is_hidden_pk(i, table_arg, m_tbl_def))
    {
      for (uint part= 0; part < table_arg->key_info[i].actual_key_parts; part++)
      {
        if (!is_myrocks_collation_supported(
            table_arg->key_info[i].key_part[part].field) &&
            !rdb_is_table_collation_excepted(table_arg->s->table_name.str))
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
    if (is_hidden_pk(i, table_arg, m_tbl_def)) {
      comment= nullptr;
      key_name= const_cast<char*>("HIDDEN_PK_ID");
    } else {
      comment= table_arg->key_info[i].comment.str;
      key_name= table_arg->key_info[i].name;
    }

    if (Rdb_cf_manager::looks_like_per_index_cf_typo(comment))
    {
      my_error(ER_NOT_SUPPORTED_YET, MYF(0),
               "column family name looks like a typo of $per_index_cf");
      goto error;
    }
    /* Prevent create from using the system column family */
    if (comment && strcmp(RDB_DEFAULT_SYSTEM_CF_NAME, comment) == 0)
    {
      my_error(ER_WRONG_ARGUMENTS, MYF(0),
               "column family not valid for storing index data");
      goto error;
    }
    bool is_auto_cf_flag;
    cf_handle= rdb_cf_manager.get_or_create_cf(
      rdb_rocksdb_db, comment, db_table,
      key_name, &is_auto_cf_flag);
    if (!cf_handle)
      goto error;

    cf_handles[i]= cf_handle;
    is_cf_reverse[i]= Rdb_cf_manager::is_cf_name_reverse(comment);
    is_auto_cf[i]= is_auto_cf_flag;
  }

  /*
    Get the index numbers (this will update the next_index_number)
    and create Rdb_key_def structures.
  */
  for (i= 0; i < m_tbl_def->m_key_count; i++)
  {
    uint index_id=
      rdb_ddl_manager.get_and_update_next_number(&rdb_dict_manager);
    uint16_t index_dict_version= Rdb_key_def::INDEX_INFO_VERSION_GLOBAL_ID;
    uchar index_type;
    uint16_t kv_version;

    if (is_hidden_pk(i, table_arg, m_tbl_def))
    {
      index_type= Rdb_key_def::INDEX_TYPE_HIDDEN_PRIMARY;
      kv_version= Rdb_key_def::PRIMARY_FORMAT_VERSION_INITIAL;
    }
    else if (i == table_arg->s->primary_key)
    {
      index_type= Rdb_key_def::INDEX_TYPE_PRIMARY;
      kv_version= Rdb_key_def::PRIMARY_FORMAT_VERSION_INITIAL;
    }
    else
    {
      index_type= Rdb_key_def::INDEX_TYPE_SECONDARY;
      kv_version= Rdb_key_def::SECONDARY_FORMAT_VERSION_INITIAL;
    }

    const char *key_name;
    if (is_hidden_pk(i, table_arg, m_tbl_def)) {
      key_name= const_cast<char*>("HIDDEN_PK_ID");
    } else {
      key_name= table_arg->key_info[i].name;
    }

    m_key_descriptors[i]= new Rdb_key_def(index_id, i,
                                          cf_handles[i],
                                          index_dict_version,
                                          index_type,
                                          kv_version,
                                          is_cf_reverse[i],
                                          is_auto_cf[i],
                                          key_name);
  }

  m_pk_descr= m_key_descriptors[pk_index(table_arg, m_tbl_def)];

  m_tbl_def->set_name(db_table, len);
  rdb_dict_manager.lock();
  write_err= rdb_ddl_manager.put_and_write(m_tbl_def, batch)
             || rdb_dict_manager.commit(batch);
  rdb_dict_manager.unlock();

  if (write_err)
    goto error;

  DBUG_RETURN(0);

error:
  /* Delete what we have allocated so far */
  delete m_tbl_def;
  m_tbl_def = nullptr;

  DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
}


int rdb_normalize_tablename(const char *tablename,
                            StringBuffer<256> *strbuf)
{
  DBUG_ASSERT(tablename != nullptr);
  DBUG_ASSERT(strbuf != nullptr);
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
int rdb_split_normalized_tablename(const char *fullname,
                                   StringBuffer<256> *dbbuf,
                                   StringBuffer<256> *tablebuf,
                                   StringBuffer<256> *partitionbuf)
{
  DBUG_ASSERT(fullname != nullptr);
  DBUG_ASSERT(dbbuf != nullptr);
  DBUG_ASSERT(tablebuf != nullptr);
  DBUG_ASSERT(partitionbuf != nullptr);

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

int ha_rocksdb::create(const char *name, my_core::TABLE *table_arg,
                       HA_CREATE_INFO *create_info)
{
  int res;
  StringBuffer<256> strbuf;
  DBUG_ENTER("ha_rocksdb::create");

  if (get_table_if_exists(name)) {
    my_core::THD *thd = my_core::thd_get_current_thd();
    if (thd->lex->sql_command == SQLCOM_TRUNCATE) {
      if (delete_table(name)) {
        DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
      }
    } else {
      if (rdb_normalize_tablename(name, &strbuf)) {
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
  if (rdb_normalize_tablename(name, &strbuf))
  {
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  /*
    TODO(alexyang): Temporarily disable unique indexes support when there is no
    Primary Key
  */
  for (uint keyno= 0; keyno < table_arg->s->keys; keyno++)
  {
    if ((table_arg->key_info[keyno].flags & HA_NOSAME) &&
        rdb_table_has_hidden_pk(table_arg))
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
  {
    m_tbl_def->m_auto_incr_val= create_info->auto_increment_value;
  }

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
  KEY *key_info= &this->handler::table_share->key_info[inx];

  Rdb_field_packing dummy1;
  res= dummy1.setup(key_info->key_part[part].field, inx, part);

  if (res && all_parts)
  {
    for (uint i= 0; i < part; i++)
    {
      Field *field;
      if ((field= key_info->key_part[i].field))
      {
        Rdb_field_packing dummy;
        if (!dummy.setup(field, inx, i))
        {
          /* Cannot do index-only reads for this column */
          res= false;
          break;
        }
      }
    }
  }

  uint pk= this->handler::table_share->primary_key;
  if (inx == pk && all_parts &&
      part + 1 ==
        this->handler::table_share->key_info[pk].user_defined_key_parts)
  {
    m_pk_can_be_decoded= res;
  }

  return res;
}

int ha_rocksdb::read_key_exact(Rdb_key_def *kd,
                               rocksdb::Iterator *iter,
                               bool full_key_match,
                               const rocksdb::Slice& key_slice)
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

int ha_rocksdb::read_before_key(Rdb_key_def *kd,
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
    {
      m_scan_it->Prev();
    }
    else
    {
      m_scan_it->SeekToLast();
    }
  }

  return m_scan_it->Valid() ? 0 : HA_ERR_KEY_NOT_FOUND;
}


int ha_rocksdb::read_after_key(Rdb_key_def *kd,
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

int ha_rocksdb::position_to_correct_key(Rdb_key_def *kd,
                                        my_core::ha_rkey_function find_flag,
                                        bool full_key_match,
                                        const uchar* key,
                                        my_core::key_part_map keypart_map,
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
        uint size = kd->pack_index_tuple(this, table, m_pack_buffer,
                                         m_sec_key_packed_tuple, key,
                                         keypart_map);
        rocksdb::Slice lookup_tuple(
            reinterpret_cast<char*>(m_sec_key_packed_tuple), size);

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

int ha_rocksdb::calc_eq_cond_len(Rdb_key_def *kd,
                                 my_core::ha_rkey_function find_flag,
                                 const rocksdb::Slice& slice,
                                 int bytes_changed_by_succ,
                                 const my_core::key_range *end_key,
                                 uint* end_key_packed_size)
{
  if (find_flag == HA_READ_KEY_EXACT)
    return slice.size();

  if (find_flag == HA_READ_PREFIX_LAST)
  {
    /*
      We have made the kd->successor(m_sec_key_packed_tuple) call above.

      The slice is at least Rdb_key_def::INDEX_NUMBER_SIZE bytes long.
    */
    return slice.size() - bytes_changed_by_succ;
  }

  if (end_key)
  {
    *end_key_packed_size= kd->pack_index_tuple(this, table, m_pack_buffer,
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


int ha_rocksdb::read_row_from_secondary_key(uchar *buf, Rdb_key_def* kd,
                                            bool move_forward)
{
  DBUG_ASSERT(buf != nullptr);
  DBUG_ASSERT(kd != nullptr);

  int rc= 0;
  uint pk_size;

  if (m_key_is_read_only && m_lock_rows == RDB_LOCK_NONE &&
      !rdb_table_has_hidden_pk(this->table))
  {
    /* Get the key columns and primary key value */
    const rocksdb::Slice& rkey= m_scan_it->key();
    pk_size= kd->get_primary_key_tuple(table, m_pk_descr, &rkey,
                                       m_pk_packed_tuple);
    const rocksdb::Slice& value= m_scan_it->value();
    if (pk_size == INVALID_LEN ||
        kd->unpack_record(this, table, buf, &rkey, &value))
    {
      rc= HA_ERR_INTERNAL_ERROR;
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
      if (pk_size == INVALID_LEN)
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
    m_last_rowkey.copy(reinterpret_cast<const char*>(m_pk_packed_tuple),
                       pk_size, &my_charset_bin);
  }

  return rc;
}


/*
  @note
    The problem with this function is that SQL layer calls it, when
     - the table has not been yet opened (no ::open() call done)
     - this->handler::table_share already exists, but it is in the process of
        being filled, so some of fields are still NULL.
     - In particular, this->handler::table_share->key_info[inx].key_part[] is
        filled only up to part #part. Subsequent key parts are not yet filled.

    To complicate things further, SQL layer will call index_flags() with
    all_parts=TRUE. Essentially, we're asked to provide flags for reading
    keyparts whose datatype is not yet known.

    We walk around this problem by using check_keyread_allowed(), which uses
    this->handler::table_share object and is careful not to step on unitialized
    data.

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

  if (inx == this->handler::table_share->primary_key)
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

  @detail
    m_scan_it points at the index key-value pair that we should read the
    (pk,row) pair for.
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
    if (m_key_descriptors[keyno]->covers_key(key))
    {
      int rc;

      //TODO: We could here check if we have ran out of range we're scanning
      uint size= m_key_descriptors[keyno]->get_primary_key_tuple(
          table, m_pk_descr, &key, m_pk_packed_tuple);
      if (size == (uint)-1)
        return HA_ERR_INTERNAL_ERROR;

      m_last_rowkey.copy((const char*)m_pk_packed_tuple, size, &my_charset_bin);

      if (m_key_is_read_only && m_lock_rows == RDB_LOCK_NONE &&
          !rdb_table_has_hidden_pk(this->table))
      {
        rocksdb::Slice value= m_scan_it->value();
        if (m_key_descriptors[keyno]->unpack_record(this, table, buf, &key,
                                                    &value))
        {
          return HA_ERR_INTERNAL_ERROR;
        }
        rc= 0;
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
int ha_rocksdb::read_range_first(const my_core::key_range *start_key,
                                 const my_core::key_range *end_key,
                                 bool eq_range_arg, bool sorted)
{
  int result;
  DBUG_ENTER("ha_rocksdb::read_range_first");

  eq_range= eq_range_arg;
  set_end_range(end_key, RANGE_SCAN_ASC);

  range_key_part= table->key_info[handler::active_index].key_part;

  if (!start_key)                       // Read first record
    result= ha_index_first(table->record[0]);
  else
  {
    if (is_using_prohibited_gap_locks(table->in_use,
                                      table->reginfo.lock_type,
                                      is_using_full_primary_key(
                                        handler::active_index,
                                        start_key->keypart_map,
                                        start_key->flag)))
    {
      DBUG_RETURN(HA_ERR_LOCK_DEADLOCK);
    }

    MYSQL_TABLE_IO_WAIT(m_psi, PSI_TABLE_FETCH_ROW, handler::active_index, 0,
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
                               my_core::key_part_map keypart_map,
                               my_core::ha_rkey_function find_flag)
{
  int rc= index_read_map_impl(buf, key, keypart_map, find_flag, nullptr);
  if (rc == HA_ERR_KEY_NOT_FOUND)
    rc = HA_ERR_END_OF_FILE;
  return rc;
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
                                    my_core::key_part_map keypart_map,
                                    my_core::ha_rkey_function find_flag,
                                    const my_core::key_range *end_key)
{
  int rc= 0;
  DBUG_ENTER("ha_rocksdb::index_read_map");

  Rdb_key_def *kd= m_key_descriptors[handler::active_index];
  uint actual_key_parts= kd->get_m_key_parts();
  bool using_full_key= is_using_full_key(keypart_map, actual_key_parts);

  if (!end_key)
    end_key= end_range;

  /* By default, we don't need the retrieved records to match the prefix */
  m_sec_key_match_prefix= nullptr;
  stats.rows_requested++;
  io_perf_start();

  if (handler::active_index == table->s->primary_key &&
      find_flag == HA_READ_KEY_EXACT && using_full_key)
  {
    /*
      Equality lookup over primary key, using full tuple.
      This is a special case, use DB::Get.
    */
    uint size= kd->pack_index_tuple(this,
                                    table, m_pack_buffer, m_pk_packed_tuple,
                                    key, keypart_map);
    rc= get_row_by_rowid(buf, m_pk_packed_tuple, size);
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
  if (handler::active_index != table->s->primary_key &&
      table->key_info[handler::active_index].flags & HA_NOSAME &&
      find_flag == HA_READ_KEY_EXACT && using_full_key)
  {
    my_core::key_part_map tmp_map=
      (my_core::key_part_map(1) <<
        table->key_info[handler::active_index].user_defined_key_parts) - 1;
    packed_size= kd->pack_index_tuple(this, table, m_pack_buffer,
                                      m_sec_key_packed_tuple, key, tmp_map);
    if (table->key_info[handler::active_index].user_defined_key_parts !=
        kd->get_m_key_parts())
    {
      using_full_key= false;
    }
  }
  else
  {
    packed_size= kd->pack_index_tuple(this, table, m_pack_buffer,
                                      m_sec_key_packed_tuple, key,
                                      keypart_map);
  }
  if ((pushed_idx_cond && pushed_idx_cond_keyno == handler::active_index) &&
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
    m_sec_key_match_prefix= m_sec_key_match_prefix_buf;
    m_sec_key_match_length= packed_size;
    memcpy(m_sec_key_match_prefix, m_sec_key_packed_tuple, packed_size);
  }

  int bytes_changed_by_succ= 0;
  if (find_flag == HA_READ_PREFIX_LAST_OR_PREV ||
      find_flag == HA_READ_PREFIX_LAST ||
      find_flag == HA_READ_AFTER_KEY)
  {
    /* See below */
    bytes_changed_by_succ= kd->successor(m_sec_key_packed_tuple, packed_size);
  }

  rocksdb::Slice slice(reinterpret_cast<char*>(m_sec_key_packed_tuple),
                       packed_size);

  uint end_key_packed_size= 0;
  uint eq_cond_len= calc_eq_cond_len(kd, find_flag, slice,
                                     bytes_changed_by_succ, end_key,
                                     &end_key_packed_size);

  bool use_all_keys= false;
  if (find_flag == HA_READ_KEY_EXACT &&
      my_count_bits(keypart_map) == kd->get_m_key_parts())
    use_all_keys= true;

  Rdb_transaction *tx= rdb_get_or_create_tx(table->in_use);
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
      io_perf_end_and_record();
      DBUG_RETURN(rc);
    }

    m_skip_scan_it_next_call= false;

    /*
      Now get the data for the row into 'buf'.  If we were using a primary key
      then we have all the rows we need.  For a secondary key we now need to
      lookup the primary key.
    */
    if (handler::active_index == this->handler::table->s->primary_key)
    {
      rc= read_row_from_primary_key(buf);
    }
    else
    {
      rc= read_row_from_secondary_key(buf, kd, move_forward);
    }

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
  io_perf_end_and_record();
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

  if (pushed_idx_cond && pushed_idx_cond_keyno == handler::active_index)
  {
    Rdb_key_def *kd= m_key_descriptors[handler::active_index];
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

      if (m_sec_key_match_prefix)
      {
        rocksdb::Slice prefix(
          reinterpret_cast<const char*>(m_sec_key_match_prefix),
          m_sec_key_match_length);
        if (!kd->value_matches_prefix(rkey, prefix))
        {
          table->status= STATUS_NOT_FOUND;
          return HA_ERR_END_OF_FILE;
        }
      }

      rocksdb::Slice value= m_scan_it->value();
      if (kd->unpack_record(this, table, buf, &rkey, &value))
      {
        return HA_ERR_INTERNAL_ERROR;
      }

      enum icp_result icp_status= check_index_cond();
      if (icp_status == ICP_NO_MATCH)
      {
        if (move_forward)
        {
          m_scan_it->Next();
        }
        else
        {
          m_scan_it->Prev();
        }
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
                                    my_core::key_part_map keypart_map)
{
  return index_read_map(buf, key, keypart_map, HA_READ_PREFIX_LAST);
}


/*
  Print a value into provided buffer, in hex form.
*/
void rdb_dump_hex(char *strbuf, size_t strbuf_size, const rocksdb::Slice &val)
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


int ha_rocksdb::check(
  my_core::THD* thd __attribute__((__unused__)),
  my_core::HA_CHECK_OPT* check_opt __attribute__((__unused__)))
{
  uint pk= pk_index(handler::table, m_tbl_def);
  String rowkey_copy;
  String sec_key_copy;
  const char* table_name = handler::table->s->table_name.str;

  // Only when debugging: don't use snapshot when reading
  // Rdb_transaction *tx= rdb_get_or_create_tx(table->in_use);
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

        if (m_key_descriptors[keyno]->unpack_info_has_checksum(
              m_scan_it->value()))
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
        if (rdb_table_has_hidden_pk(table) &&
            read_hidden_pk_id_from_rowkey(&hidden_pk_id))
        {
          goto error;
        }

        /* Check if we get the same PK value */
        uint packed_size= m_pk_descr->pack_record(this, table, m_pack_buffer,
                                                  table->record[0],
                                                  m_pk_packed_tuple, nullptr,
                                                  nullptr, 0, nullptr,
                                                  hidden_pk_id);
        if (packed_size != rowkey_copy.length() ||
            memcmp(m_pk_packed_tuple, rowkey_copy.ptr(), packed_size))
        {
          // NO_LINT_DEBUG
          sql_print_error("CHECKTABLE %s:   .. row %lld: PK value mismatch",
                          table_name, rows);
          goto print_and_error;
        }

        /* Check if we get the same secondary key value */
        int tail_size;
        packed_size= m_key_descriptors[keyno]->pack_record(
            this, table, m_pack_buffer, table->record[0],
            m_sec_key_packed_tuple, m_sec_key_tails, &tail_size,
            0, nullptr, hidden_pk_id);
        if (packed_size != sec_key_copy.length() ||
            memcmp(m_sec_key_packed_tuple, sec_key_copy.ptr(), packed_size))
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
          rdb_dump_hex(buf, 1000, rocksdb::Slice(rowkey_copy.ptr(),
                                                 rowkey_copy.length()));
          // NO_LINT_DEBUG
          sql_print_error("CHECKTABLE %s:   rowkey: %s", table_name, buf);

          rdb_dump_hex(buf, 1000, rocksdb::Slice(m_retrieved_record.data(),
                                                 m_retrieved_record.size()));
          // NO_LINT_DEBUG
          sql_print_error("CHECKTABLE %s:   record: %s", table_name, buf);

          rdb_dump_hex(buf, 1000, rocksdb::Slice(sec_key_copy.ptr(),
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


#ifndef NDEBUG

static void rdb_dbug_dump_str(FILE *out, const char *str, int len)
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
    rdb_dbug_dump_database(rdb_rocksdb_db);
*/

void rdb_dbug_dump_database(rocksdb::DB *db)
{
  FILE *out= fopen("/tmp/rocksdb.dump", "wt");
  if (!out)
    return;

  rocksdb::Iterator* it = db->NewIterator(rocksdb::ReadOptions());
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    rocksdb::Slice key= it->key();
    rocksdb::Slice val= it->value();
    rdb_dbug_dump_str(out, key.data(), key.size());
    fprintf(out, " -> ");
    rdb_dbug_dump_str(out, val.data(), val.size());
    fprintf(out, "\n");
  }

  delete it;
  fclose(out);
}

#endif  // NDEBUG


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

  Rdb_transaction *tx= rdb_get_or_create_tx(table->in_use);
  DBUG_ASSERT(tx != nullptr);

  DEBUG_SYNC(handler::ha_thd(), "rocksdb.get_row_by_rowid");
  DBUG_EXECUTE_IF("dbug.rocksdb.get_row_by_rowid",
                  {
                    my_core::THD *thd= handler::ha_thd();
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
  {
    s= tx->get_for_update(m_pk_descr->get_cf(), key_slice, &m_retrieved_record);
  }

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
  if (m_key_descriptors[handler::active_index]->m_is_reverse_cf)
  {
    moves_forward= false;
  }

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
  if (m_key_descriptors[handler::active_index]->m_is_reverse_cf)
  {
    moves_forward= true;
  }

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

  if (handler::active_index == handler::table->s->primary_key)
  {
    rc= rnd_next_with_direction(buf, move_forward);
  }
  else
  {
    if (m_skip_scan_it_next_call)
    {
      m_skip_scan_it_next_call= FALSE;
    }
    else
    {
      if (move_forward)
      {
        m_scan_it->Next();  /* this call cannot fail */
      }
      else
      {
        m_scan_it->Prev();
      }
    }
    rc= find_icp_matching_index_rec(move_forward, buf);
    if (!rc)
    {
      rc= secondary_index_read(handler::active_index, buf);
    }
  }

  DBUG_RETURN(rc);
}


int ha_rocksdb::index_first(uchar *buf)
{
  io_perf_start();
  m_sec_key_match_prefix= nullptr;
  int rc= m_key_descriptors[handler::active_index]->m_is_reverse_cf ?
      index_last_intern(buf): index_first_intern(buf);
  io_perf_end_and_record();
  if (rc == HA_ERR_KEY_NOT_FOUND)
    rc = HA_ERR_END_OF_FILE;
  return rc;
}


int ha_rocksdb::index_last(uchar *buf)
{
  io_perf_start();
  m_sec_key_match_prefix= nullptr;
  int rc= m_key_descriptors[handler::active_index]->m_is_reverse_cf ?
      index_first_intern(buf) : index_last_intern(buf);
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
  DBUG_ASSERT(buf != nullptr);

  uchar *key;
  uint key_size;
  int rc;
  DBUG_ENTER("ha_rocksdb::index_first");

  if (is_pk(handler::active_index, handler::table, m_tbl_def))
  {
    key= m_pk_packed_tuple;
  }
  else
  {
    key= m_sec_key_packed_tuple;
  }

  DBUG_ASSERT(key != nullptr);

  if (m_key_descriptors[handler::active_index]->m_is_reverse_cf)
  {
    m_key_descriptors[handler::active_index]->get_supremum_key(key, &key_size);
  }
  else
  {
    m_key_descriptors[handler::active_index]->get_infimum_key(key, &key_size);
  }

  rocksdb::Slice index_key((const char*)key, key_size);

  Rdb_transaction *tx= rdb_get_or_create_tx(table->in_use);
  DBUG_ASSERT(tx != nullptr);

  bool is_new_snapshot= !tx->has_snapshot();
  // Loop as long as we get a deadlock error AND we end up creating the
  // snapshot here (i.e. it did not exist prior to this)
  for (;;)
  {
    setup_scan_iterator(m_key_descriptors[handler::active_index],
                        &index_key, false,
                        !m_key_descriptors[handler::active_index]->
                            m_is_reverse_cf,
                        Rdb_key_def::INDEX_NUMBER_SIZE);
    m_skip_scan_it_next_call= TRUE;

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
  DBUG_ASSERT(buf != nullptr);

  uchar *key;
  uint key_size;
  int rc;
  DBUG_ENTER("ha_rocksdb::index_last");

  if (is_pk(handler::active_index, handler::table, m_tbl_def))
  {
    key= m_pk_packed_tuple;
  }
  else
  {
    key= m_sec_key_packed_tuple;
  }

  DBUG_ASSERT(key != nullptr);

  if (m_key_descriptors[handler::active_index]->m_is_reverse_cf)
  {
    m_key_descriptors[handler::active_index]->get_infimum_key(key, &key_size);
  }
  else
  {
    m_key_descriptors[handler::active_index]->get_supremum_key(key, &key_size);
  }

  rocksdb::Slice index_key((const char*)key, key_size);

  Rdb_transaction *tx= rdb_get_or_create_tx(table->in_use);
  DBUG_ASSERT(tx != nullptr);

  bool is_new_snapshot= !tx->has_snapshot();
  // Loop as long as we get a deadlock error AND we end up creating the
  // snapshot here (i.e. it did not exist prior to this)
  for (;;)
  {
    setup_scan_iterator(m_key_descriptors[handler::active_index],
                        &index_key, false,
                        m_key_descriptors[handler::active_index]->
                            m_is_reverse_cf,
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
    m_skip_scan_it_next_call= FALSE;

    if (is_pk(handler::active_index, handler::table, m_tbl_def))
    {
      m_skip_scan_it_next_call= true;
      rc= rnd_next_with_direction(buf, false);
    }
    else
    {
      rc= find_icp_matching_index_rec(false /*move_forward*/, buf);
      if (!rc)
        rc= secondary_index_read(handler::active_index, buf);
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
    Rdb_transaction *tx= rdb_get_or_create_tx(handler::table->in_use);
    tx->release_lock(m_pk_descr->get_cf(),
                     std::string(m_last_rowkey.ptr(), m_last_rowkey.length()));
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
  return (index != pk_index(this->table, m_tbl_def) ||
          (!rdb_table_has_hidden_pk(this->table) &&
          this->table->key_info[index].actual_key_parts ==
              this->table->s->fields));
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
bool rdb_table_has_hidden_pk(const my_core::TABLE* table_arg)
{
  DBUG_ASSERT(table_arg != nullptr);
  return table_arg->s->primary_key == MAX_INDEXES;
}


/*
  Returns true if given index number is a hidden_pk.
  - This is used when a table is created with no primary key.
*/
bool ha_rocksdb::is_hidden_pk(const uint index, const my_core::TABLE* table_arg,
                              const Rdb_tbl_def* tbl_def_arg)
{
  DBUG_ASSERT(table_arg != nullptr);
  DBUG_ASSERT(table_arg->s != nullptr);
  DBUG_ASSERT(tbl_def_arg != nullptr);

  return (table_arg->s->primary_key == MAX_INDEXES &&
          index == tbl_def_arg->m_key_count - 1);
}


/* Returns index of primary key */
uint ha_rocksdb::pk_index(const my_core::TABLE* table_arg,
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


int ha_rocksdb::write_row(uchar *buf)
{
  DBUG_ENTER("ha_rocksdb::write_row");
  DBUG_ASSERT(buf == this->table->record[0]);
  DBUG_ASSERT(m_lock_rows == RDB_LOCK_WRITE);

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
  Rdb_transaction *tx= rdb_get_or_create_tx(table->in_use);

  if (rv == 0 && commit_in_the_middle() &&
      tx->get_n_writes() >= THDVAR(table->in_use, bulk_load_size))
  {
    /* Bulk insert mode: flush the write batch if it became too big */
    if (tx->flush_batch())
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
  if (old_data && !rpl_lookup_rows())
  {
    int old_pk_size= m_pk_descr->pack_record(this, table, m_pack_buffer,
                                           old_data,
                                           m_pk_packed_tuple, nullptr,
                                           nullptr, 0, nullptr);
    m_last_rowkey.copy((const char*)m_pk_packed_tuple, old_pk_size,
                       &my_charset_bin);
  }
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

  set_last_rowkey(old_data);

  Rdb_transaction *tx= rdb_get_or_create_tx(handler::table->in_use);

  /*
    Get new row key for any insert, and any update where the pk is not hidden.
    Row key for updates with hidden pk is handled below.
  */
  if (!rdb_table_has_hidden_pk(handler::table))
  {
    pk_packed_size= m_pk_descr->pack_record(this, table, m_pack_buffer,
                                            new_data,
                                            m_pk_packed_tuple, nullptr,
                                            nullptr, 0, nullptr);
  }
  else if (!old_data)
  {
    hidden_pk_id= update_hidden_pk_val();
    pk_packed_size= m_pk_descr->pack_hidden_pk(hidden_pk_id, m_pk_packed_tuple);
  }

  if (old_data)
  {
    old_pk_key_slice= rocksdb::Slice(m_last_rowkey.ptr(),
                                     m_last_rowkey.length());
    /*
      If hidden primary key, rowkey for new record will always be the same as
      before
    */
    if (rdb_table_has_hidden_pk(handler::table))
    {
      memcpy(m_pk_packed_tuple, old_pk_key_slice.data(),
             old_pk_key_slice.size());
      pk_packed_size= old_pk_key_slice.size();
      if (read_hidden_pk_id_from_rowkey(&hidden_pk_id))
      {
        DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
      }
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
  for (uint i= 0; i < m_tbl_def->m_key_count; i++)
  {
    KEY* key_info= nullptr;
    uint n_null_fields= 0;
    uint user_defined_key_parts= 1;
    if (!is_hidden_pk(i, handler::table, m_tbl_def))
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
      if (is_pk(i, handler::table, m_tbl_def))
      {
        old_key_slice= old_pk_key_slice;
      }
      else
      {
        if (!m_updated_indexes.is_set(i))
        {
          continue;
        }

        old_packed_size= m_key_descriptors[i]->pack_record(
            this, table, m_pack_buffer, old_data, m_sec_key_packed_tuple_old,
            nullptr, nullptr, user_defined_key_parts, nullptr, hidden_pk_id);
        old_key_slice= rocksdb::Slice((const char*)m_sec_key_packed_tuple_old,
                                      old_packed_size);
      }
    }

    /*
      Calculate the new key for obtaining the lock
    */
    if (is_pk(i, handler::table, m_tbl_def))
    {
      new_key_packed_tuple= m_pk_packed_tuple;
      new_packed_size= pk_packed_size;
    }
    else
    {
      /*
        For unique secondary indexes, the key used for locking does not
        include the extended fields.
      */
      new_packed_size= m_key_descriptors[i]->pack_record(
          this, table, m_pack_buffer, new_data, m_sec_key_packed_tuple, nullptr,
          nullptr, user_defined_key_parts, &n_null_fields, 0);
      new_key_packed_tuple = m_sec_key_packed_tuple;
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
    if (!Rdb_pk_comparator::bytewise_compare(new_key_slice, old_key_slice) ||
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
    if (is_pk(i, handler::table, m_tbl_def))
    {
      /* Primary key has changed, it should be deleted later. */
      if (old_data)
        pk_key_changed= true;

      rocksdb::Status s;

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
      s= tx->get_for_update(m_pk_descr->get_cf(), new_key_slice,
          &m_retrieved_record);

      if (!s.ok() && !s.IsNotFound())
        DBUG_RETURN(tx->set_status_error(table->in_use, s, m_key_descriptors[i],
                                         m_tbl_def));

      found= !s.IsNotFound();
    }
    else
    {
      bool all_parts_used= (user_defined_key_parts ==
                            m_key_descriptors[i]->get_m_key_parts());
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
        !rdb_can_use_bloom_filter(m_key_descriptors[i], new_key_slice,
                                  all_parts_used,
                                  is_ascending(m_key_descriptors[i],
                                  HA_READ_KEY_EXACT));
      bool fill_cache= !THDVAR(handler::ha_thd(), skip_fill_cache);

      /*
        psergey-todo: we just need to take lock, lookups not needed:
      */
      std::string dummy_value;
      rocksdb::Status s= tx->get_for_update(m_key_descriptors[i]->get_cf(),
                                            new_key_slice,
                                            &dummy_value);
      if (!s.ok() && !s.IsNotFound())
      {
        DBUG_RETURN(tx->set_status_error(table->in_use, s, m_key_descriptors[i],
                                         m_tbl_def));
      }

      rocksdb::Iterator *iter=
          tx->get_iterator(m_key_descriptors[i]->get_cf(),
                           total_order_seek,
                           fill_cache,
                           true /* read current data */,
                           false /* acquire snapshot */);
      /*
        Need to scan the transaction to see if there is a duplicate key.
        Also need to scan RocksDB and verify the key has not been deleted
        in the transaction.
      */
      iter->Seek(new_key_slice);
      found= !read_key_exact(m_key_descriptors[i], iter, all_parts_used,
                             new_key_slice);
      delete iter;
    }

    if (found)
    {
      /* There is a row with this rowid already, so error out. */
      errkey= i;
      m_dupp_errkey= errkey;
      DBUG_RETURN(HA_ERR_FOUND_DUPP_KEY);
    }
  }

  DEBUG_SYNC(handler::ha_thd(), "rocksdb.update_write_row_after_unique_check");

  /*
    At this point, all locks have been obtained, and all checks for duplicate
    keys have been performed. No further errors can be allowed to occur from
    here because updates to the transaction will be made and those updates
    cannot be easily removed without rolling back the entire transaction.
  */
  for (uint i= 0; i < m_tbl_def->m_key_count; i++)
  {
    if (is_hidden_pk(i, handler::table, m_tbl_def))
    {
      continue;
    }

    /*
      Determine if the old key needs to be deleted.
    */
    if (is_pk(i, handler::table, m_tbl_def))
    {
      if (pk_key_changed)
      {
        rocksdb::Status s=
            delete_or_singledelete(i, tx,
                                   m_key_descriptors[i]->get_cf(),
                                   old_pk_key_slice);
        if (!s.ok())
        {
          DBUG_RETURN(tx->set_status_error(table->in_use, s,
                                           m_key_descriptors[i],
                                           m_tbl_def));
        }
      }
      continue;
    }

    /*
      Can skip updating this key if none of the key fields have changed.
    */
    if (old_data && !m_updated_indexes.is_set(i))
    {
      continue;
    }

    new_packed_size= m_key_descriptors[i]->
        pack_record(this, table, m_pack_buffer, new_data,
                    m_sec_key_packed_tuple, m_sec_key_tails,
                    &new_key_tail_size, 0, nullptr, hidden_pk_id);
    new_key_slice= rocksdb::Slice((const char*)m_sec_key_packed_tuple,
                                   new_packed_size);
    new_value_slice= rocksdb::Slice((const char*)m_sec_key_tails,
                                    new_key_tail_size);

    if (old_data)
    {
      // The old value (just used for delete)
      old_packed_size= m_key_descriptors[i]->
          pack_record(this, table, m_pack_buffer, old_data,
                      m_sec_key_packed_tuple_old, m_sec_key_tails_old,
                      &old_key_tail_size, 0, nullptr, hidden_pk_id);
      old_key_slice= rocksdb::Slice((const char*)m_sec_key_packed_tuple_old,
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
          memcmp(m_sec_key_packed_tuple_old, m_sec_key_packed_tuple,
                 old_packed_size) == 0 &&
          old_key_tail_size == new_key_tail_size &&
          memcmp(m_sec_key_tails_old, m_sec_key_tails, old_key_tail_size) == 0)
        continue;

      /*
        Deleting entries from secondary index should skip locking, but
        be visible to the transaction.
        (also note that DDL statements do not delete rows, so this is not a DDL
         statement)
      */
      tx->get_indexed_write_batch()->
          SingleDelete(m_key_descriptors[i]->get_cf(), old_key_slice);
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
    if ((tx->m_ddl_transaction && !(table->key_info[i].flags & HA_NOSAME)) ||
        skip_unique_check)
    {
      write_batch= tx->get_blind_write_batch();
    }
    else
    {
      write_batch= tx->get_indexed_write_batch();
    }

    write_batch->Put(m_key_descriptors[i]->get_cf(), new_key_slice,
                     new_value_slice);
  }

  if (table->next_number_field)
    update_auto_incr_val();

  new_key_slice= rocksdb::Slice((const char*)m_pk_packed_tuple, pk_packed_size);
  rocksdb::Slice value_slice;

  convert_record_to_storage_format(pk_packed_size, &value_slice);
  if (skip_unique_check)
  {
    /*
      It is responsibility of the user to make sure that the data being
      inserted doesn't violate any unique keys.
    */
    tx->get_blind_write_batch()->Put(m_pk_descr->get_cf(), new_key_slice,
                                     value_slice);
  }
  else if (tx->m_ddl_transaction)
  {
    /*
      DDL statement must check for unique key conflicts. For example:
      ALTER TABLE tbl DROP PRIMARY KEY, ADD PRIMARY KEY(non_unique_column)
    */
    tx->get_indexed_write_batch()->Put(m_pk_descr->get_cf(), new_key_slice,
                                     value_slice);
  }
  else
  {
    auto s= tx->put(m_pk_descr->get_cf(), new_key_slice, value_slice);
    if (!s.ok())
    {
      if (s.IsBusy())
      {
        errkey= table->s->primary_key;
        m_dupp_errkey= errkey;
        DBUG_RETURN(HA_ERR_FOUND_DUPP_KEY);
      }

      DBUG_RETURN(tx->set_status_error(handler::table->in_use, s, m_pk_descr,
                                       m_tbl_def));
    }
  }

  DBUG_RETURN(0);
}


/*
  Open a cursor and position it at the passed record
*/

void ha_rocksdb::setup_scan_iterator(Rdb_key_def *keydef,
                                     rocksdb::Slice *slice,
                                     const bool use_all_keys,
                                     const bool is_ascending,
                                     const uint eq_cond_len)
{
  DBUG_ASSERT(keydef != nullptr);
  DBUG_ASSERT(slice != nullptr);
  DBUG_ASSERT(slice->size() >= eq_cond_len);

  Rdb_transaction *tx= rdb_get_or_create_tx(table->in_use);

  bool skip_bloom= true;

  rocksdb::Slice eq_cond(slice->data(), eq_cond_len);
  if (rdb_can_use_bloom_filter(keydef, eq_cond, use_all_keys, is_ascending))
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
    bool fill_cache= !THDVAR(handler::ha_thd(), skip_fill_cache);
    m_scan_it= tx->get_iterator(keydef->get_cf(), skip_bloom, fill_cache);
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
  {
    m_pk_descr->get_supremum_key(m_pk_packed_tuple, &key_size);
  }
  else
  {
    m_pk_descr->get_infimum_key(m_pk_packed_tuple, &key_size);
  }

  rocksdb::Slice table_key((const char*)m_pk_packed_tuple, key_size);

  setup_scan_iterator(m_pk_descr, &table_key);
  m_skip_scan_it_next_call= true;
}


int ha_rocksdb::rnd_init(bool scan)
{
  DBUG_ENTER("ha_rocksdb::rnd_init");

  io_perf_start();

  Rdb_transaction *tx= rdb_get_or_create_tx(table->in_use);
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
    if (rc != HA_ERR_LOCK_DEADLOCK || !m_rnd_scan_is_new_snapshot)
    {
      break;  /* exit the loop */
    }

    // release the snapshot and iterator and then regenerate them
    Rdb_transaction *tx= rdb_get_or_create_tx(table->in_use);
    tx->release_snapshot();
    release_scan_iterator();
    setup_iterator_for_rnd_scan();
  }

  m_rnd_scan_is_new_snapshot= false;

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

  if (!m_scan_it || !m_scan_it->Valid())
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

  if (m_skip_scan_it_next_call)
  {
    m_skip_scan_it_next_call= false;
  }
  else
  {
    if (move_forward)
    {
      m_scan_it->Next();  /* this call cannot fail */
    }
    else
    {
      m_scan_it->Prev();  /* this call cannot fail */
    }
  }

  if (m_scan_it->Valid())
  {
    /* check if we're out of this table */
    rocksdb::Slice key= m_scan_it->key();
    if (!m_pk_descr->covers_key(key))
    {
      rc= HA_ERR_END_OF_FILE;
      table->status= STATUS_NOT_FOUND;
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
        Rdb_transaction *tx= rdb_get_or_create_tx(table->in_use);
        DEBUG_SYNC(handler::ha_thd(), "rocksdb_concurrent_delete");
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
  Rdb_transaction *tx= rdb_get_or_create_tx(table->in_use);
  DBUG_ASSERT(tx != nullptr);

  // If m_lock_rows is not RDB_LOCK_NONE then we will be doing a get_for_update
  // when accessing the index, so don't acquire the snapshot right away.
  // Otherwise acquire the snapshot immediately.
  tx->acquire_snapshot(m_lock_rows == RDB_LOCK_NONE);

  this->handler::active_index= idx;

  return 0;
}


int ha_rocksdb::index_end()
{
  release_scan_iterator();

  handler::active_index= MAX_KEY;
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
  remove_rows();
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

  set_last_rowkey(buf);

  rocksdb::Slice key_slice(m_last_rowkey.ptr(), m_last_rowkey.length());
  Rdb_transaction *tx= rdb_get_or_create_tx(table->in_use);

  uint index= pk_index(handler::table, m_tbl_def);
  rocksdb::Status s= delete_or_singledelete(index, tx,
                                            m_pk_descr->get_cf(),
                                            key_slice);
  if (!s.ok())
  {
    DBUG_RETURN(tx->set_status_error(table->in_use, s, m_pk_descr, m_tbl_def));
  }

  longlong hidden_pk_id= 0;
  if (m_tbl_def->m_key_count > 1 &&
      rdb_table_has_hidden_pk(table) &&
      read_hidden_pk_id_from_rowkey(&hidden_pk_id))
  {
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  // Delete the record for every secondary index
  for (uint i= 0; i < m_tbl_def->m_key_count; i++)
  {
    if (!is_pk(i, handler::table, m_tbl_def))
    {
      int packed_size= m_key_descriptors[i]->pack_record(
          this, table, m_pack_buffer, buf, m_sec_key_packed_tuple,
          nullptr, nullptr, 0, nullptr, hidden_pk_id);
      rocksdb::Slice secondary_key_slice(
          reinterpret_cast<char*>(m_sec_key_packed_tuple),
          packed_size);
      /* Deleting on secondary key doesn't need any locks: */
      tx->get_indexed_write_batch()->
          SingleDelete(m_key_descriptors[i]->get_cf(), secondary_key_slice);
    }
  }

  this->handler::stats.rows_deleted++;
  update_row_stats(ROWS_DELETED);

  io_perf_end_and_record();

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

  this->handler::stats.records= 0;
  this->handler::stats.index_file_length= 0ul;
  this->handler::stats.data_file_length= 0ul;
  this->handler::stats.mean_rec_length= 0;
  for (uint i= 0; i < m_tbl_def->m_key_count; i++)
  {
    if (is_pk(i, handler::table, m_tbl_def))
    {
      this->handler::stats.data_file_length=
          m_pk_descr->m_stats.m_actual_disk_size;
      this->handler::stats.records= m_pk_descr->m_stats.m_rows;
    }
    else
    {
      this->handler::stats.index_file_length+=
          m_key_descriptors[i]->m_stats.m_actual_disk_size;
    }
  }
  DBUG_VOID_RETURN;
}


int ha_rocksdb::info(uint flag)
{
  DBUG_ENTER("ha_rocksdb::info");

  if (!this->handler::table)
  {
    return 1;
  }

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

    if (this->handler::stats.records == 0)
    {
      // most likely, the table is in memtable
      // try to deduce from GetApproximateSizes
      Rdb_key_def *kd= m_pk_descr;
      uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE*2];
      auto r= get_range(pk_index(table, m_tbl_def), buf);
      uint64_t sz= 0;
      rdb_rocksdb_db->GetApproximateSizes(
        kd->get_cf(),
        &r, 1,
        &sz, true);
      this->handler::stats.records= sz/RDB_ASSUMED_KEY_VALUE_DISK_SIZE;
      this->handler::stats.data_file_length= sz;

      if (rdb_debug_optimizer_n_rows_sysvar > 0)
      {
        this->handler::stats.records= rdb_debug_optimizer_n_rows_sysvar;
      }
    }

    if (this->handler::stats.records != 0)
    {
      this->handler::stats.mean_rec_length=
          this->handler::stats.data_file_length / this->handler::stats.records;
    }
  }
  if (flag & HA_STATUS_CONST)
  {
    ref_length= m_pk_descr->max_storage_fmt_length();

    // TODO: Needs to reimplement after having real index statistics
    for (uint i= 0; i < m_tbl_def->m_key_count; i++)
    {
      if (is_hidden_pk(i, handler::table, m_tbl_def))
      {
        continue;
      }
      my_core::KEY* k= &handler::table->key_info[i];
      for (uint j = 0; j < k->actual_key_parts; j++)
      {
        const Rdb_index_stats& k_stats= m_key_descriptors[i]->m_stats;
        uint x = k_stats.m_distinct_keys_per_prefix.size() > j &&
                 k_stats.m_distinct_keys_per_prefix[j] > 0 ?
          k_stats.m_rows / k_stats.m_distinct_keys_per_prefix[j] :
          0;
        if (x > stats.records)
          x = stats.records;
        if ((x == 0 && rdb_dbg_optimizer_no_zero_cardinality_sysvar) ||
            rdb_debug_optimizer_n_rows_sysvar > 0)
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
    dup_ref= m_pk_tuple;  // TODO: this should store packed PK.
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
  if (rdb_table_has_hidden_pk(table) &&
      read_hidden_pk_id_from_rowkey(&hidden_pk_id))
  {
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
  uint packed_size= m_pk_descr->pack_record(this, table, m_pack_buffer, record,
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
  if (!m_updated_indexes_is_valid)
  {
    m_updated_indexes_is_valid= true;
    m_updated_indexes.clear_all();

    for (uint keynr= 0; keynr < table->s->keys; keynr++)
    {
      /* Walk over all key parts, including the "extended key" suffix */
      uint key_parts= m_key_descriptors[keynr]->get_m_key_parts();
      for (uint kp= 0; kp < key_parts; kp++)
      {
        if (rdb_table_has_hidden_pk(table) && kp + 1 == key_parts)
        {
          break;
        }

        Field *field=
            m_key_descriptors[keynr]->get_table_field_for_part_no(table, kp);
        if (bitmap_is_set(table->write_set, field->field_index))
        {
          m_updated_indexes.set_bit(keynr);
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
my_core::THR_LOCK_DATA **ha_rocksdb::store_lock(
  my_core::THD *thd,
  my_core::THR_LOCK_DATA **db_lock_dest,
  my_core::thr_lock_type lock_type)
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
  *db_lock_dest++= &m_db_lock;
  DBUG_RETURN(db_lock_dest);
}


void ha_rocksdb::read_thd_vars(my_core::THD *thd)
{
  m_store_checksums= THDVAR(thd, store_checksums);
  m_verify_checksums= THDVAR(thd, verify_checksums);
  m_checksums_pct= THDVAR(thd, checksums_pct);
}


int ha_rocksdb::external_lock(my_core::THD *thd, int lock_type)
{
  int res= 0;
  DBUG_ENTER("ha_rocksdb::external_lock");

  int binlog_format= my_core::thd_binlog_format(thd);
  bool unsafe_for_binlog= THDVAR(handler::ha_thd(), unsafe_for_binlog);
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

  if (my_core::thd_tx_isolation(thd) < ISO_READ_COMMITTED ||
      my_core::thd_tx_isolation(thd) > ISO_REPEATABLE_READ)
  {
    my_printf_error(ER_UNKNOWN_ERROR,
                    "MyRocks supports only READ COMMITTED and REPEATABLE READ "
                    "isolation levels. Please change from current isolation "
                    "level %s",
                    MYF(0), tx_isolation_names[my_core::thd_tx_isolation(thd)]);
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  io_perf_start();

  if (lock_type == F_UNLCK)
  {
    Rdb_transaction* tx= rdb_get_or_create_tx(thd);

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
    /*
      It's nice to do the following on start of every statement. The problem
      is, handler->start_stmt() is not called for INSERTs.
      So, we put this code here.
    */
    Rdb_transaction* tx= rdb_get_or_create_tx(thd);
    read_thd_vars(thd);

    m_updated_indexes_is_valid= false;

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
    rdb_register_tx(rdb_handlerton, thd, tx);
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

int ha_rocksdb::start_stmt(
    my_core::THD *thd,
    my_core::thr_lock_type lock_type __attribute__((__unused__)))
{
  Rdb_transaction* tx= rdb_get_or_create_tx(thd);
  read_thd_vars(thd);
  rdb_register_tx(ht, thd, tx);
  return 0;
}


static rocksdb::Range rdb_get_range(
  uint32_t i, uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE*2], int offset1,
  int offset2)
{
  uchar* buf_begin = buf;
  uchar* buf_end = buf+Rdb_key_def::INDEX_NUMBER_SIZE;
  store_index_number(buf_begin, i + offset1);
  store_index_number(buf_end, i + offset2);

  return rocksdb::Range(
    rocksdb::Slice((const char*) buf_begin, Rdb_key_def::INDEX_NUMBER_SIZE),
    rocksdb::Slice((const char*) buf_end, Rdb_key_def::INDEX_NUMBER_SIZE));
}


static rocksdb::Range rdb_get_range(
  Rdb_key_def* keydef,
  uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE*2],
  int offset1, int offset2)
{
  return rdb_get_range(keydef->get_index_number(), buf, offset1, offset2);
}


static rocksdb::Range rdb_get_range(
  Rdb_key_def* keydef, uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE*2])
{
  if (keydef->m_is_reverse_cf)
  {
    return rdb_get_range(keydef, buf, 1, 0);
  }
  else
  {
    return rdb_get_range(keydef, buf, 0, 1);
  }
}


rocksdb::Range ha_rocksdb::get_range(
  int i, uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE*2]) const
{
  return rdb_get_range(m_key_descriptors[i], buf);
}


void Rdb_drop_index_thread::signal(bool stop_thread)
{
  mysql_mutex_lock(&rdb_drop_index_thd.m_interrupt_mutex);
  if (stop_thread) {
    rdb_drop_index_thd.m_stop= true;
  }
  mysql_cond_signal(&rdb_drop_index_thd.m_interrupt_cond);
  mysql_mutex_unlock(&rdb_drop_index_thd.m_interrupt_mutex);
}

void* Rdb_drop_index_thread::thread_fn(void* ptr __attribute__((__unused__)))
{
  mysql_mutex_lock(&rdb_drop_index_thd.m_mutex);
  mysql_mutex_lock(&rdb_drop_index_thd.m_interrupt_mutex);

  for (;;) {
    // "rdb_drop_index_thd.m_stop = true" might be set by shutdown command
    // after drop_index_thread releases rdb_drop_index_thd.m_interrupt_mutex
    // (i.e. while executing expensive Seek()). To prevent drop_index_thread
    // from entering long cond_timedwait, checking if rdb_drop_index_thd.m_stop
    // is true or not is needed, with rdb_drop_index_thd.m_interrupt_mutex held.
    if (rdb_drop_index_thd.m_stop) {
      break;
    }

    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += rdb_dict_manager.is_drop_index_empty()
      ? 24*60*60 // no filtering
      : 60; // filtering

    auto ret __attribute__((__unused__)) = mysql_cond_timedwait(
                                    &rdb_drop_index_thd.m_interrupt_cond,
                                    &rdb_drop_index_thd.m_interrupt_mutex, &ts);
    if (rdb_drop_index_thd.m_stop) {
      break;
    }
    // make sure, no program error is returned
    DBUG_ASSERT(ret == 0 || ret == ETIMEDOUT);
    mysql_mutex_unlock(&rdb_drop_index_thd.m_interrupt_mutex);

    std::vector<Rdb_gl_index_id> indices;
    rdb_dict_manager.get_drop_indexes_ongoing(&indices);
    if (!indices.empty()) {
      std::unordered_set<Rdb_gl_index_id> finished;
      rocksdb::ReadOptions read_opts;
      read_opts.total_order_seek = true; // disable bloom filter

      for (auto d : indices) {
        uint32 cf_flags= 0;
        if (!rdb_dict_manager.get_cf_flags(d.m_cf_id, &cf_flags))
        {
          sql_print_error("RocksDB: Failed to get column family flags "
                          "from cf id %u. MyRocks data dictionary may "
                          "get corrupted.", d.m_cf_id);
          abort_with_stack_traces();
        }
        rocksdb::ColumnFamilyHandle* cfh= rdb_cf_manager.get_cf(d.m_cf_id);
        DBUG_ASSERT(cfh);
        bool is_reverse_cf= cf_flags & Rdb_key_def::REVERSE_CF_FLAG;

        bool index_removed= false;
        uchar key_buf[Rdb_key_def::INDEX_NUMBER_SIZE]= {0};
        store_big_uint4(key_buf, d.m_index_id);
        rocksdb::Slice key = rocksdb::Slice((char*)key_buf, sizeof(key_buf));
        uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE*2];
        rocksdb::Range range = rdb_get_range(d.m_index_id, buf,
                                             is_reverse_cf?1:0,
                                             is_reverse_cf?0:1);
        rocksdb::CompactRangeOptions compact_range_options;
        compact_range_options.bottommost_level_compaction =
                  rocksdb::BottommostLevelCompaction::kForce;
        compact_range_options.exclusive_manual_compaction = false;
        rocksdb::Status status = rocksdb::DeleteFilesInRange(
            rdb_rocksdb_db->GetBaseDB(), cfh,
            &range.start, &range.limit);
        if (!status.ok())
        {
          if (status.IsShutdownInProgress())
          {
            break;
          }
          rdb_handle_io_error(status, RDB_IO_ERROR_BG_THREAD);
        }
        status = rdb_rocksdb_db->CompactRange(
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
          rdb_rocksdb_db->NewIterator(read_opts, cfh));
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
        rdb_dict_manager.done_drop_indexes(finished);
      }
    }
    mysql_mutex_lock(&rdb_drop_index_thd.m_interrupt_mutex);
  }

  mysql_mutex_unlock(&rdb_drop_index_thd.m_interrupt_mutex);
  mysql_mutex_unlock(&rdb_drop_index_thd.m_mutex);

  return nullptr;
}


Rdb_tbl_def* ha_rocksdb::get_table_if_exists(const char* tablename) const
{
  DBUG_ASSERT(tablename != nullptr);

  StringBuffer<256> strbuf;
  if (rdb_normalize_tablename(tablename, &strbuf))
  {
    // We were not passed table name?
    DBUG_ASSERT(0);
    return nullptr;
  }
  return rdb_ddl_manager.find(reinterpret_cast<uchar*>(strbuf.c_ptr()),
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
  std::unique_ptr<rocksdb::WriteBatch> wb= rdb_dict_manager.begin();
  rocksdb::WriteBatch *batch= wb.get();

  /* Find the table in the hash */
  Rdb_tbl_def *tbl = get_table_if_exists(tablename);
  if (!tbl)
  {
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  rdb_dict_manager.add_drop_table(tbl->m_key_descriptors,
                                  tbl->m_key_count, batch);

  /*
    Remove the table entry in data dictionary (this will also remove it from
    the persistent data dictionary).
  */
  rdb_ddl_manager.remove(tbl, batch, true);
  if (rdb_dict_manager.commit(batch))
  {
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  rdb_drop_index_thd.signal();

  DBUG_RETURN(0);
}


void ha_rocksdb::remove_rows(void)
{
  rocksdb::WriteOptions wo = rdb_get_write_options(handler::ha_thd());

  rocksdb::ReadOptions opts;
  opts.total_order_seek= true;
  rocksdb::Iterator* it= rdb_rocksdb_db->NewIterator(opts);

  char key_buf[MAX_KEY_LENGTH];
  uint key_len;
  /*
    Remove all records in each index.
    (This is is not crash-safe, but it doesn't matter, because bulk row
    deletion will be handled on rocksdb side)
  */
  for (uint i= 0; i < m_tbl_def->m_key_count ; i++)
  {
    m_tbl_def->m_key_descriptors[i]->
        get_infimum_key(reinterpret_cast<uchar*>(key_buf), &key_len);

    rocksdb::Slice table_key(key_buf, key_len);
    it->Seek(table_key);
    while (it->Valid())
    {
      rocksdb::Slice key= it->key();
      if (!m_tbl_def->m_key_descriptors[i]->covers_key(key))
      {
        break;
      }
      if (can_use_single_delete(i))
      {
        rdb_rocksdb_db->SingleDelete(wo, key);
      }
      else
      {
        rdb_rocksdb_db->Delete(wo, key);
      }
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
  StringBuffer<256> from_str;
  StringBuffer<256> to_str;

  if (rdb_normalize_tablename(from, &from_str))
  {
    return HA_ERR_INTERNAL_ERROR;
  }

  if (rdb_normalize_tablename(to, &to_str))
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

      So, we have to find the old table in the rdb_ddl_manager (it's there
      because it has been opened by this statement), and check is_auto_cf there.
    */
    Rdb_tbl_def *tdef;
    if (!(tdef= rdb_ddl_manager.find(reinterpret_cast<uchar*>(from_str.c_ptr()),
      from_str.length())))
    {
      return HA_ERR_INTERNAL_ERROR;
    }

    for (uint i= 0; i < tdef->m_key_count; i++)
    {
      DBUG_ASSERT(tdef->m_key_descriptors != nullptr);

      if (tdef->m_key_descriptors[i]->m_is_auto_cf)
      {
        my_error(ER_NOT_SUPPORTED_YET, MYF(0),
                 "ALTER TABLE on table with per-index CF");
        return HA_ERR_INTERNAL_ERROR;
      }
    }
  }

  std::unique_ptr<rocksdb::WriteBatch> wb= rdb_dict_manager.begin();
  rocksdb::WriteBatch *batch= wb.get();
  rdb_dict_manager.lock();
  write_err= rdb_ddl_manager.rename(rdb_str_to_uchar_ptr(&from_str),
                                    from_str.length(),
                                    rdb_str_to_uchar_ptr(&to_str),
                                    to_str.length(), batch)
             || rdb_dict_manager.commit(batch);
  rdb_dict_manager.unlock();

  return write_err ? HA_ERR_INTERNAL_ERROR : 0;
}


/**
  @brief
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


int ha_rocksdb::extra(enum my_core::ha_extra_function operation)
{
  DBUG_ENTER("ha_rocksdb::extra");

  switch (operation) {
  case HA_EXTRA_KEYREAD:
    m_key_is_read_only= true;
    break;
  case HA_EXTRA_NO_KEYREAD:
    m_key_is_read_only= false;
    break;
  case HA_EXTRA_FLUSH:
    /*
      If the table has blobs, then they are part of m_retrieved_record. This
      call invalidates them.
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
ha_rows ha_rocksdb::records_in_range(uint inx, my_core::key_range *min_key,
                                     my_core::key_range *max_key)
{
  DBUG_ENTER("ha_rocksdb::records_in_range");
  ha_rows ret = THDVAR(handler::ha_thd(), records_in_range);
  if (ret) {
    DBUG_RETURN(ret);
  }
  if (table->force_index) {
    ha_rows force_rows= THDVAR(handler::ha_thd(), force_index_records_in_range);
    if (force_rows) {
      DBUG_RETURN(force_rows);
    }
  }

  Rdb_key_def *kd= m_key_descriptors[inx];
  DBUG_ASSERT(kd);

  uint size1=0;
  if (min_key) {
    size1 = kd->pack_index_tuple(
      this,
      table,
      m_pack_buffer,
      m_sec_key_packed_tuple,
      min_key->key,
      min_key->keypart_map
    );
    if (min_key->flag == HA_READ_PREFIX_LAST_OR_PREV ||
        min_key->flag == HA_READ_PREFIX_LAST ||
        min_key->flag == HA_READ_AFTER_KEY)
    {
      kd->successor(m_sec_key_packed_tuple, size1);
    }
  } else {
    kd->get_infimum_key(m_sec_key_packed_tuple, &size1);
  }

  uint size2=0;
  if (max_key) {
    size2 = kd->pack_index_tuple(
      this,
      table,
      m_pack_buffer,
      m_sec_key_packed_tuple_old,
      max_key->key,
      max_key->keypart_map
    );
    if (max_key->flag == HA_READ_PREFIX_LAST_OR_PREV ||
        max_key->flag == HA_READ_PREFIX_LAST ||
        max_key->flag == HA_READ_AFTER_KEY)
    {
      kd->successor(m_sec_key_packed_tuple_old, size2);
    }
    // pad the upper key with FFFFs to make sure it is more than the lower
    if (size1 > size2) {
      memset(m_sec_key_packed_tuple_old+size2, 0xff, size1-size2);
      size2 = size1;
    }
  } else {
    kd->get_supremum_key(m_sec_key_packed_tuple_old, &size2);
  }

  rocksdb::Slice slice1((const char*) m_sec_key_packed_tuple, size1);
  rocksdb::Slice slice2((const char*) m_sec_key_packed_tuple_old, size2);

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
    // no stats. Most likely, this is memtable-only table.
    // Query for the range size including memtable.
    rows = 1;
    disk_size = RDB_ASSUMED_KEY_VALUE_DISK_SIZE;
    rdb_rocksdb_db->GetApproximateSizes(
      kd->get_cf(),
      &r, 1,
      &sz, true);
  }
  else
  {
    // Have stats, query for size which doesn't include memtable.
    rdb_rocksdb_db->GetApproximateSizes(
      kd->get_cf(),
      &r, 1,
      &sz);
  }

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

  if (rdb_debug_optimizer_n_rows_sysvar > 0)
  {
    ret= rdb_debug_optimizer_n_rows_sysvar;
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

int ha_rocksdb::optimize(
    my_core::THD *thd __attribute__((__unused__)),
    my_core::HA_CHECK_OPT* check_opt __attribute__((__unused__)))
{
  int rc= 0;
  for (uint i= 0; i < table->s->keys; i++)
  {
    uchar buf[Rdb_key_def::INDEX_NUMBER_SIZE*2];
    auto range = get_range(i, buf);
    if (!rdb_rocksdb_db->CompactRange(rocksdb::CompactRangeOptions(),
                           m_key_descriptors[i]->get_cf(),
                           &range.start, &range.limit).ok())
    {
      rc= 1;
      break;
    }
  }
  return rc;
}


int ha_rocksdb::analyze(
    my_core::THD* thd,
    my_core::HA_CHECK_OPT* check_opt __attribute__((__unused__)))
{
  DBUG_ENTER("ha_rocksdb::analyze");

  if (!this->handler::table)
  {
    DBUG_RETURN(1);
  }

  // find per column family key ranges which need to be queried
  std::unordered_map<rocksdb::ColumnFamilyHandle*, std::vector<rocksdb::Range>>
    ranges;
  std::unordered_set<Rdb_gl_index_id> ids_to_check;
  std::vector<uchar> buf(table->s->keys * 2 * Rdb_key_def::INDEX_NUMBER_SIZE);
  for (uint i = 0; i < table->s->keys; i++)
  {
    auto bufp = &buf[i * 2 * Rdb_key_def::INDEX_NUMBER_SIZE];
    ranges[m_key_descriptors[i]->get_cf()].push_back(get_range(i, bufp));
    ids_to_check.insert(m_key_descriptors[i]->get_gl_index_id());
  }

  // for analyze statements, force flush on memtable to get accurate cardinality
  Rdb_cf_manager& rdb_cf_manager = rdb_get_cf_manager();
  if (thd != nullptr && THDVAR(thd, flush_memtable_on_analyze) &&
      !rdb_bg_thd.m_pause_work_sysvar)
  {
    for (auto it : ids_to_check)
    {
      rdb_rocksdb_db->Flush(rocksdb::FlushOptions(),
                            rdb_cf_manager.get_cf(it.m_cf_id));
    }
  }

  // get RocksDB table properties for these ranges
  rocksdb::TablePropertiesCollection props;
  for (auto it : ranges)
  {
    auto old_size __attribute__((__unused__)) = props.size();
    auto status = rdb_rocksdb_db->GetPropertiesOfTablesInRange(
      it.first, &it.second[0], it.second.size(), &props);
    DBUG_ASSERT(props.size() >= old_size);
    if (!status.ok())
      DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  int num_sst = 0;
  // group stats per index id
  std::unordered_map<Rdb_gl_index_id, Rdb_index_stats>
    stats;
  for (auto it : props)
  {
    std::vector<Rdb_index_stats> sst_stats =
        Rdb_tbl_prop_coll::read_stats_from_tbl_props(it.second);
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

      Rdb_key_def* kd= rdb_ddl_manager.find(it1.m_gl_index_id);
      stats[it1.m_gl_index_id].merge(it1, true, kd->max_storage_fmt_length());
    }
    num_sst++;
  }

  // set and persist new stats
  rdb_ddl_manager.set_stats(stats);
  rdb_ddl_manager.persist_stats(true);

  DBUG_RETURN(0);
}


void ha_rocksdb::get_auto_increment(ulonglong offset, ulonglong increment,
                                    ulonglong nb_desired_values,
                                    ulonglong *first_value,
                                    ulonglong *nb_reserved_values)
{
  if (!nb_desired_values)
  {
    nb_desired_values= 1;
  }

  *first_value= (m_tbl_def->m_auto_incr_val += nb_desired_values) -
      nb_desired_values;
  *nb_reserved_values= nb_desired_values;
}


#ifndef DBUG_OFF

/* Debugger help function */
static char rdb_dbug_item_print_buf[512]= {0};
const char *rdb_dbug_print_item(Item *item)
{
  char *buf= rdb_dbug_item_print_buf;
  String str(buf, sizeof(rdb_dbug_item_print_buf), &my_charset_bin);
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
bool ha_rocksdb::is_ascending(Rdb_key_def *keydef,
                              my_core::ha_rkey_function find_flag)
{
  DBUG_ASSERT(keydef != nullptr);

  bool is_ascending= false;
  switch (find_flag) {
  case HA_READ_KEY_EXACT:
  {
    if (keydef->m_is_reverse_cf)
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
    if (keydef->m_is_reverse_cf)
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
    if (keydef->m_is_reverse_cf)
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


///////////////////////////////////////////////////////////
// Define status variables for all global perf counters
///////////////////////////////////////////////////////////

#define RDB_SHOW_VAR_FN(name) rdb_show_##name

#define RDB_DEF_SHOW_FUNC(name, key) \
  static int RDB_SHOW_VAR_FN(name)(MYSQL_THD thd, SHOW_VAR *var, char *buff)  \
  {                                                                           \
    rdb_status_counters.name =                                                \
      rdb_rocksdb_stats->getTickerCount(rocksdb::key);                        \
    var->type = SHOW_LONGLONG;                                                \
    var->value = reinterpret_cast<char*>(&rdb_status_counters.name);          \
    return 0;                                                                 \
  }

#define RDB_DEF_SHOW_VAR(name) \
  {"rocksdb_" #name, reinterpret_cast<char*>(&RDB_SHOW_VAR_FN(name)), SHOW_FUNC}

#define RDB_DEF_SHOW_VAR_PTR(name, ptr) \
  {"rocksdb_" name, reinterpret_cast<char*>(ptr), SHOW_LONGLONG}

#define RDB_DEF_STATUS_VAR(name, ptr, option) \
  {name, reinterpret_cast<char*>(ptr), option}

struct rdb_status_counters_t {
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

static rdb_status_counters_t rdb_status_counters;

RDB_DEF_SHOW_FUNC(block_cache_miss, BLOCK_CACHE_MISS)
RDB_DEF_SHOW_FUNC(block_cache_hit, BLOCK_CACHE_HIT)
RDB_DEF_SHOW_FUNC(block_cache_add, BLOCK_CACHE_ADD)
RDB_DEF_SHOW_FUNC(block_cache_index_miss, BLOCK_CACHE_INDEX_MISS)
RDB_DEF_SHOW_FUNC(block_cache_index_hit, BLOCK_CACHE_INDEX_HIT)
RDB_DEF_SHOW_FUNC(block_cache_filter_miss, BLOCK_CACHE_FILTER_MISS)
RDB_DEF_SHOW_FUNC(block_cache_filter_hit, BLOCK_CACHE_FILTER_HIT)
RDB_DEF_SHOW_FUNC(block_cache_data_miss, BLOCK_CACHE_DATA_MISS)
RDB_DEF_SHOW_FUNC(block_cache_data_hit, BLOCK_CACHE_DATA_HIT)
RDB_DEF_SHOW_FUNC(bloom_filter_useful, BLOOM_FILTER_USEFUL)
RDB_DEF_SHOW_FUNC(memtable_hit, MEMTABLE_HIT)
RDB_DEF_SHOW_FUNC(memtable_miss, MEMTABLE_MISS)
RDB_DEF_SHOW_FUNC(compaction_key_drop_new, COMPACTION_KEY_DROP_NEWER_ENTRY)
RDB_DEF_SHOW_FUNC(compaction_key_drop_obsolete, COMPACTION_KEY_DROP_OBSOLETE)
RDB_DEF_SHOW_FUNC(compaction_key_drop_user, COMPACTION_KEY_DROP_USER)
RDB_DEF_SHOW_FUNC(number_keys_written, NUMBER_KEYS_WRITTEN)
RDB_DEF_SHOW_FUNC(number_keys_read, NUMBER_KEYS_READ)
RDB_DEF_SHOW_FUNC(number_keys_updated, NUMBER_KEYS_UPDATED)
RDB_DEF_SHOW_FUNC(bytes_written, BYTES_WRITTEN)
RDB_DEF_SHOW_FUNC(bytes_read, BYTES_READ)
RDB_DEF_SHOW_FUNC(no_file_closes, NO_FILE_CLOSES)
RDB_DEF_SHOW_FUNC(no_file_opens, NO_FILE_OPENS)
RDB_DEF_SHOW_FUNC(no_file_errors, NO_FILE_ERRORS)
RDB_DEF_SHOW_FUNC(l0_slowdown_micros, STALL_L0_SLOWDOWN_MICROS)
RDB_DEF_SHOW_FUNC(memtable_compaction_micros, STALL_MEMTABLE_COMPACTION_MICROS)
RDB_DEF_SHOW_FUNC(l0_num_files_stall_micros, STALL_L0_NUM_FILES_MICROS)
RDB_DEF_SHOW_FUNC(rate_limit_delay_millis, RATE_LIMIT_DELAY_MILLIS)
RDB_DEF_SHOW_FUNC(num_iterators, NO_ITERATORS)
RDB_DEF_SHOW_FUNC(number_multiget_get, NUMBER_MULTIGET_CALLS)
RDB_DEF_SHOW_FUNC(number_multiget_keys_read, NUMBER_MULTIGET_KEYS_READ)
RDB_DEF_SHOW_FUNC(number_multiget_bytes_read, NUMBER_MULTIGET_BYTES_READ)
RDB_DEF_SHOW_FUNC(number_deletes_filtered, NUMBER_FILTERED_DELETES)
RDB_DEF_SHOW_FUNC(number_merge_failures, NUMBER_MERGE_FAILURES)
RDB_DEF_SHOW_FUNC(sequence_number, SEQUENCE_NUMBER)
RDB_DEF_SHOW_FUNC(bloom_filter_prefix_checked, BLOOM_FILTER_PREFIX_CHECKED)
RDB_DEF_SHOW_FUNC(bloom_filter_prefix_useful, BLOOM_FILTER_PREFIX_USEFUL)
RDB_DEF_SHOW_FUNC(number_reseeks_iteration, NUMBER_OF_RESEEKS_IN_ITERATION)
RDB_DEF_SHOW_FUNC(getupdatessince_calls, GET_UPDATES_SINCE_CALLS)
RDB_DEF_SHOW_FUNC(block_cachecompressed_miss, BLOCK_CACHE_COMPRESSED_MISS)
RDB_DEF_SHOW_FUNC(block_cachecompressed_hit, BLOCK_CACHE_COMPRESSED_HIT)
RDB_DEF_SHOW_FUNC(wal_synced, WAL_FILE_SYNCED)
RDB_DEF_SHOW_FUNC(wal_bytes, WAL_FILE_BYTES)
RDB_DEF_SHOW_FUNC(write_self, WRITE_DONE_BY_SELF)
RDB_DEF_SHOW_FUNC(write_other, WRITE_DONE_BY_OTHER)
RDB_DEF_SHOW_FUNC(write_timedout, WRITE_TIMEDOUT)
RDB_DEF_SHOW_FUNC(write_wal, WRITE_WITH_WAL)
RDB_DEF_SHOW_FUNC(flush_write_bytes, FLUSH_WRITE_BYTES)
RDB_DEF_SHOW_FUNC(compact_read_bytes, COMPACT_READ_BYTES)
RDB_DEF_SHOW_FUNC(compact_write_bytes, COMPACT_WRITE_BYTES)
RDB_DEF_SHOW_FUNC(number_superversion_acquires, NUMBER_SUPERVERSION_ACQUIRES)
RDB_DEF_SHOW_FUNC(number_superversion_releases, NUMBER_SUPERVERSION_RELEASES)
RDB_DEF_SHOW_FUNC(number_superversion_cleanups, NUMBER_SUPERVERSION_CLEANUPS)
RDB_DEF_SHOW_FUNC(number_block_not_compressed, NUMBER_BLOCK_NOT_COMPRESSED)

static void rdb_update_status()
{
  rdb_export_stats.m_rows_deleted = rdb_global_stats.m_rows[ROWS_DELETED];
  rdb_export_stats.m_rows_inserted = rdb_global_stats.m_rows[ROWS_INSERTED];
  rdb_export_stats.m_rows_read = rdb_global_stats.m_rows[ROWS_READ];
  rdb_export_stats.m_rows_updated = rdb_global_stats.m_rows[ROWS_UPDATED];

  rdb_export_stats.m_system_rows_deleted=
    rdb_global_stats.m_system_rows[ROWS_DELETED];
  rdb_export_stats.m_system_rows_inserted=
    rdb_global_stats.m_system_rows[ROWS_INSERTED];
  rdb_export_stats.m_system_rows_read=
    rdb_global_stats.m_system_rows[ROWS_READ];
  rdb_export_stats.m_system_rows_updated=
    rdb_global_stats.m_system_rows[ROWS_UPDATED];
}

static my_core::SHOW_VAR rdb_status_variables[]=
{
  RDB_DEF_STATUS_VAR("rows_deleted", &rdb_export_stats.m_rows_deleted,
                      SHOW_LONGLONG),
  RDB_DEF_STATUS_VAR("rows_inserted", &rdb_export_stats.m_rows_inserted,
                      SHOW_LONGLONG),
  RDB_DEF_STATUS_VAR("rows_read",
                      &rdb_export_stats.m_rows_read, SHOW_LONGLONG),
  RDB_DEF_STATUS_VAR("rows_updated", &rdb_export_stats.m_rows_updated,
                      SHOW_LONGLONG),
  RDB_DEF_STATUS_VAR("system_rows_deleted",
                      &rdb_export_stats.m_system_rows_deleted,
                      SHOW_LONGLONG),
  RDB_DEF_STATUS_VAR("system_rows_inserted",
                      &rdb_export_stats.m_system_rows_inserted, SHOW_LONGLONG),
  RDB_DEF_STATUS_VAR("system_rows_read", &rdb_export_stats.m_system_rows_read,
                      SHOW_LONGLONG),
  RDB_DEF_STATUS_VAR("system_rows_updated",
                      &rdb_export_stats.m_system_rows_updated,
                      SHOW_LONGLONG),

  {NullS, NullS, SHOW_LONG}
};

static void rdb_show_vars(my_core::THD* thd __attribute__((__unused__)),
                          my_core::SHOW_VAR* var,
                          char* buff __attribute__((__unused__)))
{
  rdb_update_status();
  var->type = my_core::SHOW_ARRAY;
  var->value = reinterpret_cast<char*>(&rdb_status_variables);
}

static my_core::SHOW_VAR rdb_status_vars[]=
{
  RDB_DEF_SHOW_VAR(block_cache_miss),
  RDB_DEF_SHOW_VAR(block_cache_hit),
  RDB_DEF_SHOW_VAR(block_cache_add),
  RDB_DEF_SHOW_VAR(block_cache_index_miss),
  RDB_DEF_SHOW_VAR(block_cache_index_hit),
  RDB_DEF_SHOW_VAR(block_cache_filter_miss),
  RDB_DEF_SHOW_VAR(block_cache_filter_hit),
  RDB_DEF_SHOW_VAR(block_cache_data_miss),
  RDB_DEF_SHOW_VAR(block_cache_data_hit),
  RDB_DEF_SHOW_VAR(bloom_filter_useful),
  RDB_DEF_SHOW_VAR(memtable_hit),
  RDB_DEF_SHOW_VAR(memtable_miss),
  RDB_DEF_SHOW_VAR(compaction_key_drop_new),
  RDB_DEF_SHOW_VAR(compaction_key_drop_obsolete),
  RDB_DEF_SHOW_VAR(compaction_key_drop_user),
  RDB_DEF_SHOW_VAR(number_keys_written),
  RDB_DEF_SHOW_VAR(number_keys_read),
  RDB_DEF_SHOW_VAR(number_keys_updated),
  RDB_DEF_SHOW_VAR(bytes_written),
  RDB_DEF_SHOW_VAR(bytes_read),
  RDB_DEF_SHOW_VAR(no_file_closes),
  RDB_DEF_SHOW_VAR(no_file_opens),
  RDB_DEF_SHOW_VAR(no_file_errors),
  RDB_DEF_SHOW_VAR(l0_slowdown_micros),
  RDB_DEF_SHOW_VAR(memtable_compaction_micros),
  RDB_DEF_SHOW_VAR(l0_num_files_stall_micros),
  RDB_DEF_SHOW_VAR(rate_limit_delay_millis),
  RDB_DEF_SHOW_VAR(num_iterators),
  RDB_DEF_SHOW_VAR(number_multiget_get),
  RDB_DEF_SHOW_VAR(number_multiget_keys_read),
  RDB_DEF_SHOW_VAR(number_multiget_bytes_read),
  RDB_DEF_SHOW_VAR(number_deletes_filtered),
  RDB_DEF_SHOW_VAR(number_merge_failures),
  RDB_DEF_SHOW_VAR(sequence_number),
  RDB_DEF_SHOW_VAR(bloom_filter_prefix_checked),
  RDB_DEF_SHOW_VAR(bloom_filter_prefix_useful),
  RDB_DEF_SHOW_VAR(number_reseeks_iteration),
  RDB_DEF_SHOW_VAR(getupdatessince_calls),
  RDB_DEF_SHOW_VAR(block_cachecompressed_miss),
  RDB_DEF_SHOW_VAR(block_cachecompressed_hit),
  RDB_DEF_SHOW_VAR(wal_synced),
  RDB_DEF_SHOW_VAR(wal_bytes),
  RDB_DEF_SHOW_VAR(write_self),
  RDB_DEF_SHOW_VAR(write_other),
  RDB_DEF_SHOW_VAR(write_timedout),
  RDB_DEF_SHOW_VAR(write_wal),
  RDB_DEF_SHOW_VAR(flush_write_bytes),
  RDB_DEF_SHOW_VAR(compact_read_bytes),
  RDB_DEF_SHOW_VAR(compact_write_bytes),
  RDB_DEF_SHOW_VAR(number_superversion_acquires),
  RDB_DEF_SHOW_VAR(number_superversion_releases),
  RDB_DEF_SHOW_VAR(number_superversion_cleanups),
  RDB_DEF_SHOW_VAR(number_block_not_compressed),
  RDB_DEF_SHOW_VAR_PTR("number_stat_computes", &rdb_num_stat_computes_showvar),
  RDB_DEF_SHOW_VAR_PTR("number_sst_entry_put", &rdb_num_sst_entry_put_showvar),
  RDB_DEF_SHOW_VAR_PTR("number_sst_entry_delete",
                       &rdb_num_sst_entry_del_showvar),
  RDB_DEF_SHOW_VAR_PTR("number_sst_entry_singledelete",
                       &rdb_num_sst_entry_sd_showvar),
  RDB_DEF_SHOW_VAR_PTR("number_sst_entry_merge",
                       &rdb_num_sst_entry_merge_showvar),
  RDB_DEF_SHOW_VAR_PTR("number_sst_entry_other",
                       &rdb_num_sst_entry_other_showvar),
  {"rocksdb", reinterpret_cast<char*>(&rdb_show_vars), SHOW_FUNC},
  {NullS, NullS, SHOW_LONG}
};


void* Rdb_background_thread::thread_fn(void* ptr __attribute__((__unused__)))
{
  mysql_mutex_lock(&rdb_bg_thd.m_mutex);

  timespec ts_next_sync;
  clock_gettime(CLOCK_REALTIME, &ts_next_sync);
  ts_next_sync.tv_sec++;

  for (;;)
  {
    // wait for 1 second or until we received a condition to stop the thread
    mysql_mutex_lock(&rdb_bg_thd.m_stop_cond_mutex);
    auto ret __attribute__((__unused__)) = mysql_cond_timedwait(
      &rdb_bg_thd.m_stop_cond, &rdb_bg_thd.m_stop_cond_mutex, &ts_next_sync);
    // make sure that no program error is returned
    DBUG_ASSERT(ret == 0 || ret == ETIMEDOUT);
    auto local_bg_control = rdb_bg_thd.m_control;
    rdb_bg_thd.m_control = Rdb_bg_thread_control();
    mysql_mutex_unlock(&rdb_bg_thd.m_stop_cond_mutex);

    if (local_bg_control.m_stop)
    {
      break;
    }

    if (local_bg_control.m_save_stats)
    {
      rdb_ddl_manager.persist_stats();
    }

    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    if (ts.tv_sec-ts_next_sync.tv_sec >= 1)
    {
      if (rdb_rocksdb_db && rdb_background_sync_sysvar)
      {
        DBUG_ASSERT(!rdb_db_options.allow_mmap_writes);
        rocksdb::Status s= rdb_rocksdb_db->SyncWAL();
        if (!s.ok())
          rdb_handle_io_error(s, RDB_IO_ERROR_BG_THREAD);
      }
      ts_next_sync.tv_sec = ts.tv_sec+1;
    }
  }

  // save remaining stats which might've left unsaved
  rdb_ddl_manager.persist_stats();

  mysql_mutex_unlock(&rdb_bg_thd.m_mutex);

  return nullptr;
}


static void rdb_set_pause_bg_work_sysvar(
  my_core::THD*               thd     __attribute__((__unused__)),
  my_core::st_mysql_sys_var*  var     __attribute__((__unused__)),
  void*                       var_ptr __attribute__((__unused__)),
  const void*                 save)
{
  mysql_mutex_lock(&rdb_sysvar_mutex);
  bool pause_requested = *static_cast<const bool*>(save);
  if (rdb_bg_thd.m_pause_work_sysvar != pause_requested) {
    if (pause_requested) {
      rdb_rocksdb_db->PauseBackgroundWork();
    } else {
      rdb_rocksdb_db->ContinueBackgroundWork();
    }
    rdb_bg_thd.m_pause_work_sysvar = pause_requested;
  }
  mysql_mutex_unlock(&rdb_sysvar_mutex);
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
static bool rdb_can_use_bloom_filter(Rdb_key_def *keydef,
                                     const rocksdb::Slice &eq_cond,
                                     const bool use_all_keys,
                                     bool is_ascending)
{
  bool can_use= false;
  rocksdb::Options opt = rdb_rocksdb_db->GetOptions(keydef->get_cf());
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


int rdb_get_table_perf_counters(const char *tablename,
                                RDB_SHARE_PERF_COUNTERS *counters)
{
  DBUG_ASSERT(counters != nullptr);

  if (!tablename) {
    rdb_perf_context_collect(rdb_global_perf_context, counters);
    return 0;
  }

  Rdb_table_handler *table_handler;
  table_handler= rdb_open_tables.get_table_handler(tablename);
  if (!table_handler)
  {
    return HA_ERR_INTERNAL_ERROR;
  }

  rdb_perf_context_collect(table_handler->m_table_perf_context, counters);
  rdb_open_tables.free_table_handler(table_handler);
  return 0;
}


void rdb_handle_io_error(rocksdb::Status status, RDB_IO_ERROR_TYPES type)
{
  if (status.IsIOError())
  {
    switch (type) {
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
    switch (type) {
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


// Split a string based on a delimiter.  Two delimiters in a row will not
// add an empty string in the list.
static std::vector<std::string> rdb_split_string(const std::string& input,
                                                 char delimiter)
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

static void rdb_set_collation_exception_list(
    my_core::THD*               thd     __attribute__((__unused__)),
    my_core::st_mysql_sys_var*  var     __attribute__((__unused__)),
    void*                       var_ptr,
    const void*                 save)
{
  const char* val = *static_cast<const char*const*>(save);

  // Make sure we are not updating the list when it is being used elsewhere
  mysql_mutex_lock(&rdb_collation_ex_list_mutex);

  // Split the input string into a list of table names
  rdb_collation_exception_list = rdb_split_string(val, ',');

  // Release the mutex
  mysql_mutex_unlock(&rdb_collation_ex_list_mutex);

  *static_cast<const char**>(var_ptr) = val;
}


void rdb_request_save_stats()
{
  mysql_mutex_lock(&rdb_bg_thd.m_stop_cond_mutex);
  rdb_bg_thd.m_control.m_save_stats= true;
  mysql_mutex_unlock(&rdb_bg_thd.m_stop_cond_mutex);
}

void ha_rocksdb::rpl_before_delete_rows()
{
  m_rpl_delete_rows = true;
}

void ha_rocksdb::rpl_after_delete_rows()
{
  m_rpl_delete_rows = false;
}

void ha_rocksdb::rpl_before_update_rows()
{
  m_rpl_update_rows = true;
}

void ha_rocksdb::rpl_after_update_rows()
{
  m_rpl_update_rows = false;
}

/**
  Read Free Replication can be used or not. Returning False means
  Read Free Replication can be used. Read Free Replication can be used
  on UPDATE or DELETE row events, and table must have user defined
  primary key.
*/
bool ha_rocksdb::rpl_lookup_rows()
{
  return (!m_rpl_delete_rows && !m_rpl_update_rows) ||
          rdb_table_has_hidden_pk(table) ||
          THDVAR(handler::ha_thd(), rpl_lookup_rows);
}

}  // namespace myrocks


/*
  Register the storage engine plugin outside of myrocks namespace
  so that mysql_declare_plugin does not get confused when it does
  its name generation.
*/

static struct st_mysql_storage_engine rocksdb_storage_engine=
  { MYSQL_HANDLERTON_INTERFACE_VERSION };

mysql_declare_plugin(rocksdb_se)
{
  MYSQL_STORAGE_ENGINE_PLUGIN,                  /* Plugin Type */
  &rocksdb_storage_engine,                      /* Plugin Descriptor */
  "ROCKSDB",                                    /* Plugin Name */
  "Monty Program Ab",                           /* Plugin Author */
  "RocksDB storage engine",                     /* Plugin Description */
  PLUGIN_LICENSE_GPL,                           /* Plugin Licence */
  myrocks::rdb_plugin_load_fn,                  /* Plugin Entry Point */
  myrocks::rdb_plugin_unload_fn,                /* Plugin Deinitializer */
  0x0001,                                       /* version number (0.1) */
  myrocks::rdb_status_vars,                     /* status variables */
  myrocks::rdb_system_variables,                /* system variables */
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
