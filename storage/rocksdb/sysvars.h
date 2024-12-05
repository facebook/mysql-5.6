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

#ifndef MYROCKS_SYSVARS_H_
#define MYROCKS_SYSVARS_H_

#include <cstdint>
#include <cstring>

#include "rocksdb/options.h"

#include "my_inttypes.h"

class THD;
struct SYS_VAR;

namespace myrocks::sysvars {

// To add a new variable:
// - Declare a getter to myrocks::sysvars:: namespace in sysvars.h, either above
//   the "Thread variables" comment for a global variable, or below it for a
//   thread one. It's possible if needed (but it's a code smell) to
//   declare/define a public setter and/or exported variable itself too.
// - Add a MYSQL_SYSVAR_* or a MYSQL_THDVAR_* definition to the end of anonymous
//   namespace in sysvars.cc. If it needs one of or both of the validate and set
//   functions, define them right above. If it needs constants for default, min,
//   or max values, either declare them locally here too, either in sysvars.h
//   next to the getter if something else needs them too.
// - Define the getter in sysvars.cc, either above or below the "Thread
//   variables" comment.
// - Add "MYSQL_SYSVAR(your_new_var)" to vars variable at the end of sysvars.cc.
// - Don't prefix any new declarations with "rocksdb_" unless it is necessary to
//   avoid confusion (i.e. datadir). The role of the prefix is done by the
//   namespace.

// TODO(laurynas): hide the existing mutable globals behind getters. If the
// getters are inline, and the variables still need declarations here, create a
// "detail" or "internal" namespace for them.

constexpr char DEFAULT_READ_FREE_RPL_TABLES[] = ".*";

extern bool enable_bulk_load_api;
extern bool enable_remove_orphaned_dropped_cfs;
extern bool enable_udt_in_mem;

// TODO(laurynas): look into enum class conversion. That might need a new MySQL
// SYSVAR macro family type.
enum read_free_rpl_type { OFF = 0, PK_ONLY, PK_SK };
extern ulong read_free_rpl;

enum corrupt_data_action_type { ERROR = 0, ABORT_SERVER, WARNING };
extern ulong corrupt_data_action;

enum invalid_create_option_action_type { LOG = 0, PUSH_WARNING, PUSH_ERROR };
extern ulong invalid_create_option_action;

enum class io_error_action_type : ulong { ABORT_SERVER = 0, IGNORE_ERROR };
extern ulong io_error_action;

extern ulong write_policy;
// TODO(laurynas): if MYSQL_SYSVAR_INT64_T is introduced, use it to convert
// these vars to std::int64_t to match the RocksDB API.
extern unsigned long long rate_limiter_bytes_per_sec;
extern unsigned long long sst_mgr_rate_bytes_per_sec;
extern std::uint32_t max_latest_deadlocks;
extern ulong info_log_level;
extern std::uint32_t wal_recovery_mode;
extern bool track_and_verify_wals_in_manifest;
extern std::uint32_t rocksdb_stats_level;

extern char *wal_dir;
extern char *rocksdb_datadir;

[[nodiscard]] inline char *get_wal_dir() noexcept {
  return (wal_dir && *wal_dir) ? wal_dir : rocksdb_datadir;
}

[[nodiscard]] inline bool is_wal_dir_separate() noexcept {
  return wal_dir && *wal_dir &&
         // Prefer cheapness over accuracy by doing lexicographic path
         // comparison only
         strcmp(wal_dir, rocksdb_datadir);
}

extern char *persistent_cache_path;
extern unsigned long persistent_cache_size_mb;
extern char *wsenv_path;
extern char *wsenv_tenant;
extern char *wsenv_oncall;
extern int max_bottom_pri_background_compactions;
extern int block_cache_numshardbits;
extern long long block_cache_size;
extern long long sim_cache_size;
extern bool use_io_uring;
extern bool use_hyper_clock_cache;
extern bool cache_dump;
extern double cache_high_pri_pool_ratio;
extern ulong index_type;
extern bool charge_memory;
extern bool use_write_buffer_manager;
extern char *default_cf_options;
extern char *override_cf_options;
extern char *rocksdb_update_cf_options;
extern bool use_default_sk_cf;

enum flush_log_at_trx_commit_type : unsigned int {
  FLUSH_LOG_NEVER = 0,
  FLUSH_LOG_SYNC,
  FLUSH_LOG_BACKGROUND,
  FLUSH_LOG_MAX /* must be last */
};
extern uint32_t flush_log_at_trx_commit;

extern uint32_t debug_optimizer_n_rows;
extern bool force_compute_memtable_stats;
extern uint32_t force_compute_memtable_stats_cachetime;
extern bool debug_optimizer_no_zero_cardinality;
extern uint32_t debug_cardinality_multiplier;
extern bool pause_background_work;
extern uint32_t binlog_ttl_compaction_ts_interval_secs;
extern uint32_t binlog_ttl_compaction_ts_offset_secs;
extern uint32_t rocksdb_max_manual_compactions;
extern bool rollback_on_timeout;
extern uint32_t debug_manual_compaction_delay;
extern uint32_t io_write_timeout_secs;
extern bool ignore_unknown_options;
extern bool collect_sst_properties;
extern uint64_t compaction_sequential_deletes;
extern uint64_t compaction_sequential_deletes_window;
extern long long compaction_sequential_deletes_file_size;
extern bool compaction_sequential_deletes_count_sd;
extern bool print_snapshot_conflict_queries;
extern uint32_t validate_tables;
extern uint32_t table_stats_sampling_pct;
extern uint32_t table_stats_recalc_threshold_pct;
extern unsigned long long table_stats_recalc_threshold_count;
extern int32_t table_stats_background_thread_nice_value;
extern unsigned long long table_stats_max_num_rows_scanned;
extern uint32_t stats_recalc_rate;
extern bool table_stats_skip_system_cf;
extern bool allow_to_start_after_corruption;
extern bool enable_insert_with_update_caching;
extern unsigned long long max_compaction_history;
extern uint32_t bypass_rpc_rejected_log_ts_interval_secs;
extern bool skip_locks_if_skip_unique_check;
extern bool alter_column_default_inplace;
extern bool partial_index_blind_delete;
extern bool partial_index_ignore_killed;
extern bool disable_instant_ddl;
extern bool enable_instant_ddl;
extern bool enable_instant_ddl_for_append_column;
extern bool enable_instant_ddl_for_column_default_changes;
extern bool enable_instant_ddl_for_table_comment_changes;
extern bool enable_instant_ddl_for_drop_index_changes;
extern bool enable_instant_ddl_for_update_index_visibility;
extern bool enable_tmp_table;
extern bool enable_delete_range_for_drop_index;
extern bool alter_table_comment_inplace;
extern bool column_default_value_as_expression;
extern uint32_t max_intrinsic_tmp_table_write_count;
extern uint clone_checkpoint_max_count;
extern uint clone_checkpoint_max_age;
extern unsigned long long converter_record_cached_length;

enum file_checksums_type {
  CHECKSUMS_OFF = 0,
  CHECKSUMS_WRITE_ONLY,
  CHECKSUMS_WRITE_AND_VERIFY,
  CHECKSUMS_WRITE_AND_VERIFY_ON_CLONE,
};
extern ulong file_checksums;

extern bool debug_skip_bloom_filter_check_on_iterator_bounds;
extern unsigned long long max_trash_db_ratio_pct;

constexpr uint BULK_LOAD_HISTORY_DEFAULT_SIZE = 10;
extern uint bulk_load_history_size;

extern unsigned long long write_batch_mem_free_threshold;

[[nodiscard]] bool is_ttl_enabled() noexcept;
[[nodiscard]] bool is_ttl_read_filtering_enabled() noexcept;
[[nodiscard]] bool is_ttl_compaction_filter_paused() noexcept;
[[nodiscard]] bool is_binlog_ttl_enabled() noexcept;
[[nodiscard]] bool is_table_scan_index_stats_calculation_enabled() noexcept;

/*
  SELECT BYPASS related policy:
  0x1 = ON/OFF MASK - whether bypass is on or off
  0x2 = FORCED/DEFAULT MASK, whether the ON/OFF bit is being FORCED (0x0)
  or simply inteneded as DEFAULT (0x1).

  always_off = 0x0, always off regardless of hint. This is the default
  always_on  = 0x1, always on regardless of hint
  opt_in     = 0x2, default off, turn on with +bypass hint
  opt_out    = 0x3, default on, turn off with +no_bypass hint
 */
constexpr unsigned SELECT_BYPASS_POLICY_ON_MASK = 0x1;
constexpr unsigned SELECT_BYPASS_POLICY_DEFAULT_MASK = 0x2;

enum select_bypass_policy_type {
  always_off = 0,
  always_on = SELECT_BYPASS_POLICY_ON_MASK,
  opt_in = SELECT_BYPASS_POLICY_DEFAULT_MASK,
  opt_out = (SELECT_BYPASS_POLICY_DEFAULT_MASK | SELECT_BYPASS_POLICY_ON_MASK),
  default_value = 0
};

/* Whether ROCKSDB_ENABLE_SELECT_BYPASS is enabled */
[[nodiscard]] select_bypass_policy_type get_select_bypass_policy() noexcept;

/* Whether we should log unsupported SELECT bypass */
[[nodiscard]] bool should_fail_unsupported_select_bypass() noexcept;

/* Whether we should log rejected unsupported SELECT bypass */
[[nodiscard]] bool should_log_rejected_select_bypass() noexcept;

/* Whether we should log failed unsupported SELECT bypass */
[[nodiscard]] bool should_log_failed_select_bypass() noexcept;

/* Whether we should allow non-optimal filters in SELECT bypass */
[[nodiscard]] bool should_allow_filters_select_bypass() noexcept;

[[nodiscard]] uint32_t get_select_bypass_rejected_query_history_size() noexcept;
[[nodiscard]] uint32_t get_select_bypass_debug_row_delay() noexcept;

[[nodiscard]] unsigned long long  // NOLINT(runtime/int)
get_select_bypass_multiget_min() noexcept;

/* Whether ROCKSDB_BYPASS_RPC_ON is enabled */
[[nodiscard]] bool is_bypass_rpc_on() noexcept;

/* Whether we should log rejected unsupported bypass rpc */
[[nodiscard]] bool should_log_rejected_bypass_rpc() noexcept;

#ifndef NDEBUG
[[nodiscard]] int debug_set_ttl_rec_ts() noexcept;
[[nodiscard]] int debug_set_ttl_snapshot_ts() noexcept;
[[nodiscard]] int debug_set_ttl_read_filter_ts() noexcept;
[[nodiscard]] bool debug_set_ttl_ignore_pk() noexcept;
#endif

// Thread variables

[[nodiscard]] ulong lock_wait_timeout(THD *thd);
[[nodiscard]] bool deadlock_detect(THD *thd);
[[nodiscard]] ulong deadlock_detect_depth(THD *thd);
[[nodiscard]] bool commit_time_batch_for_recovery(THD *thd);
[[nodiscard]] bool trace_sst_api(THD *thd);
[[nodiscard]] bool bulk_load(THD *thd);
[[nodiscard]] bool bulk_load_allow_sk(THD *thd);
[[nodiscard]] bool bulk_load_allow_unsorted(THD *thd);
[[nodiscard]] bool bulk_load_fail_if_not_bottommost_level(THD *thd);
[[nodiscard]] bool bulk_load_use_sst_partitioner(THD *thd);
[[nodiscard]] bool bulk_load_enable_unique_key_check(THD *thd);
[[nodiscard]] uint bulk_load_compression_parallel_threads(THD *thd);
[[nodiscard]] char *tmpdir(THD *thd);
[[nodiscard]] bool commit_in_the_middle(THD *thd);
[[nodiscard]] bool blind_delete_primary_key(THD *thd);
[[nodiscard]] bool enable_iterate_bounds(THD *thd);
[[nodiscard]] bool check_iterate_bounds(THD *thd);
[[nodiscard]] bool skip_snapshot_validation(THD *thd);
[[nodiscard]] bool skip_bloom_filter_on_read(THD *thd);
[[nodiscard]] ulong max_row_locks(THD *thd);
[[nodiscard]] ulonglong write_batch_max_bytes(THD *thd);
[[nodiscard]] ulonglong write_batch_flush_threshold(THD *thd);
[[nodiscard]] bool lock_scanned_rows(THD *thd);
[[nodiscard]] ulong bulk_load_size(THD *thd);
[[nodiscard]] bool bulk_load_partial_index(THD *thd);
[[nodiscard]] ulonglong merge_buf_size(THD *thd);
[[nodiscard]] ulonglong merge_combine_read_size(THD *thd);
[[nodiscard]] ulonglong merge_tmp_file_removal_delay_ms(THD *thd);
[[nodiscard]] int manual_compaction_threads(THD *thd);
[[nodiscard]] rocksdb::BottommostLevelCompaction
manual_compaction_bottommost_level(THD *thd);
[[nodiscard]] int perf_context_level(THD *thd);
[[nodiscard]] bool write_disable_wal(THD *thd);
[[nodiscard]] bool write_ignore_missing_column_families(THD *thd);
[[nodiscard]] ulong protection_bytes_per_key(THD *thd);
[[nodiscard]] bool skip_fill_cache(THD *thd);
[[nodiscard]] bool unsafe_for_binlog(THD *thd);
[[nodiscard]] uint records_in_range(THD *thd);
[[nodiscard]] uint force_index_records_in_range(THD *thd);
[[nodiscard]] char *create_temporary_checkpoint(THD *thd);
[[nodiscard]] int checksums_pct(THD *thd);
[[nodiscard]] bool store_row_debug_checksums(THD *thd);
[[nodiscard]] bool verify_row_debug_checksums(THD *thd);
[[nodiscard]] bool master_skip_tx_api(THD *thd);
[[nodiscard]] ulong mrr_batch_size(THD *thd);
[[nodiscard]] unsigned long long get_partial_index_sort_max_mem(THD *thd);
[[nodiscard]] bool enable_autoinc_compat_mode(THD *thd);
[[nodiscard]] ulonglong consistent_snapshot_ttl_read_filtering_ts_nsec(
    THD *thd);

void init();
void shutdown();

extern SYS_VAR *vars[];

}  // namespace myrocks::sysvars

#endif  // MYROCKS_SYSVARS_H_
