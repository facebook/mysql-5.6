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

#include "sysvars.h"

#include "my_inttypes.h"
#include "mysql/plugin.h"
#include "mysqld_error.h"
#include "sql/sql_error.h"

#include "rocksdb/perf_level.h"
#include "rocksdb/utilities/transaction_db.h"

#include "ha_rocksdb.h"
#include "ha_rocksdb_proto.h"
#include "rdb_bulk_load.h"
#include "rdb_global.h"
#include "rdb_mutex_wrapper.h"
#include "rdb_psi.h"

namespace myrocks::sysvars {

void set_bulk_load(THD *thd, bool new_val);

namespace {

int mysql_value_to_bool(st_mysql_value *value, bool *return_value) {
  const auto new_value_type = value->value_type(value);
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

// A stub sysvar update function that does nothing. Used instead of nullptr to
// indicate that the sysvar is not read-only.
void rw_sysvar_update_noop(THD *, SYS_VAR *, void *, const void *) {}

constexpr ulong RDB_MAX_LOCK_WAIT_SECONDS = 1024 * 1024 * 1024;

// TODO: 0 means don't wait at all, and we don't support it yet?
MYSQL_THDVAR_ULONG(lock_wait_timeout, PLUGIN_VAR_RQCMDARG,
                   "Number of seconds to wait for lock", nullptr, nullptr,
                   /*default*/ 1, /*min*/ 1,
                   /*max*/ RDB_MAX_LOCK_WAIT_SECONDS, 0);

MYSQL_THDVAR_BOOL(deadlock_detect, PLUGIN_VAR_RQCMDARG,
                  "Enables deadlock detection", nullptr, nullptr, false);

constexpr ulong RDB_DEADLOCK_DETECT_DEPTH = 50;

MYSQL_THDVAR_ULONG(deadlock_detect_depth, PLUGIN_VAR_RQCMDARG,
                   "Number of transactions deadlock detection will traverse "
                   "through before assuming deadlock",
                   nullptr, nullptr,
                   /*default*/ RDB_DEADLOCK_DETECT_DEPTH,
                   /*min*/ 2,
                   /*max*/ ULONG_MAX, 0);

MYSQL_THDVAR_BOOL(
    commit_time_batch_for_recovery, PLUGIN_VAR_RQCMDARG,
    "TransactionOptions::commit_time_batch_for_recovery for RocksDB", nullptr,
    nullptr, true);

MYSQL_THDVAR_BOOL(
    trace_sst_api, PLUGIN_VAR_RQCMDARG,
    "Generate trace output in the log for each call to the SstFileWriter",
    nullptr, nullptr, false);

int check_bulk_load(THD *thd, SYS_VAR *, void *save, st_mysql_value *value) {
  bool new_value;
  if (mysql_value_to_bool(value, &new_value) != 0) {
    return 1;
  }

  const auto rc = rocksdb_finish_bulk_load_if_any(thd);
  if (rc) {
    // NO_LINT_DEBUG
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "RocksDB: Error %d finalizing last SST file while setting "
                    "bulk loading variable",
                    rc);
    set_bulk_load(thd, false);
    return 1;
  }

  *static_cast<bool *>(save) = new_value;
  return 0;
}

MYSQL_THDVAR_BOOL(bulk_load, PLUGIN_VAR_RQCMDARG,
                  "Use bulk-load mode for inserts. This disables unique_checks "
                  "and enables rocksdb_commit_in_the_middle.",
                  check_bulk_load, nullptr, false);

/** only allow setting the variable when bulk_load is off */
int check_bulk_load_allow_unsorted(THD *thd, SYS_VAR *, void *save,
                                   st_mysql_value *value) {
  bool new_value;
  if (mysql_value_to_bool(value, &new_value) != 0) {
    return 1;
  }

  if (bulk_load(thd)) {
    my_error(ER_ERROR_WHEN_EXECUTING_COMMAND, MYF(0), "SET",
             "Cannot change this setting while bulk load is enabled");

    return 1;
  }

  *static_cast<bool *>(save) = new_value;
  return 0;
}

MYSQL_THDVAR_BOOL(bulk_load_allow_sk, PLUGIN_VAR_RQCMDARG,
                  "Allow bulk loading of sk keys during bulk-load. Can be "
                  "changed only when bulk load is disabled.",
                  /* Intentionally reuse unsorted's check function */
                  check_bulk_load_allow_unsorted, nullptr, false);

MYSQL_THDVAR_BOOL(bulk_load_allow_unsorted, PLUGIN_VAR_RQCMDARG,
                  "Allow unsorted input during bulk-load. Can be changed only "
                  "when bulk load is disabled.",
                  check_bulk_load_allow_unsorted, nullptr, false);

/* This enum needs to be kept up to date with invalid_create_option_action */
const char *invalid_create_option_action_names[] = {"LOG", "PUSH_WARNING",
                                                    "PUSH_ERROR", NullS};

TYPELIB invalid_create_option_action_typelib = {
    array_elements(invalid_create_option_action_names) - 1,
    "invalid_create_option_action_typelib", invalid_create_option_action_names,
    nullptr};

MYSQL_SYSVAR_ENUM(
    invalid_create_option_action, invalid_create_option_action,
    PLUGIN_VAR_RQCMDARG,
    "Control behavior when creating the table hits some error. We can log "
    "the "
    "error only, pass the query and give users a warning, or fail the query.",
    nullptr, nullptr, invalid_create_option_action_type::LOG,
    &invalid_create_option_action_typelib);

int check_bulk_load_fail_if_not_bottommost_level(THD *thd, SYS_VAR *var,
                                                 void *save,
                                                 st_mysql_value *value) {
  // reuse the same logic
  return check_bulk_load_allow_unsorted(thd, var, save, value);
}

MYSQL_THDVAR_BOOL(
    bulk_load_fail_if_not_bottommost_level, PLUGIN_VAR_RQCMDARG,
    "Fail the bulk load if a sst file created from bulk load cannot fit in the "
    "rocksdb bottommost level. Turn off this variable could have severe "
    "performance impact. Can be changed only when bulk load is disabled.",
    check_bulk_load_fail_if_not_bottommost_level, nullptr, false);

int check_bulk_load_use_sst_partitioner(THD *thd, SYS_VAR *var, void *save,
                                        st_mysql_value *value) {
  // reuse the same logic
  return check_bulk_load_allow_unsorted(thd, var, save, value);
}

MYSQL_THDVAR_BOOL(bulk_load_use_sst_partitioner, PLUGIN_VAR_RQCMDARG,
                  "Use sst partitioner to split sst files to ensure bulk load "
                  "sst files can be ingested to bottommost level",
                  check_bulk_load_use_sst_partitioner, nullptr, false);

int check_bulk_load_unique_key_check(THD *thd, SYS_VAR *var, void *save,
                                     st_mysql_value *value) {
  // reuse the same logic
  return check_bulk_load_allow_unsorted(thd, var, save, value);
}

MYSQL_THDVAR_BOOL(bulk_load_enable_unique_key_check, PLUGIN_VAR_RQCMDARG,
                  "Check the violation of unique key constraint during "
                  "bulk-load. Can be changed only when bulk load is disabled.",
                  check_bulk_load_unique_key_check, nullptr, false);

MYSQL_SYSVAR_BOOL(enable_bulk_load_api, enable_bulk_load_api,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "Enables using SstFileWriter for bulk loading", nullptr,
                  nullptr, enable_bulk_load_api);

MYSQL_THDVAR_UINT(bulk_load_compression_parallel_threads, PLUGIN_VAR_RQCMDARG,
                  "The number of parallel worker threads to compress SST data "
                  "blocks during bulk load",
                  nullptr, nullptr, /*default*/ 1, /*min*/ 1,
                  /*max*/ 1024, 0);

MYSQL_SYSVAR_BOOL(enable_remove_orphaned_dropped_cfs,
                  enable_remove_orphaned_dropped_cfs, PLUGIN_VAR_RQCMDARG,
                  "Enables removing dropped cfs from metadata if it doesn't "
                  "exist in cf manager",
                  nullptr, nullptr, enable_remove_orphaned_dropped_cfs);

MYSQL_SYSVAR_BOOL(enable_udt_in_mem, enable_udt_in_mem,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "Enabled user define timestamp in memtable feature to "
                  "support HLC snapshot reads in MyRocks",
                  nullptr, nullptr, enable_udt_in_mem);

MYSQL_THDVAR_STR(tmpdir, PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_MEMALLOC,
                 "Directory for temporary files during DDL operations.",
                 nullptr, nullptr, "");

MYSQL_THDVAR_BOOL(commit_in_the_middle, PLUGIN_VAR_RQCMDARG,
                  "Commit rows implicitly every rocksdb_bulk_load_size, on "
                  "bulk load/insert, update and delete",
                  nullptr, nullptr, false);

MYSQL_THDVAR_BOOL(
    blind_delete_primary_key, PLUGIN_VAR_RQCMDARG,
    "Deleting rows by primary key lookup, without reading rows (Blind "
    "Deletes). Blind delete is disabled if the table has secondary key",
    nullptr, nullptr, false);

MYSQL_THDVAR_BOOL(enable_iterate_bounds, PLUGIN_VAR_OPCMDARG,
                  "Enable rocksdb iterator upper/lower bounds in read options.",
                  nullptr, nullptr, true);

MYSQL_THDVAR_BOOL(check_iterate_bounds, PLUGIN_VAR_OPCMDARG,
                  "Check rocksdb iterator upper/lower bounds during iterating.",
                  nullptr, nullptr, true);

MYSQL_THDVAR_BOOL(skip_snapshot_validation, PLUGIN_VAR_OPCMDARG,
                  "Skips snapshot validation on locking reads. This makes "
                  "MyRocks Repeatable Read behavior close to InnoDB -- forcing "
                  "reading the newest data with locking reads.",
                  nullptr, nullptr, false);

char *read_free_rpl_tables;

int validate_read_free_rpl_tables(THD *, SYS_VAR *, void *save,
                                  st_mysql_value *value) {
  int length = 0;
  const char *wlist_buf = value->val_str(value, nullptr, &length);
  const auto *wlist = wlist_buf ? wlist_buf : DEFAULT_READ_FREE_RPL_TABLES;

  if (rocksdb_validate_read_free_rpl_tables(wlist)) {
    return HA_EXIT_FAILURE;
  }

  *static_cast<const char **>(save) = wlist;
  return HA_EXIT_SUCCESS;
}

void update_read_free_rpl_tables(THD *, SYS_VAR *, void *var_ptr,
                                 const void *save) {
  const auto *wlist = *static_cast<const char *const *>(save);
  rocksdb_update_read_free_rpl_tables(wlist);
  if (wlist == DEFAULT_READ_FREE_RPL_TABLES) {
    // If running SET var = DEFAULT, then
    // rocksdb_validate_read_free_rpl_tables isn't called, and memory is never
    // allocated for the value. Allocate it here.
    *static_cast<const char **>(var_ptr) =
        my_strdup(PSI_NOT_INSTRUMENTED, wlist, MYF(MY_WME));
  } else {
    // Otherwise, we just reuse the value allocated from
    // rocksdb_validate_read_free_rpl_tables.
    *static_cast<const char **>(var_ptr) = wlist;
  }
}

MYSQL_SYSVAR_STR(read_free_rpl_tables, read_free_rpl_tables,
                 PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
                 "List of tables that will use read-free replication on the "
                 "slave (i.e. not lookup a row during replication)",
                 validate_read_free_rpl_tables, update_read_free_rpl_tables,
                 DEFAULT_READ_FREE_RPL_TABLES);

/* This array needs to be kept up to date with
read_free_rpl_type */
const char *read_free_rpl_names[] = {"OFF", "PK_ONLY", "PK_SK", NullS};

TYPELIB read_free_rpl_typelib = {array_elements(read_free_rpl_names) - 1,
                                 "read_free_rpl_typelib", read_free_rpl_names,
                                 nullptr};

MYSQL_SYSVAR_ENUM(
    read_free_rpl, read_free_rpl, PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
    "Use read-free replication on the slave (i.e. no row lookup during "
    "replication). Default is OFF, PK_SK will enable it on all tables with "
    "primary key. PK_ONLY will enable it on tables where the only key is the "
    "primary key (i.e. no secondary keys).",
    nullptr, nullptr, read_free_rpl_type::OFF, &read_free_rpl_typelib);

/* This enum needs to be kept up to date with
corrupt_data_action */
const char *corrupt_data_action_names[] = {"ERROR", "ABORT_SERVER", "WARNING",
                                           NullS};

TYPELIB corrupt_data_action_typelib = {
    array_elements(corrupt_data_action_names) - 1,
    "corrupt_data_action_typelib", corrupt_data_action_names, nullptr};

MYSQL_SYSVAR_ENUM(
    corrupt_data_action, corrupt_data_action, PLUGIN_VAR_RQCMDARG,
    "Control behavior when hitting data corruption. We can fail the query, "
    "crash the server or pass the query and give users a warning. ",
    nullptr, nullptr, corrupt_data_action_type::ERROR,
    &corrupt_data_action_typelib);

/* This enum needs to be kept up to date with io_error_action */
const char *io_error_action_names[] = {"ABORT_SERVER", "IGNORE_ERROR", NullS};

TYPELIB io_error_action_typelib = {array_elements(io_error_action_names) - 1,
                                   "io_error_action_typelib",
                                   io_error_action_names, nullptr};

MYSQL_SYSVAR_ENUM(
    io_error_action, io_error_action, PLUGIN_VAR_RQCMDARG,
    "Control behavior when hitting I/O error. By default MyRocks aborts server "
    "and refuses to start. Setting IGNORE_ERROR suppresses an error instead.",
    nullptr, nullptr, static_cast<ulong>(io_error_action_type::ABORT_SERVER),
    &io_error_action_typelib);

MYSQL_THDVAR_BOOL(skip_bloom_filter_on_read, PLUGIN_VAR_RQCMDARG,
                  "Skip using bloom filter for reads", nullptr, nullptr, false);

constexpr ulong RDB_DEFAULT_MAX_ROW_LOCKS = 1024 * 1024;
constexpr ulong RDB_MAX_ROW_LOCKS = 1024 * 1024 * 1024;

MYSQL_THDVAR_ULONG(max_row_locks, PLUGIN_VAR_RQCMDARG,
                   "Maximum number of locks a transaction can have", nullptr,
                   nullptr,
                   /*default*/ RDB_DEFAULT_MAX_ROW_LOCKS,
                   /*min*/ 1,
                   /*max*/ RDB_MAX_ROW_LOCKS, 0);

MYSQL_THDVAR_ULONGLONG(
    write_batch_max_bytes, PLUGIN_VAR_RQCMDARG,
    "Maximum size of write batch in bytes. 0 means no limit.", nullptr, nullptr,
    /* default */ 0, /* min */ 0, /* max */ SIZE_T_MAX, 1);

MYSQL_THDVAR_ULONGLONG(
    write_batch_flush_threshold, PLUGIN_VAR_RQCMDARG,
    "Maximum size of write batch in bytes before flushing. Only valid if "
    "rocksdb_write_policy is WRITE_UNPREPARED. 0 means no limit.",
    nullptr, nullptr, /* default */ 0, /* min */ 0, /* max */ SIZE_T_MAX, 1);

MYSQL_THDVAR_BOOL(
    lock_scanned_rows, PLUGIN_VAR_RQCMDARG,
    "Take and hold locks on rows that are scanned but not updated", nullptr,
    nullptr, false);

constexpr ulong RDB_DEFAULT_BULK_LOAD_SIZE = 1000;
constexpr ulong RDB_MAX_BULK_LOAD_SIZE = 1024 * 1024 * 1024;

MYSQL_THDVAR_ULONG(bulk_load_size, PLUGIN_VAR_RQCMDARG,
                   "Max #records in a batch for bulk-load mode", nullptr,
                   nullptr,
                   /*default*/ RDB_DEFAULT_BULK_LOAD_SIZE,
                   /*min*/ 1,
                   /*max*/ RDB_MAX_BULK_LOAD_SIZE, 0);

MYSQL_THDVAR_BOOL(
    bulk_load_partial_index, PLUGIN_VAR_RQCMDARG,
    "Materialize partial index during bulk load, instead of leaving it empty.",
    nullptr, nullptr, true);

constexpr size_t RDB_DEFAULT_MERGE_BUF_SIZE = 64 * 1024 * 1024;
constexpr size_t RDB_MIN_MERGE_BUF_SIZE = 100;

MYSQL_THDVAR_ULONGLONG(merge_buf_size, PLUGIN_VAR_RQCMDARG,
                       "Size to allocate for merge sort buffers written out to "
                       "disk during inplace index creation.",
                       nullptr, nullptr,
                       /* default (64MB) */ RDB_DEFAULT_MERGE_BUF_SIZE,
                       /* min (100B) */ RDB_MIN_MERGE_BUF_SIZE,
                       /* max */ SIZE_T_MAX, 1);

constexpr size_t RDB_DEFAULT_MERGE_COMBINE_READ_SIZE = 1024 * 1024 * 1024;
constexpr size_t RDB_MIN_MERGE_COMBINE_READ_SIZE = 100;

MYSQL_THDVAR_ULONGLONG(
    merge_combine_read_size, PLUGIN_VAR_RQCMDARG,
    "Size that we have to work with during combine (reading from disk) phase "
    "of external sort during fast index creation.",
    nullptr, nullptr,
    /* default (1GB) */ RDB_DEFAULT_MERGE_COMBINE_READ_SIZE,
    /* min (100B) */ RDB_MIN_MERGE_COMBINE_READ_SIZE,
    /* max */ SIZE_T_MAX, 1);

constexpr size_t RDB_DEFAULT_MERGE_TMP_FILE_REMOVAL_DELAY = 0;
constexpr size_t RDB_MIN_MERGE_TMP_FILE_REMOVAL_DELAY = 0;

MYSQL_THDVAR_ULONGLONG(
    merge_tmp_file_removal_delay_ms, PLUGIN_VAR_RQCMDARG,
    "Fast index creation creates a large tmp file on disk during index "
    "creation.  Removing this large file all at once when index creation is "
    "complete can cause trim stalls on Flash.  This variable specifies a "
    "duration to sleep (in milliseconds) between calling chsize() to truncate "
    "the file in chunks.  The chunk size is the same as merge_buf_size.",
    nullptr, nullptr,
    /* default (0ms) */ RDB_DEFAULT_MERGE_TMP_FILE_REMOVAL_DELAY,
    /* min (0ms) */ RDB_MIN_MERGE_TMP_FILE_REMOVAL_DELAY,
    /* max */ SIZE_T_MAX, 1);

MYSQL_THDVAR_INT(manual_compaction_threads, PLUGIN_VAR_RQCMDARG,
                 "How many rocksdb threads to run for manual compactions",
                 nullptr, nullptr,
                 /* default rocksdb.dboption max_subcompactions */ 0,
                 /* min */ 0, /* max */ 128, 0);

// This enum needs to be kept up to date with
// rocksdb::BottommostLevelCompaction
const char *bottommost_level_compaction_names[] = {
    "kSkip", "kIfHaveCompactionFilter", "kForce", "kForceOptimized", NullS};

TYPELIB bottommost_level_compaction_typelib = {
    array_elements(bottommost_level_compaction_names) - 1,
    "bottommost_level_compaction_typelib", bottommost_level_compaction_names,
    nullptr};

MYSQL_THDVAR_ENUM(
    manual_compaction_bottommost_level, PLUGIN_VAR_RQCMDARG,
    "Option for bottommost level compaction during manual compaction", nullptr,
    nullptr,
    /* default */
    static_cast<ulong>(rocksdb::BottommostLevelCompaction::kForceOptimized),
    &bottommost_level_compaction_typelib);

MYSQL_SYSVAR_BOOL(create_if_missing, get_rocksdb_db_options().create_if_missing,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::create_if_missing for RocksDB", nullptr, nullptr,
                  get_rocksdb_db_options().create_if_missing);

MYSQL_SYSVAR_BOOL(two_write_queues, get_rocksdb_db_options().two_write_queues,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::two_write_queues for RocksDB", nullptr, nullptr,
                  get_rocksdb_db_options().two_write_queues);

MYSQL_SYSVAR_BOOL(manual_wal_flush, get_rocksdb_db_options().manual_wal_flush,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::manual_wal_flush for RocksDB", nullptr, nullptr,
                  get_rocksdb_db_options().manual_wal_flush);

/* This enum needs to be kept up to date with rocksdb::TxnDBWritePolicy */
const char *write_policy_names[] = {"write_committed", "write_prepared",
                                    "write_unprepared", NullS};

TYPELIB write_policy_typelib = {array_elements(write_policy_names) - 1,
                                "write_policy_typelib", write_policy_names,
                                nullptr};

MYSQL_SYSVAR_ENUM(write_policy, write_policy,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::write_policy for RocksDB", nullptr, nullptr,
                  rocksdb::TxnDBWritePolicy::WRITE_COMMITTED,
                  &write_policy_typelib);

MYSQL_SYSVAR_BOOL(create_missing_column_families,
                  get_rocksdb_db_options().create_missing_column_families,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::create_missing_column_families for RocksDB",
                  nullptr, nullptr,
                  get_rocksdb_db_options().create_missing_column_families);

MYSQL_SYSVAR_BOOL(error_if_exists, get_rocksdb_db_options().error_if_exists,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::error_if_exists for RocksDB", nullptr, nullptr,
                  get_rocksdb_db_options().error_if_exists);

MYSQL_SYSVAR_BOOL(paranoid_checks, get_rocksdb_db_options().paranoid_checks,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::paranoid_checks for RocksDB", nullptr, nullptr,
                  get_rocksdb_db_options().paranoid_checks);

/*
    This function allows setting the rate limiter's bytes per second value
    but only if the rate limiter is turned on which has to be done at startup.
    If the rate is already 0 (turned off) or we are changing it to 0 (trying
    to turn it off) this function will push a warning to the client and do
    nothing.
    This is similar to the code in innodb_doublewrite_update (found in
    storage/innobase/handler/ha_innodb.cc).
  */
void set_rate_limiter_bytes_per_sec(THD *thd, SYS_VAR *, void *,
                                    const void *save) {
  const auto new_val = *static_cast<const uint64_t *>(save);
  if (new_val == 0 || rate_limiter_bytes_per_sec == 0) {
    /*
      If a rate_limiter was not enabled at startup we can't change it nor
      can we disable it if one was created at startup
    */
    push_warning_printf(thd, Sql_condition::SL_WARNING, ER_WRONG_ARGUMENTS,
                        "RocksDB: rocksdb_rate_limiter_bytes_per_sec cannot "
                        "be dynamically changed to or from 0.  Do a clean "
                        "shutdown if you want to change it from or to 0.");
  } else if (new_val != rate_limiter_bytes_per_sec) {
    rocksdb_set_rate_limiter_bytes_per_sec(new_val);
    rate_limiter_bytes_per_sec = new_val;
  }
}

MYSQL_SYSVAR_ULONGLONG(rate_limiter_bytes_per_sec, rate_limiter_bytes_per_sec,
                       PLUGIN_VAR_RQCMDARG,
                       "DBOptions::rate_limiter bytes_per_sec for RocksDB",
                       nullptr, set_rate_limiter_bytes_per_sec,
                       /* default */ 0L,
                       /* min */ 0L, /* max */ MAX_RATE_LIMITER_BYTES_PER_SEC,
                       0);

void set_sst_mgr_rate_bytes_per_sec(THD *, SYS_VAR *, void *,
                                    const void *save) {
  const auto new_val = *static_cast<const uint64_t *>(save);

  if (new_val != sst_mgr_rate_bytes_per_sec) {
    sst_mgr_rate_bytes_per_sec = new_val;
    rocksdb_set_sst_mgr_rate_bytes_per_sec(new_val);
  }
}

MYSQL_SYSVAR_ULONGLONG(
    sst_mgr_rate_bytes_per_sec, sst_mgr_rate_bytes_per_sec, PLUGIN_VAR_RQCMDARG,
    "DBOptions::sst_file_manager rate_bytes_per_sec for RocksDB", nullptr,
    set_sst_mgr_rate_bytes_per_sec,
    /* default */ DEFAULT_SST_MGR_RATE_BYTES_PER_SEC,
    /* min */ 0L, /* max */ UINT64_MAX, 0);

void set_delayed_write_rate(THD *, SYS_VAR *, void *, const void *save) {
  const auto new_val = *static_cast<const uint64_t *>(save);
  rocksdb_set_delayed_write_rate(new_val);
}

MYSQL_SYSVAR_UINT64_T(delayed_write_rate,
                      get_rocksdb_db_options().delayed_write_rate,
                      PLUGIN_VAR_RQCMDARG, "DBOptions::delayed_write_rate",
                      nullptr, set_delayed_write_rate,
                      get_rocksdb_db_options().delayed_write_rate, 0,
                      UINT64_MAX, 0);

static_assert(sizeof(uint) == sizeof(max_latest_deadlocks));

void set_max_latest_deadlocks(THD *, SYS_VAR *, void *, const void *save) {
  const auto new_val = *static_cast<const uint32_t *>(save);
  if (max_latest_deadlocks != new_val) {
    max_latest_deadlocks = new_val;
    rocksdb_set_max_latest_deadlocks(new_val);
  }
}

MYSQL_SYSVAR_UINT(max_latest_deadlocks, max_latest_deadlocks,
                  PLUGIN_VAR_RQCMDARG,
                  "Maximum number of recent deadlocks to store", nullptr,
                  set_max_latest_deadlocks, rocksdb::kInitialMaxDeadlocks, 0,
                  UINT32_MAX, 0);

void set_rocksdb_info_log_level(THD *, SYS_VAR *, void *, const void *save) {
  assert(save != nullptr);

  info_log_level = *static_cast<const uint64_t *>(save);
  rocksdb_set_info_log_level(info_log_level);
}

/* This array needs to be kept up to date with rocksdb::InfoLogLevel */
const char *info_log_level_names[] = {"debug_level", "info_level",
                                      "warn_level",  "error_level",
                                      "fatal_level", NullS};

TYPELIB info_log_level_typelib = {array_elements(info_log_level_names) - 1,
                                  "info_log_level_typelib",
                                  info_log_level_names, nullptr};

MYSQL_SYSVAR_ENUM(info_log_level, info_log_level, PLUGIN_VAR_RQCMDARG,
                  "Filter level for info logs to be written mysqld error log. "
                  "Valid values include 'debug_level', 'info_level', "
                  "'warn_level', 'error_level' and 'fatal_level'.",
                  nullptr, set_rocksdb_info_log_level,
                  rocksdb::InfoLogLevel::ERROR_LEVEL, &info_log_level_typelib);

MYSQL_THDVAR_INT(
    perf_context_level, PLUGIN_VAR_RQCMDARG,
    "Perf Context Level for rocksdb internal timer stat collection", nullptr,
    nullptr,
    /* default */ rocksdb::PerfLevel::kUninitialized,
    /* min */ rocksdb::PerfLevel::kUninitialized,
    /* max */ rocksdb::PerfLevel::kOutOfBounds - 1, 0);

MYSQL_SYSVAR_UINT(
    wal_recovery_mode, wal_recovery_mode, PLUGIN_VAR_RQCMDARG,
    "DBOptions::wal_recovery_mode for RocksDB. Default is kPointInTimeRecovery",
    nullptr, nullptr,
    /* default */
    static_cast<uint>(rocksdb::WALRecoveryMode::kPointInTimeRecovery),
    /* min */
    static_cast<uint>(rocksdb::WALRecoveryMode::kTolerateCorruptedTailRecords),
    /* max */
    static_cast<uint>(rocksdb::WALRecoveryMode::kSkipAnyCorruptedRecords), 0);

MYSQL_SYSVAR_BOOL(track_and_verify_wals_in_manifest,
                  track_and_verify_wals_in_manifest,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::track_and_verify_wals_in_manifest for RocksDB",
                  nullptr, nullptr, true);

void set_rocksdb_stats_level(THD *, SYS_VAR *, void *, const void *save) {
  assert(save != nullptr);

  const auto new_val =
      static_cast<rocksdb::StatsLevel>(*static_cast<const uint64_t *>(save));
  rocksdb_stats_level = myrocks ::rocksdb_set_stats_level(new_val);
}

MYSQL_SYSVAR_UINT(
    stats_level, rocksdb_stats_level, PLUGIN_VAR_RQCMDARG,
    "Statistics Level for RocksDB. Default is 1 (kExceptHistogramOrTimers)",
    nullptr, set_rocksdb_stats_level,
    /* default */ (uint)rocksdb::StatsLevel::kExceptHistogramOrTimers,
    /* min */ (uint)rocksdb::StatsLevel::kExceptTickers,
    /* max */ (uint)rocksdb::StatsLevel::kAll, 0);

MYSQL_SYSVAR_ULONG(compaction_readahead_size,
                   get_rocksdb_db_options().compaction_readahead_size,
                   PLUGIN_VAR_RQCMDARG,
                   "DBOptions::compaction_readahead_size for RocksDB", nullptr,
                   nullptr, get_rocksdb_db_options().compaction_readahead_size,
                   /* min */ 0L, /* max */ ULONG_MAX, 0);

MYSQL_SYSVAR_BOOL(allow_concurrent_memtable_write,
                  get_rocksdb_db_options().allow_concurrent_memtable_write,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::allow_concurrent_memtable_write for RocksDB",
                  nullptr, nullptr, false);

MYSQL_SYSVAR_BOOL(enable_write_thread_adaptive_yield,
                  get_rocksdb_db_options().enable_write_thread_adaptive_yield,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::enable_write_thread_adaptive_yield for RocksDB",
                  nullptr, nullptr, false);

MYSQL_SYSVAR_INT(max_open_files, get_rocksdb_db_options().max_open_files,
                 PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                 "DBOptions::max_open_files for RocksDB", nullptr, nullptr,
                 get_rocksdb_db_options().max_open_files,
                 /* min */ -2, /* max */ INT_MAX, 0);

MYSQL_SYSVAR_INT(max_file_opening_threads,
                 get_rocksdb_db_options().max_file_opening_threads,
                 PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                 "DBOptions::max_file_opening_threads for RocksDB", nullptr,
                 nullptr, get_rocksdb_db_options().max_file_opening_threads,
                 /* min */ 1, /* max */ INT_MAX, 0);

MYSQL_SYSVAR_UINT64_T(max_total_wal_size,
                      get_rocksdb_db_options().max_total_wal_size,
                      PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                      "DBOptions::max_total_wal_size for RocksDB", nullptr,
                      nullptr, get_rocksdb_db_options().max_total_wal_size,
                      /* min */ 0, /* max */ UINT64_MAX, 0);

MYSQL_SYSVAR_BOOL(use_fsync, get_rocksdb_db_options().use_fsync,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::use_fsync for RocksDB", nullptr, nullptr,
                  get_rocksdb_db_options().use_fsync);

MYSQL_SYSVAR_STR(wal_dir, wal_dir, PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                 "DBOptions::wal_dir for RocksDB", nullptr, nullptr,
                 get_rocksdb_db_options().wal_dir.c_str());

MYSQL_SYSVAR_STR(datadir, rocksdb_datadir,
                 PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_READONLY,
                 "RocksDB data directory", nullptr, nullptr, "./.rocksdb");

MYSQL_SYSVAR_STR(
    persistent_cache_path, persistent_cache_path,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "Path for BlockBasedTableOptions::persistent_cache for RocksDB", nullptr,
    nullptr, "");

MYSQL_SYSVAR_ULONG(persistent_cache_size_mb, persistent_cache_size_mb,
                   PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                   "Size of cache in MB for "
                   "BlockBasedTableOptions::persistent_cache for RocksDB",
                   nullptr, nullptr, persistent_cache_size_mb,
                   /* min */ 0L, /* max */ ULONG_MAX, 0);

MYSQL_SYSVAR_STR(wsenv_path, wsenv_path,
                 PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                 "Path for RocksDB WSEnv", nullptr, nullptr, "");

MYSQL_SYSVAR_STR(wsenv_tenant, wsenv_tenant,
                 PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                 "Tenant for RocksDB WSEnv", nullptr, nullptr, "");

MYSQL_SYSVAR_STR(wsenv_oncall, wsenv_oncall,
                 PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                 "Oncall for RocksDB WSEnv", nullptr, nullptr, "");

MYSQL_SYSVAR_UINT64_T(
    delete_obsolete_files_period_micros,
    get_rocksdb_db_options().delete_obsolete_files_period_micros,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::delete_obsolete_files_period_micros for RocksDB", nullptr,
    nullptr, get_rocksdb_db_options().delete_obsolete_files_period_micros,
    /* min */ 0, /* max */ UINT64_MAX, 0);

void set_max_background_jobs(THD *, SYS_VAR *, void *, const void *save) {
  const auto new_val = *static_cast<const int *>(save);

  rocksdb_set_max_background_jobs(new_val);
}

MYSQL_SYSVAR_INT(max_background_jobs,
                 get_rocksdb_db_options().max_background_jobs,
                 PLUGIN_VAR_RQCMDARG,
                 "DBOptions::max_background_jobs for RocksDB", nullptr,
                 set_max_background_jobs,
                 get_rocksdb_db_options().max_background_jobs,
                 /* min */ -1, /* max */ MAX_BACKGROUND_JOBS, 0);

MYSQL_SYSVAR_INT(max_background_flushes,
                 get_rocksdb_db_options().max_background_flushes,
                 PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                 "DBOptions::max_background_flushes for RocksDB", nullptr,
                 nullptr, get_rocksdb_db_options().max_background_flushes,
                 /* min */ -1, /* max */ 64, 0);

void set_max_background_compactions(THD *, SYS_VAR *, void *,
                                    const void *save) {
  const auto new_val = *static_cast<const int *>(save);

  rocksdb_set_max_background_compactions(new_val);
}

MYSQL_SYSVAR_INT(max_background_compactions,
                 get_rocksdb_db_options().max_background_compactions,
                 PLUGIN_VAR_RQCMDARG,
                 "DBOptions::max_background_compactions for RocksDB", nullptr,
                 set_max_background_compactions,
                 get_rocksdb_db_options().max_background_compactions,
                 /* min */ -1, /* max */ 64, 0);

constexpr int MAX_BOTTOM_PRI_BACKGROUND_COMPACTIONS = 64;

Rds_mysql_mutex bottom_pri_background_compactions_resize_mutex;

/**
   rocksdb_set_max_bottom_pri_background_compactions_internal() changes
   the number of rocksdb background threads.
   Creating new threads may take up to a few seconds, so instead of
   calling the function at sys_var::update path where global mutex is held,
   doing at sys_var::check path so that other queries are not blocked.
   Same optimization is done for rocksdb_block_cache_size too.
*/
int validate_max_bottom_pri_background_compactions(THD *, SYS_VAR *,
                                                   void *var_ptr,
                                                   st_mysql_value *value) {
  assert(value != nullptr);

  long long new_value_ll;

  /* value is nullptr */
  if (value->val_int(value, &new_value_ll)) {
    return HA_EXIT_FAILURE;
  }
  if (new_value_ll < 0 ||
      new_value_ll > MAX_BOTTOM_PRI_BACKGROUND_COMPACTIONS) {
    return HA_EXIT_FAILURE;
  }
  const auto new_value = static_cast<int>(new_value_ll);
  RDB_MUTEX_LOCK_CHECK(bottom_pri_background_compactions_resize_mutex);
  if (max_bottom_pri_background_compactions != new_value) {
    if (new_value == 0) {
      my_error(ER_ERROR_WHEN_EXECUTING_COMMAND, MYF(0), "SET",
               "max_bottom_pri_background_compactions can't be changed to 0 "
               "online.");
      RDB_MUTEX_UNLOCK_CHECK(bottom_pri_background_compactions_resize_mutex);
      return HA_EXIT_FAILURE;
    }
    rocksdb_set_max_bottom_pri_background_compactions_internal(new_value);
  }
  *static_cast<int *>(var_ptr) = new_value;
  RDB_MUTEX_UNLOCK_CHECK(bottom_pri_background_compactions_resize_mutex);
  return HA_EXIT_SUCCESS;
}

MYSQL_SYSVAR_INT(
    max_bottom_pri_background_compactions,
    max_bottom_pri_background_compactions, PLUGIN_VAR_RQCMDARG,
    "Creating specified number of threads, setting lower CPU priority, and "
    "letting Lmax compactions use them. Maximum total compaction concurrency "
    "continues to be capped to rocksdb_max_background_compactions or "
    "rocksdb_max_background_jobs. In addition to that, Lmax compaction "
    "concurrency is capped to rocksdb_max_bottom_pri_background_compactions. "
    "Default value is 0, which means all compactions are under concurrency of "
    "rocksdb_max_background_compactions|jobs. If you set very low "
    "rocksdb_max_bottom_pri_background_compactions (e.g. 1 or 2), compactions "
    "may not be able to keep up. Since Lmax normally has 90 percent of data, "
    "it is recommended to set closer number to "
    "rocksdb_max_background_compactions|jobs. This option is helpful to give "
    "more CPU resources to other threads (e.g. query processing).",
    validate_max_bottom_pri_background_compactions, nullptr, 0,
    /* min */ 0, /* max */ MAX_BOTTOM_PRI_BACKGROUND_COMPACTIONS, 0);

MYSQL_SYSVAR_UINT(max_subcompactions,
                  get_rocksdb_db_options().max_subcompactions,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::max_subcompactions for RocksDB", nullptr, nullptr,
                  get_rocksdb_db_options().max_subcompactions,
                  /* min */ 1, /* max */ MAX_SUBCOMPACTIONS, 0);

MYSQL_SYSVAR_ULONG(max_log_file_size,
                   get_rocksdb_db_options().max_log_file_size,
                   PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                   "DBOptions::max_log_file_size for RocksDB", nullptr, nullptr,
                   get_rocksdb_db_options().max_log_file_size,
                   /* min */ 0L, /* max */ LONG_MAX, 0);

MYSQL_SYSVAR_ULONG(log_file_time_to_roll,
                   get_rocksdb_db_options().log_file_time_to_roll,
                   PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                   "DBOptions::log_file_time_to_roll for RocksDB", nullptr,
                   nullptr, get_rocksdb_db_options().log_file_time_to_roll,
                   /* min */ 0L, /* max */ LONG_MAX, 0);

MYSQL_SYSVAR_ULONG(keep_log_file_num,
                   get_rocksdb_db_options().keep_log_file_num,
                   PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                   "DBOptions::keep_log_file_num for RocksDB", nullptr, nullptr,
                   get_rocksdb_db_options().keep_log_file_num,
                   /* min */ 0L, /* max */ LONG_MAX, 0);

MYSQL_SYSVAR_UINT64_T(max_manifest_file_size,
                      get_rocksdb_db_options().max_manifest_file_size,
                      PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                      "DBOptions::max_manifest_file_size for RocksDB", nullptr,
                      nullptr, get_rocksdb_db_options().max_manifest_file_size,
                      /* min */ 0, /* max */ UINT64_MAX, 0);

MYSQL_SYSVAR_INT(table_cache_numshardbits,
                 get_rocksdb_db_options().table_cache_numshardbits,
                 PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                 "DBOptions::table_cache_numshardbits for RocksDB", nullptr,
                 nullptr, get_rocksdb_db_options().table_cache_numshardbits,
                 // LRUCache limits this to 19 bits, anything greater
                 // fails to create a cache and returns a nullptr
                 /* min */ 0, /* max */ 19, 0);

MYSQL_SYSVAR_INT(block_cache_numshardbits, block_cache_numshardbits,
                 PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                 "Block cache numshardbits for RocksDB", nullptr, nullptr,
                 /* default */ -1, /* min */ -1, /* max */ 8, 0);

MYSQL_SYSVAR_UINT64_T(wal_ttl_seconds, get_rocksdb_db_options().WAL_ttl_seconds,
                      PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                      "DBOptions::WAL_ttl_seconds for RocksDB", nullptr,
                      nullptr, get_rocksdb_db_options().WAL_ttl_seconds,
                      /* min */ 0, /* max */ UINT64_MAX, 0);

MYSQL_SYSVAR_UINT64_T(wal_size_limit_mb,
                      get_rocksdb_db_options().WAL_size_limit_MB,
                      PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                      "DBOptions::WAL_size_limit_MB for RocksDB", nullptr,
                      nullptr, get_rocksdb_db_options().WAL_size_limit_MB,
                      /* min */ 0, /* max */ UINT64_MAX, 0);

MYSQL_SYSVAR_ULONG(manifest_preallocation_size,
                   get_rocksdb_db_options().manifest_preallocation_size,
                   PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                   "DBOptions::manifest_preallocation_size for RocksDB",
                   nullptr, nullptr,
                   get_rocksdb_db_options().manifest_preallocation_size,
                   /* min */ 0L, /* max */ LONG_MAX, 0);

MYSQL_SYSVAR_BOOL(use_direct_reads, get_rocksdb_db_options().use_direct_reads,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::use_direct_reads for RocksDB", nullptr, nullptr,
                  get_rocksdb_db_options().use_direct_reads);

MYSQL_SYSVAR_BOOL(
    use_direct_io_for_flush_and_compaction,
    get_rocksdb_db_options().use_direct_io_for_flush_and_compaction,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "DBOptions::use_direct_io_for_flush_and_compaction for RocksDB", nullptr,
    nullptr, get_rocksdb_db_options().use_direct_io_for_flush_and_compaction);

MYSQL_SYSVAR_BOOL(allow_mmap_reads, get_rocksdb_db_options().allow_mmap_reads,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::allow_mmap_reads for RocksDB", nullptr, nullptr,
                  get_rocksdb_db_options().allow_mmap_reads);

MYSQL_SYSVAR_BOOL(allow_mmap_writes, get_rocksdb_db_options().allow_mmap_writes,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::allow_mmap_writes for RocksDB", nullptr, nullptr,
                  get_rocksdb_db_options().allow_mmap_writes);

MYSQL_SYSVAR_BOOL(is_fd_close_on_exec,
                  get_rocksdb_db_options().is_fd_close_on_exec,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::is_fd_close_on_exec for RocksDB", nullptr,
                  nullptr, get_rocksdb_db_options().is_fd_close_on_exec);

MYSQL_SYSVAR_UINT(stats_dump_period_sec,
                  get_rocksdb_db_options().stats_dump_period_sec,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::stats_dump_period_sec for RocksDB", nullptr,
                  nullptr, get_rocksdb_db_options().stats_dump_period_sec,
                  /* min */ 0, /* max */ INT_MAX, 0);

MYSQL_SYSVAR_BOOL(advise_random_on_open,
                  get_rocksdb_db_options().advise_random_on_open,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::advise_random_on_open for RocksDB", nullptr,
                  nullptr, get_rocksdb_db_options().advise_random_on_open);

MYSQL_SYSVAR_ULONG(db_write_buffer_size,
                   get_rocksdb_db_options().db_write_buffer_size,
                   PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                   "DBOptions::db_write_buffer_size for RocksDB", nullptr,
                   nullptr, get_rocksdb_db_options().db_write_buffer_size,
                   /* min */ 0L, /* max */ LONG_MAX, 0);

MYSQL_SYSVAR_BOOL(use_adaptive_mutex,
                  get_rocksdb_db_options().use_adaptive_mutex,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::use_adaptive_mutex for RocksDB", nullptr, nullptr,
                  get_rocksdb_db_options().use_adaptive_mutex);

void set_bytes_per_sync(THD *, SYS_VAR *, void *, const void *save) {
  const auto new_val = *static_cast<const std::uint64_t *>(save);
  rocksdb_set_bytes_per_sync(new_val);
}

MYSQL_SYSVAR_UINT64_T(bytes_per_sync, get_rocksdb_db_options().bytes_per_sync,
                      PLUGIN_VAR_RQCMDARG,
                      "DBOptions::bytes_per_sync for RocksDB", nullptr,
                      set_bytes_per_sync,
                      get_rocksdb_db_options().bytes_per_sync,
                      /* min */ 0, /* max */ UINT64_MAX, 0);

void set_wal_bytes_per_sync(THD *, SYS_VAR *, void *, const void *save) {
  const auto new_val = *static_cast<const std::uint64_t *>(save);
  rocksdb_set_wal_bytes_per_sync(new_val);
}

MYSQL_SYSVAR_UINT64_T(wal_bytes_per_sync,
                      get_rocksdb_db_options().wal_bytes_per_sync,
                      PLUGIN_VAR_RQCMDARG,
                      "DBOptions::wal_bytes_per_sync for RocksDB", nullptr,
                      set_wal_bytes_per_sync,
                      get_rocksdb_db_options().wal_bytes_per_sync,
                      /* min */ 0, /* max */ UINT64_MAX, 0);

MYSQL_SYSVAR_BOOL(enable_thread_tracking,
                  get_rocksdb_db_options().enable_thread_tracking,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "DBOptions::enable_thread_tracking for RocksDB", nullptr,
                  nullptr, true);

constexpr std::int64_t RDB_DEFAULT_BLOCK_CACHE_SIZE = 512 * 1024 * 1024;
constexpr std::int64_t RDB_MIN_BLOCK_CACHE_SIZE = 1024;

Rds_mysql_mutex rdb_block_cache_resize_mutex;

/*
  Validating and updating block cache size via sys_var::check path.
  SetCapacity may take seconds when reducing block cache, and
  sys_var::update holds LOCK_global_system_variables mutex, so
  updating block cache size is done at check path instead.
*/
int validate_set_block_cache_size(THD *, SYS_VAR *, void *var_ptr,
                                  st_mysql_value *value) {
  assert(value != nullptr);

  long long new_value;

  /* value is nullptr */
  if (value->val_int(value, &new_value)) {
    return HA_EXIT_FAILURE;
  }

  if (new_value < RDB_MIN_BLOCK_CACHE_SIZE ||
      static_cast<std::uint64_t>(new_value) >
          static_cast<uint64_t>(LLONG_MAX)) {
    return HA_EXIT_FAILURE;
  }

  RDB_MUTEX_LOCK_CHECK(rdb_block_cache_resize_mutex);
  rocksdb_set_block_cache_size(new_value);
  *static_cast<int64_t *>(var_ptr) = static_cast<int64_t>(new_value);
  RDB_MUTEX_UNLOCK_CHECK(rdb_block_cache_resize_mutex);
  return HA_EXIT_SUCCESS;
}

MYSQL_SYSVAR_LONGLONG(block_cache_size, block_cache_size, PLUGIN_VAR_RQCMDARG,
                      "block_cache size for RocksDB",
                      validate_set_block_cache_size, nullptr,
                      /* default */ RDB_DEFAULT_BLOCK_CACHE_SIZE,
                      /* min */ RDB_MIN_BLOCK_CACHE_SIZE,
                      /* max */ LLONG_MAX,
                      /* Block size */ RDB_MIN_BLOCK_CACHE_SIZE);

MYSQL_SYSVAR_LONGLONG(sim_cache_size, sim_cache_size,
                      PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                      "Simulated cache size for RocksDB", nullptr, nullptr,
                      /* default */ 0,
                      /* min */ 0,
                      /* max */ LLONG_MAX,
                      /* Block size */ 0);

MYSQL_SYSVAR_BOOL(use_io_uring, use_io_uring, PLUGIN_VAR_RQCMDARG,
                  "Use io_uring for RocksDB", nullptr, nullptr, use_io_uring);

MYSQL_SYSVAR_BOOL(use_hyper_clock_cache, use_hyper_clock_cache,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "Use HyperClockCache instead of default LRUCache for RocksDB",
                  nullptr, nullptr, false);

MYSQL_SYSVAR_BOOL(cache_dump, cache_dump,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "Include RocksDB block cache content in core dump.", nullptr,
                  nullptr, true);

MYSQL_SYSVAR_DOUBLE(cache_high_pri_pool_ratio, cache_high_pri_pool_ratio,
                    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                    "Specify the size of block cache high-pri pool", nullptr,
                    nullptr, /* default */ 0.0, /* min */ 0.0,
                    /* max */ 1.0, 0);

MYSQL_SYSVAR_BOOL(
    cache_index_and_filter_blocks,
    rdb_get_table_options().cache_index_and_filter_blocks,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "BlockBasedTableOptions::cache_index_and_filter_blocks for RocksDB",
    nullptr, nullptr, true);

MYSQL_SYSVAR_BOOL(
    cache_index_and_filter_with_high_priority,
    rdb_get_table_options().cache_index_and_filter_blocks_with_high_priority,
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
MYSQL_SYSVAR_BOOL(
    pin_l0_filter_and_index_blocks_in_cache,
    rdb_get_table_options().pin_l0_filter_and_index_blocks_in_cache,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "pin_l0_filter_and_index_blocks_in_cache for RocksDB", nullptr, nullptr,
    true);

const char *index_type_names[] = {"kBinarySearch", "kHashSearch", NullS};

TYPELIB index_type_typelib = {array_elements(index_type_names) - 1,
                              "index_type_typelib", index_type_names, nullptr};

MYSQL_SYSVAR_ENUM(index_type, index_type,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "BlockBasedTableOptions::index_type for RocksDB", nullptr,
                  nullptr,
                  static_cast<ulong>(rdb_get_table_options().index_type),
                  &index_type_typelib);

MYSQL_SYSVAR_BOOL(no_block_cache, rdb_get_table_options().no_block_cache,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "BlockBasedTableOptions::no_block_cache for RocksDB", nullptr,
                  nullptr, rdb_get_table_options().no_block_cache);

MYSQL_SYSVAR_UINT64_T(block_size, rdb_get_table_options().block_size,
                      PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                      "BlockBasedTableOptions::block_size for RocksDB", nullptr,
                      nullptr, rdb_get_table_options().block_size,
                      /* min */ 1, /* max */ UINT64_MAX, 0);

MYSQL_SYSVAR_BOOL(
    charge_memory, charge_memory, PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "For experiment only. Turn on memory charging feature of RocksDB", nullptr,
    nullptr, false);

MYSQL_SYSVAR_BOOL(use_write_buffer_manager, use_write_buffer_manager,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "For experiment only. Use write buffer manager", nullptr,
                  nullptr, false);

MYSQL_SYSVAR_INT(block_size_deviation,
                 rdb_get_table_options().block_size_deviation,
                 PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                 "BlockBasedTableOptions::block_size_deviation for RocksDB",
                 nullptr, nullptr, rdb_get_table_options().block_size_deviation,
                 /* min */ 0, /* max */ INT_MAX, 0);

MYSQL_SYSVAR_INT(block_restart_interval,
                 rdb_get_table_options().block_restart_interval,
                 PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                 "BlockBasedTableOptions::block_restart_interval for RocksDB",
                 nullptr, nullptr,
                 rdb_get_table_options().block_restart_interval,
                 /* min */ 1, /* max */ INT_MAX, 0);

MYSQL_SYSVAR_BOOL(whole_key_filtering,
                  rdb_get_table_options().whole_key_filtering,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "BlockBasedTableOptions::whole_key_filtering for RocksDB",
                  nullptr, nullptr,
                  rdb_get_table_options().whole_key_filtering);

MYSQL_SYSVAR_STR(default_cf_options, default_cf_options,
                 PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                 "default cf options for RocksDB", nullptr, nullptr, "");

MYSQL_SYSVAR_STR(override_cf_options, override_cf_options,
                 PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                 "option overrides per cf for RocksDB", nullptr, nullptr, "");

int validate_update_cf_options(THD *, SYS_VAR *, void *save,
                               st_mysql_value *value) {
  int length = 0;
  const char *str = value->val_str(value, nullptr, &length);

  *static_cast<const char **>(save) = nullptr;

  if (str == nullptr) {
    return HA_EXIT_SUCCESS;
  }

  const auto ret = rocksdb_validate_update_cf_options(str);
  if (ret != HA_EXIT_SUCCESS) return ret;

  *static_cast<const char **>(save) = str;

  return HA_EXIT_SUCCESS;
}

void set_update_cf_options(THD *, SYS_VAR *, void *var_ptr, const void *save) {
  const auto *const val = *static_cast<const char *const *>(save);

  if (!val) {
    *reinterpret_cast<char **>(var_ptr) = nullptr;
    return;
  }

  assert(val != nullptr);

  // Reset the pointers regardless of how much success we had with updating
  // the CF options. This will results in consistent behavior and avoids
  // dealing with cases when only a subset of CF-s was successfully updated.
  *reinterpret_cast<const char **>(var_ptr) = val;

  rocksdb_set_update_cf_options(val);
  // Our caller (`plugin_var_memalloc_global_update`) will call `my_free` to
  // free up resources used before.
}

MYSQL_SYSVAR_STR(update_cf_options, rocksdb_update_cf_options,
                 PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
                 "Option updates per column family for RocksDB",
                 validate_update_cf_options, set_update_cf_options, nullptr);

MYSQL_SYSVAR_BOOL(use_default_sk_cf, use_default_sk_cf,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "Use default_sk for secondary keys", nullptr, nullptr, false);

int validate_flush_log_at_trx_commit(
    THD *, SYS_VAR *,
    /* out: immediate result for update function */
    void *var_ptr,
    /* in: incoming value */
    st_mysql_value *value) {
  long long new_value;

  /* value is NULL */
  if (value->val_int(value, &new_value)) {
    return HA_EXIT_FAILURE;
  }

  const auto ret = rocksdb_validate_flush_log_at_trx_commit(new_value);
  if (ret != HA_EXIT_SUCCESS) return ret;

  *static_cast<uint32_t *>(var_ptr) = static_cast<uint32_t>(new_value);
  return HA_EXIT_SUCCESS;
}

MYSQL_SYSVAR_UINT(
    flush_log_at_trx_commit, flush_log_at_trx_commit, PLUGIN_VAR_RQCMDARG,
    "Sync on transaction commit. Similar to innodb_flush_log_at_trx_commit. 1: "
    "sync on commit, 0,2: not sync on commit",
    validate_flush_log_at_trx_commit, nullptr,
    /* default */ FLUSH_LOG_SYNC,
    /* min */ FLUSH_LOG_NEVER,
    /* max */ FLUSH_LOG_BACKGROUND, 0);

MYSQL_THDVAR_BOOL(write_disable_wal, PLUGIN_VAR_RQCMDARG,
                  "WriteOptions::disableWAL for RocksDB", nullptr, nullptr,
                  rocksdb::WriteOptions().disableWAL);

MYSQL_THDVAR_BOOL(write_ignore_missing_column_families, PLUGIN_VAR_RQCMDARG,
                  "WriteOptions::ignore_missing_column_families for RocksDB",
                  nullptr, nullptr,
                  rocksdb::WriteOptions().ignore_missing_column_families);

int validate_protection_bytes_per_key(THD *, SYS_VAR *, void *var_ptr,
                                      st_mysql_value *value) {
  assert(value != nullptr);

  long long new_value;

  /* value is NULL */
  if (value->val_int(value, &new_value)) {
    return HA_EXIT_FAILURE;
  }

  if (new_value != 0 && new_value != 1 && new_value != 2 && new_value != 4 &&
      new_value != 8) {
    return HA_EXIT_FAILURE;
  }

  *static_cast<unsigned long *>(var_ptr) =
      static_cast<unsigned long>(new_value);

  return HA_EXIT_SUCCESS;
}

MYSQL_THDVAR_ULONG(protection_bytes_per_key, PLUGIN_VAR_RQCMDARG,
                   "WriteOptions::protection_bytes_per_key for RocksDB",
                   validate_protection_bytes_per_key, nullptr, 0 /* default */,
                   0 /* min */, ULONG_MAX /* max */, 0);

MYSQL_THDVAR_BOOL(skip_fill_cache, PLUGIN_VAR_RQCMDARG,
                  "Skip filling block cache on read requests", nullptr, nullptr,
                  false);

MYSQL_THDVAR_BOOL(
    unsafe_for_binlog, PLUGIN_VAR_RQCMDARG,
    "Allowing statement based binary logging which may break consistency",
    nullptr, nullptr, false);

MYSQL_THDVAR_UINT(records_in_range, PLUGIN_VAR_RQCMDARG,
                  "Used to override the result of records_in_range(). Set to a "
                  "positive number to override",
                  nullptr, nullptr, 0,
                  /* min */ 0, /* max */ INT_MAX, 0);

MYSQL_THDVAR_UINT(force_index_records_in_range, PLUGIN_VAR_RQCMDARG,
                  "Used to override the result of records_in_range() when "
                  "FORCE INDEX is used.",
                  nullptr, nullptr, 0,
                  /* min */ 0, /* max */ INT_MAX, 0);

MYSQL_SYSVAR_UINT(
    debug_optimizer_n_rows, debug_optimizer_n_rows,
    PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY | PLUGIN_VAR_NOSYSVAR,
    "Test only to override rocksdb estimates of table size in a memtable",
    nullptr, nullptr, 0, /* min */ 0, /* max */ INT_MAX, 0);

MYSQL_SYSVAR_BOOL(force_compute_memtable_stats, force_compute_memtable_stats,
                  PLUGIN_VAR_RQCMDARG, "Force to always compute memtable stats",
                  nullptr, nullptr, true);

MYSQL_SYSVAR_UINT(force_compute_memtable_stats_cachetime,
                  force_compute_memtable_stats_cachetime, PLUGIN_VAR_RQCMDARG,
                  "Time in usecs to cache memtable estimates", nullptr, nullptr,
                  /* default */ 60 * 1000 * 1000,
                  /* min */ 0, /* max */ INT_MAX, 0);

MYSQL_SYSVAR_BOOL(
    debug_optimizer_no_zero_cardinality, debug_optimizer_no_zero_cardinality,
    PLUGIN_VAR_RQCMDARG,
    "In case if cardinality is zero, overrides it with some value", nullptr,
    nullptr, true);

MYSQL_SYSVAR_UINT(debug_cardinality_multiplier, debug_cardinality_multiplier,
                  PLUGIN_VAR_RQCMDARG, "Cardinality multiplier used in tests",
                  nullptr, nullptr,
                  /* default */ 2,
                  /* min */ 0, /* max */ INT_MAX, 0);

char *compact_cf_name;

int compact_column_family(THD *thd, SYS_VAR *, void *, st_mysql_value *value) {
  char buff[STRING_BUFFER_USUAL_SIZE];
  int len = sizeof(buff);

  assert(value != nullptr);

  if (const char *const cf = value->val_str(value, buff, &len)) {
    return rocksdb_compact_column_family(thd, cf);
  }
  return HA_EXIT_SUCCESS;
}

MYSQL_SYSVAR_STR(compact_cf, compact_cf_name, PLUGIN_VAR_RQCMDARG,
                 "Compact column family", compact_column_family,
                 rw_sysvar_update_noop, "");

char *delete_cf_name;

int delete_column_family(THD *, SYS_VAR *, void *, st_mysql_value *value) {
  char buff[STRING_BUFFER_USUAL_SIZE];
  int len = sizeof(buff);

  const char *const cf = value->val_str(value, buff, &len);
  if (cf == nullptr) return HA_EXIT_SUCCESS;

  return rocksdb_delete_column_family(cf);
}

MYSQL_SYSVAR_STR(delete_cf, delete_cf_name, PLUGIN_VAR_RQCMDARG,
                 "Delete column family", delete_column_family,
                 rw_sysvar_update_noop, "");

char *checkpoint_name;

int create_checkpoint_validate(THD *, SYS_VAR *, void *,
                               st_mysql_value *value) {
  char buf[FN_REFLEN];
  int len = sizeof(buf);
  const auto *const checkpoint_dir_raw = value->val_str(value, buf, &len);
  if (checkpoint_dir_raw) {
    return rocksdb_create_checkpoint(checkpoint_dir_raw);
  }
  return HA_EXIT_FAILURE;
}

MYSQL_SYSVAR_STR(create_checkpoint, checkpoint_name, PLUGIN_VAR_RQCMDARG,
                 "Checkpoint directory", create_checkpoint_validate,
                 rw_sysvar_update_noop, "");

int create_temporary_checkpoint_validate(THD *thd, SYS_VAR *, void *save,
                                         st_mysql_value *value) {
  assert(thd != nullptr);

  char buf[FN_REFLEN];
  int len = sizeof(buf);
  const char *new_checkpoint_dir = value->val_str(value, buf, &len);

  const char *current_checkpoint_dir = create_temporary_checkpoint(thd);
  if (current_checkpoint_dir != nullptr && new_checkpoint_dir != nullptr) {
    *reinterpret_cast<const char **>(save) = current_checkpoint_dir;
    my_error(
        ER_GET_ERRMSG, MYF(0), HA_ERR_ROCKSDB_STATUS_INVALID_ARGUMENT,
        "Invalid argument: Temporary checkpoint already exists for session",
        hton_name);
    return HA_EXIT_FAILURE;
  } else if (new_checkpoint_dir != nullptr) {
    const auto res =
        rocksdb_create_temporary_checkpoint(thd, new_checkpoint_dir);
    if (res == HA_EXIT_SUCCESS) {
      *reinterpret_cast<const char **>(save) = new_checkpoint_dir;
    } else {
      return res;
    }
  } else if (current_checkpoint_dir != nullptr) {
    const auto res =
        rocksdb_remove_temporary_checkpoint(thd, current_checkpoint_dir);
    *reinterpret_cast<const char **>(save) = nullptr;
    if (res != HA_EXIT_SUCCESS) {
      return res;
    }
  } else {
    rocksdb_reset_temporary_checkpoint(thd);
    *reinterpret_cast<const char **>(save) = nullptr;
  }
  return HA_EXIT_SUCCESS;
}

MYSQL_THDVAR_STR(create_temporary_checkpoint,
                 PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC |
                     PLUGIN_VAR_NOCMDOPT,
                 "Temporary checkpoint directory",
                 create_temporary_checkpoint_validate, nullptr, nullptr);

bool rocksdb_signal_drop_index_thread;

void drop_index_wakeup_thread(my_core::THD *, struct SYS_VAR *, void *,
                              const void *save) {
  if (*static_cast<const bool *>(save)) {
    rocksdb_signal_drop_idx_thread();
  }
}

MYSQL_SYSVAR_BOOL(signal_drop_index_thread, rocksdb_signal_drop_index_thread,
                  PLUGIN_VAR_RQCMDARG, "Wake up drop index thread", nullptr,
                  drop_index_wakeup_thread, false);

void set_pause_background_work(THD *, SYS_VAR *, void *, const void *save) {
  const bool pause_requested = *static_cast<const bool *>(save);
  if (pause_background_work != pause_requested) {
    rocksdb_pause_or_continue_background_work(pause_requested);
    pause_background_work = pause_requested;
  }
}

MYSQL_SYSVAR_BOOL(pause_background_work, pause_background_work,
                  PLUGIN_VAR_RQCMDARG,
                  "Disable all rocksdb background operations", nullptr,
                  set_pause_background_work, false);

bool enable_ttl = true;

MYSQL_SYSVAR_BOOL(enable_ttl, enable_ttl, PLUGIN_VAR_RQCMDARG,
                  "Enable expired TTL records to be dropped during compaction.",
                  nullptr, nullptr, true);

bool enable_ttl_read_filtering = true;

MYSQL_SYSVAR_BOOL(
    enable_ttl_read_filtering, enable_ttl_read_filtering, PLUGIN_VAR_RQCMDARG,
    "For tables with TTL, expired records are skipped/filtered out during "
    "processing and in query results. Disabling this will allow these records "
    "to be seen, but as a result rows may disappear in the middle of "
    "transactions as they are dropped during compaction. Use with caution.",
    nullptr, nullptr, true);

int debug_ttl_rec_ts = 0;

MYSQL_SYSVAR_INT(
    debug_ttl_rec_ts, debug_ttl_rec_ts, PLUGIN_VAR_RQCMDARG,
    "For debugging purposes only.  Overrides the TTL of records to now() + "
    "debug_ttl_rec_ts.  The value can be +/- to simulate a record inserted in "
    "the past vs a record inserted in the 'future'. A value of 0 denotes that "
    "the variable is not set. This variable is a no-op in non-debug builds.",
    nullptr, nullptr, 0, /* min */ -3600, /* max */ 3600, 0);

int debug_ttl_snapshot_ts = 0;

MYSQL_SYSVAR_INT(
    debug_ttl_snapshot_ts, debug_ttl_snapshot_ts, PLUGIN_VAR_RQCMDARG,
    "For debugging purposes only.  Sets the snapshot during compaction to "
    "now() + debug_set_ttl_snapshot_ts.  The value can be +/- to simulate a "
    "snapshot in the past vs a snapshot created in the 'future'. A value of 0 "
    "denotes that the variable is not set. This variable is a no-op in "
    "non-debug builds.",
    nullptr, nullptr, 0, /* min */ -3600, /* max */ 3600, 0);

int debug_ttl_read_filter_ts = 0;

MYSQL_SYSVAR_INT(
    debug_ttl_read_filter_ts, debug_ttl_read_filter_ts, PLUGIN_VAR_RQCMDARG,
    "For debugging purposes only. Overrides the TTL read filtering time to "
    "time + debug_ttl_read_filter_ts. A value of 0 denotes that the variable "
    "is not set. This variable is a no-op in non-debug builds.",
    nullptr, nullptr, 0, /* min */ -3600, /* max */ 3600, 0);

bool debug_ttl_ignore_pk = false;

MYSQL_SYSVAR_BOOL(
    debug_ttl_ignore_pk, debug_ttl_ignore_pk, PLUGIN_VAR_RQCMDARG,
    "For debugging purposes only. If true, compaction filtering will not occur "
    "on PK TTL data. This variable is a no-op in non-debug builds.",
    nullptr, nullptr, false);

bool pause_ttl_compaction_filter = false;

MYSQL_SYSVAR_BOOL(
    pause_ttl_compaction_filter, pause_ttl_compaction_filter,
    PLUGIN_VAR_RQCMDARG,
    "Pauses TTL compaction filter. This means that as long as this variable is "
    "enabled, compactions will not delete expired rows.",
    nullptr, nullptr, false);

bool binlog_ttl = false;

MYSQL_SYSVAR_BOOL(binlog_ttl, binlog_ttl, PLUGIN_VAR_RQCMDARG,
                  "Sync TTL timestamps over replication", nullptr, nullptr,
                  false);

MYSQL_SYSVAR_UINT(binlog_ttl_compaction_ts_interval_secs,
                  binlog_ttl_compaction_ts_interval_secs, PLUGIN_VAR_RQCMDARG,
                  "Interval in seconds when compaction timestamp is written to "
                  "the binlog. Default: 1 hour",
                  nullptr, nullptr, 3600,
                  /* min */ 0,
                  /* max */ UINT_MAX, 0);

MYSQL_SYSVAR_UINT(binlog_ttl_compaction_ts_offset_secs,
                  binlog_ttl_compaction_ts_offset_secs, PLUGIN_VAR_RQCMDARG,
                  "Offset in seconds which is subtracted from the compaction "
                  "ts when it's written to the binlog. Default: 60s",
                  nullptr, nullptr, 60,
                  /* min */ 0,
                  /* max */ UINT_MAX, 0);

int rocksdb_debug_binlog_ttl_compaction_ts_delta = 0;

void rocksdb_debug_binlog_ttl_compaction_ts_delta_update(THD *, SYS_VAR *,
                                                         void *var_ptr
                                                         [[maybe_unused]],
                                                         const void *save
                                                         [[maybe_unused]]) {
#ifndef NDEBUG
  const auto val = *static_cast<int *>(var_ptr) =
      *static_cast<const int *>(save);
  rocksdb_binlog_ttl_compaction_timestamp += val;
#endif
}

MYSQL_SYSVAR_INT(debug_binlog_ttl_compaction_ts_delta,
                 rocksdb_debug_binlog_ttl_compaction_ts_delta,
                 PLUGIN_VAR_RQCMDARG,
                 "For debugging purposes only. Overrides the binlog TTL "
                 "compaction time by adding the delta provided in this var",
                 nullptr, rocksdb_debug_binlog_ttl_compaction_ts_delta_update,
                 0,
                 /* min */ INT_MIN,
                 /* max */ INT_MAX, 0);

MYSQL_SYSVAR_UINT(
    max_manual_compactions, rocksdb_max_manual_compactions, PLUGIN_VAR_RQCMDARG,
    "Maximum number of pending + ongoing number of manual compactions.",
    nullptr, nullptr, /* default */ 10, /* min */ 0, /* max */ UINT_MAX, 0);

MYSQL_SYSVAR_BOOL(
    rollback_on_timeout, rollback_on_timeout, PLUGIN_VAR_OPCMDARG,
    "Whether to roll back the complete transaction or a single statement on "
    "lock wait timeout (a single statement by default)",
    nullptr, nullptr, false);

MYSQL_SYSVAR_UINT(debug_manual_compaction_delay, debug_manual_compaction_delay,
                  PLUGIN_VAR_RQCMDARG,
                  "For debugging purposes only. Sleeping specified seconds for "
                  "simulating long running compactions.",
                  nullptr, nullptr, 0, /* min */ 0, /* max */ UINT_MAX, 0);

bool reset_stats = false;

void set_reset_stats(THD *, SYS_VAR *, void *var_ptr, const void *save) {
  *static_cast<bool *>(var_ptr) = *static_cast<const bool *>(save);

  if (reset_stats) {
    rocksdb_reset_stats();
  }
}

MYSQL_SYSVAR_BOOL(
    reset_stats, reset_stats, PLUGIN_VAR_RQCMDARG,
    "Reset the RocksDB internal statistics without restarting the DB.", nullptr,
    set_reset_stats, false);

void set_io_write_timeout(THD *, SYS_VAR *, void *, const void *save) {
  const auto new_val = *static_cast<const uint32_t *>(save);

  io_write_timeout_secs = new_val;
  rocksdb_set_io_write_timeout(new_val);
}

MYSQL_SYSVAR_UINT(io_write_timeout, io_write_timeout_secs, PLUGIN_VAR_RQCMDARG,
                  "Timeout for experimental I/O watchdog.", nullptr,
                  set_io_write_timeout, /* default */ 0,
                  /* min */ 0L,
                  /* max */ UINT_MAX, 0);

MYSQL_SYSVAR_BOOL(ignore_unknown_options, ignore_unknown_options,
                  PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_READONLY,
                  "Enable ignoring unknown options passed to RocksDB", nullptr,
                  nullptr, true);

MYSQL_SYSVAR_BOOL(collect_sst_properties, collect_sst_properties,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "Enables collecting SST file properties on each flush",
                  nullptr, nullptr, collect_sst_properties);

bool force_flush_memtable_now_var = false;

int force_flush_memtable_now(THD *, SYS_VAR *, void *, st_mysql_value *value) {
  bool parsed_value = false;
  if (mysql_value_to_bool(value, &parsed_value) != 0) {
    return 1;
  } else if (!parsed_value) {
    // Setting to OFF is a no-op and this supports mtr tests
    return HA_EXIT_SUCCESS;
  }

  // NO_LINT_DEBUG
  LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                  "RocksDB: Manual memtable flush.");
  rocksdb_flush_all_memtables();
  return HA_EXIT_SUCCESS;
}

MYSQL_SYSVAR_BOOL(
    force_flush_memtable_now, force_flush_memtable_now_var, PLUGIN_VAR_RQCMDARG,
    "Forces memstore flush which may block all write requests so be careful",
    force_flush_memtable_now, rw_sysvar_update_noop, false);

bool compact_lzero_now_var = false;

int compact_lzero_now(THD *, SYS_VAR *, void *, st_mysql_value *value) {
  bool parsed_value = false;

  if (mysql_value_to_bool(value, &parsed_value) != 0) {
    return 1;
  } else if (!parsed_value) {
    // Setting to OFF is a no-op and this supports mtr tests
    return HA_EXIT_SUCCESS;
  }

  return rocksdb_compact_lzero();
}

MYSQL_SYSVAR_BOOL(compact_lzero_now, compact_lzero_now_var, PLUGIN_VAR_RQCMDARG,
                  "Compacts all L0 files.", compact_lzero_now,
                  rw_sysvar_update_noop, false);

bool force_flush_memtable_and_lzero_now_var = false;

int force_flush_memtable_and_lzero_now(THD *, SYS_VAR *, void *,
                                       st_mysql_value *value) {
  bool parsed_value = false;

  if (mysql_value_to_bool(value, &parsed_value) != 0) {
    return 1;
  } else if (!parsed_value) {
    // Setting to OFF is a no-op and this supports mtr tests
    return HA_EXIT_SUCCESS;
  }

  // NO_LINT_DEBUG
  LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                  "RocksDB: Manual memtable and L0 flush.");
  rocksdb_flush_all_memtables();

  // Try to avoid https://github.com/facebook/mysql-5.6/issues/1200
  my_sleep(1000000);

  return rocksdb_compact_lzero();
}

MYSQL_SYSVAR_BOOL(
    force_flush_memtable_and_lzero_now, force_flush_memtable_and_lzero_now_var,
    PLUGIN_VAR_RQCMDARG,
    "Acts similar to force_flush_memtable_now, but also compacts all L0 files.",
    force_flush_memtable_and_lzero_now, rw_sysvar_update_noop, false);

bool cancel_manual_compactions_var = false;

int cancel_manual_compactions(THD *, SYS_VAR *, void *, st_mysql_value *) {
  rocksdb_cancel_manual_compactions();
  return HA_EXIT_SUCCESS;
}

MYSQL_SYSVAR_BOOL(cancel_manual_compactions, cancel_manual_compactions_var,
                  PLUGIN_VAR_RQCMDARG,
                  "Cancelling all ongoing manual compactions.",
                  cancel_manual_compactions, rw_sysvar_update_noop, false);

uint32_t seconds_between_stat_computes = 3600;

MYSQL_SYSVAR_UINT(seconds_between_stat_computes, seconds_between_stat_computes,
                  PLUGIN_VAR_RQCMDARG,
                  "Sets a number of seconds to wait between optimizer stats "
                  "recomputation. Only changed indexes will be refreshed.",
                  nullptr, nullptr, seconds_between_stat_computes,
                  /* min */ 0L, /* max */ UINT_MAX, 0);

void set_compaction_options(THD *, SYS_VAR *, void *var_ptr, const void *save) {
  if (var_ptr && save) {
    *static_cast<uint64_t *>(var_ptr) = *static_cast<const uint64_t *>(save);
  }
  rocksdb_set_compaction_options();
}

constexpr auto DEFAULT_COMPACTION_SEQUENTIAL_DELETES = 149999;
constexpr auto MAX_COMPACTION_SEQUENTIAL_DELETES = 2000000;

MYSQL_SYSVAR_UINT64_T(compaction_sequential_deletes,
                      compaction_sequential_deletes, PLUGIN_VAR_RQCMDARG,
                      "RocksDB will trigger compaction for the file if it has "
                      "more than this number sequential deletes per window",
                      nullptr, set_compaction_options,
                      DEFAULT_COMPACTION_SEQUENTIAL_DELETES,
                      /* min */ 0,
                      /* max */ MAX_COMPACTION_SEQUENTIAL_DELETES, 0);

constexpr auto DEFAULT_COMPACTION_SEQUENTIAL_DELETES_WINDOW = 150000;
constexpr auto MAX_COMPACTION_SEQUENTIAL_DELETES_WINDOW = 2000000;

MYSQL_SYSVAR_UINT64_T(
    compaction_sequential_deletes_window, compaction_sequential_deletes_window,
    PLUGIN_VAR_RQCMDARG,
    "Size of the window for counting rocksdb_compaction_sequential_deletes",
    nullptr, set_compaction_options,
    DEFAULT_COMPACTION_SEQUENTIAL_DELETES_WINDOW,
    /* min */ 0, /* max */ MAX_COMPACTION_SEQUENTIAL_DELETES_WINDOW, 0);

MYSQL_SYSVAR_LONGLONG(
    compaction_sequential_deletes_file_size,
    compaction_sequential_deletes_file_size, PLUGIN_VAR_RQCMDARG,
    "Minimum file size required for compaction_sequential_deletes", nullptr,
    set_compaction_options, 0L,
    /* min */ -1LL, /* max */ LLONG_MAX, 0);

MYSQL_SYSVAR_BOOL(
    compaction_sequential_deletes_count_sd,
    compaction_sequential_deletes_count_sd, PLUGIN_VAR_RQCMDARG,
    "Counting SingleDelete as rocksdb_compaction_sequential_deletes", nullptr,
    nullptr, compaction_sequential_deletes_count_sd);

MYSQL_SYSVAR_BOOL(
    print_snapshot_conflict_queries, print_snapshot_conflict_queries,
    PLUGIN_VAR_RQCMDARG,
    "Logging queries that got snapshot conflict errors into *.err log", nullptr,
    nullptr, print_snapshot_conflict_queries);

constexpr int RDB_MAX_CHECKSUMS_PCT = 100;

MYSQL_THDVAR_INT(checksums_pct, PLUGIN_VAR_RQCMDARG,
                 "How many percentages of rows to be checksummed", nullptr,
                 nullptr, RDB_MAX_CHECKSUMS_PCT,
                 /* min */ 0, /* max */ RDB_MAX_CHECKSUMS_PCT, 0);

MYSQL_THDVAR_BOOL(store_row_debug_checksums, PLUGIN_VAR_RQCMDARG,
                  "Include checksums when writing index/table records", nullptr,
                  nullptr, false /* default value */);

MYSQL_THDVAR_BOOL(verify_row_debug_checksums, PLUGIN_VAR_RQCMDARG,
                  "Verify checksums when reading index/table records", nullptr,
                  nullptr, false /* default value */);

MYSQL_THDVAR_BOOL(master_skip_tx_api, PLUGIN_VAR_RQCMDARG,
                  "Skipping holding any lock on row access. Not effective on "
                  "slave.",
                  nullptr, nullptr, false);

MYSQL_SYSVAR_UINT(
    validate_tables, validate_tables, PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
    "Verify all DD tables match all RocksDB tables (0 means no verification, "
    "1 means verify and fail on error, and 2 means verify but continue",
    nullptr, nullptr, 1 /* default value */, 0 /* min value */,
    2 /* max value */, 0);

void set_table_stats_sampling_pct(THD *, SYS_VAR *, void *, const void *save) {
  const auto new_val = *static_cast<const uint32_t *>(save);

  if (new_val != table_stats_sampling_pct) {
    table_stats_sampling_pct = new_val;
    rocksdb_set_table_stats_sampling_pct(new_val);
  }
}

MYSQL_SYSVAR_UINT(
    table_stats_sampling_pct, table_stats_sampling_pct, PLUGIN_VAR_RQCMDARG,
    "Percentage of entries to sample when collecting statistics about table "
    "properties. Specify either 0 to sample everything or percentage "
    "[" STRINGIFY_ARG(RDB_TBL_STATS_SAMPLE_PCT_MIN) ".." STRINGIFY_ARG(
        RDB_TBL_STATS_SAMPLE_PCT_MAX) "]. By default "
    STRINGIFY_ARG(RDB_DEFAULT_TBL_STATS_SAMPLE_PCT) "% of entries are sampled.",
    nullptr, set_table_stats_sampling_pct, /* default */
    RDB_DEFAULT_TBL_STATS_SAMPLE_PCT, /* everything */ 0,
    /* max */ RDB_TBL_STATS_SAMPLE_PCT_MAX, 0);

constexpr uint RDB_TBL_STATS_RECALC_THRESHOLD_PCT_MAX = 100U;

MYSQL_SYSVAR_UINT(table_stats_recalc_threshold_pct,
                  table_stats_recalc_threshold_pct, PLUGIN_VAR_RQCMDARG,
                  "Percentage of number of modified rows over total number of "
                  "rows to trigger stats recalculation",
                  nullptr, nullptr, /* default */
                  table_stats_recalc_threshold_pct,
                  /* everything */ 0,
                  /* max */ RDB_TBL_STATS_RECALC_THRESHOLD_PCT_MAX, 0);

MYSQL_SYSVAR_ULONGLONG(table_stats_recalc_threshold_count,
                       table_stats_recalc_threshold_count, PLUGIN_VAR_RQCMDARG,
                       "Number of modified rows to trigger stats recalculation",
                       nullptr, nullptr, /* default */
                       table_stats_recalc_threshold_count,
                       /* everything */ 0,
                       /* max */ UINT64_MAX, 0);

int index_stats_thread_renice(THD *, SYS_VAR *, void *save,
                              st_mysql_value *value) {
#ifndef __APPLE__
  long long nice_val;
  /* value is NULL */
  if (value->val_int(value, &nice_val)) {
    return HA_EXIT_FAILURE;
  }

  if (rocksdb_index_stats_thread_renice(nice_val) != HA_EXIT_SUCCESS) {
    return HA_EXIT_FAILURE;
  }

  *static_cast<int32_t *>(save) = static_cast<int32_t>(nice_val);
  return HA_EXIT_SUCCESS;
#else   // ! __APPLE__
  (void)save;
  (void)value;
  assert(0);
  return HA_EXIT_SUCCESS;
#endif  // ! __APPLE__
}

MYSQL_SYSVAR_INT(table_stats_background_thread_nice_value,
                 table_stats_background_thread_nice_value,
                 PLUGIN_VAR_RQCMDARG
#ifdef __APPLE__
                     | PLUGIN_VAR_READONLY
#endif
                 ,
                 "nice value for index stats", index_stats_thread_renice,
                 nullptr,
                 /* default */ table_stats_background_thread_nice_value,
                 /* min */ THREAD_PRIO_MIN, /* max */ THREAD_PRIO_MAX, 0);

MYSQL_SYSVAR_ULONGLONG(table_stats_max_num_rows_scanned,
                       table_stats_max_num_rows_scanned, PLUGIN_VAR_RQCMDARG,
                       "The maximum number of rows to scan in table scan based "
                       "cardinality calculation",
                       nullptr, nullptr, /* default */
                       0, /* everything */ 0,
                       /* max */ UINT64_MAX, 0);

MYSQL_SYSVAR_UINT(stats_recalc_rate, stats_recalc_rate, PLUGIN_VAR_RQCMDARG,
                  "The number of indexes per second to recalculate statistics "
                  "for. 0 to disable background recalculation.",
                  nullptr, nullptr, 0 /* default value */, 0 /* min value */,
                  UINT_MAX /* max value */, 0);

void rocksdb_update_table_stats_use_table_scan(THD *, SYS_VAR *, void *var_ptr,
                                               const void *save) {
  const auto old_val = *static_cast<const bool *>(var_ptr);
  const auto new_val = *static_cast<const bool *>(save);

  if (old_val == new_val) {
    return;
  }

  myrocks::rocksdb_update_table_stats_use_table_scan(new_val);

  *static_cast<bool *>(var_ptr) = *static_cast<const bool *>(save);
}

bool rocksdb_table_stats_use_table_scan = false;

MYSQL_SYSVAR_BOOL(table_stats_use_table_scan,
                  rocksdb_table_stats_use_table_scan, PLUGIN_VAR_RQCMDARG,
                  "Enable table scan based index calculation.", nullptr,
                  rocksdb_update_table_stats_use_table_scan,
                  rocksdb_table_stats_use_table_scan);

void update_table_stats_skip_system_cf(THD *, SYS_VAR *, void *var_ptr,
                                       const void *const save) {
  const auto old_val = *static_cast<const bool *>(var_ptr);
  const auto new_val = *static_cast<const bool *>(save);

  if (old_val == new_val) {
    return;
  }

  myrocks::rocksdb_update_table_stats_skip_system_cf(new_val);

  *static_cast<bool *>(var_ptr) = new_val;
}

MYSQL_SYSVAR_BOOL(table_stats_skip_system_cf, table_stats_skip_system_cf,
                  PLUGIN_VAR_RQCMDARG,
                  "skip recording table stats for system column family",
                  nullptr, update_table_stats_skip_system_cf,
                  table_stats_skip_system_cf);

MYSQL_SYSVAR_BOOL(allow_to_start_after_corruption,
                  allow_to_start_after_corruption,
                  PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_READONLY,
                  "Allow server still to start successfully even if RocksDB "
                  "corruption is detected.",
                  nullptr, nullptr, false);

MYSQL_SYSVAR_BOOL(
    enable_insert_with_update_caching, enable_insert_with_update_caching,
    PLUGIN_VAR_OPCMDARG,
    "Whether to enable optimization where we cache the read from a failed "
    "insertion attempt in INSERT ON DUPLICATE KEY UPDATE",
    nullptr, nullptr, true);

char *block_cache_trace_options_str;

int trace_block_cache_access(THD *, SYS_VAR *, void *const save,
                             st_mysql_value *const value) {
  return rocksdb_tracing(save, value, /* trace_block_cache_accecss = */ true);
}

/* This method is needed to indicate that the ROCKSDB_TRACE_BLOCK_CACHE_ACCESS
or ROCKSDB_TRACE_QUERIES command is not read-only */
void trace_stub(THD *, SYS_VAR *, void *var_ptr, const void *save) {
  const auto *trace_opt_str_raw = *static_cast<const char *const *>(save);
  assert(trace_opt_str_raw != nullptr);
  *static_cast<const char **>(var_ptr) = trace_opt_str_raw;
}

MYSQL_SYSVAR_STR(trace_block_cache_access, block_cache_trace_options_str,
                 PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
                 "Block cache trace option string. The format is "
                 "sampling_frequency:max_trace_file_size:trace_file_name. "
                 "sampling_frequency and max_trace_file_size are positive "
                 "integers. The block accesses are saved to the "
                 "rocksdb_datadir/block_cache_traces/trace_file_name.",
                 trace_block_cache_access, trace_stub, "");

char *trace_options_str;

int trace_queries(THD *, SYS_VAR *, void *save, st_mysql_value *value) {
  return rocksdb_tracing(save, value, /* trace_block_cache_accecss = */ false);
}

MYSQL_SYSVAR_STR(
    trace_queries, trace_options_str, PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_MEMALLOC,
    "Trace option string. The format is "
    "sampling_frequency:max_trace_file_size:trace_file_name. "
    "sampling_frequency and max_trace_file_size are positive integers. The "
    "queries are saved to the rocksdb_datadir/queries_traces/trace_file_name.",
    trace_queries, trace_stub, "");

void rocksdb_max_compaction_history_update(THD *, SYS_VAR *, void *var_ptr,
                                           const void *save) {
  const auto val = *static_cast<uint64_t *>(var_ptr) =
      *static_cast<const uint64_t *>(save);
  myrocks::rocksdb_max_compaction_history_update(val);
}

constexpr ulonglong DEFAULT_MAX_COMPACTION_HISTORY = 64ULL;

MYSQL_SYSVAR_ULONGLONG(
    max_compaction_history, max_compaction_history, PLUGIN_VAR_OPCMDARG,
    "Track history for at most this many completed compactions. The history is "
    "in the INFORMATION_SCHEMA.ROCKSDB_COMPACTION_HISTORY table.",
    nullptr, rocksdb_max_compaction_history_update,
    DEFAULT_MAX_COMPACTION_HISTORY, 0ULL /* min */, UINT64_MAX /* max */,
    0 /* blk */);

void disable_file_deletions_update(THD *thd, SYS_VAR *, void *const var_ptr,
                                   const void *const save) {
  const auto new_val = *static_cast<bool *>(var_ptr) =
      *static_cast<const bool *>(save);
  myrocks::rocksdb_disable_file_deletions_update(thd, new_val);
}

MYSQL_THDVAR_BOOL(disable_file_deletions,
                  PLUGIN_VAR_NOCMDARG | PLUGIN_VAR_RQCMDARG,
                  "Prevent file deletions", nullptr,
                  disable_file_deletions_update, false);

/* This enum needs to be kept up to date with myrocks::select_bypass_policy_type
 */
const char *select_bypass_policy_names[] = {"always_off", "always_on", "opt_in",
                                            "opt_out", NullS};

TYPELIB select_bypass_policy_typelib = {
    array_elements(select_bypass_policy_names) - 1,
    "select_bypass_policy_typelib", select_bypass_policy_names, nullptr};

ulong select_bypass_policy = select_bypass_policy_type::default_value;

MYSQL_SYSVAR_ENUM(
    select_bypass_policy, select_bypass_policy, PLUGIN_VAR_RQCMDARG,
    "Change bypass SELECT related policy and allow directly talk to RocksDB. "
    "Valid values include 'always_off', 'always_on', 'opt_in', 'opt_out'. ",
    nullptr, nullptr, select_bypass_policy_type::default_value,
    &select_bypass_policy_typelib);

bool select_bypass_fail_unsupported = true;

MYSQL_SYSVAR_BOOL(select_bypass_fail_unsupported,
                  select_bypass_fail_unsupported, PLUGIN_VAR_RQCMDARG,
                  "Select bypass would fail for unsupported SELECT commands",
                  nullptr, nullptr, true);

bool select_bypass_log_rejected = true;

MYSQL_SYSVAR_BOOL(select_bypass_log_rejected, select_bypass_log_rejected,
                  PLUGIN_VAR_RQCMDARG, "Log rejected SELECT bypass queries",
                  nullptr, nullptr, true);

bool select_bypass_log_failed = false;

MYSQL_SYSVAR_BOOL(select_bypass_log_failed, select_bypass_log_failed,
                  PLUGIN_VAR_RQCMDARG, "Log failed SELECT bypass queries",
                  nullptr, nullptr, false);

bool select_bypass_allow_filters = true;

MYSQL_SYSVAR_BOOL(select_bypass_allow_filters, select_bypass_allow_filters,
                  PLUGIN_VAR_RQCMDARG,
                  "Allow non-optimal filters in SELECT bypass queries", nullptr,
                  nullptr, true);

uint32_t select_bypass_rejected_query_history_size = 0;

void select_bypass_rejected_query_history_size_update(THD *, SYS_VAR *,
                                                      void *var_ptr,
                                                      const void *save) {
  const auto val = *static_cast<uint32_t *>(var_ptr) =
      *static_cast<const uint32_t *>(save);

  rocksdb_select_bypass_rejected_query_history_size_update(val);
}

MYSQL_SYSVAR_UINT(
    select_bypass_rejected_query_history_size,
    select_bypass_rejected_query_history_size, PLUGIN_VAR_RQCMDARG,
    "History size of rejected bypass queries in "
    "information_schema.bypass_rejected_query_history. Set to 0 to turn off",
    nullptr, select_bypass_rejected_query_history_size_update, 0,
    /* min */ 0, /* max */ INT_MAX, 0);

uint32_t select_bypass_debug_row_delay = 0;

MYSQL_SYSVAR_UINT(select_bypass_debug_row_delay, select_bypass_debug_row_delay,
                  PLUGIN_VAR_RQCMDARG,
                  "Test only to inject delays in bypass select to simulate "
                  "long queries for each row sent",
                  nullptr, nullptr, 0, /* min */ 0, /* max */ INT_MAX, 0);

unsigned long long select_bypass_multiget_min = 0;  // NOLINT(runtime/int)

MYSQL_SYSVAR_ULONGLONG(select_bypass_multiget_min, select_bypass_multiget_min,
                       PLUGIN_VAR_RQCMDARG,
                       "Minimum number of items to use RocksDB MultiGet API. "
                       "Default is SIZE_T_MAX meaning it is turned off. Set to "
                       "0 to enable always using MultiGet",
                       nullptr, nullptr, SIZE_T_MAX, /* min */ 0,
                       /* max */ SIZE_T_MAX, 0);

MYSQL_SYSVAR_UINT(bypass_rpc_rejected_log_ts_interval_secs,
                  bypass_rpc_rejected_log_ts_interval_secs, PLUGIN_VAR_RQCMDARG,
                  "Interval in seconds when rejected Bypass RPC is "
                  "written to the query history. Default: 1 second",
                  nullptr, nullptr, 1,
                  /* min */ 0,
                  /* max */ UINT_MAX, 0);

bool bypass_rpc_on = true;

MYSQL_SYSVAR_BOOL(bypass_rpc_on, bypass_rpc_on, PLUGIN_VAR_RQCMDARG,
                  "Toggle Bypass RPC feature", nullptr, nullptr, true);

bool bypass_rpc_log_rejected = false;

MYSQL_SYSVAR_BOOL(bypass_rpc_log_rejected, bypass_rpc_log_rejected,
                  PLUGIN_VAR_RQCMDARG, "Log rejected Bypass RPC queries",
                  nullptr, nullptr, false);

constexpr ulong MAX_MRR_BATCH_SIZE = 1000UL;

MYSQL_THDVAR_ULONG(mrr_batch_size, PLUGIN_VAR_RQCMDARG,
                   "maximum number of keys to fetch during each MRR", nullptr,
                   nullptr, /* default */ 100, /* min */ 0,
                   /* max */ MAX_MRR_BATCH_SIZE, 0);

MYSQL_SYSVAR_BOOL(skip_locks_if_skip_unique_check,
                  skip_locks_if_skip_unique_check, PLUGIN_VAR_RQCMDARG,
                  "Skip row locking when unique checks are disabled.", nullptr,
                  nullptr, false);

MYSQL_SYSVAR_BOOL(alter_column_default_inplace, alter_column_default_inplace,
                  PLUGIN_VAR_RQCMDARG,
                  "Allow inplace alter for alter column default operation",
                  nullptr, nullptr, true);

MYSQL_THDVAR_ULONGLONG(
    partial_index_sort_max_mem, PLUGIN_VAR_RQCMDARG,
    "Maximum memory to use when sorting an unmaterialized group for partial "
    "indexes. 0 means no limit.",
    nullptr, nullptr,
    /* default */ 0, /* min */ 0, /* max */ SIZE_T_MAX, 1);

MYSQL_SYSVAR_BOOL(
    partial_index_blind_delete, partial_index_blind_delete, PLUGIN_VAR_RQCMDARG,
    "If OFF, always read from partial index to check if key exists before "
    "deleting. Otherwise, delete marker is unconditionally written.",
    nullptr, nullptr, true);

MYSQL_SYSVAR_BOOL(
    partial_index_ignore_killed, partial_index_ignore_killed,
    PLUGIN_VAR_RQCMDARG,
    "If ON, partial index materialization will ignore the killed flag and "
    "continue materialization until completion. If queries are killed during "
    "materialization due to timeout, then the work done so far is wasted, and "
    "it is likely that killed query will be retried later, hitting the same "
    "problem.",
    nullptr, nullptr, true);

MYSQL_SYSVAR_BOOL(
    disable_instant_ddl, disable_instant_ddl, PLUGIN_VAR_RQCMDARG,
    "Disable instant ddl during alter table, This variable is deprecated",
    nullptr, nullptr, false);

MYSQL_SYSVAR_BOOL(enable_instant_ddl, enable_instant_ddl, PLUGIN_VAR_RQCMDARG,
                  "Enable instant ddl during alter table if possible. If "
                  "false, no DDL can be executed as instant",
                  nullptr, nullptr, true);

MYSQL_SYSVAR_BOOL(enable_instant_ddl_for_append_column,
                  enable_instant_ddl_for_append_column, PLUGIN_VAR_RQCMDARG,
                  "Enable instant ddl for append column during alter table",
                  nullptr, nullptr, false);

MYSQL_SYSVAR_BOOL(enable_instant_ddl_for_column_default_changes,
                  enable_instant_ddl_for_column_default_changes,
                  PLUGIN_VAR_RQCMDARG,
                  "Enable instant ddl for column default during alter table",
                  nullptr, nullptr, false);

MYSQL_SYSVAR_BOOL(
    enable_instant_ddl_for_table_comment_changes,
    enable_instant_ddl_for_table_comment_changes, PLUGIN_VAR_RQCMDARG,
    "Enable instant ddl for table comment changes during alter table", nullptr,
    nullptr, false);

MYSQL_SYSVAR_BOOL(
    enable_instant_ddl_for_drop_index_changes,
    enable_instant_ddl_for_drop_index_changes, PLUGIN_VAR_RQCMDARG,
    "Enable instant ddl for index drop changes during alter table", nullptr,
    nullptr, false);

MYSQL_SYSVAR_BOOL(
    enable_instant_ddl_for_update_index_visibility,
    enable_instant_ddl_for_update_index_visibility, PLUGIN_VAR_RQCMDARG,
    "Enable instant ddl for updating index visibility during alter table",
    nullptr, nullptr, false);

MYSQL_SYSVAR_BOOL(enable_tmp_table, enable_tmp_table,
                  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
                  "Allow rocksdb tmp tables", nullptr, nullptr, false);

MYSQL_SYSVAR_BOOL(enable_delete_range_for_drop_index,
                  enable_delete_range_for_drop_index, PLUGIN_VAR_RQCMDARG,
                  "Enable drop table/index by delete range", nullptr, nullptr,
                  false);

MYSQL_SYSVAR_BOOL(alter_table_comment_inplace, alter_table_comment_inplace,
                  PLUGIN_VAR_RQCMDARG,
                  "Allow inplace alter for alter table comment", nullptr,
                  nullptr, false);

MYSQL_SYSVAR_BOOL(column_default_value_as_expression,
                  column_default_value_as_expression, PLUGIN_VAR_RQCMDARG,
                  "allow column default value expressed in function", nullptr,
                  nullptr, true);

MYSQL_SYSVAR_UINT(max_intrinsic_tmp_table_write_count,
                  max_intrinsic_tmp_table_write_count, PLUGIN_VAR_RQCMDARG,
                  "Intrinsic tmp table max allowed write batch size. After "
                  "this, current transaction holding write batch will commit "
                  "and new transaction will be started.",
                  nullptr, nullptr, /* default */ 1000, /* min */ 1,
                  /* max */ UINT_MAX, 0);

MYSQL_SYSVAR_UINT(clone_checkpoint_max_age, clone_checkpoint_max_age,
                  PLUGIN_VAR_RQCMDARG,
                  "Maximum checkpoint age in seconds during clone operations. "
                  "The checkpoint will be rolled if it becomes older, unless "
                  "rocksdb_clone_checkpoint_max_count would be violated. If 0, "
                  "the age is unlimited.",
                  nullptr, nullptr, 10 * 60, 0, UINT_MAX, 0);

MYSQL_SYSVAR_UINT(clone_checkpoint_max_count, clone_checkpoint_max_count,
                  PLUGIN_VAR_RQCMDARG,
                  "Maximum number of rolled checkpoints during a single clone "
                  "operation. If 0, the number is unlimited.",
                  nullptr, nullptr, 90, 0, UINT_MAX, 0);

MYSQL_SYSVAR_ULONGLONG(converter_record_cached_length,
                       converter_record_cached_length, PLUGIN_VAR_RQCMDARG,
                       "Maximum number of bytes to cache on table handler for "
                       "encoding table record data. 0 means no limit.",
                       nullptr, nullptr,
                       /* default */ converter_record_cached_length,
                       /* min */ 0, /* max */ UINT64_MAX, 0);

/* This array needs to be kept up to date with
 * myrocks::rocksdb_file_checksum_type */
const char *file_checksums_names[] = {
    "CHECKSUMS_OFF", "CHECKSUMS_WRITE_ONLY", "CHECKSUMS_WRITE_AND_VERIFY",
    "CHECKSUMS_WRITE_AND_VERIFY_ON_CLONE", NullS};

TYPELIB file_checksums_typelib = {array_elements(file_checksums_names) - 1,
                                  "file_checksums_typelib",
                                  file_checksums_names, nullptr};

MYSQL_SYSVAR_ENUM(
    file_checksums, file_checksums, PLUGIN_VAR_OPCMDARG | PLUGIN_VAR_READONLY,
    "Whether to write and check RocksDB file-level checksums. CHECKSUMS_OFF: "
    "nothing, CHECKSUMS_WRITE_ONLY: write checksums but skips verification, "
    "CHECKSUMS_WRITE_AND_VERIFY: write checksums, and verify on DB::open",
    nullptr, nullptr, file_checksums_type::CHECKSUMS_OFF,
    &file_checksums_typelib);

MYSQL_SYSVAR_BOOL(
    debug_skip_bloom_filter_check_on_iterator_bounds,
    debug_skip_bloom_filter_check_on_iterator_bounds, PLUGIN_VAR_RQCMDARG,
    "Allow iterator bounds to be set up for rocksdb even when the query range "
    "conditions would otherwise allow bloom filters to be used.",
    nullptr, nullptr, false);

void set_max_trash_db_ratio_pct(THD *, SYS_VAR *, void *, const void *save) {
  const auto new_val = *static_cast<const unsigned long long *>(save);

  if (new_val != max_trash_db_ratio_pct) {
    max_trash_db_ratio_pct = new_val;

    rocksdb_set_max_trash_db_ratio(static_cast<double>(max_trash_db_ratio_pct) /
                                   100);
  }
}

constexpr unsigned long long DEFAULT_MAX_TRASH_DB_RATIO_PCT = 100000000000;

MYSQL_SYSVAR_ULONGLONG(
    max_trash_db_ratio_pct, max_trash_db_ratio_pct, PLUGIN_VAR_RQCMDARG,
    "Specify max_trash_db_ratio of the SstFileManager in percent", nullptr,
    set_max_trash_db_ratio_pct,
    /* default */ DEFAULT_MAX_TRASH_DB_RATIO_PCT,
    /* min */ 0ULL,
    /* max */ ULLONG_MAX, 0);

MYSQL_THDVAR_BOOL(
    enable_autoinc_compat_mode, PLUGIN_VAR_RQCMDARG,
    "if enabled, allow simple inserts generate consecutive autoinc values, "
    "similar to behavior as innodb_autoinc_lock_mode = 2",
    nullptr, nullptr, false);

void set_bulk_load_history_size(THD *, SYS_VAR *, void *, const void *save) {
  const auto new_val = *static_cast<const uint32_t *>(save);
  bulk_load_history_size = new_val;
  myrocks::rdb_set_bulk_load_history_size(new_val);
}

MYSQL_SYSVAR_UINT(bulk_load_history_size, bulk_load_history_size,
                  PLUGIN_VAR_RQCMDARG,
                  "the number of completed bulk load sessions to be kept",
                  nullptr, set_bulk_load_history_size,
                  BULK_LOAD_HISTORY_DEFAULT_SIZE /* default value */,
                  0 /* min value */, 1024 /* max value */, 0);

MYSQL_SYSVAR_ULONGLONG(write_batch_mem_free_threshold,
                       write_batch_mem_free_threshold, PLUGIN_VAR_RQCMDARG,
                       "Destructs the writebatch if the memory allocated is "
                       "above the size in bytes. 0 means no limit.",
                       nullptr, nullptr,
                       /* default */ write_batch_mem_free_threshold,
                       /* min */ 0, /* max */ SIZE_T_MAX, 0);

MYSQL_THDVAR_ULONGLONG(
    consistent_snapshot_ttl_read_filtering_ts_nsec, PLUGIN_VAR_RQCMDARG,
    "User specified read filtering time for consistent snapshots", nullptr,
    nullptr, /* default (0ns) */ 0, /* min (0ns) */ 0, /* max */ SIZE_T_MAX, 1);

}  // namespace

bool enable_bulk_load_api = true;
bool enable_remove_orphaned_dropped_cfs = true;
bool enable_udt_in_mem = false;
ulong read_free_rpl = read_free_rpl_type::OFF;
ulong corrupt_data_action = corrupt_data_action_type::ERROR;
ulong invalid_create_option_action = invalid_create_option_action_type::LOG;
ulong io_error_action = static_cast<ulong>(io_error_action_type::ABORT_SERVER);
ulong write_policy = rocksdb::TxnDBWritePolicy::WRITE_COMMITTED;
/* Use unsigned long long instead of uint64_t because of MySQL compatibility */
unsigned long long rate_limiter_bytes_per_sec;  // NOLINT(runtime/int)
unsigned long long sst_mgr_rate_bytes_per_sec;  // NOLINT(runtime/int)
std::uint32_t max_latest_deadlocks;
ulong info_log_level;
std::uint32_t wal_recovery_mode;
bool track_and_verify_wals_in_manifest;
std::uint32_t rocksdb_stats_level;
char *wal_dir;
char *rocksdb_datadir;
char *persistent_cache_path;
unsigned long persistent_cache_size_mb;  // NOLINT(runtime/int)
char *wsenv_path;
char *wsenv_tenant;
char *wsenv_oncall;
int max_bottom_pri_background_compactions = 0;
int block_cache_numshardbits = -1;
long long block_cache_size;
long long sim_cache_size;
bool use_io_uring;
bool use_hyper_clock_cache;
bool cache_dump;
double cache_high_pri_pool_ratio;
ulong index_type;
bool charge_memory;
bool use_write_buffer_manager;
char *default_cf_options = nullptr;
char *override_cf_options = nullptr;
char *rocksdb_update_cf_options = nullptr;
bool use_default_sk_cf = false;
uint32_t flush_log_at_trx_commit;
uint32_t debug_optimizer_n_rows;
bool force_compute_memtable_stats;
uint32_t force_compute_memtable_stats_cachetime;
bool debug_optimizer_no_zero_cardinality;
uint32_t debug_cardinality_multiplier;
bool pause_background_work = false;
uint32_t binlog_ttl_compaction_ts_interval_secs = 0;
uint32_t binlog_ttl_compaction_ts_offset_secs = 0;
uint32_t rocksdb_max_manual_compactions = 0;
bool rollback_on_timeout = false;
uint32_t debug_manual_compaction_delay = 0;
uint32_t io_write_timeout_secs = 0;
bool ignore_unknown_options = true;
char *strict_collation_exceptions;
bool collect_sst_properties = true;
uint64_t compaction_sequential_deletes = 0;
uint64_t compaction_sequential_deletes_window = 0;
long long compaction_sequential_deletes_file_size = 0LL;
bool compaction_sequential_deletes_count_sd = true;
bool print_snapshot_conflict_queries = false;
uint32_t validate_tables = 1;
uint32_t table_stats_sampling_pct;
uint32_t table_stats_recalc_threshold_pct = 10;
unsigned long long table_stats_recalc_threshold_count = 100ULL;
int32_t table_stats_background_thread_nice_value = THREAD_PRIO_MAX;
unsigned long long table_stats_max_num_rows_scanned = 0ULL;
uint32_t stats_recalc_rate = 0;
bool table_stats_skip_system_cf = false;
bool allow_to_start_after_corruption = false;
bool enable_insert_with_update_caching = true;
unsigned long long max_compaction_history = 0;  // NOLINT(runtime/int)
uint32_t bypass_rpc_rejected_log_ts_interval_secs = 0;
bool skip_locks_if_skip_unique_check = false;
bool alter_column_default_inplace = false;
bool partial_index_blind_delete = true;
bool partial_index_ignore_killed = true;
bool disable_instant_ddl = false;
bool enable_instant_ddl = false;
bool enable_instant_ddl_for_append_column = false;
bool enable_instant_ddl_for_column_default_changes = false;
bool enable_instant_ddl_for_table_comment_changes = false;
bool enable_instant_ddl_for_drop_index_changes = false;
bool enable_instant_ddl_for_update_index_visibility = false;
bool enable_tmp_table = false;
bool enable_delete_range_for_drop_index = false;
bool alter_table_comment_inplace = false;
bool column_default_value_as_expression = true;
uint32_t max_intrinsic_tmp_table_write_count = 0;
uint clone_checkpoint_max_count;
uint clone_checkpoint_max_age;
unsigned long long converter_record_cached_length = 0;
ulong file_checksums = file_checksums_type::CHECKSUMS_OFF;
bool debug_skip_bloom_filter_check_on_iterator_bounds = false;
unsigned long long max_trash_db_ratio_pct;
uint bulk_load_history_size = BULK_LOAD_HISTORY_DEFAULT_SIZE;
unsigned long long write_batch_mem_free_threshold = 0;

bool is_ttl_enabled() noexcept { return enable_ttl; }

bool is_ttl_read_filtering_enabled() noexcept {
  return enable_ttl_read_filtering;
}

bool is_ttl_compaction_filter_paused() noexcept {
  return pause_ttl_compaction_filter;
}

bool is_binlog_ttl_enabled() noexcept { return binlog_ttl; }

bool is_table_scan_index_stats_calculation_enabled() noexcept {
  return rocksdb_table_stats_use_table_scan;
}

select_bypass_policy_type get_select_bypass_policy() noexcept {
  return static_cast<select_bypass_policy_type>(select_bypass_policy);
}

bool should_fail_unsupported_select_bypass() noexcept {
  return select_bypass_fail_unsupported;
}

bool should_log_rejected_select_bypass() noexcept {
  return select_bypass_log_rejected;
}

bool should_log_failed_select_bypass() noexcept {
  return select_bypass_log_failed;
}

bool should_allow_filters_select_bypass() noexcept {
  return select_bypass_allow_filters;
}

uint32_t get_select_bypass_rejected_query_history_size() noexcept {
  return select_bypass_rejected_query_history_size;
}

uint32_t get_select_bypass_debug_row_delay() noexcept {
  return select_bypass_debug_row_delay;
}

unsigned long long  // NOLINT(runtime/int)
get_select_bypass_multiget_min() noexcept {
  return select_bypass_multiget_min;
}

bool is_bypass_rpc_on() noexcept { return bypass_rpc_on; }

bool should_log_rejected_bypass_rpc() noexcept {
  return bypass_rpc_log_rejected;
}

#ifndef NDEBUG
int debug_set_ttl_rec_ts() noexcept { return debug_ttl_rec_ts; }
int debug_set_ttl_snapshot_ts() noexcept { return debug_ttl_snapshot_ts; }
int debug_set_ttl_read_filter_ts() noexcept { return debug_ttl_read_filter_ts; }
bool debug_set_ttl_ignore_pk() noexcept { return debug_ttl_ignore_pk; }
#endif

// Thread variables

ulong lock_wait_timeout(THD *thd) { return THDVAR(thd, lock_wait_timeout); }

bool deadlock_detect(THD *thd) { return THDVAR(thd, deadlock_detect); }

ulong deadlock_detect_depth(THD *thd) {
  return THDVAR(thd, deadlock_detect_depth);
}

bool commit_time_batch_for_recovery(THD *thd) {
  return THDVAR(thd, commit_time_batch_for_recovery);
}

bool trace_sst_api(THD *thd) { return THDVAR(thd, trace_sst_api); }

bool bulk_load(THD *thd) { return THDVAR(thd, bulk_load); }

void set_bulk_load(THD *thd, bool new_val) { THDVAR(thd, bulk_load) = new_val; }

bool bulk_load_allow_sk(THD *thd) { return THDVAR(thd, bulk_load_allow_sk); }

bool bulk_load_allow_unsorted(THD *thd) {
  return THDVAR(thd, bulk_load_allow_unsorted);
}

bool bulk_load_fail_if_not_bottommost_level(THD *thd) {
  return THDVAR(thd, bulk_load_fail_if_not_bottommost_level);
}

bool bulk_load_use_sst_partitioner(THD *thd) {
  return THDVAR(thd, bulk_load_use_sst_partitioner);
}

bool bulk_load_enable_unique_key_check(THD *thd) {
  return THDVAR(thd, bulk_load_enable_unique_key_check);
}

uint bulk_load_compression_parallel_threads(THD *thd) {
  return THDVAR(thd, bulk_load_compression_parallel_threads);
}

char *tmpdir(THD *thd) { return THDVAR(thd, tmpdir); }

bool commit_in_the_middle(THD *thd) {
  return THDVAR(thd, commit_in_the_middle);
}

bool blind_delete_primary_key(THD *thd) {
  return THDVAR(thd, blind_delete_primary_key);
}

bool enable_iterate_bounds(THD *thd) {
  return THDVAR(thd, enable_iterate_bounds);
}

bool check_iterate_bounds(THD *thd) {
  return THDVAR(thd, check_iterate_bounds);
}

bool skip_snapshot_validation(THD *thd) {
  return THDVAR(thd, skip_snapshot_validation);
}

bool skip_bloom_filter_on_read(THD *thd) {
  return THDVAR(thd, skip_bloom_filter_on_read);
}

ulong max_row_locks(THD *thd) { return THDVAR(thd, max_row_locks); }

ulonglong write_batch_max_bytes(THD *thd) {
  return THDVAR(thd, write_batch_max_bytes);
}

ulonglong write_batch_flush_threshold(THD *thd) {
  return THDVAR(thd, write_batch_flush_threshold);
}

bool lock_scanned_rows(THD *thd) { return THDVAR(thd, lock_scanned_rows); }

ulong bulk_load_size(THD *thd) { return THDVAR(thd, bulk_load_size); }

bool bulk_load_partial_index(THD *thd) {
  return THDVAR(thd, bulk_load_partial_index);
}

ulonglong merge_buf_size(THD *thd) { return THDVAR(thd, merge_buf_size); }

ulonglong merge_combine_read_size(THD *thd) {
  return THDVAR(thd, merge_combine_read_size);
}

ulonglong merge_tmp_file_removal_delay_ms(THD *thd) {
  return THDVAR(thd, merge_tmp_file_removal_delay_ms);
}

int manual_compaction_threads(THD *thd) {
  return THDVAR(thd, manual_compaction_threads);
}

rocksdb::BottommostLevelCompaction manual_compaction_bottommost_level(
    THD *thd) {
  return static_cast<rocksdb::BottommostLevelCompaction>(
      THDVAR(thd, manual_compaction_bottommost_level));
}

int perf_context_level(THD *thd) { return THDVAR(thd, perf_context_level); }

bool write_disable_wal(THD *thd) { return THDVAR(thd, write_disable_wal); }

bool write_ignore_missing_column_families(THD *thd) {
  return THDVAR(thd, write_ignore_missing_column_families);
}

ulong protection_bytes_per_key(THD *thd) {
  return THDVAR(thd, protection_bytes_per_key);
}

bool skip_fill_cache(THD *thd) { return THDVAR(thd, skip_fill_cache); }

bool unsafe_for_binlog(THD *thd) { return THDVAR(thd, unsafe_for_binlog); }

uint records_in_range(THD *thd) { return THDVAR(thd, records_in_range); }

uint force_index_records_in_range(THD *thd) {
  return THDVAR(thd, force_index_records_in_range);
}

char *create_temporary_checkpoint(THD *thd) {
  return THDVAR(thd, create_temporary_checkpoint);
}

int checksums_pct(THD *thd) { return THDVAR(thd, checksums_pct); }

bool store_row_debug_checksums(THD *thd) {
  return THDVAR(thd, store_row_debug_checksums);
}

bool verify_row_debug_checksums(THD *thd) {
  return THDVAR(thd, verify_row_debug_checksums);
}

bool master_skip_tx_api(THD *thd) { return THDVAR(thd, master_skip_tx_api); }

ulong mrr_batch_size(THD *thd) { return THDVAR(thd, mrr_batch_size); }

unsigned long long get_partial_index_sort_max_mem(THD *thd) {
  return THDVAR(thd, partial_index_sort_max_mem);
}

bool enable_autoinc_compat_mode(THD *thd) {
  return THDVAR(thd, enable_autoinc_compat_mode);
}

ulonglong consistent_snapshot_ttl_read_filtering_ts_nsec(THD *thd) {
  return THDVAR(thd, consistent_snapshot_ttl_read_filtering_ts_nsec);
}

void init() {
  bottom_pri_background_compactions_resize_mutex.init(
      rdb_bottom_pri_background_compactions_resize_mutex_key,
      MY_MUTEX_INIT_FAST);
  rdb_block_cache_resize_mutex.init(rdb_block_cache_resize_mutex_key,
                                    MY_MUTEX_INIT_FAST);
}

void shutdown() {
  bottom_pri_background_compactions_resize_mutex.destroy();
  rdb_block_cache_resize_mutex.destroy();
}

SYS_VAR *vars[] = {
    MYSQL_SYSVAR(lock_wait_timeout),
    MYSQL_SYSVAR(deadlock_detect),
    MYSQL_SYSVAR(deadlock_detect_depth),
    MYSQL_SYSVAR(commit_time_batch_for_recovery),
    MYSQL_SYSVAR(trace_sst_api),
    MYSQL_SYSVAR(bulk_load),
    MYSQL_SYSVAR(bulk_load_allow_sk),
    MYSQL_SYSVAR(bulk_load_allow_unsorted),
    MYSQL_SYSVAR(bulk_load_fail_if_not_bottommost_level),
    MYSQL_SYSVAR(bulk_load_use_sst_partitioner),
    MYSQL_SYSVAR(bulk_load_enable_unique_key_check),
    MYSQL_SYSVAR(enable_bulk_load_api),
    MYSQL_SYSVAR(bulk_load_compression_parallel_threads),
    MYSQL_SYSVAR(enable_remove_orphaned_dropped_cfs),
    MYSQL_SYSVAR(enable_udt_in_mem),
    MYSQL_SYSVAR(tmpdir),
    MYSQL_SYSVAR(commit_in_the_middle),
    MYSQL_SYSVAR(blind_delete_primary_key),
    MYSQL_SYSVAR(enable_iterate_bounds),
    MYSQL_SYSVAR(check_iterate_bounds),
    MYSQL_SYSVAR(skip_snapshot_validation),
    MYSQL_SYSVAR(read_free_rpl_tables),
    MYSQL_SYSVAR(read_free_rpl),
    MYSQL_SYSVAR(corrupt_data_action),
    MYSQL_SYSVAR(io_error_action),
    MYSQL_SYSVAR(invalid_create_option_action),
    MYSQL_SYSVAR(skip_bloom_filter_on_read),
    MYSQL_SYSVAR(max_row_locks),
    MYSQL_SYSVAR(write_batch_max_bytes),
    MYSQL_SYSVAR(write_batch_flush_threshold),
    MYSQL_SYSVAR(write_batch_mem_free_threshold),
    MYSQL_SYSVAR(lock_scanned_rows),
    MYSQL_SYSVAR(bulk_load_size),
    MYSQL_SYSVAR(bulk_load_partial_index),
    MYSQL_SYSVAR(merge_buf_size),
    MYSQL_SYSVAR(merge_combine_read_size),
    MYSQL_SYSVAR(merge_tmp_file_removal_delay_ms),
    MYSQL_SYSVAR(manual_compaction_threads),
    MYSQL_SYSVAR(manual_compaction_bottommost_level),
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
    MYSQL_SYSVAR(perf_context_level),
    MYSQL_SYSVAR(wal_recovery_mode),
    MYSQL_SYSVAR(track_and_verify_wals_in_manifest),
    MYSQL_SYSVAR(stats_level),
    MYSQL_SYSVAR(max_open_files),
    MYSQL_SYSVAR(max_file_opening_threads),
    MYSQL_SYSVAR(max_total_wal_size),
    MYSQL_SYSVAR(use_fsync),
    MYSQL_SYSVAR(wal_dir),
    MYSQL_SYSVAR(datadir),
    MYSQL_SYSVAR(compaction_readahead_size),
    MYSQL_SYSVAR(allow_concurrent_memtable_write),
    MYSQL_SYSVAR(enable_write_thread_adaptive_yield),
    MYSQL_SYSVAR(persistent_cache_path),
    MYSQL_SYSVAR(persistent_cache_size_mb),
    MYSQL_SYSVAR(wsenv_path),
    MYSQL_SYSVAR(wsenv_tenant),
    MYSQL_SYSVAR(wsenv_oncall),
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
    MYSQL_SYSVAR(block_cache_numshardbits),
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
    MYSQL_SYSVAR(block_cache_size),
    MYSQL_SYSVAR(sim_cache_size),
    MYSQL_SYSVAR(use_io_uring),
    MYSQL_SYSVAR(use_hyper_clock_cache),
    MYSQL_SYSVAR(cache_dump),
    MYSQL_SYSVAR(cache_high_pri_pool_ratio),
    MYSQL_SYSVAR(cache_index_and_filter_blocks),
    MYSQL_SYSVAR(cache_index_and_filter_with_high_priority),
    MYSQL_SYSVAR(pin_l0_filter_and_index_blocks_in_cache),
    MYSQL_SYSVAR(index_type),
    MYSQL_SYSVAR(no_block_cache),
    MYSQL_SYSVAR(block_size),
    MYSQL_SYSVAR(charge_memory),
    MYSQL_SYSVAR(use_write_buffer_manager),
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
    MYSQL_SYSVAR(protection_bytes_per_key),
    MYSQL_SYSVAR(skip_fill_cache),
    MYSQL_SYSVAR(unsafe_for_binlog),
    MYSQL_SYSVAR(records_in_range),
    MYSQL_SYSVAR(force_index_records_in_range),
    MYSQL_SYSVAR(debug_optimizer_n_rows),
    MYSQL_SYSVAR(force_compute_memtable_stats),
    MYSQL_SYSVAR(force_compute_memtable_stats_cachetime),
    MYSQL_SYSVAR(debug_optimizer_no_zero_cardinality),
    MYSQL_SYSVAR(debug_cardinality_multiplier),
    MYSQL_SYSVAR(create_temporary_checkpoint),
    MYSQL_SYSVAR(signal_drop_index_thread),
    MYSQL_SYSVAR(pause_background_work),
    MYSQL_SYSVAR(enable_ttl),
    MYSQL_SYSVAR(enable_ttl_read_filtering),
    MYSQL_SYSVAR(debug_ttl_rec_ts),
    MYSQL_SYSVAR(debug_ttl_snapshot_ts),
    MYSQL_SYSVAR(debug_ttl_read_filter_ts),
    MYSQL_SYSVAR(debug_ttl_ignore_pk),
    MYSQL_SYSVAR(pause_ttl_compaction_filter),
    MYSQL_SYSVAR(binlog_ttl),
    MYSQL_SYSVAR(binlog_ttl_compaction_ts_interval_secs),
    MYSQL_SYSVAR(binlog_ttl_compaction_ts_offset_secs),
    MYSQL_SYSVAR(debug_binlog_ttl_compaction_ts_delta),
    MYSQL_SYSVAR(max_manual_compactions),
    MYSQL_SYSVAR(rollback_on_timeout),
    MYSQL_SYSVAR(debug_manual_compaction_delay),
    MYSQL_SYSVAR(reset_stats),
    MYSQL_SYSVAR(io_write_timeout),
    MYSQL_SYSVAR(ignore_unknown_options),
    MYSQL_SYSVAR(collect_sst_properties),
    MYSQL_SYSVAR(seconds_between_stat_computes),
    MYSQL_SYSVAR(compaction_sequential_deletes),
    MYSQL_SYSVAR(compaction_sequential_deletes_window),
    MYSQL_SYSVAR(compaction_sequential_deletes_file_size),
    MYSQL_SYSVAR(compaction_sequential_deletes_count_sd),
    MYSQL_SYSVAR(print_snapshot_conflict_queries),
    MYSQL_SYSVAR(checksums_pct),
    MYSQL_SYSVAR(store_row_debug_checksums),
    MYSQL_SYSVAR(verify_row_debug_checksums),
    MYSQL_SYSVAR(master_skip_tx_api),
    MYSQL_SYSVAR(validate_tables),
    MYSQL_SYSVAR(table_stats_sampling_pct),
    MYSQL_SYSVAR(table_stats_recalc_threshold_pct),
    MYSQL_SYSVAR(table_stats_recalc_threshold_count),
    MYSQL_SYSVAR(table_stats_background_thread_nice_value),
    MYSQL_SYSVAR(table_stats_max_num_rows_scanned),
    MYSQL_SYSVAR(stats_recalc_rate),
    MYSQL_SYSVAR(table_stats_use_table_scan),
    MYSQL_SYSVAR(table_stats_skip_system_cf),
    MYSQL_SYSVAR(allow_to_start_after_corruption),
    MYSQL_SYSVAR(enable_insert_with_update_caching),
    MYSQL_SYSVAR(trace_block_cache_access),
    MYSQL_SYSVAR(trace_queries),
    MYSQL_SYSVAR(max_compaction_history),
    MYSQL_SYSVAR(disable_file_deletions),
    MYSQL_SYSVAR(select_bypass_policy),
    MYSQL_SYSVAR(select_bypass_fail_unsupported),
    MYSQL_SYSVAR(select_bypass_log_rejected),
    MYSQL_SYSVAR(select_bypass_log_failed),
    MYSQL_SYSVAR(select_bypass_allow_filters),
    MYSQL_SYSVAR(select_bypass_rejected_query_history_size),
    MYSQL_SYSVAR(select_bypass_debug_row_delay),
    MYSQL_SYSVAR(select_bypass_multiget_min),
    MYSQL_SYSVAR(bypass_rpc_rejected_log_ts_interval_secs),
    MYSQL_SYSVAR(bypass_rpc_on),
    MYSQL_SYSVAR(bypass_rpc_log_rejected),
    MYSQL_SYSVAR(mrr_batch_size),
    MYSQL_SYSVAR(skip_locks_if_skip_unique_check),
    MYSQL_SYSVAR(alter_column_default_inplace),
    MYSQL_SYSVAR(partial_index_sort_max_mem),
    MYSQL_SYSVAR(partial_index_blind_delete),
    MYSQL_SYSVAR(partial_index_ignore_killed),
    MYSQL_SYSVAR(disable_instant_ddl),
    MYSQL_SYSVAR(enable_instant_ddl),
    MYSQL_SYSVAR(enable_instant_ddl_for_append_column),
    MYSQL_SYSVAR(enable_instant_ddl_for_column_default_changes),
    MYSQL_SYSVAR(enable_instant_ddl_for_table_comment_changes),
    MYSQL_SYSVAR(enable_instant_ddl_for_drop_index_changes),
    MYSQL_SYSVAR(enable_instant_ddl_for_update_index_visibility),
    MYSQL_SYSVAR(enable_tmp_table),
    MYSQL_SYSVAR(enable_delete_range_for_drop_index),
    MYSQL_SYSVAR(alter_table_comment_inplace),
    MYSQL_SYSVAR(column_default_value_as_expression),
    MYSQL_SYSVAR(max_intrinsic_tmp_table_write_count),
    MYSQL_SYSVAR(clone_checkpoint_max_age),
    MYSQL_SYSVAR(clone_checkpoint_max_count),
    MYSQL_SYSVAR(converter_record_cached_length),
    MYSQL_SYSVAR(file_checksums),
    MYSQL_SYSVAR(debug_skip_bloom_filter_check_on_iterator_bounds),
    MYSQL_SYSVAR(compact_cf),
    MYSQL_SYSVAR(delete_cf),
    MYSQL_SYSVAR(create_checkpoint),
    MYSQL_SYSVAR(force_flush_memtable_now),
    MYSQL_SYSVAR(force_flush_memtable_and_lzero_now),
    MYSQL_SYSVAR(compact_lzero_now),
    MYSQL_SYSVAR(cancel_manual_compactions),
    MYSQL_SYSVAR(max_trash_db_ratio_pct),
    MYSQL_SYSVAR(enable_autoinc_compat_mode),
    MYSQL_SYSVAR(bulk_load_history_size),
    MYSQL_SYSVAR(consistent_snapshot_ttl_read_filtering_ts_nsec),
    nullptr};

}  // namespace myrocks::sysvars
