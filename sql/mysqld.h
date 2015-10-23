/* Copyright (c) 2010, 2015, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */

#ifndef MYSQLD_INCLUDED
#define MYSQLD_INCLUDED

#include "my_global.h" /* MYSQL_PLUGIN_IMPORT, FN_REFLEN, FN_EXTLEN */
#include "sql_bitmap.h"                         /* Bitmap */
#include "my_decimal.h"                         /* my_decimal */
#include "mysql_com.h"                     /* SERVER_VERSION_LENGTH */
#include "my_atomic.h"                     /* my_atomic_rwlock_t */
#include "mysql/psi/mysql_file.h"          /* MYSQL_FILE */
#include "sql_list.h"                      /* I_List */
#include "sql_cmd.h"                       /* SQLCOM_END */
#include "my_rdtsc.h"                      /* my_timer* */
#include <set>
#include "sql_priv.h"                      /* enum_var_type */
// for unix sockets
#include <sys/socket.h>
#include <sys/un.h>
#include "atomic_stat.h"

class THD;
struct handlerton;
class Time_zone;

struct scheduler_functions;

typedef struct st_mysql_const_lex_string LEX_CSTRING;
typedef struct st_mysql_show_var SHOW_VAR;

typedef std::set<int> engine_set;
/* Store all engines that support handler::flush_logs into global_trx_engine.*/
extern engine_set global_trx_engine;
extern my_bool plugins_are_initialized;

typedef struct lsn_map
{
  int       db_type; /* The engine type. */
  ulonglong  lsn; /* LSN of prepared log for each engine. */
} lsn_map;

class engine_lsn_map {
public:
  engine_lsn_map()
  {
    /* If all trx engines are skipped, ignore memory allocating. */
    if (global_trx_engine.size() == 0)
    {
      m_count= 0;
      m_empty= true;
      maps= NULL;
      return;
    }

    m_count= global_trx_engine.size();
    m_empty= true;

    maps= (lsn_map **)my_malloc(sizeof(lsn_map*) * m_count,
        MYF(MY_FAE|MY_ZEROFILL));

    int i= 0;
    for (engine_set::iterator it= global_trx_engine.begin();
        it != global_trx_engine.end(); ++it)
    {
      maps[i]= (lsn_map *)my_malloc(sizeof(lsn_map),
          MYF(MY_FAE|MY_ZEROFILL));

      maps[i]->db_type= *it;
      maps[i]->lsn= 0;
      i++;
    }
  }

  ~engine_lsn_map()
  {
   for (int i=0; i<m_count; i++)
   {
    my_free(maps[i]);
    maps[i]= NULL;
   }

   if (m_count)
    my_free(maps);
  }

  lsn_map* get_map_by_type(int db_type)
  {
    for (int i=0; i<m_count; i++)
    {
      if (maps[i]->db_type == db_type)
        return maps[i];
    }

    return NULL;
  }

  ulonglong get_lsn_by_type(int db_type)
  {
    lsn_map* target_map= get_map_by_type(db_type);
    if (target_map)
      return target_map->lsn;
    else
      return 0;
  }

  /* If lsn value of current maps is smaller than other_map,
     then update it. */
  void compare_and_update(lsn_map** other_map)
  {
    DBUG_ASSERT(other_map != NULL);

    for (int i=0; i<m_count; i++)
    {
      DBUG_ASSERT(other_map[i] != NULL);
      DBUG_ASSERT(maps[i] != NULL);
      DBUG_ASSERT(maps[i]->db_type == other_map[i]->db_type);

      if (other_map[i]->lsn > maps[i]->lsn)
      {
        maps[i]->lsn= other_map[i]->lsn;

        m_empty= false;
      }
    }
  }

#ifndef DBUG_OFF
  /* Return true if lsn in current map is smaller than
     other_map( or equal to). */
  bool compare_lt(lsn_map** other_map)
  {
    for (int i=0; i<m_count; i++)
    {
      if (other_map[i]->lsn < maps[i]->lsn)
        return false;
    }

    return true;
  }
#endif

  void clear()
  {
    if (m_empty)
      return;

    for (int i=0; i<m_count; i++)
    {
      maps[i]->lsn= 0;
    }

    m_empty= true;
  }

  bool is_empty() { return m_empty; }

  lsn_map** get_maps() { return maps; }

  void update_lsn(int db_type, ulonglong lsn)
  {
    lsn_map *target_map= get_map_by_type(db_type);
    DBUG_ASSERT(target_map != NULL);

    target_map->lsn= lsn;

    m_empty= false;
  }

private:
  /* If lsn of all elements in maps array is zero. */
  bool m_empty;

  /* Elements in maps array. */
  int m_count;

  /* Used to store db_type=>lsn. */
  lsn_map **maps;
};

/*
  This forward declaration is used from C files where the real
  definition is included before.  Since C does not allow repeated
  typedef declarations, even when identical, the definition may not be
  repeated.
*/
#ifndef CHARSET_INFO_DEFINED
typedef struct charset_info_st CHARSET_INFO;
#endif  /* CHARSET_INFO_DEFINED */

#if MAX_INDEXES <= 64
typedef Bitmap<64>  key_map;          /* Used for finding keys */
#else
typedef Bitmap<((MAX_INDEXES+7)/8*8)> key_map; /* Used for finding keys */
#endif

	/* Bits from testflag */
#define TEST_PRINT_CACHED_TABLES 1
#define TEST_NO_KEY_GROUP	 2
#define TEST_MIT_THREAD		4
#define TEST_BLOCKING		8
#define TEST_KEEP_TMP_TABLES	16
#define TEST_READCHECK		64	/**< Force use of readcheck */
#define TEST_NO_EXTRA		128
#define TEST_NO_STACKTRACE	512
#define TEST_SIGINT		1024	/**< Allow sigint on threads */
#define TEST_SYNCHRONIZATION    2048    /**< get server to do sleep in
                                           some places */
#define HISTOGRAM_BUCKET_NAME_MAX_SIZE 16	/**< This is the maximum size
						   of the string:
						   "LowerBucketValue-"
						   "UpperBucketValue<units>"
						   where bucket is the latency
						   histogram bucket and units
						   can be us,ms or s */

/* Function prototypes */
void kill_mysql(void);
void close_connection(THD *thd, uint sql_errno= 0);
void handle_connection_in_main_thread(THD *thd);
void create_thread_to_handle_connection(THD *thd);
void destroy_thd(THD *thd);
bool one_thread_per_connection_end(THD *thd, bool block_pthread);
void kill_blocked_pthreads();
void refresh_status(THD *thd);
bool is_secure_file_path(char *path);
void dec_connection_count();

// These are needed for unit testing.
void set_remaining_args(int argc, char **argv);
int init_common_variables(my_bool logging);
void my_init_signals();
bool gtid_server_init();
void gtid_server_cleanup();

extern "C" MYSQL_PLUGIN_IMPORT CHARSET_INFO *system_charset_info;
extern MYSQL_PLUGIN_IMPORT CHARSET_INFO *files_charset_info ;
extern MYSQL_PLUGIN_IMPORT CHARSET_INFO *national_charset_info;
extern MYSQL_PLUGIN_IMPORT CHARSET_INFO *table_alias_charset;

/**
  Character set of the buildin error messages loaded from errmsg.sys.
*/
extern CHARSET_INFO *error_message_charset_info;

extern CHARSET_INFO *character_set_filesystem;

extern MY_BITMAP temp_pool;
extern bool opt_large_files, server_id_supplied;
extern bool opt_update_log, opt_bin_log, opt_error_log;
extern my_bool opt_log, opt_slow_log, opt_log_raw;
extern my_bool opt_backup_history_log;
extern my_bool opt_backup_progress_log;
extern ulonglong log_output_options;
extern ulong log_backup_output_options;
extern my_bool opt_log_queries_not_using_indexes;
extern ulong opt_log_throttle_queries_not_using_indexes;
extern my_bool opt_disable_working_set_size;
extern bool opt_disable_networking, opt_skip_show_db;
extern bool opt_skip_name_resolve;
extern bool opt_ignore_builtin_innodb;
extern my_bool opt_character_set_client_handshake;
extern MYSQL_PLUGIN_IMPORT bool volatile abort_loop;
extern bool in_bootstrap;
extern my_bool opt_bootstrap;
extern uint connection_count;
extern ulong opt_srv_fatal_semaphore_timeout;
extern my_bool opt_safe_user_create;
extern my_bool opt_safe_show_db, opt_local_infile, opt_myisam_use_mmap;
extern my_bool opt_slave_compressed_protocol, use_temp_pool;
extern ulong slave_exec_mode_options;
extern ulong slave_run_triggers_for_rbr;
extern ulonglong slave_type_conversions_options;
extern my_bool read_only, opt_readonly, super_read_only, opt_super_readonly;
extern my_bool allow_document_type;
extern my_bool block_create_myisam;
extern my_bool block_create_memory;
extern my_bool block_create_no_primary_key;
extern my_bool lower_case_file_system;
extern ulonglong slave_rows_search_algorithms_options;
#ifndef DBUG_OFF
extern uint slave_rows_last_search_algorithm_used;
#endif
#ifndef EMBEDDED_LIBRARY
extern "C" int check_enough_stack_size(int);
#endif
extern my_bool opt_enable_named_pipe, opt_sync_frm, opt_allow_suspicious_udfs;
extern my_bool opt_secure_auth;
extern char* opt_secure_file_priv;
extern char* opt_secure_backup_file_priv;
extern size_t opt_secure_backup_file_priv_len;
extern my_bool opt_log_slow_admin_statements, opt_log_slow_slave_statements;
extern my_bool sp_automatic_privileges, opt_noacl;
extern my_bool opt_old_style_user_limits, trust_function_creators;
extern uint opt_crash_binlog_innodb;
extern char *shared_memory_base_name, *mysqld_unix_port;
extern my_bool opt_enable_shared_memory;
extern char *default_tz_name;
extern Time_zone *default_tz;
extern char *default_storage_engine;
extern char *default_tmp_storage_engine;
extern bool opt_endinfo, using_udf_functions;
extern my_bool locked_in_memory;
extern bool opt_using_transactions;
extern ulong max_long_data_size;
extern ulong current_pid;
extern ulong expire_logs_days;
extern my_bool relay_log_recovery;
extern uint sync_binlog_period, sync_relaylog_period,
            sync_relayloginfo_period, sync_masterinfo_period,
            opt_mts_checkpoint_period, opt_mts_checkpoint_group;
extern ulong opt_tc_log_size, tc_log_max_pages_used, tc_log_page_size;
extern ulong tc_log_page_waits;
extern my_bool relay_log_purge, opt_innodb_safe_binlog, opt_innodb;
extern my_bool relay_log_recovery;
extern uint test_flags,select_errors,ha_open_options;
extern uint protocol_version, mysqld_port, dropping_tables;
extern ulong mysqld_admin_port;
extern ulong delay_key_write_options;
extern char *opt_logname, *opt_slow_logname, *opt_bin_logname,
            *opt_relay_logname;
extern char *opt_backup_history_logname, *opt_backup_progress_logname,
            *opt_backup_settings_name;
extern const char *log_output_str;
extern const char *log_backup_output_str;
extern char *mysql_home_ptr, *pidfile_name_ptr;
extern char *my_bind_addr_str;
extern char *binlog_file_basedir_ptr, *binlog_index_basedir_ptr;
extern char glob_hostname[FN_REFLEN], mysql_home[FN_REFLEN];
extern char pidfile_name[FN_REFLEN], system_time_zone[30], *opt_init_file;
extern char default_logfile_name[FN_REFLEN];
extern char log_error_file[FN_REFLEN], *opt_tc_log_file;

extern int32 thread_binlog_client;

extern my_bool opt_log_slow_extra;
extern ulonglong binlog_fsync_count;

extern uint net_compression_level;

extern ulong relay_io_connected;

extern ulong opt_peak_lag_time;
extern ulong opt_peak_lag_sample_rate;

extern ulong relay_io_events, relay_sql_events;
extern ulonglong relay_io_bytes, relay_sql_bytes;
extern ulonglong relay_sql_wait_time;
extern my_bool recv_skip_ibuf_operations;

/* SHOW STATS var: Name of current timer */
extern const char *timer_in_use;
/* Current timer stats */
extern struct my_timer_unit_info my_timer;
/* Get current time */
extern ulonglong (*my_timer_now)(void);
/* Get time passed since "then" */
inline ulonglong my_timer_since(ulonglong then)
{
  return (my_timer_now() - then) - my_timer.overhead;
}
/* Get time passed since "then", and update then to now */
inline ulonglong my_timer_since_and_update(ulonglong *then)
{
  ulonglong now = my_timer_now();
  ulonglong ret = (now - (*then)) - my_timer.overhead;
  *then = now;
  return ret;
}
/* Convert native timer units in a ulonglong into seconds in a double */
inline double my_timer_to_seconds(ulonglong when)
{
  double ret = (double)(when);
  ret /= (double)(my_timer.frequency);
  return ret;
}
/* Convert native timer units in a ulonglong into milliseconds in a double */
inline double my_timer_to_milliseconds(ulonglong when)
{
  double ret = (double)(when);
  ret *= 1000.0;
  ret /= (double)(my_timer.frequency);
  return ret;
}
/* Convert native timer units in a ulonglong into microseconds in a double */
inline double my_timer_to_microseconds(ulonglong when)
{
  double ret = (double)(when);
  ret *= 1000000.0;
  ret /= (double)(my_timer.frequency);
  return ret;
}
/* Convert microseconds in a double to native timer units in a ulonglong */
inline ulonglong microseconds_to_my_timer(double when)
{
  double ret = when;
  ret *= (double)(my_timer.frequency);
  ret /= 1000000.0;
  return (ulonglong)ret;
}

/* Convert native timer units in a ulonglong into microseconds in a ulonglong */
inline ulonglong my_timer_to_microseconds_ulonglong(ulonglong when)
{
  ulonglong ret = (ulonglong)(when);
  ret *= 1000000;
  ret = (ulonglong)((ret + my_timer.frequency -1) / my_timer.frequency);
  return ret;
}

/** Compression statistics for a fil_space */
struct comp_stats_struct {
  /** Size of the compressed data on the page */
  int page_size;
  /** Current padding for compression */
  int padding;
  /** Number of page compressions */
  ulonglong compressed;
  /** Number of successful page compressions */
  ulonglong compressed_ok;
  /** Number of compressions in primary index */
  ulonglong compressed_primary;
  /** Number of successful compressions in primary index */
  ulonglong compressed_primary_ok;
  /** Number of page decompressions */
  ulonglong decompressed;
  /** Duration of page compressions */
  ulonglong compressed_time;
  /** Duration of successful page compressions */
  ulonglong compressed_ok_time;
  /** Duration of page decompressions */
  ulonglong decompressed_time;
  /** Duration of primary index page compressions */
  ulonglong compressed_primary_time;
  /** Duration of successful primary index page compressions */
  ulonglong compressed_primary_ok_time;
};

/** Compression statistics */
typedef struct comp_stats_struct comp_stats_t;

/** Compression statistics for a fil_space */
struct comp_stats_atomic_struct {
  /** Size of the compressed data on the page */
  atomic_stat<int> page_size;
  /** Current padding for compression */
  atomic_stat<int> padding;
  /** Number of page compressions */
  atomic_stat<ulonglong> compressed;
  /** Number of successful page compressions */
  atomic_stat<ulonglong> compressed_ok;
  /** Number of compressions in primary index */
  atomic_stat<ulonglong> compressed_primary;
  /** Number of successful compressions in primary index */
  atomic_stat<ulonglong> compressed_primary_ok;
  /** Number of page decompressions */
  atomic_stat<ulonglong> decompressed;
  /** Duration of page compressions */
  atomic_stat<ulonglong> compressed_time;
  /** Duration of successful page compressions */
  atomic_stat<ulonglong> compressed_ok_time;
  /** Duration of page decompressions */
  atomic_stat<ulonglong> decompressed_time;
  /** Duration of primary index page compressions */
  atomic_stat<ulonglong> compressed_primary_time;
  /** Duration of successful primary index page compressions */
  atomic_stat<ulonglong> compressed_primary_ok_time;
};

/** Compression statistics, atomic */
typedef struct comp_stats_atomic_struct comp_stats_atomic_t;

/* Struct used for IO performance counters within a single thread */
struct my_io_perf_struct {
  ulonglong bytes;
  ulonglong requests;
  ulonglong svc_time; /*!< time to do read or write operation */
  ulonglong svc_time_max;
  ulonglong wait_time; /*!< total time in the request array */
  ulonglong wait_time_max;
  ulonglong slow_ios; /*!< requests that take too long */
};
typedef struct my_io_perf_struct my_io_perf_t;

/* Struct used for IO performance counters, shared among multiple threads */
struct my_io_perf_atomic_struct {
  atomic_stat<ulonglong> bytes;
  atomic_stat<ulonglong> requests;
  atomic_stat<ulonglong> svc_time; /*!< time to do read or write operation */
  atomic_stat<ulonglong> svc_time_max;
  atomic_stat<ulonglong> wait_time; /*!< total time in the request array */
  atomic_stat<ulonglong> wait_time_max;
  atomic_stat<ulonglong> slow_ios; /*!< requests that take too long */
};
typedef struct my_io_perf_atomic_struct my_io_perf_atomic_t;

/* struct used in per page type stats in IS.table_stats */
struct page_stats_struct {
  /*!< number read operations of all pages at given space*/
  ulong n_pages_read;

  /*!< number read operations of FIL_PAGE_INDEX pages at given space*/
  ulong n_pages_read_index;

  /*!< number read operations FIL_PAGE_TYPE_BLOB and FIL_PAGE_TYPE_ZBLOB
       and FIL_PAGE_TYPE_ZBLOB2 pages at given space*/
  ulong n_pages_read_blob;

  /*!< number write operations of all pages at given space*/
  ulong n_pages_written;

  /*!< number write operations of FIL_PAGE_INDEX pages at given space*/
  ulong n_pages_written_index;

  /*!< number write operations FIL_PAGE_TYPE_BLOB and FIL_PAGE_TYPE_ZBLOB
       and FIL_PAGE_TYPE_ZBLOB2 pages at given space*/
  ulong n_pages_written_blob;
};
typedef struct page_stats_struct page_stats_t;

/* struct used in per page type stats in IS.table_stats, atomic version */
struct page_stats_atomic_struct {
  atomic_stat<ulong> n_pages_read;
  atomic_stat<ulong> n_pages_read_index;
  atomic_stat<ulong> n_pages_read_blob;
  atomic_stat<ulong> n_pages_written;
  atomic_stat<ulong> n_pages_written_index;
  atomic_stat<ulong> n_pages_written_blob;
};
typedef struct page_stats_atomic_struct page_stats_atomic_t;

/* Per-table operation and IO statistics */

/* Initialize an my_io_perf_t struct. */
static inline void my_io_perf_init(my_io_perf_t* perf) {
  memset(perf, 0, sizeof(*perf));
}

/* Initialize an my_io_perf_atomic_t struct. */
static inline void my_io_perf_atomic_init(my_io_perf_atomic_t* perf) {
  perf->bytes.clear();
  perf->requests.clear();
  perf->svc_time.clear();
  perf->svc_time_max.clear();
  perf->wait_time.clear();
  perf->wait_time_max.clear();
  perf->slow_ios.clear();
}

/* Accumulate per-table compression stats helper function */
void my_comp_stats_sum_atomic(comp_stats_atomic_t* sum,
                              comp_stats_t* comp_stats);

/* Accumulate per-table page stats helper function */
void my_page_stats_sum_atomic(page_stats_atomic_t* sum,
                              page_stats_t* page_stats);

/* Returns a - b in diff */
void my_io_perf_diff(my_io_perf_t* diff,
                     const my_io_perf_t* a, const my_io_perf_t* b);
/* Accumulates io perf values */
void my_io_perf_sum(my_io_perf_t* sum, const my_io_perf_t* perf);

/* Accumulates io perf values using atomic operations */
void my_io_perf_sum_atomic(
  my_io_perf_atomic_t* sum,
  ulonglong bytes,
  ulonglong requests,
  ulonglong svc_time,
  ulonglong wait_time,
  ulonglong slow_ios);

/* Accumulates io perf values using atomic operations */
static inline void my_io_perf_sum_atomic_helper(
  my_io_perf_atomic_t* sum,
  const my_io_perf_t* perf)
{
  my_io_perf_sum_atomic(
    sum,
    perf->bytes,
    perf->requests,
    perf->svc_time,
    perf->wait_time,
    perf->slow_ios);
}

/* Histogram struct to track various latencies */
#define NUMBER_OF_HISTOGRAM_BINS 15
struct latency_histogram {
  size_t num_bins;
  ulonglong step_size;
  ulonglong count_per_bin[NUMBER_OF_HISTOGRAM_BINS];
};

/**
  Create a new Histogram.

  @param current_histogram    The histogram being initialized.
  @param step_size_with_unit  Configurable system variable containing
                              step size and unit of the Histogram.
*/
void latency_histogram_init(latency_histogram* current_histogram,
                    const char* step_size_with_unit);

/**
  Increment the count of a bin in Histogram.

  @param current_histogram  The current histogram.
  @param value              Value of which corresponding bin has to be found.
  @param count              Amount by which the count of a bin has to be
                            increased.

*/
void latency_histogram_increment(latency_histogram* current_histogram,
                                   ulonglong value, ulonglong count);
/**
  Get the count corresponding to a bin of the Histogram.

  @param current_histogram  The current histogram.
  @param bin_num            The bin whose count has to be returned.

  @return                   Returns the count of that bin.
*/
ulonglong latency_histogram_get_count(latency_histogram* current_histogram,
                                     size_t bin_num);

/**
  Validate if the string passed to the configurable histogram step size
  conforms to proper syntax.

  @param step_size_with_unit  The configurable step size string to be checked.

  @return                     1 if invalid, 0 if valid.
*/
int histogram_validate_step_size_string(const char* step_size_with_unit);

/** To return the displayable histogram name from
  my_timer_to_display_string() */
struct histogram_display_string {
  char name[HISTOGRAM_BUCKET_NAME_MAX_SIZE];
};

/**
  This function is called by show_innodb_latency_histgoram()
  to convert the histogram bucket ranges in system time units
  to a string and calculates units on the fly, which can be
  displayed in the output of SHOW GLOBAL STATUS.
  The string has the following form:

  <HistogramName>_<BucketLowerValue>-<BucketUpperValue><Unit>

  @param bucket_lower_display  Lower Range value of the Histogram Bucket
  @param bucket_upper_display  Upper Range value of the Histogram Bucket

  @return                      The display string for the Histogram Bucket
*/
histogram_display_string
histogram_bucket_to_display_string(ulonglong bucket_lower_display,
                                   ulonglong bucket_upper_display);

/**
  This function is called by the Callback function show_innodb_vars()
  to add entries into the latency_histogram_xxxx array, by forming
  the appropriate display string and fetching the histogram bin
  counts.

  @param current_histogram       Histogram whose values are currently added
                                 in the SHOW_VAR array
  @param latency_histogram_data  SHOW_VAR array for the corresponding Histogram
  @param histogram_values        Values to be exported to Innodb status.
                                 This array contains the bin counts of the
                                 respective Histograms.
*/
void prepare_latency_histogram_vars(latency_histogram* current_histogram,
                                    SHOW_VAR* latency_histogram_data,
                                    ulonglong* histogram_values);
/**
   Frees old histogram bucket display strings before assigning new ones.
*/
void free_latency_histogram_sysvars(SHOW_VAR* latency_histogram_data);

/* Fetches table stats for a given table */
struct TABLE;
struct st_table_stats* get_table_stats(TABLE *table,
                                       struct handlerton *engine_type);

unsigned char get_db_stats_index(const char* db);
void update_global_db_stats_access(unsigned char db_stats_index,
                                   uint64 space,
                                   uint64 offset);

/*Move UUID_LENGTH from item_strfunc.h*/
#define UUID_LENGTH (8+1+4+1+4+1+4+1+12)
extern char server_uuid[UUID_LENGTH+1];
extern const char *server_uuid_ptr;
extern const double log_10[309];
extern ulonglong keybuff_size;
extern ulonglong thd_startup_options;
extern ulong thread_id;
extern ulong binlog_cache_use, binlog_cache_disk_use;
extern ulong binlog_stmt_cache_use, binlog_stmt_cache_disk_use;
extern ulonglong binlog_bytes_written;
extern ulonglong relay_log_bytes_written;
extern ulong aborted_threads,aborted_connects;
extern ulong delayed_insert_timeout;
extern ulong delayed_insert_limit, delayed_queue_size;
extern ulong delayed_insert_threads, delayed_insert_writes;
extern ulong delayed_rows_in_use,delayed_insert_errors;
extern int32 slave_open_temp_tables;
extern ulong query_cache_size, query_cache_min_res_unit;
extern ulong slow_launch_threads, slow_launch_time;
extern ulong table_cache_size, table_def_size;
extern ulong table_cache_size_per_instance, table_cache_instances;
extern MYSQL_PLUGIN_IMPORT ulong max_connections;
extern ulong max_digest_length;
extern ulong max_connect_errors, connect_timeout;
extern my_bool opt_slave_allow_batching;
extern my_bool allow_slave_start;
extern LEX_CSTRING reason_slave_blocked;
extern ulong slave_trans_retries;
extern uint  slave_net_timeout;
extern ulong opt_mts_slave_parallel_workers;
extern ulonglong opt_mts_pending_jobs_size_max;
extern uint max_user_connections;
extern ulong rpl_stop_slave_timeout;
extern my_bool log_bin_use_v1_row_events;
extern ulong what_to_log,flush_time;
extern ulong max_prepared_stmt_count, prepared_stmt_count;
extern ulong open_files_limit;
extern ulong binlog_cache_size, binlog_stmt_cache_size;
extern ulonglong max_binlog_cache_size, max_binlog_stmt_cache_size;
extern ulong max_binlog_size, max_relay_log_size;
extern ulong slave_max_allowed_packet;
extern ulong opt_binlog_rows_event_max_size;
extern bool opt_log_only_query_comments;
extern bool opt_log_column_names;
extern ulong binlog_checksum_options;
extern const char *binlog_checksum_type_names[];
extern my_bool opt_master_verify_checksum;
extern my_bool opt_slave_sql_verify_checksum;
extern my_bool enforce_gtid_consistency;
extern my_bool binlog_gtid_simple_recovery;
extern ulong binlog_error_action;
enum enum_binlog_error_action
{
  /// Ignore the error and let server continue without binlogging
  IGNORE_ERROR= 0,
  /// Abort the server
  ABORT_SERVER= 1
};
extern const char *binlog_error_action_list[];
extern my_bool log_gtid_unsafe_statements;
extern char *mysqld_socket_umask;
extern my_bool is_slave;
extern my_bool read_only_slave;
extern ulong sql_slave_skip_counter_usage;
extern ulonglong rbr_unsafe_queries;

enum enum_gtid_mode
{
  /// Support only anonymous groups, not GTIDs.
  GTID_MODE_OFF= 0,
  /// Support both GTIDs and anonymous groups; generate anonymous groups.
  GTID_MODE_UPGRADE_STEP_1= 1,
  /// Support both GTIDs and anonymous groups; generate GTIDs.
  GTID_MODE_UPGRADE_STEP_2= 2,
  /// Support only GTIDs, not anonymous groups.
  GTID_MODE_ON= 3
};
extern ulong gtid_mode;
extern bool enable_gtid_mode_on_new_slave_with_old_master;
extern const char *gtid_mode_names[];
extern TYPELIB gtid_mode_typelib;

extern ulong max_blocked_pthreads;
extern ulong stored_program_cache_size;
extern ulong back_log;
extern char language[FN_REFLEN];
extern "C" MYSQL_PLUGIN_IMPORT ulong server_id;
extern ulong concurrency;
extern time_t server_start_time, flush_status_time;
extern char *opt_mysql_tmpdir, mysql_charsets_dir[];
extern int mysql_unpacked_real_data_home_len;
extern MYSQL_PLUGIN_IMPORT MY_TMPDIR mysql_tmpdir_list;
extern const char *first_keyword, *delayed_user, *binary_keyword;
extern MYSQL_PLUGIN_IMPORT const char  *my_localhost;
extern MYSQL_PLUGIN_IMPORT const char **errmesg;			/* Error messages */
extern const char *myisam_recover_options_str;
extern const char *in_left_expr_name, *in_additional_cond, *in_having_cond;
extern SHOW_VAR status_vars[];
extern struct system_variables max_system_variables;
extern struct system_status_var global_status_var;
extern struct rand_struct sql_rand;
extern const char *opt_date_time_formats[];
extern handlerton *partition_hton;
extern handlerton *myisam_hton;
extern handlerton *heap_hton;
extern uint opt_server_id_bits;
extern ulong opt_server_id_mask;
#ifdef WITH_NDBCLUSTER_STORAGE_ENGINE
/* engine specific hook, to be made generic */
extern int(*ndb_wait_setup_func)(ulong);
extern ulong opt_ndb_wait_setup;
#endif
extern const char *load_default_groups[];
extern struct my_option my_long_options[];
extern struct my_option my_long_early_options[];
int handle_early_options(my_bool logging);
void adjust_related_options(ulong *requested_open_files);
extern int mysqld_server_started;
extern "C" MYSQL_PLUGIN_IMPORT int orig_argc;
extern "C" MYSQL_PLUGIN_IMPORT char **orig_argv;
extern pthread_attr_t connection_attrib;
extern MYSQL_FILE *bootstrap_file;
extern my_bool old_mode;
extern my_bool avoid_temporal_upgrade;
extern LEX_STRING opt_init_connect, opt_init_slave;
extern int bootstrap_error;
extern char err_shared_dir[];
extern TYPELIB thread_handling_typelib;
extern my_decimal decimal_zero;
extern ulong connection_errors_select;
extern ulong connection_errors_accept;
extern ulong connection_errors_tcpwrap;
extern ulong connection_errors_internal;
extern ulong connection_errors_max_connection;
extern ulong connection_errors_peer_addr;
extern ulong connection_errors_net_ER_NET_ERROR_ON_WRITE;
extern ulong connection_errors_net_ER_NET_PACKETS_OUT_OF_ORDER;
extern ulong connection_errors_net_ER_NET_PACKET_TOO_LARGE;
extern ulong connection_errors_net_ER_NET_READ_ERROR;
extern ulong connection_errors_net_ER_NET_READ_INTERRUPTED;
extern ulong connection_errors_net_ER_NET_UNCOMPRESS_ERROR;
extern ulong connection_errors_net_ER_NET_WRITE_INTERRUPTED;
extern ulong log_warnings;
extern uint opt_general_query_throttling_limit;
extern uint opt_write_query_throttling_limit;
extern ulonglong read_queries, write_queries;
extern ulonglong total_query_rejected, write_query_rejected;
extern int32 write_query_running;
extern my_atomic_rwlock_t write_query_running_lock;
extern ulonglong tmp_table_rpl_max_file_size;
extern ulonglong tmp_table_bytes_written;

/** The size of the host_cache. */
extern uint host_cache_size;
void init_sql_statement_names();

/* Enable logging queries to a unix local datagram socket */
extern my_bool log_datagram;
extern ulong log_datagram_usecs;
extern int log_datagram_sock;

/* flashcache */
extern int cachedev_fd;

/*
  THR_MALLOC is a key which will be used to set/get MEM_ROOT** for a thread,
  using my_pthread_setspecific_ptr()/my_thread_getspecific_ptr().
*/
extern pthread_key(MEM_ROOT**,THR_MALLOC);

#ifdef HAVE_PSI_INTERFACE
#ifdef HAVE_MMAP
extern PSI_mutex_key key_PAGE_lock, key_LOCK_sync, key_LOCK_active,
       key_LOCK_pool;
#endif /* HAVE_MMAP */

#ifdef HAVE_OPENSSL
extern PSI_mutex_key key_LOCK_des_key_file;
extern PSI_rwlock_key key_rwlock_LOCK_use_ssl;
#endif

extern PSI_mutex_key key_BINLOG_LOCK_commit;
extern PSI_mutex_key key_BINLOG_LOCK_commit_queue;
extern PSI_mutex_key key_BINLOG_LOCK_semisync;
extern PSI_mutex_key key_BINLOG_LOCK_semisync_queue;
extern PSI_mutex_key key_BINLOG_LOCK_done;
extern PSI_mutex_key key_BINLOG_LOCK_flush_queue;
extern PSI_mutex_key key_BINLOG_LOCK_index;
extern PSI_mutex_key key_BINLOG_LOCK_log;
extern PSI_mutex_key key_BINLOG_LOCK_sync;
extern PSI_mutex_key key_BINLOG_LOCK_sync_queue;
extern PSI_mutex_key key_BINLOG_LOCK_xids;
extern PSI_mutex_key key_BINLOG_LOCK_binlog_end_pos;
extern PSI_mutex_key
  key_delayed_insert_mutex, key_hash_filo_lock, key_LOCK_active_mi,
  key_LOCK_connection_count, key_LOCK_crypt, key_LOCK_delayed_create,
  key_LOCK_delayed_insert, key_LOCK_delayed_status, key_LOCK_error_log,
  key_LOCK_gdl, key_LOCK_global_system_variables,
  key_LOCK_lock_db, key_LOCK_logger, key_LOCK_manager,
  key_LOCK_prepared_stmt_count,
  key_LOCK_sql_slave_skip_counter,
  key_LOCK_slave_net_timeout,
  key_LOCK_server_started, key_LOCK_status,
  key_LOCK_table_share, key_LOCK_thd_data,
  key_LOCK_thd_db_read_only_hash,
  key_LOCK_user_conn, key_LOCK_uuid_generator, key_LOG_LOCK_log,
  key_master_info_data_lock, key_master_info_run_lock,
  key_master_info_sleep_lock, key_master_info_thd_lock,
  key_mutex_slave_reporting_capability_err_lock, key_relay_log_info_data_lock,
  key_relay_log_info_sleep_lock, key_relay_log_info_thd_lock,
  key_relay_log_info_log_space_lock, key_relay_log_info_run_lock,
  key_mutex_slave_parallel_pend_jobs, key_mutex_mts_temp_tables_lock,
  key_mutex_slave_parallel_worker,
  key_structure_guard_mutex, key_TABLE_SHARE_LOCK_ha_data,
  key_LOCK_error_messages, key_LOCK_thread_count, key_LOCK_thd_remove,
  key_LOCK_global_table_stats,
  key_LOCK_log_throttle_qni,
  key_gtid_info_run_lock,
  key_gtid_info_data_lock,
  key_gtid_info_sleep_lock,
  key_gtid_info_thd_lock;
extern PSI_mutex_key key_RELAYLOG_LOCK_commit;
extern PSI_mutex_key key_RELAYLOG_LOCK_commit_queue;
extern PSI_mutex_key key_RELAYLOG_LOCK_semisync;
extern PSI_mutex_key key_RELAYLOG_LOCK_semisync_queue;
extern PSI_mutex_key key_RELAYLOG_LOCK_done;
extern PSI_mutex_key key_RELAYLOG_LOCK_flush_queue;
extern PSI_mutex_key key_RELAYLOG_LOCK_index;
extern PSI_mutex_key key_RELAYLOG_LOCK_log;
extern PSI_mutex_key key_RELAYLOG_LOCK_sync;
extern PSI_mutex_key key_RELAYLOG_LOCK_sync_queue;
extern PSI_mutex_key key_RELAYLOG_LOCK_xids;
extern PSI_mutex_key key_RELAYLOG_LOCK_binlog_end_pos;
extern PSI_mutex_key key_LOCK_sql_rand;
extern PSI_mutex_key key_gtid_ensure_index_mutex;
extern PSI_mutex_key key_LOCK_thread_created;

extern PSI_rwlock_key key_rwlock_LOCK_grant, key_rwlock_LOCK_logger,
  key_rwlock_LOCK_sys_init_connect, key_rwlock_LOCK_sys_init_slave,
  key_rwlock_LOCK_system_variables_hash, key_rwlock_query_cache_query_lock,
  key_rwlock_global_sid_lock;

#ifdef HAVE_MMAP
extern PSI_cond_key key_PAGE_cond, key_COND_active, key_COND_pool;
#endif /* HAVE_MMAP */

extern PSI_cond_key key_BINLOG_update_cond,
  key_COND_cache_status_changed, key_COND_manager,
  key_COND_server_started,
  key_delayed_insert_cond, key_delayed_insert_cond_client,
  key_item_func_sleep_cond, key_master_info_data_cond,
  key_master_info_start_cond, key_master_info_stop_cond,
  key_master_info_sleep_cond,
  key_relay_log_info_data_cond, key_relay_log_info_log_space_cond,
  key_relay_log_info_start_cond, key_relay_log_info_stop_cond,
  key_relay_log_info_sleep_cond, key_cond_slave_parallel_pend_jobs,
  key_cond_slave_parallel_worker,
  key_TABLE_SHARE_cond, key_user_level_lock_cond,
  key_COND_thread_count, key_COND_thread_cache, key_COND_flush_thread_cache,
  key_gtid_info_data_cond, key_gtid_info_start_cond, key_gtid_info_stop_cond,
  key_gtid_info_sleep_cond;
extern PSI_cond_key key_BINLOG_COND_done;
extern PSI_cond_key key_RELAYLOG_COND_done;
extern PSI_cond_key key_RELAYLOG_update_cond;
extern PSI_cond_key key_BINLOG_prep_xids_cond;
extern PSI_cond_key key_RELAYLOG_prep_xids_cond;
extern PSI_cond_key key_gtid_ensure_index_cond;

extern PSI_thread_key key_thread_bootstrap, key_thread_delayed_insert,
  key_thread_handle_manager, key_thread_kill_server, key_thread_main,
  key_thread_one_connection, key_thread_signal_hand;

#ifdef HAVE_MMAP
extern PSI_file_key key_file_map;
#endif /* HAVE_MMAP */

extern PSI_file_key key_file_binlog, key_file_binlog_index, key_file_casetest,
  key_file_dbopt, key_file_des_key_file, key_file_ERRMSG, key_select_to_file,
  key_file_fileparser, key_file_frm, key_file_global_ddl_log, key_file_load,
  key_file_loadfile, key_file_log_event_data, key_file_log_event_info,
  key_file_master_info, key_file_misc, key_file_partition,
  key_file_pid, key_file_relay_log_info, key_file_send_file, key_file_tclog,
  key_file_trg, key_file_trn, key_file_init;
extern PSI_file_key key_file_query_log, key_file_slow_log;
extern PSI_file_key key_file_relaylog, key_file_relaylog_index;
extern PSI_socket_key key_socket_tcpip, key_socket_unix, key_socket_client_connection;

void init_server_psi_keys();
#endif /* HAVE_PSI_INTERFACE */
bool setup_datagram_socket(sys_var *self, THD *thd, enum_var_type type);
bool init_ssl();
void end_ssl();

/*
  MAINTAINER: Please keep this list in order, to limit merge collisions.
  Hint: grep PSI_stage_info | sort -u
*/
extern PSI_stage_info stage_after_create;
extern PSI_stage_info stage_allocating_local_table;
extern PSI_stage_info stage_alter_inplace_prepare;
extern PSI_stage_info stage_alter_inplace;
extern PSI_stage_info stage_alter_inplace_commit;
extern PSI_stage_info stage_changing_master;
extern PSI_stage_info stage_checking_master_version;
extern PSI_stage_info stage_checking_permissions;
extern PSI_stage_info stage_checking_privileges_on_cached_query;
extern PSI_stage_info stage_checking_query_cache_for_query;
extern PSI_stage_info stage_cleaning_up;
extern PSI_stage_info stage_closing_tables;
extern PSI_stage_info stage_connecting_to_master;
extern PSI_stage_info stage_converting_heap_to_myisam;
extern PSI_stage_info stage_copying_to_group_table;
extern PSI_stage_info stage_copying_to_tmp_table;
extern PSI_stage_info stage_copy_to_tmp_table;
extern PSI_stage_info stage_creating_delayed_handler;
extern PSI_stage_info stage_creating_sort_index;
extern PSI_stage_info stage_creating_table;
extern PSI_stage_info stage_creating_tmp_table;
extern PSI_stage_info stage_deleting_from_main_table;
extern PSI_stage_info stage_deleting_from_reference_tables;
extern PSI_stage_info stage_discard_or_import_tablespace;
extern PSI_stage_info stage_end;
extern PSI_stage_info stage_executing;
extern PSI_stage_info stage_execution_of_init_command;
extern PSI_stage_info stage_explaining;
extern PSI_stage_info stage_finished_reading_one_binlog_switching_to_next_binlog;
extern PSI_stage_info stage_flushing_relay_log_and_master_info_repository;
extern PSI_stage_info stage_flushing_relay_log_info_file;
extern PSI_stage_info stage_freeing_items;
extern PSI_stage_info stage_fulltext_initialization;
extern PSI_stage_info stage_got_handler_lock;
extern PSI_stage_info stage_got_old_table;
extern PSI_stage_info stage_init;
extern PSI_stage_info stage_insert;
extern PSI_stage_info stage_invalidating_query_cache_entries_table;
extern PSI_stage_info stage_invalidating_query_cache_entries_table_list;
extern PSI_stage_info stage_killing_slave;
extern PSI_stage_info stage_logging_slow_query;
extern PSI_stage_info stage_making_temp_file_append_before_load_data;
extern PSI_stage_info stage_making_temp_file_create_before_load_data;
extern PSI_stage_info stage_manage_keys;
extern PSI_stage_info stage_master_has_sent_all_binlog_to_slave;
extern PSI_stage_info stage_opening_tables;
extern PSI_stage_info stage_optimizing;
extern PSI_stage_info stage_preparing;
extern PSI_stage_info stage_purging_old_relay_logs;
extern PSI_stage_info stage_query_end;
extern PSI_stage_info stage_queueing_master_event_to_the_relay_log;
extern PSI_stage_info stage_reading_event_from_the_relay_log;
extern PSI_stage_info stage_registering_slave_on_master;
extern PSI_stage_info stage_removing_duplicates;
extern PSI_stage_info stage_removing_tmp_table;
extern PSI_stage_info stage_rename;
extern PSI_stage_info stage_rename_result_table;
extern PSI_stage_info stage_requesting_binlog_dump;
extern PSI_stage_info stage_reschedule;
extern PSI_stage_info stage_searching_rows_for_update;
extern PSI_stage_info stage_sending_binlog_event_to_slave;
extern PSI_stage_info stage_sending_cached_result_to_client;
extern PSI_stage_info stage_sending_data;
extern PSI_stage_info stage_setup;
extern PSI_stage_info stage_slave_has_read_all_relay_log;
extern PSI_stage_info stage_sorting_for_group;
extern PSI_stage_info stage_sorting_for_order;
extern PSI_stage_info stage_sorting_result;
extern PSI_stage_info stage_sql_thd_waiting_until_delay;
extern PSI_stage_info stage_statistics;
extern PSI_stage_info stage_storing_result_in_query_cache;
extern PSI_stage_info stage_storing_row_into_queue;
extern PSI_stage_info stage_system_lock;
extern PSI_stage_info stage_update;
extern PSI_stage_info stage_updating;
extern PSI_stage_info stage_updating_main_table;
extern PSI_stage_info stage_updating_reference_tables;
extern PSI_stage_info stage_upgrading_lock;
extern PSI_stage_info stage_user_lock;
extern PSI_stage_info stage_user_sleep;
extern PSI_stage_info stage_verifying_table;
extern PSI_stage_info stage_waiting_for_commit;
extern PSI_stage_info stage_waiting_for_delay_list;
extern PSI_stage_info stage_waiting_for_gtid_to_be_written_to_binary_log;
extern PSI_stage_info stage_waiting_for_handler_insert;
extern PSI_stage_info stage_waiting_for_handler_lock;
extern PSI_stage_info stage_waiting_for_handler_open;
extern PSI_stage_info stage_waiting_for_insert;
extern PSI_stage_info stage_waiting_for_master_to_send_event;
extern PSI_stage_info stage_waiting_for_master_update;
extern PSI_stage_info stage_waiting_for_relay_log_space;
extern PSI_stage_info stage_waiting_for_slave_mutex_on_exit;
extern PSI_stage_info stage_waiting_for_slave_thread_to_start;
extern PSI_stage_info stage_waiting_for_query_cache_lock;
extern PSI_stage_info stage_waiting_for_table_flush;
extern PSI_stage_info stage_waiting_for_the_next_event_in_relay_log;
extern PSI_stage_info stage_waiting_for_the_slave_thread_to_advance_position;
extern PSI_stage_info stage_waiting_to_finalize_termination;
extern PSI_stage_info stage_waiting_to_get_readlock;
extern PSI_stage_info stage_slave_waiting_worker_to_release_partition;
extern PSI_stage_info stage_slave_waiting_worker_to_free_events;
extern PSI_stage_info stage_slave_waiting_worker_queue;
extern PSI_stage_info stage_slave_waiting_event_from_coordinator;
extern PSI_stage_info stage_slave_waiting_workers_to_exit;
#ifdef HAVE_PSI_STATEMENT_INTERFACE
/**
  Statement instrumentation keys (sql).
  The last entry, at [SQLCOM_END], is for parsing errors.
*/
extern PSI_statement_info sql_statement_info[(uint) SQLCOM_END + 1];

/**
  Statement instrumentation keys (com).
  The last entry, at [COM_END], is for packet errors.
*/
extern PSI_statement_info com_statement_info[(uint) COM_END + 1];

/**
  Statement instrumentation key for replication.
*/
extern PSI_statement_info stmt_info_rpl;

void init_sql_statement_info();
void init_com_statement_info();
#endif /* HAVE_PSI_STATEMENT_INTERFACE */

#ifndef __WIN__
extern pthread_t signal_thread;
#endif

#ifdef HAVE_OPENSSL
extern struct st_VioSSLFd * ssl_acceptor_fd;
#endif /* HAVE_OPENSSL */

/*
  The following variables were under INNODB_COMPABILITY_HOOKS
 */
extern my_bool opt_large_pages;
extern uint opt_large_page_size;
extern char lc_messages_dir[FN_REFLEN];
extern char *lc_messages_dir_ptr, *log_error_file_ptr;
extern MYSQL_PLUGIN_IMPORT char reg_ext[FN_EXTLEN];
extern MYSQL_PLUGIN_IMPORT uint reg_ext_length;
extern MYSQL_PLUGIN_IMPORT uint lower_case_table_names;
extern MYSQL_PLUGIN_IMPORT bool mysqld_embedded;
extern ulong specialflag;
extern uint mysql_data_home_len;
extern uint mysql_real_data_home_len;
extern const char *mysql_real_data_home_ptr;
extern ulong thread_handling;
extern MYSQL_PLUGIN_IMPORT char  *mysql_data_home;
extern "C" MYSQL_PLUGIN_IMPORT char server_version[SERVER_VERSION_LENGTH];
extern MYSQL_PLUGIN_IMPORT char mysql_real_data_home[];
extern char mysql_unpacked_real_data_home[];
extern MYSQL_PLUGIN_IMPORT struct system_variables global_system_variables;
extern char default_logfile_name[FN_REFLEN];

#define mysql_tmpdir (my_tmpdir(&mysql_tmpdir_list))

/* Time handling client commands for replication */
extern ulonglong command_slave_seconds;

extern MYSQL_PLUGIN_IMPORT const key_map key_map_empty;
extern MYSQL_PLUGIN_IMPORT key_map key_map_full;          /* Should be threaded as const */

/*
  Server mutex locks and condition variables.
 */
extern mysql_mutex_t
       LOCK_user_locks, LOCK_status,
       LOCK_error_log, LOCK_delayed_insert, LOCK_uuid_generator,
       LOCK_delayed_status, LOCK_delayed_create, LOCK_crypt, LOCK_timezone,
       LOCK_slave_list, LOCK_active_mi, LOCK_manager,
       LOCK_global_system_variables, LOCK_user_conn, LOCK_log_throttle_qni,
       LOCK_prepared_stmt_count, LOCK_error_messages, LOCK_connection_count,
       LOCK_sql_slave_skip_counter, LOCK_slave_net_timeout;
#ifdef HAVE_OPENSSL
extern mysql_mutex_t LOCK_des_key_file;
extern mysql_rwlock_t LOCK_use_ssl;
#endif
extern mysql_mutex_t LOCK_server_started;
extern mysql_cond_t COND_server_started;
extern mysql_rwlock_t LOCK_grant, LOCK_sys_init_connect, LOCK_sys_init_slave;
extern mysql_rwlock_t LOCK_system_variables_hash;
extern mysql_cond_t COND_manager;
extern int32 num_thread_running;
extern my_atomic_rwlock_t thread_running_lock;
extern my_atomic_rwlock_t slave_open_temp_tables_lock;

extern my_bool opt_use_ssl;
extern char *opt_ssl_ca, *opt_ssl_capath, *opt_ssl_cert, *opt_ssl_cipher,
            *opt_ssl_key, *opt_ssl_crl, *opt_ssl_crlpath;

extern MYSQL_PLUGIN_IMPORT pthread_key(THD*, THR_THD);

/**
  only options that need special treatment in get_one_option() deserve
  to be listed below
*/
enum options_mysqld
{
  OPT_to_set_the_start_number=256,
  OPT_BIND_ADDRESS,
  OPT_BINLOG_CHECKSUM,
  OPT_BINLOG_DO_DB,
  OPT_BINLOG_FORMAT,
  OPT_BINLOG_IGNORE_DB,
  OPT_BIN_LOG,
  OPT_BINLOGGING_IMPOSSIBLE_MODE,
  OPT_SIMPLIFIED_BINLOG_GTID_RECOVERY,
  OPT_BOOTSTRAP,
  OPT_CONSOLE,
  OPT_DEBUG_SYNC_TIMEOUT,
  OPT_DELAY_KEY_WRITE_ALL,
  OPT_DISABLE_WORKING_SET_SIZE,
  OPT_ISAM_LOG,
  OPT_IGNORE_DB_DIRECTORY,
  OPT_KEY_BUFFER_SIZE,
  OPT_KEY_CACHE_AGE_THRESHOLD,
  OPT_KEY_CACHE_BLOCK_SIZE,
  OPT_KEY_CACHE_DIVISION_LIMIT,
  OPT_LC_MESSAGES_DIRECTORY,
  OPT_LOWER_CASE_TABLE_NAMES,
  OPT_MASTER_RETRY_COUNT,
  OPT_MASTER_VERIFY_CHECKSUM,
  OPT_POOL_OF_THREADS,
  OPT_REPLICATE_DO_DB,
  OPT_REPLICATE_DO_TABLE,
  OPT_REPLICATE_IGNORE_DB,
  OPT_REPLICATE_IGNORE_TABLE,
  OPT_REPLICATE_REWRITE_DB,
  OPT_REPLICATE_WILD_DO_TABLE,
  OPT_REPLICATE_WILD_IGNORE_TABLE,
  OPT_SERVER_ID,
  OPT_SKIP_HOST_CACHE,
  OPT_SKIP_LOCK,
  OPT_SKIP_NEW,
  OPT_SKIP_RESOLVE,
  OPT_SKIP_STACK_TRACE,
  OPT_SKIP_SYMLINKS,
  OPT_SRV_FATAL_SEMAPHORE_TIMEOUT,
  OPT_SLAVE_SQL_VERIFY_CHECKSUM,
  OPT_SSL_CA,
  OPT_SSL_CAPATH,
  OPT_SSL_CERT,
  OPT_SSL_CIPHER,
  OPT_SSL_KEY,
  OPT_THREAD_CONCURRENCY,
  OPT_UPDATE_LOG,
  OPT_WANT_CORE,
  OPT_ENGINE_CONDITION_PUSHDOWN,
  OPT_LOG_ERROR,
  OPT_MAX_LONG_DATA_SIZE,
  OPT_PLUGIN_LOAD,
  OPT_PLUGIN_LOAD_ADD,
  OPT_SSL_CRL,
  OPT_SSL_CRLPATH,
  OPT_PFS_INSTRUMENT,
  OPT_DEFAULT_AUTH,
  OPT_SECURE_AUTH,
  OPT_THREAD_CACHE_SIZE,
  OPT_HOST_CACHE_SIZE,
  OPT_TABLE_DEFINITION_CACHE,
  OPT_AVOID_TEMPORAL_UPGRADE,
  OPT_SHOW_OLD_TEMPORALS,
  OPT_LOG_SLOW_EXTRA,
  OPT_SLOW_LOG_IF_ROWS_EXAMINED_EXCEED,
  OPT_PROCESS_CAN_DISABLE_BIN_LOG,
};


/**
   Query type constants (usable as bitmap flags).
*/
enum enum_query_type
{
  /// Nothing specific, ordinary SQL query.
  QT_ORDINARY= 0,
  /// In utf8.
  QT_TO_SYSTEM_CHARSET= (1 << 0),
  /// Without character set introducers.
  QT_WITHOUT_INTRODUCERS= (1 << 1),
  /// When printing a SELECT, add its number (select_lex->number)
  QT_SHOW_SELECT_NUMBER= (1 << 2),
  /// Don't print a database if it's equal to the connection's database
  QT_NO_DEFAULT_DB= (1 << 3),
  /// When printing a derived table, don't print its expression, only alias
  QT_DERIVED_TABLE_ONLY_ALIAS= (1 << 4)
};

/* query_id */
typedef int64 query_id_t;
extern query_id_t global_query_id;
extern my_atomic_rwlock_t global_query_id_lock;

void unireg_end(void) __attribute__((noreturn));

/* increment query_id and return it.  */
inline __attribute__((warn_unused_result)) query_id_t next_query_id()
{
  query_id_t id;
  my_atomic_rwlock_wrlock(&global_query_id_lock);
  id= my_atomic_add64(&global_query_id, 1);
  my_atomic_rwlock_wrunlock(&global_query_id_lock);
  return (id+1);
}

/*
  TODO: Replace this with an inline function.
 */
#ifndef EMBEDDED_LIBRARY
extern "C" void unireg_abort(int exit_code) __attribute__((noreturn));
#else
extern "C" void unireg_clear(int exit_code);
#define unireg_abort(exit_code) do { unireg_clear(exit_code); DBUG_RETURN(exit_code); } while(0)
#endif

inline void table_case_convert(char * name, uint length)
{
  if (lower_case_table_names)
    files_charset_info->cset->casedn(files_charset_info,
                                     name, length, name, length);
}

ulong sql_rnd_with_mutex();

extern int32 num_thread_running;
inline int32
inc_thread_running()
{
  int32 num_threads;
  my_atomic_rwlock_wrlock(&thread_running_lock);
  num_threads= my_atomic_add32(&num_thread_running, 1);
  my_atomic_rwlock_wrunlock(&thread_running_lock);
  return (num_threads+1);
}

inline int32
dec_thread_running()
{
  int32 num_threads;
  my_atomic_rwlock_wrlock(&thread_running_lock);
  num_threads= my_atomic_add32(&num_thread_running, -1);
  my_atomic_rwlock_wrunlock(&thread_running_lock);
  return (num_threads-1);
}

inline int32
get_thread_running()
{
  int32 num_threads;
  my_atomic_rwlock_rdlock(&thread_running_lock);
  num_threads= my_atomic_load32(&num_thread_running);
  my_atomic_rwlock_rdunlock(&thread_running_lock);
  return num_threads;
}

inline int32
get_write_query_running()
{
  int32 num_writes_running;
  my_atomic_rwlock_rdlock(&write_query_running_lock);
  num_writes_running= my_atomic_load32(&write_query_running);
  my_atomic_rwlock_rdunlock(&write_query_running_lock);
  return (num_writes_running);
}

inline int32
inc_write_query_running()
{
  int32 num_writes_running;
  my_atomic_rwlock_wrlock(&write_query_running_lock);
  num_writes_running= my_atomic_add32(&write_query_running, 1);
  my_atomic_rwlock_wrunlock(&write_query_running_lock);
  return (num_writes_running+1);
}

inline int32
dec_write_query_running()
{
  int32 num_writes_running;
  my_atomic_rwlock_wrlock(&write_query_running_lock);
  num_writes_running= my_atomic_add32(&write_query_running, -1);
  my_atomic_rwlock_wrunlock(&write_query_running_lock);
  return (num_writes_running-1);
}

#if defined(MYSQL_DYNAMIC_PLUGIN) && defined(_WIN32)
extern "C" THD *_current_thd_noinline();
#define _current_thd() _current_thd_noinline()
#else
/*
  THR_THD is a key which will be used to set/get THD* for a thread,
  using my_pthread_setspecific_ptr()/my_thread_getspecific_ptr().
*/
extern pthread_key(THD*, THR_THD);
inline THD *_current_thd(void)
{
  return my_pthread_getspecific_ptr(THD*,THR_THD);
}
#endif
#define current_thd _current_thd()

extern const char *MY_BIND_ALL_ADDRESSES;

/*
  Implementation of a Substitution Box (S-Box) hash using 256 values
  Ideal for use in generating uniform hashes (CRC32 is very unsuitable
  for use as a uniform hash)
*/
uint32 my_sbox_hash(const uchar* data, ulong length);

#endif /* MYSQLD_INCLUDED */
