#include "column_statistics_dt.h"
#include "mysqld.h"
#include "sql_base.h"
#include "sql_parse.h"
#include "sql_show.h"
#include "sql_string.h"
#include "sql_stats.h"
#include "structs.h"
#include "tztime.h"                             // struct Time_zone
#include "rpl_master.h"                         // get_current_replication_lag
#include <mysql/plugin_rim.h>
#include <handler.h>

/* Global map to track the number of active identical sql statements */
static std::unordered_map<md5_key, uint> global_active_sql;

static bool mt_lock(mysql_mutex_t *mutex)
{
/*
  In debug mode safe_mutex is turned on, and
  PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP is ignored.

  However, safe_mutex contains information about the thread ID
  which we can use to determine if we are re-locking the calling thread.

  In release builds pthread_mutex_t is used, which respects the
  PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP property.
*/
#if defined(SAFE_MUTEX) && !defined(DBUG_OFF)
  if (!pthread_equal(pthread_self(),
                     (&(mutex)->m_mutex)->thread))
  {
    mysql_mutex_lock(mutex);
    return false;
  }

  return true;
#else
  return mysql_mutex_lock(mutex) == EDEADLK;
#endif
}

static void mt_unlock(bool acquired, mysql_mutex_t *mutex)
{
  /* If lock was already acquired by calling thread, do nothing. */
  if (acquired)
    return;

  /* Otherwise, unlock the mutex */
  mysql_mutex_unlock(mutex);
}

/***********************************************************************
 Begin - Functions to support capping the number of duplicate executions
************************************************************************/
/*
  free_global_active_sql
    Frees global_active_sql
*/
void free_global_active_sql(void)
{
  bool lock_acquired = mt_lock(&LOCK_global_active_sql);

  global_active_sql.clear();

  mt_unlock(lock_acquired, &LOCK_global_active_sql);
}

/*
  skip_quote
  Skips over quoted strings
  It handles both single and double quotes by calling
  it twice by passing the quote character as argument
  first time single quote and second as double quote.
  Skipping happens by changing the offset of the next
  comment search (position of the second quote+1).
  It handles both cases when the string includes the
  begining of a comment or not
*/
static bool skip_quote(
    std::string& query_str,
    size_t      *offset,
    size_t       c_start_pos,
    const char  *quote)
{
  size_t quote_pos = query_str.find(quote, *offset);

  if (quote_pos != std::string::npos &&
      quote_pos < c_start_pos)
  {
    quote_pos = query_str.find(quote, quote_pos+1);
    *offset   = quote_pos+1;
    return true;
  }
  return false;
}

/*
  strip_query_comment
    Return the query text without comments

  Examples (where begin comment is /+ and end comment is +/)
   IN: /+C1+/ select '''Q2''', '''/+''', """+/""" /+C2+/ from dual /+C3+/
   OUT:  select '''Q2''', '''/+''', """+/"""  from dual
   IN: /+C1+/ select 'Q2', '/+', "+/" /+C2+/ from dual /+C3+/
   OUT:  select 'Q2', '/+', "+/"  from dual
*/
static void strip_query_comments(std::string& query_str)
{
  const uint comment_size = 2;
  size_t offset = 0;
  while (true)
  {
    size_t c_start_pos = query_str.find("/*", offset);

    if (c_start_pos == std::string::npos)
      break;

    // so far we found a start of a comment; next we
    // check if it is enclosed in a string i.e either
    // single or double quoted
    if (skip_quote(query_str, &offset, c_start_pos, "'"))
      continue;

    if (skip_quote(query_str, &offset, c_start_pos, "\""))
      continue;

    size_t c_stop_pos = query_str.find("*/", c_start_pos);
    DBUG_ASSERT(c_stop_pos != std::string::npos);

    query_str.erase(c_start_pos, c_stop_pos + comment_size - c_start_pos);
  }
}

/*
  register_active_sql
    Register a new active SQL, called right after SQL parsing

  Returns TRUE if the query is rejected
*/
bool register_active_sql(THD *thd, char *query_text,
                         uint query_length)
{
  /* Get the maximum number of duplicate executions from query attribute,
     connection attribute, or server variable
  */
  ulong max_dup_exe = thd->get_query_or_connect_attr_value("@@sql_max_dup_exe",
                                              sql_maximum_duplicate_executions,
                                              ULONG_MAX);
  ulong control     = sql_duplicate_executions_control;
  /* Exit now if any of the conditions that prevent the feature is true
     - feature is off
     - command type is not SELECT
     - EXPLAIN
     - in a transaction
  */
  if (max_dup_exe == 0                       ||
      control == CONTROL_LEVEL_OFF           ||
      thd->lex->sql_command != SQLCOM_SELECT ||
      thd->lex->describe                     ||
      thd->in_active_multi_stmt_transaction())
    return false;

  /* prepare a string with a size large enough to store the sql text,
     database, and some flags (that affect query similarity).
  */
  std::string query_str;
  query_str.reserve(query_length+thd->db_length+QUERY_CACHE_FLAGS_SIZE+1);

  /* load the sql text */
  query_str.append(query_text, query_length);

  /* strip the query comments (everywhere in the text) */
  strip_query_comments(query_str);

  /* load the database name */
  query_str.append((const char*) thd->db, thd->db_length);

  /* load the flags */
  Query_cache_query_flags flags = get_query_cache_flags(thd, false);
  query_str.append((const char*) &flags, QUERY_CACHE_FLAGS_SIZE);

  /* compute MD5 from the key value (query, db, flags) */
  md5_key sql_hash;
  compute_md5_hash((char*)sql_hash.data(), query_str.c_str(), query_str.length());

  // so far did not exceed the max number of dups
  bool rejected = false;

  bool lock_acquired = mt_lock(&LOCK_global_active_sql);
  auto iter = global_active_sql.find(sql_hash);
  if (iter == global_active_sql.end())
  {
    global_active_sql.emplace(sql_hash, 1); // its first occurrence
  }
  else
  {
    if (iter->second+1 > max_dup_exe)       // one too many
    {
      if (control == CONTROL_LEVEL_ERROR)   // reject it
      {
        rejected = true;
        my_error(ER_DUPLICATE_STATEMENT_EXECUTION, MYF(0));
      }
      else // control is NOTE or WARN => report note / warning
      {
        push_warning_printf(thd,
                            (control == CONTROL_LEVEL_NOTE) ?
                             Sql_condition::WARN_LEVEL_NOTE :
                             Sql_condition::WARN_LEVEL_WARN,
                            ER_DUPLICATE_STATEMENT_EXECUTION,
                            ER(ER_DUPLICATE_STATEMENT_EXECUTION));
      }
    }
    if (!rejected)
      iter->second++;    // increment the number of duplicates
  }

  if (!rejected)
  {
    // remember the sql_hash
    thd->mt_key_set(THD::SQL_HASH, sql_hash.data());
    DBUG_ASSERT(thd->mt_key_is_set(THD::SQL_HASH));
  }

  mt_unlock(lock_acquired, &LOCK_global_active_sql);
  return rejected;
}

/*
  remove_active_sql
    Remove an active SQL, called at end of the execution
*/
void remove_active_sql(THD *thd)
{
  if (!thd->mt_key_is_set(THD::SQL_HASH))
    return;

  bool lock_acquired = mt_lock(&LOCK_global_active_sql);

  auto iter = global_active_sql.find(thd->mt_key_value(THD::SQL_HASH));
  if (iter != global_active_sql.end())
  {
    if (iter->second == 1)
      global_active_sql.erase(iter);
    else
      iter->second--;
  }

  mt_unlock(lock_acquired, &LOCK_global_active_sql);
}

/*********************************************************************
 End - Functions to support capping the number of duplicate executions
**********************************************************************/

/*
  SQL_STATISTICS

  This provides statistics for a unique combination of
  sql statement, plan id, table schema and user.
*/
ST_FIELD_INFO sql_stats_fields_info[]=
{
  {"SQL_ID", MD5_BUFF_LENGTH, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"PLAN_ID", MD5_BUFF_LENGTH, MYSQL_TYPE_STRING, 0, MY_I_S_MAYBE_NULL, 0,
   SKIP_OPEN_TABLE},
  {"CLIENT_ID", MD5_BUFF_LENGTH, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"TABLE_SCHEMA", NAME_CHAR_LEN, MYSQL_TYPE_STRING,
      0, 0, 0, SKIP_OPEN_TABLE},
  {"USER_NAME", USERNAME_CHAR_LENGTH, MYSQL_TYPE_STRING,
      0, 0, 0, SKIP_OPEN_TABLE},
  {"QUERY_SAMPLE_TEXT", SQL_TEXT_COL_SIZE, MYSQL_TYPE_STRING,
      0, 0, 0, SKIP_OPEN_TABLE},
  {"QUERY_SAMPLE_SEEN", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_DATETIME,
      0, MY_I_S_MAYBE_NULL, 0, SKIP_OPEN_TABLE},

  {"EXECUTION_COUNT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},

  {"SKIPPED_COUNT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},

  {"ROWS_INSERTED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"ROWS_UPDATED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"ROWS_DELETED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"ROWS_READ", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"ROWS_SENT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},

  {"TOTAL_CPU", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},

  {"ELAPSED_TIME", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},

  {"TMP_TABLE_BYTES_WRITTEN", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},

  {"FILESORT_BYTES_WRITTEN", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},

  {"INDEX_DIVE_COUNT", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},

  {"INDEX_DIVE_CPU", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},

  {"COMPILATION_CPU", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},

  {"TMP_TABLE_DISK_USAGE", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},

  {"FILESORT_DISK_USAGE", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},

  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};

/*
  SQL_TEXT

  Associates a SQL Id with a normalized SQL text.
  Captures SQL type and the SQL text length attributes as well.
*/
ST_FIELD_INFO sql_text_fields_info[]=
{
  {"SQL_ID", MD5_BUFF_LENGTH, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"SQL_TYPE", 16, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"SQL_TEXT_LENGTH", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"SQL_TEXT", SQL_TEXT_COL_SIZE, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},

  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};

/*
  CLIENT_ATTRIBUTES

  Associates a client id with the attributes of that client. The attributes
  are displayed in JSON format.
*/
ST_FIELD_INFO client_attrs_fields_info[]=
{
  {"CLIENT_ID", 33, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"CLIENT_ATTRIBUTES", 4096, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};

/* Global sql stats hash maps to track and update metrics in-memory */
Sql_stats_maps global_sql_stats;

/* Snapshot of SQL stats. */
Sql_stats_maps sql_stats_snapshot;
extern mysql_rwlock_t LOCK_sql_stats_snapshot;
extern my_bool sql_stats_snapshot_status;

/*
  SQL_FINDINGS

  Associates a SQL ID with its findings (aka SQL conditions).
*/
const uint sf_max_level_size   =   64;          // max level text size
const uint sf_max_message_size =  256;        // max message text size
const uint sf_max_query_size   = 1024;          // max query text size
ST_FIELD_INFO sql_findings_fields_info[]=
{
  {"SQL_ID", MD5_BUFF_LENGTH, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"CODE", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"LEVEL", sf_max_level_size, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"MESSAGE", sf_max_message_size, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"QUERY_TEXT", sf_max_query_size, MYSQL_TYPE_MEDIUM_BLOB, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COUNT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"LAST_RECORDED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},

  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};

/* Global SQL findings map to track findings for all SQL statements */
std::unordered_map<md5_key, SQL_FINDING_VEC> global_sql_findings_map;


/*
  WRITE_STATISTICS

  Associates binlog bytes written and CPU write time to various
  dimensions such as user_id, client_id, sql_id, shard_id.
*/

ST_FIELD_INFO write_statistics_fields_info[]=
{
  {"TIMESTAMP", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_DATETIME, 0, 0, 0,
    SKIP_OPEN_TABLE},
  {"TYPE", 16, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"VALUE", MD5_BUFF_LENGTH, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"WRITE_DATA_BYTES", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
    0, 0, 0, SKIP_OPEN_TABLE},
  {"CPU_WRITE_TIME_MS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
    0, 0, 0, SKIP_OPEN_TABLE},
  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};

/*
  Map integer representatin of write stats dimensions to string
  constants. The integer value of these dimensions map to the
  index in TIME_BUCKET_STATS array
*/
const std::string WRITE_STATS_TYPE_STRING[] = {"USER", "CLIENT", "SHARD", "SQL_ID"};

typedef std::array<
  std::unordered_map<std::string, WRITE_STATS>,
  WRITE_STATISTICS_DIMENSION_COUNT
> TIME_BUCKET_STATS;

/* Global write statistics map */
std::list<std::pair<int, TIME_BUCKET_STATS>> global_write_statistics_map;

/*
  TRANSACTION_SIZE_HISTOGRAM

  Histogram of data written in binlogs by all transactions
*/

ST_FIELD_INFO tx_size_histogram_fields_info[]=
{
  {"DB", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"BUCKET_NUMBER", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"BUCKET_WRITE_LOW", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"BUCKET_WRITE_HIGH", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"COUNT_BUCKET", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"COUNT_BUCKET_AND_LOWER", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"BUCKET_QUANTILE", MAX_DOUBLE_STR_LENGTH, MYSQL_TYPE_DOUBLE,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};

/* Number of buckets in the histogram */
#define TX_SIZE_HIST_COUNT   20

/* histogram is represented as an array of counters (e.g 20 elements) */
typedef std::array<ulonglong, TX_SIZE_HIST_COUNT> TX_SIZE_HIST;
/* we need per database hence using a map */
std::unordered_map<std::string, TX_SIZE_HIST> global_tx_size_histogram;

/*
  WRITE_STATISTICS_HISTOGRAM

  Histogram for write throughput distribution
*/

ST_FIELD_INFO write_stat_histogram_fields_info[]=
{
  {"BUCKET_NUMBER", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"BUCKET_WRITE_LOW", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"BUCKET_WRITE_HIGH", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"COUNT_BUCKET", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"COUNT_BUCKET_AND_LOWER", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"BUCKET_QUANTILE", MAX_DOUBLE_STR_LENGTH, MYSQL_TYPE_DOUBLE,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};

#define WRITE_STAT_HIST_COUNT   20

/* histogram is represented as an array of counters */
std::array<ulonglong, WRITE_STAT_HIST_COUNT> global_write_stat_histogram;

/***********************************************************************
              Begin - Functions to support TRANSACTION_SIZE_HISTOGRAM
************************************************************************/
/*
  free_global_tx_size_histogram
   Free the global_tx_size_histogram map
*/
void free_global_tx_size_histogram(void)
{
  bool lock_acquired = mt_lock(&LOCK_global_tx_size_histogram);

  global_tx_size_histogram.clear();

  mt_unlock(lock_acquired, &LOCK_global_tx_size_histogram);
}

/*
  update_tx_size_histogram
   Updates the transaction_size_histogram with current txn info
*/

void update_tx_size_histogram(THD *thd)
{
  if (!thd->trx_bytes_written || !thd->db)
    return;

  ulong bucket_number = thd->trx_bytes_written
                      / (transaction_size_histogram_width*1024);
  if (bucket_number >= TX_SIZE_HIST_COUNT)
    bucket_number = TX_SIZE_HIST_COUNT-1;

  std::string db_name = thd->db;

  bool lock_acquired = mt_lock(&LOCK_global_tx_size_histogram);

  auto hist_iter = global_tx_size_histogram.find(db_name);
  /* check if this is the first time we record a txn for this db */
  if (hist_iter == global_tx_size_histogram.end())
  {
    TX_SIZE_HIST hist;
    hist.fill(0);
    hist[bucket_number] = 1;
    global_tx_size_histogram.insert(std::make_pair(db_name, hist));
  }
  else
  {
    hist_iter->second[bucket_number] ++;
  }

  mt_unlock(lock_acquired, &LOCK_global_tx_size_histogram);
}

/*
  fill_tx_size_histogram
    Fills the rows returned for statements selecting from
    TRANSACTION_SIZE_HISTOGRAM

*/
int fill_tx_size_histogram(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_tx_size_histogram");
  TABLE* table= tables->table;

  bool lock_acquired = mt_lock(&LOCK_global_tx_size_histogram);

  for (auto hist_iter= global_tx_size_histogram.cbegin();
       hist_iter != global_tx_size_histogram.cend(); ++hist_iter)
  {
    ulonglong rolling_count = 0, total_count = 0;
    const TX_SIZE_HIST & hist = hist_iter->second;

    for(int bucket_iter = 0;
        bucket_iter < TX_SIZE_HIST_COUNT;
        bucket_iter++)
      total_count += hist[bucket_iter];

    for(int bucket_iter = 0;
        bucket_iter < TX_SIZE_HIST_COUNT;
        bucket_iter++)
    {
      int f= 0;
      restore_record(table, s->default_values);

      /* DB */
      table->field[f++]->store(hist_iter->first.c_str(),
                               hist_iter->first.length(),
                               system_charset_info);

      /* BUCKET_NUMBER */
      table->field[f++]->store(bucket_iter, TRUE);

      /* BUCKET_WRITE_LOW */
      table->field[f++]->store(bucket_iter*transaction_size_histogram_width, TRUE);

      uint high_value;
      /* BUCKET_WRITE_HIGH */
      if (bucket_iter == TX_SIZE_HIST_COUNT -1)
        high_value = UINT_MAX;
      else
        high_value = (bucket_iter+1)*transaction_size_histogram_width;
      table->field[f++]->store(high_value, TRUE);

      /* COUNT_BUCKET */
      table->field[f++]->store(hist[bucket_iter], TRUE);

      /* COUNT_BUCKET_AND_LOWER */
      rolling_count += hist[bucket_iter];
      table->field[f++]->store(rolling_count, TRUE);

      /* BUCKET_QUANTILE */
      double bucket_quantile = (double)rolling_count / total_count;
      table->field[f++]->store(bucket_quantile);

      if (schema_table_store_record(thd, table))
      {
        mt_unlock(lock_acquired, &LOCK_global_tx_size_histogram);
        DBUG_RETURN(-1);
      }
    }
  }

  mt_unlock(lock_acquired, &LOCK_global_tx_size_histogram);

  DBUG_RETURN(0);
}

/***********************************************************************
              End   - Functions to support TRANSACTION_SIZE_HISTOGRAM
************************************************************************/

/***********************************************************************
              Begin - Functions to support WRITE_STATISTICS_HISTOGRAM
************************************************************************/
void reset_write_stat_histogram()
{
  bool lock_acquired = mt_lock(&LOCK_global_write_stat_histogram);
  global_write_stat_histogram.fill(0);
  mt_unlock(lock_acquired, &LOCK_global_write_stat_histogram);
}

/*
  update_write_stat_histogram
    Increment the count in the bucket corresponding to the specified
    write throughput
 */
static void update_write_stat_histogram(TIME_BUCKET_STATS& time_bucket)
{
  ulonglong total_writes_in_interval = 0;
  /* Aggregate the bytes written into binlog on the USER dimension
     during the time interval as defined by write_stats_frequency.
     We expect the same result if we aggregate on other dimensions
   */
  for(auto stats_iter = time_bucket[0].begin();
      stats_iter != time_bucket[0].end();
      ++stats_iter)
    total_writes_in_interval += stats_iter->second.binlog_bytes_written;

  /* Convert into kilo-bytes/second and map to histogram bucket */
  ulong bucket_number = total_writes_in_interval
    / (write_stats_frequency ? write_stats_frequency : 1)
    / (write_statistics_histogram_width*1024);

  if (bucket_number >= WRITE_STAT_HIST_COUNT)
    bucket_number = WRITE_STAT_HIST_COUNT-1;

  bool lock_acquired = mt_lock(&LOCK_global_write_stat_histogram);
  global_write_stat_histogram[bucket_number]++;
  mt_unlock(lock_acquired, &LOCK_global_write_stat_histogram);
}

/*
  fill_write_stat_histogram
    Fills the rows returned for statements selecting from
    WRITE_STATISTICS_HISTOGRAM

*/
int fill_write_stat_histogram(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_write_stat_histogram");
  TABLE* table= tables->table;
  ulonglong rolling_count = 0, total_count = 0;

  bool lock_acquired = mt_lock(&LOCK_global_write_stat_histogram);

  for (auto bucket_iter= 0; bucket_iter < WRITE_STAT_HIST_COUNT; bucket_iter++)
    total_count += global_write_stat_histogram[bucket_iter];

  for (auto bucket_iter= 0; bucket_iter < WRITE_STAT_HIST_COUNT; bucket_iter++)
  {
    int f= 0;
    restore_record(table, s->default_values);

    /* BUCKET_NUMBER */
    table->field[f++]->store(bucket_iter, TRUE);

    /* BUCKET_WRITE_LOW */
    table->field[f++]->store(bucket_iter*write_statistics_histogram_width, TRUE);

    /* BUCKET_WRITE_HIGH */
    uint high_value;
    if (bucket_iter == WRITE_STAT_HIST_COUNT-1) // use max for the last bucket
      high_value = UINT_MAX;
    else
      high_value = (bucket_iter+1)*write_statistics_histogram_width;

    table->field[f++]->store(high_value, TRUE);

    /* COUNT_BUCKET */
    table->field[f++]->store(global_write_stat_histogram[bucket_iter], TRUE);

    /* COUNT_BUCKET_AND_LOWER */
    rolling_count += global_write_stat_histogram[bucket_iter];
    table->field[f++]->store(rolling_count, TRUE);

    /* BUCKET_QUANTILE */
    double bucket_quantile = (double)rolling_count / total_count;
    table->field[f++]->store(bucket_quantile);

    if (schema_table_store_record(thd, table))
    {
      mt_unlock(lock_acquired, &LOCK_global_write_stat_histogram);
      DBUG_RETURN(-1);
    }
  }

  mt_unlock(lock_acquired, &LOCK_global_write_stat_histogram);

  DBUG_RETURN(0);
}

/***********************************************************************
              End   - Functions to support WRITE_STATISTICS_HISTOGRAM
************************************************************************/

/***********************************************************************
              Begin - Functions to support WRITE_STATISTICS
************************************************************************/
/*
  free_global_write_statistics
    Frees global_write_statistics
*/
void free_global_write_statistics(void)
{
  bool lock_acquired = mt_lock(&LOCK_global_write_statistics);
  global_write_statistics_map.clear();
  mt_unlock(lock_acquired, &LOCK_global_write_statistics);
}

/*
  populate_write_statistics
    Populate the write statistics

  Input:
    thd                 in:  - THD
    time_bucket_stats  out:  - Array structure containing write stats populated.
*/
static void populate_write_statistics(
  THD *thd,
  TIME_BUCKET_STATS& time_bucket_stats)
{
  ulonglong binlog_bytes_written = thd->get_row_binlog_bytes_written();
  ulonglong total_write_time = thd->get_stmt_total_write_time();

  // Get keys for all the target dimensions to update write stats for
  std::array<std::string, WRITE_STATISTICS_DIMENSION_COUNT> keys;
  thd->get_mt_keys_for_write_query(keys);

  // Add/Update the write stats
  for (uint i = 0; i<WRITE_STATISTICS_DIMENSION_COUNT; i++)
  {
    auto iter = time_bucket_stats[i].find(keys[i]);
    if (iter == time_bucket_stats[i].end())
    {
      WRITE_STATS ws;
      ws.binlog_bytes_written = binlog_bytes_written;
      ws.cpu_write_time_us = total_write_time;
      time_bucket_stats[i].insert(std::make_pair(keys[i], ws));
    } else
    {
      WRITE_STATS & ws = iter->second;
      ws.binlog_bytes_written += binlog_bytes_written;
      ws.cpu_write_time_us += total_write_time;
    }
  }
}

/*
  store_write_statistics
    Store the write statistics for the executed statement.
    The bulk of the work is done in populate_write_stats()

  Input:
    thd         in:  - THD
*/
void store_write_statistics(THD *thd)
{
  // write_stats_frequency may be updated dynamically. Caching it for the
  // logic below
  ulong write_stats_frequency_cached = write_stats_frequency;
  if (write_stats_frequency_cached == 0) {
    return;
  }

  bool lock_acquired = mt_lock(&LOCK_global_write_statistics);
  time_t timestamp = my_time(0);
  int time_bucket_key = timestamp - (timestamp % write_stats_frequency_cached);
  auto time_bucket_iter = global_write_statistics_map.begin();

  DBUG_EXECUTE_IF("dbug.add_write_stats_to_most_recent_bucket", {
    if (time_bucket_iter != global_write_statistics_map.end()) {
      time_bucket_key = time_bucket_iter->first;
    }
  });
  if (time_bucket_iter == global_write_statistics_map.end()
    ||  time_bucket_key > time_bucket_iter->first)
  {
    // time_bucket is newer than last registered bucket...

    // update the write throughput histogram with last bucket
    if (time_bucket_iter != global_write_statistics_map.end())
      update_write_stat_histogram(time_bucket_iter->second);

    // need to insert a new one
    while(!global_write_statistics_map.empty() &&
      (uint)global_write_statistics_map.size() >= write_stats_count)
    {
      // We are over the configured size. Erase older entries first.
      global_write_statistics_map.pop_back();
    }
    TIME_BUCKET_STATS time_bucket_stats;
    populate_write_statistics(thd, time_bucket_stats);
    global_write_statistics_map.push_front(std::make_pair(time_bucket_key, time_bucket_stats));
  } else
  {
    populate_write_statistics(thd, time_bucket_iter->second);
  }
  mt_unlock(lock_acquired, &LOCK_global_write_statistics);

  /* update txn binlog bytes written */
  thd->trx_bytes_written += thd->get_row_binlog_bytes_written();
  /* update txn size histogram now if single statement transaction
     update for multi-statements transaction happens during commit
   */
  if (!thd->in_active_multi_stmt_transaction())
    update_tx_size_histogram(thd);
}

/*
  fill_write_statistics
    Fills the rows returned for statements selecting from WRITE_STATISTICS

*/
int fill_write_statistics(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_write_statistics");
  TABLE* table= tables->table;

  bool lock_acquired = mt_lock(&LOCK_global_write_statistics);
  MYSQL_TIME time;
  std::string type_string;

  for (auto time_bucket_iter= global_write_statistics_map.cbegin();
       time_bucket_iter != global_write_statistics_map.cend(); ++time_bucket_iter)
  {
    const TIME_BUCKET_STATS & time_bucket = time_bucket_iter->second;

    for(int type = 0; type != (int)time_bucket.size(); ++type)
    {
      for(auto stats_iter = time_bucket[type].begin();
          stats_iter != time_bucket[type].end();
          ++stats_iter)
      {
        int f= 0;
        restore_record(table, s->default_values);

        // timestamp
        thd->variables.time_zone->gmt_sec_to_TIME(
            &time, (my_time_t)time_bucket_iter->first);
        table->field[f]->set_notnull();
        table->field[f++]->store_time(&time);

        // type
        type_string = WRITE_STATS_TYPE_STRING[type];
        table->field[f++]->store(type_string.c_str(), type_string.length(),
                                 system_charset_info);

        // value
        table->field[f++]->store(stats_iter->first.c_str(),
                                 stats_iter->first.length(),
                                 system_charset_info);

        // binlog_bytes_written
        table->field[f++]->store(stats_iter->second.binlog_bytes_written, TRUE);

        // cpu_write_time_ms (convert from micro-secs to mill-secs)
        table->field[f++]->store(stats_iter->second.cpu_write_time_us/1000, TRUE);

        if (schema_table_store_record(thd, table))
        {
          mt_unlock(lock_acquired, &LOCK_global_write_statistics);
          DBUG_RETURN(-1);
        }
      }
    }
  }
  mt_unlock(lock_acquired, &LOCK_global_write_statistics);

  DBUG_RETURN(0);
}

/***********************************************************************
              End - Functions to support WRITE_STATISTICS
************************************************************************/

/***********************************************************************
              Begin - Functions to support WRITE_THROTTLING_RULES
************************************************************************/

/*
  WRITE_THROTTLING_RULES

  This table is used to display the write throttling rules used to mitigate
  replication lag
*/

ST_FIELD_INFO write_throttling_rules_fields_info[]=
{
  {"MODE", 16, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"CREATION_TIME", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_DATETIME, 0, 0, 0,
    SKIP_OPEN_TABLE},
  {"TYPE", 16, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"VALUE", MD5_BUFF_LENGTH, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"THROTTLE_RATE", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
    0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};

/*
  Map integer representation of write throttling rules mode to string
  constants.
*/
const std::string WRITE_THROTTLING_MODE_STRING[] = {"MANUAL", "AUTO"};

/*
  free_global_write_throttling_rules
    Frees auto and manual rules from global_write_throttling_rules data structure
*/
void free_global_write_throttling_rules() {
  bool lock_acquired = mt_lock(&LOCK_global_write_throttling_rules);
  for(uint i = 0; i<WRITE_STATISTICS_DIMENSION_COUNT; i++) {
    global_write_throttling_rules[i].clear();
  }
  mt_unlock(lock_acquired, &LOCK_global_write_throttling_rules);
}

/*
  free_global_write_auto_throttling_rules
    Frees only auto rules from global_write_throttling_rules data structure
*/
void free_global_write_auto_throttling_rules() {
  bool lock_acquired = mt_lock(&LOCK_global_write_throttling_rules);
  for (auto &rules : global_write_throttling_rules) {
    auto rules_iter = rules.begin();
    while (rules_iter != rules.end()) {
      if (rules_iter->second.mode == WTR_AUTO) {
        rules_iter = rules.erase(rules_iter);
      } else {
        ++rules_iter;
      }
    }
  }
  mt_unlock(lock_acquired, &LOCK_global_write_throttling_rules);
}

/*
  Utility method to convert a dimension(client, sql_id, user, shard)
  string to integer index. These indices are used in global_write_throttling_rules
  data structure.
  If the string doesn't represent a dimension, WTR_DIM_UNKNOWN is returned.
*/
enum_wtr_dimension get_wtr_dimension_from_str(std::string type_str) {
  int type = WRITE_STATISTICS_DIMENSION_COUNT - 1;

  while(type >= 0 && WRITE_STATS_TYPE_STRING[type] != type_str)
    type--;

  return static_cast<enum_wtr_dimension>(type);
}

/*
  Stores a user specified order in which throttling system should throttle
  write throttling dimensions in case of replication lag
*/
bool store_write_throttle_permissible_dimensions_in_order(char *new_value) {
  if (strcmp(new_value, "OFF") == 0) {
    mysql_mutex_lock(&LOCK_replication_lag_auto_throttling);
    write_throttle_permissible_dimensions_in_order.clear();
    mysql_mutex_unlock(&LOCK_replication_lag_auto_throttling);
    return false;
  }
  // copy the string to avoid mutating new var value.
  char * new_value_copy = (char *)my_malloc(strlen(new_value)+ 1, MYF(MY_WME));
  if(new_value_copy == nullptr) {
    return true; // failure allocating memory
  }
  strcpy(new_value_copy, new_value);
  char* wtr_dim_str;
  enum_wtr_dimension wtr_dim;
  std::vector<enum_wtr_dimension> new_dimensions;
  bool result = false;

  wtr_dim_str = strtok(new_value_copy, ",");
  while (wtr_dim_str != nullptr) {
    wtr_dim = get_wtr_dimension_from_str(wtr_dim_str);
    if (wtr_dim == WTR_DIM_UNKNOWN) {
      result = true; // not a valid dimension string, failure
      break;
    }
    auto it = std::find(new_dimensions.begin(), new_dimensions.end(), wtr_dim);
    if (it != new_dimensions.end()) {
      result = true; // duplicate dimension, failure
      break;
    }
    new_dimensions.push_back(wtr_dim);
    wtr_dim_str = strtok(nullptr, ",");
  }
  if (!result) {
    mysql_mutex_lock(&LOCK_replication_lag_auto_throttling);
    write_throttle_permissible_dimensions_in_order = new_dimensions;
    mysql_mutex_unlock(&LOCK_replication_lag_auto_throttling);
  }
  my_free(new_value_copy);
  return result;
}

/*
  Stores user specified query command types that throttling system should
  throttle in case of replication lag
*/
bool store_write_throttle_permissible_query_types(char *new_value) {
  if (strcmp(new_value, "OFF") == 0) {
    mysql_mutex_lock(&LOCK_replication_lag_auto_throttling);
    write_throttle_permissible_query_types.clear();
    mysql_mutex_unlock(&LOCK_replication_lag_auto_throttling);
    return false;
  }
  // copy the string to avoid mutating new var value.
  char * new_value_copy = (char *)my_malloc(strlen(new_value)+ 1, MYF(MY_WME));
  if(new_value_copy == nullptr) {
    return true; // failure allocating memory
  }
  strcpy(new_value_copy, new_value);
  char* strtok_str;
  std::string query_type;
  std::set<enum_sql_command> new_query_types;
  bool result = false;

  strtok_str = strtok(new_value_copy, ",");
  while (strtok_str != nullptr) {
    query_type = strtok_str;
    if (query_type == "INSERT") {
      new_query_types.emplace(SQLCOM_INSERT);
      new_query_types.emplace(SQLCOM_INSERT_SELECT);
    } else if (query_type == "UPDATE") {
      new_query_types.emplace(SQLCOM_UPDATE);
      new_query_types.emplace(SQLCOM_UPDATE_MULTI);
    } else if (query_type == "DELETE") {
      new_query_types.emplace(SQLCOM_DELETE);
      new_query_types.emplace(SQLCOM_DELETE_MULTI);
    } else if (query_type == "REPLACE") {
      new_query_types.emplace(SQLCOM_REPLACE);
      new_query_types.emplace(SQLCOM_REPLACE_SELECT);
    } else {
      result = true; // not a valid query type string, failure
      break;
    }
    strtok_str = strtok(nullptr, ",");
  }
  if (!result) {
    mysql_mutex_lock(&LOCK_replication_lag_auto_throttling);
    write_throttle_permissible_query_types = new_query_types;
    mysql_mutex_unlock(&LOCK_replication_lag_auto_throttling);
  }
  my_free(new_value_copy);
  return result;
}

/*
  Stores a user specified throttling rule from write_throttling_patterns
  sys_var into global_write_throttling_rules
*/
bool store_write_throttling_rules(THD *thd) {
  char * wtr_string_cur_pos;
  std::string type_str;
  std::string value_str;
  enum_wtr_dimension wtr_dim;

  char op = latest_write_throttling_rule[0];

  // first character is + or -
  if (op == '+' || op == '-') {
    wtr_string_cur_pos = latest_write_throttling_rule;
    wtr_string_cur_pos++;

    type_str = strtok(wtr_string_cur_pos, "=");
    wtr_dim = get_wtr_dimension_from_str(type_str);
    value_str = strtok(nullptr, "");
    if (wtr_dim == WTR_DIM_UNKNOWN || value_str == "") {
      return true;
    }
    WRITE_THROTTLING_RULE rule;
    rule.mode = WTR_MANUAL; // manual
    rule.create_time = my_time(0);
    rule.throttle_rate = 100; // manual rules are fully throttled

    bool lock_acquired = mt_lock(&LOCK_global_write_throttling_rules);
    auto & rules_map = global_write_throttling_rules[wtr_dim];
    auto iter = rules_map.find(value_str);
    bool remove_from_currently_throttled_entities = false;
    if (op == '+') {
      if (iter != rules_map.end()) {
        // If manually overriding an auto rule, remove it from
        // currently_throttled_entities queue
        if (rules_map[value_str].mode == WTR_AUTO)
          remove_from_currently_throttled_entities = true;
        rules_map[value_str] = rule;
      }
      else
        rules_map.insert(std::make_pair(value_str, rule));
    } else { // op == '-'
      if (iter != rules_map.end()) {
        // If manually overriding an auto rule, remove it from
        // currently_throttled_entities queue
        if (rules_map[value_str].mode == WTR_AUTO)
          remove_from_currently_throttled_entities = true;
        rules_map.erase(iter);
      }
    }
    if (remove_from_currently_throttled_entities) {
      for(auto q_iter = currently_throttled_entities.begin();
          q_iter != currently_throttled_entities.end(); q_iter++)
      {
        if (q_iter->first == value_str) {
          currently_throttled_entities.erase(q_iter);
          break;
        }
      }
    }
    mt_unlock(lock_acquired, &LOCK_global_write_throttling_rules);
    return false; // success
  }
  return true; // failure
}

/*
  fill_write_throttling_rules
    Fills the rows returned for statements selecting from WRITE_THROTTLING_RULES
*/
int fill_write_throttling_rules(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_write_throttling_rules");
  TABLE* table= tables->table;

  bool lock_acquired = mt_lock(&LOCK_global_write_throttling_rules);
  MYSQL_TIME time;
  std::string type_string, mode_string;

  for(int type = 0; type != (int)global_write_throttling_rules.size(); ++type)
  {
    for(auto rules_iter = global_write_throttling_rules[type].begin();
        rules_iter != global_write_throttling_rules[type].end(); ++rules_iter)
    {
      int f= 0;
      restore_record(table, s->default_values);

      // mode
      mode_string = WRITE_THROTTLING_MODE_STRING[rules_iter->second.mode];
      table->field[f++]->store(mode_string.c_str(), mode_string.length(), system_charset_info);

      // creation_time
      thd->variables.time_zone->gmt_sec_to_TIME(
          &time, (my_time_t)rules_iter->second.create_time);
      table->field[f++]->store_time(&time);

      // type
      type_string = WRITE_STATS_TYPE_STRING[type];
      table->field[f++]->store(type_string.c_str(), type_string.length(), system_charset_info);

      // value
      table->field[f++]->store(rules_iter->first.c_str(), rules_iter->first.length(), system_charset_info);

      // throttle_rate
      table->field[f++]->store(rules_iter->second.throttle_rate, TRUE);

      if (schema_table_store_record(thd, table))
      {
        mt_unlock(lock_acquired, &LOCK_global_write_throttling_rules);
        DBUG_RETURN(-1);
      }
    }
  }
  mt_unlock(lock_acquired, &LOCK_global_write_throttling_rules);

  DBUG_RETURN(0);
}

/***********************************************************************
              End - Functions to support WRITE_THROTTLING_RULES
************************************************************************/

/***********************************************************************
              Begin - Functions to support WRITE_THROTTLING_LOG
************************************************************************/
#define WRITE_THROTTLING_MODE_COUNT 2

ST_FIELD_INFO write_throttling_log_fields_info[]=
{
  {"MODE", 16, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"LAST_RECORDED", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_DATETIME, 0, 0, 0,
    SKIP_OPEN_TABLE},
  {"TYPE", 16, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"VALUE", MD5_BUFF_LENGTH, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"TRANSACTION_TYPE", 16, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COUNT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
    0, 0, 0, SKIP_OPEN_TABLE},
  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};

/*
** enum_wtr_thrlog_txn_type
**
** valid values for the COLUMN I_S.write_throttling_log."TRANSACTION_TYPE"
*/
enum enum_wtr_thrlog_txn_type
{
  WTR_THRLOG_TXN_TYPE_SHORT = 0,
  WTR_THRLOG_TXN_TYPE_LONG  = 1,
};

/*
** WRITE_THRLOG_TXN_TYPE
**
** valid values for the COLUMN I_S.write_throttling_log."TRANSACTION_TYPE"
*/
const std::string WRITE_THRLOG_TXN_TYPE[] = {"SHORT", "LONG"};

std::array<
  std::array<
    std::unordered_map<std::string, WRITE_THROTTLING_LOG>,
    WRITE_THROTTLING_MODE_COUNT
  >,
  WRITE_STATISTICS_DIMENSION_COUNT
> global_write_throttling_log;

/*
  free_global_write_throttling_log
    Frees global_write_throttling_log
*/
void free_global_write_throttling_log(void)
{
  bool lock_acquired = mt_lock(&LOCK_global_write_throttling_log);
  for (uint i = 0; i < WRITE_STATISTICS_DIMENSION_COUNT; i++) {
    for (uint j = 0; j < WRITE_THROTTLING_MODE_COUNT; j++) {
      global_write_throttling_log[i][j].clear();
    }
  }
  mt_unlock(lock_acquired, &LOCK_global_write_throttling_log);
}

/*
** global_long_qry_abort_log
**
** global map that stores the long queries information to populate
** the table I_S.write_throttling_log
*/
std::unordered_map<std::string, WRITE_THROTTLING_LOG> global_long_qry_abort_log;

/*
  fill_write_throttling_log
    Fills the rows returned for statements selecting from WRITE_THROTTLING_LOG
*/
int fill_write_throttling_log(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_write_throttling_log");
  TABLE* table= tables->table;

  bool lock_acquired = mt_lock(&LOCK_global_write_throttling_log);
  MYSQL_TIME time;
  std::string type_string, mode_string;

  /* populate rows from throttling short running queries */
  for(size_t type = 0; type != global_write_throttling_log.size(); ++type)
  {
    for(size_t mode = 0; mode != global_write_throttling_log[type].size(); ++mode)
    {
      for(auto log_iter = global_write_throttling_log[type][mode].begin();
        log_iter != global_write_throttling_log[type][mode].end(); ++log_iter)
      {
        WRITE_THROTTLING_LOG & log = log_iter->second;
        int f= 0;
        restore_record(table, s->default_values);

        // mode
        mode_string = WRITE_THROTTLING_MODE_STRING[mode];
        table->field[f++]->store(mode_string.c_str(), mode_string.length(), system_charset_info);

        // last_recorded
        thd->variables.time_zone->gmt_sec_to_TIME(
            &time, (my_time_t)log.last_time);
        table->field[f++]->store_time(&time);

        // type
        type_string = WRITE_STATS_TYPE_STRING[type];
        table->field[f++]->store(type_string.c_str(), type_string.length(), system_charset_info);

        // value
        table->field[f++]->store(log_iter->first.c_str(), log_iter->first.length(), system_charset_info);

        // transaction_type: SHORT
        table->field[f++]->store(WRITE_THRLOG_TXN_TYPE[WTR_THRLOG_TXN_TYPE_SHORT].c_str(),
                                 WRITE_THRLOG_TXN_TYPE[WTR_THRLOG_TXN_TYPE_SHORT].length(),
                                 system_charset_info);

        // count
        table->field[f++]->store(log.count, TRUE);

        if (schema_table_store_record(thd, table))
        {
          mt_unlock(lock_acquired, &LOCK_global_write_throttling_log);
          DBUG_RETURN(-1);
        }
      }
    }
  }

  /* populate rows from aborting long running queries */
  for(auto log_iter = global_long_qry_abort_log.begin();
      log_iter != global_long_qry_abort_log.end(); ++log_iter)
  {
    WRITE_THROTTLING_LOG & log = log_iter->second;
    int f= 0;
    restore_record(table, s->default_values);

    // mode: AUTO
    mode_string = WRITE_THROTTLING_MODE_STRING[WTR_AUTO];
    table->field[f++]->store(mode_string.c_str(), mode_string.length(), system_charset_info);

    // last_recorded
    thd->variables.time_zone->gmt_sec_to_TIME(
        &time, (my_time_t)log.last_time);
    table->field[f++]->store_time(&time);

    // type: SQL_ID
    type_string = WRITE_STATS_TYPE_STRING[WTR_DIM_SQL_ID];
    table->field[f++]->store(type_string.c_str(), type_string.length(), system_charset_info);

    // value
    table->field[f++]->store(log_iter->first.c_str(), log_iter->first.length(), system_charset_info);

    // transaction_type: LONG
    table->field[f++]->store(WRITE_THRLOG_TXN_TYPE[WTR_THRLOG_TXN_TYPE_LONG].c_str(),
                             WRITE_THRLOG_TXN_TYPE[WTR_THRLOG_TXN_TYPE_LONG].length(),
                             system_charset_info);

    // count
    table->field[f++]->store(log.count, TRUE);

    if (schema_table_store_record(thd, table))
    {
      mt_unlock(lock_acquired, &LOCK_global_write_throttling_log);
      DBUG_RETURN(-1);
    }
  }

  mt_unlock(lock_acquired, &LOCK_global_write_throttling_log);

  DBUG_RETURN(0);
}

/*
  store_write_throttling_log
    Stores a log for when a query was throttled due to a throttling
    rule in I_S.WRITE_THROTTLING_RULES
*/
void store_write_throttling_log(
  THD *thd,
  int type,
  std::string value,
  WRITE_THROTTLING_RULE &rule)
{
  bool lock_acquired = mt_lock(&LOCK_global_write_throttling_log);
  WRITE_THROTTLING_LOG log{};
  time_t timestamp = my_time(0);
  auto &log_map = global_write_throttling_log[type][rule.mode];
  auto &inserted_log = log_map.insert(std::make_pair(value, log)).first->second;
  inserted_log.last_time = timestamp;
  inserted_log.count++;
  mt_unlock(lock_acquired, &LOCK_global_write_throttling_log);
}

/*
  store_long_qry_abort_log
    Stores a log for a query that is aborted due to it being identified
    as long running query. This will be added to I_S.write_throttling_log.
*/
void store_long_qry_abort_log(THD *thd)
{
  char md5_hex_buffer[MD5_BUFF_LENGTH];
  /* generate SQL_ID value */
  array_to_hex(md5_hex_buffer, thd->mt_key_value(THD::SQL_ID).data(),
               MD5_HASH_SIZE);
  bool lock_acquired = mt_lock(&LOCK_global_write_throttling_log);
  WRITE_THROTTLING_LOG log{};
  time_t timestamp = my_time(0);
  auto &inserted_log =
    global_long_qry_abort_log
      .insert(
        std::make_pair(std::string(md5_hex_buffer, MD5_BUFF_LENGTH), log))
      .first->second;
  inserted_log.last_time = timestamp;
  inserted_log.count++;
  mt_unlock(lock_acquired, &LOCK_global_write_throttling_log);
}

/***********************************************************************
              End - Functions to support WRITE_THROTTLING_LOG
************************************************************************/

/***********************************************************************
              Begin - Functions to support throttling of write queries
************************************************************************/
#ifdef HAVE_REPLICATION

/*
  update_monitoring_status_for_entity
    Given a potential entity that is causing replication lag, this method either marks it
    to be monitored for next cycle or marks it to be throttled if we have already
    monitored it for enough cycles.
*/
void static update_monitoring_status_for_entity(std::string name, enum_wtr_dimension dimension) {
  if (currently_monitored_entity.dimension == dimension &&
      currently_monitored_entity.name == name) {
    // increment monitor hits counter
    currently_monitored_entity.hits++;
  } else {
    // update the currently monitored entity
    sql_print_information(
        "[Write Throttling]Updating monitored entity. dim:%s, name:%s",
        WRITE_STATS_TYPE_STRING[dimension].c_str(), name.c_str());
    currently_monitored_entity.dimension = dimension;
    currently_monitored_entity.name = name;
    currently_monitored_entity.hits = 0;
  }

  if (write_throttle_monitor_cycles == 0 ||
      currently_monitored_entity.hits == write_throttle_monitor_cycles) {
      // throttle the entity, create a rule if not already created
      WRITE_THROTTLING_RULE rule;
      rule.mode = WTR_AUTO; // auto
      rule.create_time = my_time(0);
      rule.throttle_rate = write_throttle_rate_step;

      bool lock_acquired = mt_lock(&LOCK_global_write_throttling_rules);
      auto & rules_map = global_write_throttling_rules[dimension];
      auto iter = rules_map.find(name);
      if (iter == rules_map.end()) {
        sql_print_information(
          "[Write Throttling]New rule created. dim:%s, name:%s, rate:%d",
          WRITE_STATS_TYPE_STRING[dimension].c_str(), name.c_str(),
          rule.throttle_rate);
        rules_map.insert(std::make_pair(name, rule));
        // insert the entity into currently_throttled_entities queue
        currently_throttled_entities.push_back(std::make_pair(name, dimension));
      }
      mt_unlock(lock_acquired, &LOCK_global_write_throttling_rules);
    }
}

/*
  get_top_two_entities
    Given a map of string entities mapped to WRITE_STATS, this method returns the keys
    for top two entities with highest binlog_bytes_written.
    Defaults to empty string keys in return value if there aren't enough entries
    in provided map.

  @retval pair<first_key, second key>
*/
std::pair<std::string, std::string> get_top_two_entities(
  std::unordered_map<std::string, WRITE_STATS> &dim_stats
) {
  std::string first_entity = "";
  std::string second_entity = "";
  ulonglong first_bytes_written = 0;
  ulonglong second_bytes_written = 0;
  for(auto iter = dim_stats.begin(); iter != dim_stats.end(); iter++) {
    if (iter->second.binlog_bytes_written > first_bytes_written) {
      second_bytes_written = first_bytes_written;
      second_entity = first_entity;
      first_bytes_written = iter->second.binlog_bytes_written;
      first_entity = iter->first;
    } else if (iter->second.binlog_bytes_written > second_bytes_written) {
      second_bytes_written = iter->second.binlog_bytes_written;
      second_entity = iter->first;
    }
  }
  return std::make_pair(first_entity, second_entity);
}

/*
  check_lag_and_throttle
    Main method responsible for auto throttling to avoid replication lag.
    It checks if there is lag in the replication topology.
    If yes, it finds the entity that it should throttle. Otherwise, it optionally
    releases one of the previously throttled entities if replication lag is below
    safe threshold.
*/
void check_lag_and_throttle(time_t time_now) {
  ulong lag = get_current_replication_lag();

  if (lag < write_stop_throttle_lag_milliseconds) {
    if (are_replicas_lagging) {
      are_replicas_lagging = false;
      sql_print_information(
          "[Write Throttling]Lag is over. Starting to release throttled "
          "entites");
    }
    // Replication lag below safe threshold, reduce throttle rate or release
    // at most one throttled entity. If releasing, erase corresponding throttling rule.
    if (currently_throttled_entities.empty())
      return;
    auto throttled_entity = currently_throttled_entities.front();

    enum_wtr_dimension wtr_dim = throttled_entity.second;
    std::string name = throttled_entity.first;

    bool lock_acquired = mt_lock(&LOCK_global_write_throttling_rules);
    auto & rules_map = global_write_throttling_rules[wtr_dim];
    auto iter = rules_map.find(name);
    if (iter != rules_map.end()) {
      if (iter->second.mode == WTR_MANUAL) {
        // Safe guard. A manual rule should not end up in currently_throttled_entities
        // But if it does, simply pop it out.
        currently_throttled_entities.pop_front();
      } else if (iter->second.throttle_rate > write_throttle_rate_step) {
        iter->second.throttle_rate -= write_throttle_rate_step;
      } else {
        sql_print_information("[Write Throttling]Rule removed. dim:%s, name:%s",
                              WRITE_STATS_TYPE_STRING[wtr_dim].c_str(),
                              name.c_str());
        global_write_throttling_rules[wtr_dim].erase(iter);
        currently_throttled_entities.pop_front();
        if (currently_throttled_entities.empty()) {
          currently_monitored_entity.reset();
          sql_print_information(
              "[Write Throttling]All throttled rules removed. Fully stopping "
              "auto throttling.");
        }
      }
    }
    mt_unlock(lock_acquired, &LOCK_global_write_throttling_rules);
  }

  if (lag > write_start_throttle_lag_milliseconds) {
    if (!are_replicas_lagging) {
      are_replicas_lagging = true;
      sql_print_information(
          "[Write Throttling]Lag detected. Starting auto throttling.");
    }
    // Replication lag above threshold, Check if we can increase throttle rate for last throttled entity
    if (!currently_throttled_entities.empty()) {
      auto last_throttled_entity = currently_throttled_entities.back();
      bool throttle_rate_increased = false;
      bool lock_acquired = mt_lock(&LOCK_global_write_throttling_rules);
      auto & rules_map = global_write_throttling_rules[last_throttled_entity.second];
      auto iter = rules_map.find(last_throttled_entity.first);
      if (iter != rules_map.end() && iter->second.throttle_rate < 100) {
        iter->second.throttle_rate = std::min((uint)100,
          iter->second.throttle_rate + write_throttle_rate_step);
        throttle_rate_increased = true;
      }
      mt_unlock(lock_acquired, &LOCK_global_write_throttling_rules);
      if (throttle_rate_increased)
        return;
    }

    // Replication lag above threshold, find an entity to throttle
    if (global_write_statistics_map.size() == 0) {
      // no stats collected so far
      return;
    }

    // write_stats_frequency may be updated dynamically. Caching it for the
    // logic below
    ulong write_stats_frequency_cached = write_stats_frequency;
    if (write_stats_frequency_cached == 0) {
      return;
    }

    bool lock_acquired = mt_lock(&LOCK_global_write_statistics);

    // Find latest write_statistics time bucket that is complete.
    // Example - For write_stats_frequency=6s, At t=8s, this method should
    // return write_stats bucket for stats between t=0 to t=6 i.e. bucket_key=0
    // and not bucket_key=6 which is incomplete and only has 2s worth of
    // write_stats data;
    int time_bucket_key = time_now - (time_now % write_stats_frequency_cached) - write_stats_frequency_cached;
    auto latest_write_stats_iter = global_write_statistics_map.begin();

    // For testing purpose, force use the latest write stats bucket for culprit
    // analysis
    bool dbug_skip_last_complete_bucket_check = false;
    DBUG_EXECUTE_IF("dbug.skip_last_complete_bucket_check",
      {dbug_skip_last_complete_bucket_check = true;});

    if (!dbug_skip_last_complete_bucket_check) {
      if (latest_write_stats_iter->first != time_bucket_key) {
        // move to the second from front time bucket
        latest_write_stats_iter++;
      }
      if(latest_write_stats_iter == global_write_statistics_map.end()
        || latest_write_stats_iter->first != time_bucket_key) {
        // no complete write statistics bucket for analysis
        // reset currently monitored entity, if any, as there's been a
        // significant gap in time since we last did culprit analysis. It is
        // outdated.
        currently_monitored_entity.resetHits();
        mt_unlock(lock_acquired, &LOCK_global_write_statistics);
        return;
      }
    }
    TIME_BUCKET_STATS & latest_write_stats = latest_write_stats_iter->second;

    std::vector<enum_wtr_dimension> & dimensions =
      write_throttle_permissible_dimensions_in_order;

    bool is_fallback_entity_set = false;
    std::pair<std::string, enum_wtr_dimension> fallback_entity;
    bool is_entity_to_throttle_set = false;
    std::pair<std::string, enum_wtr_dimension> entity_to_throttle;

    for(auto dim_iter = dimensions.begin(); dim_iter != dimensions.end(); dim_iter++)
    {
      enum_wtr_dimension dim = *dim_iter;
      auto & dim_stats = latest_write_stats[dim];
      std::pair<std::string, std::string> top_entities = get_top_two_entities(dim_stats);

      // Set the fallback entity as the top entity in the highest cardinality dimension
      // This entity is throttled if there is no conclusive entity causing the lag.
      if (!is_fallback_entity_set && !dim_stats.empty()) {
        fallback_entity = std::make_pair(top_entities.first, dim);
        is_fallback_entity_set = true;
      }

      // For testing purpose, skip to throttle fallback entity
      bool dbug_simulate_fallback_sql_throttling = false;
      DBUG_EXECUTE_IF("dbug.simulate_fallback_sql_throttling",
        {dbug_simulate_fallback_sql_throttling = true;});

      if (dim_stats.empty() || dbug_simulate_fallback_sql_throttling) {
        // move on to the next dimension
        continue;
      } else if (dim_stats.size() == 1) {
        // throttle the first entity
        entity_to_throttle = std::make_pair(top_entities.first, dim);
        is_entity_to_throttle_set = true;
        break;
      } else {
        // compare the top two entities in this dimension
        auto first_bytes_written = dim_stats[top_entities.first].binlog_bytes_written;
        auto second_bytes_written = dim_stats[top_entities.second].binlog_bytes_written;
        if (first_bytes_written > second_bytes_written * write_throttle_min_ratio) {
          // first entity can be throttled
          entity_to_throttle = std::make_pair(top_entities.first, dim);
          is_entity_to_throttle_set = true;
          break;
        }
      }
    }
    mt_unlock(lock_acquired, &LOCK_global_write_statistics);

    if (is_entity_to_throttle_set) {
      // throttle the culprit entity if set
      update_monitoring_status_for_entity(entity_to_throttle.first, entity_to_throttle.second);
    } else if (is_fallback_entity_set) {
      // throttle fallback sql in case of no conclusive culprit
      update_monitoring_status_for_entity(fallback_entity.first, fallback_entity.second);
    }
  } else {
    // reset the currently monitored entity since the replication lag has fallen down
    currently_monitored_entity.resetHits();
  }
}

#endif
/***********************************************************************
              End - Functions to support throttling of write queries
************************************************************************/

/*
  These limits control the maximum accumulation of all sql statistics.
  They are controlled by setting the respective system variables.
*/
ulonglong max_sql_stats_count;
ulonglong max_sql_stats_size;

/*
  The current utilization for the sql metrics
  (statistics, text, client attributes, plans)
*/
ulonglong sql_stats_count= 0;
ulonglong sql_stats_size= 0; // extern from mysqld.h

/*
  These are used to determine if existing stats need to be flushed on changing
  the limits.
*/
ulonglong current_max_sql_stats_count= 0;
ulonglong current_max_sql_stats_size= 0;

extern ulonglong sql_plans_size;
ulonglong sql_findings_size = 0;

/*
  max_sql_text_storage_size is the maximum size of the sql text's underlying
  token array.
*/
uint max_sql_text_storage_size;

/**
  @brief Create and initialize global objects storing SQL stats.
*/
void init_global_sql_stats()
{
  if (!global_sql_stats.init())
    sql_print_error("Initialization of global_sql_stats failed.");
}

/*
  sql_cmd_type
    Categorizes the sql command into buckets like ALTER, CREATE, DELETE, etc.
    The list of commands checked is exhaustive. So any new command type added
    to enum_sql_command should be added here too; build will fail otherwise.
  Input:
    sql_command    in: sql command type
  Returns: A higher level categorization of the command, as a string.
*/
static std::string sql_cmd_type(enum_sql_command sql_command)
{
  switch (sql_command) {
    case SQLCOM_ALTER_TABLE:
    case SQLCOM_ALTER_DB:
    case SQLCOM_ALTER_PROCEDURE:
    case SQLCOM_ALTER_FUNCTION:
    case SQLCOM_ALTER_TABLESPACE:
    case SQLCOM_ALTER_SERVER:
    case SQLCOM_ALTER_EVENT:
    case SQLCOM_ALTER_DB_UPGRADE:
    case SQLCOM_ALTER_USER:
    case SQLCOM_RENAME_TABLE:
    case SQLCOM_RENAME_USER:
      return "ALTER";

    case SQLCOM_BEGIN:
    case SQLCOM_XA_START:
      return "BEGIN";

    case SQLCOM_COMMIT:
    case SQLCOM_XA_COMMIT:
      return "COMMIT";

    case SQLCOM_CREATE_TABLE:
    case SQLCOM_CREATE_INDEX:
    case SQLCOM_CREATE_DB:
    case SQLCOM_CREATE_FUNCTION:
    case SQLCOM_CREATE_USER:
    case SQLCOM_CREATE_PROCEDURE:
    case SQLCOM_CREATE_SPFUNCTION:
    case SQLCOM_CREATE_VIEW:
    case SQLCOM_CREATE_TRIGGER:
    case SQLCOM_CREATE_SERVER:
    case SQLCOM_CREATE_EVENT:
    case SQLCOM_CREATE_NPROCEDURE:
    case SQLCOM_CREATE_EXPLICIT_SNAPSHOT:
      return "CREATE";

    case SQLCOM_DELETE:
    case SQLCOM_DELETE_MULTI:
    case SQLCOM_RELEASE_EXPLICIT_SNAPSHOT:
      return "DELETE";

    case SQLCOM_DROP_TABLE:
    case SQLCOM_DROP_INDEX:
    case SQLCOM_DROP_DB:
    case SQLCOM_DROP_FUNCTION:
    case SQLCOM_DROP_USER:
    case SQLCOM_DROP_PROCEDURE:
    case SQLCOM_DROP_VIEW:
    case SQLCOM_DROP_TRIGGER:
    case SQLCOM_DROP_SERVER:
    case SQLCOM_DROP_EVENT:
    case SQLCOM_DROP_NPROCEDURE:
      return "DROP";

    case SQLCOM_INSERT:
    case SQLCOM_INSERT_SELECT:
      return "INSERT";

    case SQLCOM_LOAD:
      return "LOAD";

    case SQLCOM_SELECT:
      return "SELECT";

    case SQLCOM_SET_OPTION:
      return "SET";

    case SQLCOM_REPLACE:
    case SQLCOM_REPLACE_SELECT:
      return "REPLACE";

    case SQLCOM_ROLLBACK:
    case SQLCOM_ROLLBACK_TO_SAVEPOINT:
    case SQLCOM_XA_ROLLBACK:
      return "ROLLBACK";

    case SQLCOM_TRUNCATE:
      return "TRUNCATE";

    case SQLCOM_UPDATE:
    case SQLCOM_UPDATE_MULTI:
      return "UPDATE";

    case SQLCOM_SHOW_DATABASES:
    case SQLCOM_SHOW_TABLES:
    case SQLCOM_SHOW_FIELDS:
    case SQLCOM_SHOW_KEYS:
    case SQLCOM_SHOW_VARIABLES:
    case SQLCOM_SHOW_STATUS:
    case SQLCOM_SHOW_ENGINE_LOGS:
    case SQLCOM_SHOW_ENGINE_STATUS:
    case SQLCOM_SHOW_ENGINE_MUTEX:
    case SQLCOM_SHOW_PROCESSLIST:
    case SQLCOM_SHOW_TRANSACTION_LIST:
    case SQLCOM_SHOW_CONNECTION_ATTRIBUTES:
    case SQLCOM_SHOW_SRV_SESSIONS:
    case SQLCOM_SHOW_MASTER_STAT:
    case SQLCOM_SHOW_SLAVE_STAT:
    case SQLCOM_SHOW_GRANTS:
    case SQLCOM_SHOW_CREATE:
    case SQLCOM_SHOW_CHARSETS:
    case SQLCOM_SHOW_COLLATIONS:
    case SQLCOM_SHOW_CREATE_DB:
    case SQLCOM_SHOW_TABLE_STATUS:
    case SQLCOM_SHOW_TRIGGERS:
    case SQLCOM_SHOW_RESOURCE_COUNTERS:
    case SQLCOM_SHOW_BINLOGS:
    case SQLCOM_SHOW_OPEN_TABLES:
    case SQLCOM_SHOW_SLAVE_HOSTS:
    case SQLCOM_SHOW_BINLOG_EVENTS:
    case SQLCOM_SHOW_BINLOG_CACHE:
    case SQLCOM_SHOW_WARNS:
    case SQLCOM_SHOW_ERRORS:
    case SQLCOM_SHOW_STORAGE_ENGINES:
    case SQLCOM_SHOW_PRIVILEGES:
    case SQLCOM_SHOW_CREATE_PROC:
    case SQLCOM_SHOW_CREATE_FUNC:
    case SQLCOM_SHOW_STATUS_PROC:
    case SQLCOM_SHOW_STATUS_FUNC:
    case SQLCOM_SHOW_PROC_CODE:
    case SQLCOM_SHOW_FUNC_CODE:
    case SQLCOM_SHOW_PLUGINS:
    case SQLCOM_SHOW_CREATE_EVENT:
    case SQLCOM_SHOW_EVENTS:
    case SQLCOM_SHOW_CREATE_TRIGGER:
    case SQLCOM_SHOW_PROFILE:
    case SQLCOM_SHOW_PROFILES:
    case SQLCOM_SHOW_RELAYLOG_EVENTS:
    case SQLCOM_SHOW_ENGINE_TRX:
    case SQLCOM_SHOW_MEMORY_STATUS:
    case SQLCOM_SHOW_RAFT_LOGS:
    case SQLCOM_SHOW_RAFT_STATUS:
      return "SHOW";

    case SQLCOM_CHANGE_DB:
      return "USE";

    case SQLCOM_LOCK_TABLES:
    case SQLCOM_UNLOCK_TABLES:
    case SQLCOM_GRANT:
    case SQLCOM_REPAIR:
    case SQLCOM_REVOKE:
    case SQLCOM_OPTIMIZE:
    case SQLCOM_CHECK:
    case SQLCOM_ASSIGN_TO_KEYCACHE:
    case SQLCOM_PRELOAD_KEYS:
    case SQLCOM_FLUSH:
    case SQLCOM_KILL:
    case SQLCOM_ANALYZE:
    case SQLCOM_SAVEPOINT:
    case SQLCOM_RELEASE_SAVEPOINT:
    case SQLCOM_SLAVE_START:
    case SQLCOM_SLAVE_STOP:
    case SQLCOM_CHANGE_MASTER:
    case SQLCOM_RESET:
    case SQLCOM_PURGE:
    case SQLCOM_PURGE_BEFORE:
    case SQLCOM_PURGE_UUID:
    case SQLCOM_PURGE_RAFT_LOG:
    case SQLCOM_PURGE_RAFT_LOG_BEFORE:
    case SQLCOM_HA_OPEN:
    case SQLCOM_HA_CLOSE:
    case SQLCOM_HA_READ:
    case SQLCOM_DO:
    case SQLCOM_EMPTY_QUERY:
    case SQLCOM_HELP:
    case SQLCOM_REVOKE_ALL:
    case SQLCOM_CHECKSUM:
    case SQLCOM_CALL:
    case SQLCOM_PREPARE:
    case SQLCOM_EXECUTE:
    case SQLCOM_DEALLOCATE_PREPARE:
    case SQLCOM_XA_END:
    case SQLCOM_XA_PREPARE:
    case SQLCOM_XA_RECOVER:
    case SQLCOM_INSTALL_PLUGIN:
    case SQLCOM_UNINSTALL_PLUGIN:
    case SQLCOM_BINLOG_BASE64_EVENT:
    case SQLCOM_SIGNAL:
    case SQLCOM_RESIGNAL:
    case SQLCOM_GET_DIAGNOSTICS:
    case SQLCOM_FIND_GTID_POSITION:
    case SQLCOM_GTID_EXECUTED:
    case SQLCOM_RBR_COLUMN_NAMES:
    case SQLCOM_ATTACH_EXPLICIT_SNAPSHOT:
    case SQLCOM_SHUTDOWN:
    case SQLCOM_END:
      return "OTHER";
  }
}

/*
  valid_sql_stats
    Returns TRUE if sql statistics are valid: considered as non
    valid if all the statistics are 0.
  Input:
    sql_stats  in: - sql statistics handle
*/
static bool valid_sql_stats(SQL_STATS *sql_stats)
{
  /* check statistics that are specific to SQL_STATISTICS */
  if (sql_stats->count == 0 &&
      sql_stats->skipped_count == 0 &&
      sql_stats->shared_stats.rows_sent == 0 &&
      sql_stats->shared_stats.tmp_table_bytes_written == 0 &&
      sql_stats->shared_stats.filesort_bytes_written == 0 &&
      sql_stats->shared_stats.index_dive_count == 0 &&
      sql_stats->shared_stats.index_dive_cpu == 0 &&
      sql_stats->shared_stats.compilation_cpu == 0 &&
      sql_stats->shared_stats.tmp_table_disk_usage == 0 &&
      sql_stats->shared_stats.filesort_disk_usage == 0 &&
      sql_stats->shared_stats.rows_inserted == 0 &&
      sql_stats->shared_stats.rows_updated == 0 &&
      sql_stats->shared_stats.rows_deleted == 0 &&
      sql_stats->shared_stats.rows_read == 0 &&
      sql_stats->shared_stats.stmt_cpu_utime == 0 &&
      sql_stats->shared_stats.stmt_elapsed_utime == 0)
    return false;
  else
    return true;
}

/*
  set_sql_stats_cache_key
    Sets the sql statistic cache key value and length
    Returns TRUE if successful and FALSE otherwise
  Input:
    sql_id        in:  - sql id
    plan_id       in:  - plan id
    client_id     in:  - client id
    schema        in:  - schema
    user          in:  - user
    cache_key_val out: - cache key value
*/
static bool set_sql_stats_cache_key(
    md5_key& sql_id,
    md5_key& plan_id,
    const bool plan_id_set,
    md5_key& client_id,
    const uint32_t db_id,
    const uint32_t user_id,
    unsigned char *cache_key)
{
  char sql_stats_cache_key[(MD5_HASH_SIZE*3) + sizeof(db_id) + sizeof(user_id)];
  int offset = 0;

  memcpy(sql_stats_cache_key + offset, sql_id.data(), MD5_HASH_SIZE);
  offset += MD5_HASH_SIZE;
  /* skip plan_id if not available */
  if (plan_id_set)
  {
    memcpy(sql_stats_cache_key + offset, plan_id.data(), MD5_HASH_SIZE);
    offset += MD5_HASH_SIZE;
  }

  memcpy(sql_stats_cache_key + offset, client_id.data(), MD5_HASH_SIZE);
  offset += MD5_HASH_SIZE;

  memcpy(sql_stats_cache_key + offset, &db_id, sizeof(db_id));
  offset += sizeof(db_id);

  memcpy(sql_stats_cache_key + offset, &user_id, sizeof(user_id));
  offset += sizeof(user_id);

  compute_md5_hash((char *)cache_key, sql_stats_cache_key, offset);

  return true;
}

static void set_sql_stats_attributes(
    SQL_STATS *stats,
    md5_key& sql_id,
    md5_key& plan_id,
    const bool plan_id_set,
    md5_key& client_id,
    const uint32_t db_id,
    const uint32_t user_id)
{
  memcpy(stats->sql_id, sql_id.data(), MD5_HASH_SIZE);

  /* plan ID not always available */
  stats->plan_id_set = plan_id_set;
  if (plan_id_set)
    memcpy(stats->plan_id, plan_id.data(), MD5_HASH_SIZE);

  memcpy(stats->client_id, client_id.data(), MD5_HASH_SIZE);
  stats->db_id= db_id;
  stats->user_id= user_id;
	stats->query_sample_text = (char *)my_malloc(strlen("") + 1, MYF(MY_WME));
	memcpy(stats->query_sample_text, "", 1);
	stats->query_sample_seen = 0;
}

static bool set_sql_text_attributes(
  SQL_TEXT *sql_text_struct,
  enum_sql_command sql_type,
  const sql_digest_storage *digest_storage)
{
  sql_text_struct->sql_type = sql_type;

  // Compute digest text. This is anyway needed to get the full length.
  String digest_text;
  compute_digest_text(digest_storage, &digest_text);
  sql_text_struct->sql_text_length = digest_text.length();

  // Store the digest text now instead of the token array.
  // Compute the minimum necessary length and allocate the storage.
  sql_text_struct->sql_text_arr_len = std::min(
      sql_text_struct->sql_text_length,
      (size_t)max_sql_text_storage_size);
  if (!(sql_text_struct->sql_text=
        ((char*)my_malloc(sql_text_struct->sql_text_arr_len, MYF(MY_WME)))))
  {
    return false;
  }
  memcpy(sql_text_struct->sql_text, digest_text.c_ptr(),
         sql_text_struct->sql_text_arr_len);

  return true;
}

/*
  reset_sql_stats_from_thd
    Sets stats to the values from THD.
  Input:
    thd     in: - THD
    stats  out: - sql stats object to be updated
*/
void reset_sql_stats_from_thd(THD *thd, SHARED_SQL_STATS *stats)
{
  stats->rows_inserted= thd->rows_inserted;
  stats->rows_updated= thd->rows_updated;
  stats->rows_deleted= thd->rows_deleted;
  stats->rows_read= thd->rows_read;
  stats->stmt_cpu_utime = thd->sql_cpu;
  stats->stmt_elapsed_utime = thd->stmt_elapsed_utime;

  stats->rows_sent = static_cast<ulonglong>(thd->get_sent_row_count());
  stats->tmp_table_bytes_written = thd->get_tmp_table_bytes_written();
  stats->filesort_bytes_written = thd->get_filesort_bytes_written();
  stats->index_dive_count = thd->get_index_dive_count();
  stats->index_dive_cpu = thd->get_index_dive_cpu();
  stats->compilation_cpu = thd->get_compilation_cpu();
  stats->tmp_table_disk_usage = thd->get_stmt_tmp_table_disk_usage_peak();
  stats->filesort_disk_usage = thd->get_stmt_filesort_disk_usage_peak();
}

/*
  reset_sql_stats_from_diff
    Set SQL stats with the difference of values from THD and prev_stats.
    stats = THD.stats - prev_stats
  Input:
    thd         in: - THD
    prev_stats  in: - previously collected stats
    stats      out: - sql stats object to be updated
*/
void reset_sql_stats_from_diff(THD *thd, SHARED_SQL_STATS *prev_stats,
                               SHARED_SQL_STATS *stats)
{
  /*
    rows_inserted, rows_updated, rows_deleted, and rows_read in thd are
    accumulated across a multi-query. So they need to be subtracted from
    previous "checkpointed" stats to get the correct values for a (sub-)query.
  */
  stats->rows_inserted= thd->rows_inserted - prev_stats->rows_inserted;
  stats->rows_updated= thd->rows_updated - prev_stats->rows_updated;
  stats->rows_deleted= thd->rows_deleted - prev_stats->rows_deleted;
  stats->rows_read= thd->rows_read - prev_stats->rows_read;
  stats->stmt_cpu_utime = thd->sql_cpu - prev_stats->stmt_cpu_utime;
  stats->stmt_elapsed_utime = thd->stmt_elapsed_utime - prev_stats->stmt_elapsed_utime;

  stats->rows_sent =
    static_cast<ulonglong>(thd->get_sent_row_count()) - prev_stats->rows_sent;
  stats->tmp_table_bytes_written =
    thd->get_tmp_table_bytes_written() - prev_stats->tmp_table_bytes_written;
  stats->filesort_bytes_written =
    thd->get_filesort_bytes_written() - prev_stats->filesort_bytes_written;
  stats->index_dive_count =
    thd->get_index_dive_count() - prev_stats->index_dive_count;
  stats->index_dive_cpu =
    thd->get_index_dive_cpu() - prev_stats->index_dive_cpu;
  stats->compilation_cpu =
    thd->get_compilation_cpu() - prev_stats->compilation_cpu;
  if (thd->get_stmt_tmp_table_disk_usage_peak() > prev_stats->tmp_table_disk_usage) {
    stats->tmp_table_disk_usage = thd->get_stmt_tmp_table_disk_usage_peak() - prev_stats->tmp_table_disk_usage;
  } else {
    stats->tmp_table_disk_usage = 0;
  }
  if (thd->get_stmt_filesort_disk_usage_peak() > prev_stats->filesort_disk_usage) {
    stats->filesort_disk_usage = thd->get_stmt_filesort_disk_usage_peak() - prev_stats->filesort_disk_usage;
  } else {
    stats->filesort_disk_usage = 0;
  }
}

/*
  free_sql_stats_maps
    Clears out entries in the stats maps.
*/
static void free_sql_stats_maps(Sql_stats_maps &stats_maps)
{
  for (auto it= stats_maps.stats->begin(); it != stats_maps.stats->end(); ++it)
  {
    if (it->second)
    {
      my_free(it->second->query_sample_text);
      my_free(it->second);
    }
  }
  stats_maps.stats->clear();

  for (auto it= stats_maps.text->begin(); it != stats_maps.text->end(); ++it)
  {
    if (it->second)
    {
      my_free(it->second->sql_text);
      my_free(it->second);
    }
  }
  stats_maps.text->clear();

  stats_maps.client_attrs->clear();

  stats_maps.count = 0;
  stats_maps.size = 0;
}

/**
  @brief If snapshot exists, mark it for deletion by the last session.
*/
static void mark_sql_stats_snapshot_for_deletion()
{
  mysql_rwlock_wrlock(&LOCK_sql_stats_snapshot);
  if (sql_stats_snapshot.is_set())
  {
    sql_stats_snapshot.drop_maps = true;

    /* Reduce stats size and count if needed. If everything is flushed
       then they would be already reset to 0. */
    if (sql_stats_count)
    {
      DBUG_ASSERT(sql_stats_count >= sql_stats_snapshot.count);
      sql_stats_count -= sql_stats_snapshot.count;
    }

    if (sql_stats_size)
    {
      DBUG_ASSERT(sql_stats_size >= sql_stats_snapshot.size);
      sql_stats_size -= sql_stats_snapshot.size;
    }
  }
  mysql_rwlock_unlock(&LOCK_sql_stats_snapshot);
}

/*
  free_global_sql_stats
    Frees global_sql_stats_map, global_sql_text_map & global_client_attrs_map.
    Also resets the usage counters to 0.
    If limits_updated is set to true, the memory is freed only if the new
    limits are lower than the old limits. This avoids freeing up the stats
    unnecessarily when limits are increased.
*/
void free_global_sql_stats(bool limits_updated)
{
  bool lock_acquired = mt_lock(&LOCK_global_sql_stats);

  if (limits_updated) {
    if ((current_max_sql_stats_count <= max_sql_stats_count) &&
        (current_max_sql_stats_size <= max_sql_stats_size)) {
      current_max_sql_stats_count = max_sql_stats_count;
      current_max_sql_stats_size = max_sql_stats_size;
      mt_unlock(lock_acquired, &LOCK_global_sql_stats);
      return;
    }
  }

  free_sql_stats_maps(global_sql_stats);

  sql_stats_count = 0;
  sql_stats_size = 0;

  current_max_sql_stats_count = max_sql_stats_count;
  current_max_sql_stats_size = max_sql_stats_size;

  mt_unlock(lock_acquired, &LOCK_global_sql_stats);

  /* Now that the stats lock is released, mark the snapshot for deletion. It
     acquires the snapshot lock which is always taken first before the stats
     lock, and acquiring in opposite order can deadlock. */
  mark_sql_stats_snapshot_for_deletion();
}

/*
  is_sql_stats_collection_above_limit
    Checks whether we reached the counter and size limits for the
    SQL metrics (statistics, text, client attributes, plans)
*/
bool is_sql_stats_collection_above_limit() {
  return sql_stats_count >= max_sql_stats_count ||
         sql_stats_size + sql_plans_size + sql_findings_size >=
           max_sql_stats_size;
}

/***********************************************************************
              Begin - Functions to support SQL findings
************************************************************************/
/*
  free_global_sql_findings
    Frees global_sql_findings
*/
void free_global_sql_findings(void)
{
  bool lock_acquired = mt_lock(&LOCK_global_sql_findings);

  for (auto& finding_iter : global_sql_findings_map)
    finding_iter.second.clear();

  global_sql_findings_map.clear();
  sql_findings_size = 0;

  mt_unlock(lock_acquired, &LOCK_global_sql_findings);
}

/*
  populate_sql_findings
    Populate the findings for the SQL statement that just ended into
    the specificed finding map

  Input:
    thd         in:  - THD
    query_text  in:  - text of the SQL statement
    query_length  in:  - length of the SQL statement
    finding_vec out: - vector that stores the findings of the statement
                       (key is the warning code)
*/
static void populate_sql_findings(THD *thd, const char *query_text,
                                  uint query_length,
                                  SQL_FINDING_VEC &finding_vec) {
  Diagnostics_area::Sql_condition_iterator it =
      thd->get_stmt_da()->sql_conditions();

  const Sql_condition *err;
  while ((err= it++))
  {
    ulonglong now = my_getsystime() / 10000000;
    const uint err_no = err->get_sql_errno();

    // Lookup the finding map of this statement for current condition
    std::vector<SQL_FINDING>::iterator iter;
    for(iter = finding_vec.begin(); iter != finding_vec.end(); iter++)
      if (iter->code == err_no)
        break;
    if (iter == finding_vec.cend())
    {
      /* If we reached the SQL stats limits then skip adding new findings
         i.e, keep updating the count and date of existing findings
       */
      if (is_sql_stats_collection_above_limit())
        continue;

      // First time this finding is reported for this statement
      SQL_FINDING sql_find;
      sql_find.code          = err_no;
      sql_find.level         = err->get_level();
      sql_find.message.append(
          err->get_message_text(),
          std::min((uint)err->get_message_octet_length(), sf_max_message_size));
      sql_find.query_text.append(query_text,
                                 std::min(query_length, sf_max_query_size));
      sql_find.count = 1;
      sql_find.last_recorded = now;
      finding_vec.push_back(sql_find);

      /* track the size of the struct size (static) and variable length field */
      sql_findings_size += sizeof(SQL_FINDING);
      sql_findings_size += sql_find.message.size();
      sql_findings_size += sql_find.query_text.size();
    } else {
      // Increment the count and update the time
      iter->count++;
      iter->last_recorded = now;
    }
  }
}

/*
  store_sql_findings
    Store the findings for the SQL statement that just ended into
    the corresponding findings map that is looked up in the global
    map using the SQL ID of the statement. The bulk of the work is
    done in populate_sql_findings()

  Input:
    thd         in:  - THD
    query_text  in:  - text of the SQL statement
*/
void store_sql_findings(THD *thd, const char *query_text, int query_length) {
  if (sql_findings_control == SQL_INFO_CONTROL_ON &&
      thd->mt_key_is_set(THD::SQL_ID) &&
      thd->lex->select_lex.table_list.elements >
          0) // contains at least one table
  {
    bool lock_acquired = mt_lock(&LOCK_global_sql_findings);

    // Lookup finding map for this statement
    auto sql_find_it = global_sql_findings_map.find(thd->mt_key_value(THD::SQL_ID));
    if (sql_find_it == global_sql_findings_map.end())
    {
      /* Check whether we reached the SQL stats limits  */
      if (!is_sql_stats_collection_above_limit()) {
        // First time a finding is reported for this statement
        SQL_FINDING_VEC finding_vec;
        populate_sql_findings(thd, query_text, query_length, finding_vec);

        global_sql_findings_map.insert(
            std::make_pair(thd->mt_key_value(THD::SQL_ID), finding_vec));

        sql_findings_size += MD5_HASH_SIZE; // for SQL_ID
      }
    } else
      populate_sql_findings(thd, query_text, query_length, sql_find_it->second);

    mt_unlock(lock_acquired, &LOCK_global_sql_findings);
  }
}

/***********************************************************************
                End - Functions to support SQL findings
************************************************************************/

/*
  update_sql_stats_for_statement
    Updates the SQL stats for every SQL statement. The function is invoked
    multiple times during the execution.
    It is responsible for getting the SQL digest, storing and updating the
    underlying in-memory structures for SQL_TEXT and SQL_STATISTICS IS tables.
  Input:
    thd    in: - THD
    stats  in: - stats for the current SQL statement execution
*/
void update_sql_stats_for_statement(THD *thd, SHARED_SQL_STATS *stats,
                                      const char *sub_query,
                                      uint sub_query_length,
                                      bool statement_completed) {
  // Do a light weight limit check without acquiring the lock.
  // There will be another check later while holding the lock.
  if (is_sql_stats_collection_above_limit())
    return;

  /* Get the schema and the user name */
  const char *schema = thd->get_db_name();
  const char *user = thd->get_user_name();

  uint32_t db_id= get_id(DB_MAP_NAME, schema, strlen(schema));
  uint32_t user_id= get_id(USER_MAP_NAME, user, strlen(user));
  if (db_id == INVALID_NAME_ID || user_id == INVALID_NAME_ID)
    return;

  /*
    m_digest is just a defensive check and should never be null.

    m_digest_storage could be empty if sql_stats_control was turned on in the
    current statement. Since the variable was OFF during parse, but ON when
    update_sql_stats_for_statement is called, we won't have the digest
    available. In that case, just skip text and stats collection.
  */
  DBUG_ASSERT(thd->m_digest);
  if (!thd->m_digest || thd->m_digest->m_digest_storage.is_empty())
    return;

  md5_key sql_stats_cache_key{};
  if (!set_sql_stats_cache_key(thd->mt_key_value(THD::SQL_ID),
                               thd->mt_key_value(THD::PLAN_ID),
                               thd->mt_key_is_set(THD::PLAN_ID),
                               thd->mt_key_value(THD::CLIENT_ID),
                               db_id, user_id,
                               sql_stats_cache_key.data()))
    return;

  bool lock_acquired = mt_lock(&LOCK_global_sql_stats);

  // Check again inside the lock and release the lock if exiting
  if (is_sql_stats_collection_above_limit())
  {
    mt_unlock(lock_acquired, &LOCK_global_sql_stats);
    return;
  }

  current_max_sql_stats_count = max_sql_stats_count;
  current_max_sql_stats_size = max_sql_stats_size;

  /* Get or create client attributes for this statement. */
  auto client_id_iter = global_sql_stats.client_attrs->find(
      thd->mt_key_value(THD::CLIENT_ID));
  if (client_id_iter == global_sql_stats.client_attrs->end()) {
    global_sql_stats.client_attrs->emplace(thd->mt_key_value(THD::CLIENT_ID),
                                    std::string(thd->client_attrs_string.ptr(),
                                                thd->client_attrs_string.length()));

    ulonglong entry_size = MD5_HASH_SIZE + thd->client_attrs_string.length();
    global_sql_stats.size += entry_size;
    sql_stats_size += entry_size;
  }

  /* Get or create the SQL_TEXT object for this sql statement. */
  auto sql_text_iter= global_sql_stats.text->find(thd->mt_key_value(THD::SQL_ID));
  if (sql_text_iter == global_sql_stats.text->end())
  {
    SQL_TEXT *sql_text;
    if (!(sql_text= ((SQL_TEXT*)my_malloc(sizeof(SQL_TEXT), MYF(MY_WME)))) ||
        !(set_sql_text_attributes(sql_text, thd->lex->sql_command,
                            &thd->m_digest->m_digest_storage)))
    {
      sql_print_error("Cannot allocate memory for SQL_TEXT.");
      mt_unlock(lock_acquired, &LOCK_global_sql_stats);
      return;
    }

    auto ret= global_sql_stats.text->emplace(thd->mt_key_value(THD::SQL_ID), sql_text);
    if (!ret.second)
    {
      sql_print_error("Failed to insert SQL_TEXT into the hash map.");
      my_free((char*)sql_text->sql_text);
      my_free((char*)sql_text);
      mt_unlock(lock_acquired, &LOCK_global_sql_stats);
      return;
    }

    ulonglong entry_size = MD5_HASH_SIZE + sizeof(SQL_TEXT);
    global_sql_stats.size += entry_size;
    sql_stats_size += entry_size;
  }

  bool get_sample_query = false;
  /* Get or create the SQL_STATS object for this sql statement. */
  SQL_STATS *sql_stats = nullptr;
  auto sql_stats_iter= global_sql_stats.stats->find(sql_stats_cache_key);
  if (sql_stats_iter == global_sql_stats.stats->end())
  {
    if (!(sql_stats= ((SQL_STATS*)my_malloc(sizeof(SQL_STATS), MYF(MY_WME)))))
    {
      sql_print_error("Cannot allocate memory for SQL_STATS.");
      mt_unlock(lock_acquired, &LOCK_global_sql_stats);
      return;
    }

    set_sql_stats_attributes(sql_stats,
                             thd->mt_key_value(THD::SQL_ID),
                             thd->mt_key_value(THD::PLAN_ID),
                             thd->mt_key_is_set(THD::PLAN_ID),
                             thd->mt_key_value(THD::CLIENT_ID),
                             db_id, user_id);
    sql_stats->reset();

    auto ret= global_sql_stats.stats->emplace(sql_stats_cache_key, sql_stats);
    if (!ret.second)
    {
      sql_print_error("Failed to insert SQL_STATS into the hash table.");
      my_free((char*)sql_stats);
      mt_unlock(lock_acquired, &LOCK_global_sql_stats);
      return;
    }

    ulonglong entry_size = MD5_HASH_SIZE + sizeof(SQL_STATS);
    global_sql_stats.size += entry_size;
    sql_stats_size += entry_size;

    global_sql_stats.count++;
    sql_stats_count++;

		// Do not sample if max_digest_sample_age set to -1
    get_sample_query = max_digest_sample_age >= 0;
  } else {
    sql_stats= sql_stats_iter->second;
  }

  /* Re-sample if last sample is too old */
	uint time_now = my_time(true);
  if (!get_sample_query && max_digest_sample_age > 0) {
			uint sample_age = time_now - sql_stats->query_sample_seen;
			/* Comparison in micro seconds. */
      get_sample_query = sample_age > max_digest_sample_age ? true : false;
  }
  if (get_sample_query) {
    my_free((char *)sql_stats->query_sample_text);
    sql_stats->query_sample_text =
        (char *)my_malloc(sub_query_length + 1, MYF(MY_WME));
    memcpy(sql_stats->query_sample_text, sub_query, sub_query_length);
    sql_stats->query_sample_text[sub_query_length] = '\0';
    sql_stats->query_sample_seen = time_now;
  }
  /* Update stats */
  sql_stats->shared_stats.rows_sent += stats->rows_sent;
  sql_stats->shared_stats.tmp_table_bytes_written += stats->tmp_table_bytes_written;
  sql_stats->shared_stats.filesort_bytes_written += stats->filesort_bytes_written;
  sql_stats->shared_stats.index_dive_count += stats->index_dive_count;
  sql_stats->shared_stats.index_dive_cpu += stats->index_dive_cpu;
  sql_stats->shared_stats.compilation_cpu += stats->compilation_cpu;
  sql_stats->shared_stats.tmp_table_disk_usage += stats->tmp_table_disk_usage;
  sql_stats->shared_stats.filesort_disk_usage += stats->filesort_disk_usage;

  // Update CPU stats
  sql_stats->shared_stats.stmt_cpu_utime += stats->stmt_cpu_utime;

  // Update elapsed time
  sql_stats->shared_stats.stmt_elapsed_utime += stats->stmt_elapsed_utime;

  // Update Row counts
  sql_stats->shared_stats.rows_inserted += stats->rows_inserted;
  sql_stats->shared_stats.rows_updated += stats->rows_updated;
  sql_stats->shared_stats.rows_deleted += stats->rows_deleted;
  sql_stats->shared_stats.rows_read += stats->rows_read;

  if (statement_completed) {
    sql_stats->count++;
    if (thd->get_stmt_da()->is_error() &&
        thd->get_stmt_da()->sql_errno() == ER_DUPLICATE_STATEMENT_EXECUTION)
      sql_stats->skipped_count++;
  }

  mt_unlock(lock_acquired, &LOCK_global_sql_stats);
}

/**
  Merge various stats values. Note that 'storage' is always a pointer to the
  underlying value regardless of map storing pointer to value or value directly.

  @param base     initial value
  @param update   updated value
  @param out      merged value to return
  @param storage  storage for underlying merged struct if needed
*/
static void sql_stats_merge(SQL_STATS *base, SQL_STATS *update, SQL_STATS **out,
                            SQL_STATS *storage)
{
  /* Aggregate counters in provided storage and return its pointer. */
  if (storage != base)
    *storage = *base;

  /* Query sample text of the base is preserved to simplify the logic.
     Otherwise when base is merged, the current one needs to be freed,
     and the updated one needs to be moved here to make sure it's not
     freed when the whole updated value is freed. */

  storage->count += update->count;
  storage->skipped_count += update->skipped_count;
  storage->shared_stats.rows_sent += update->shared_stats.rows_sent;
  storage->shared_stats.tmp_table_bytes_written += update->shared_stats.tmp_table_bytes_written;
  storage->shared_stats.filesort_bytes_written += update->shared_stats.filesort_bytes_written;
  storage->shared_stats.index_dive_count += update->shared_stats.index_dive_count;
  storage->shared_stats.index_dive_cpu += update->shared_stats.index_dive_cpu;
  storage->shared_stats.compilation_cpu += update->shared_stats.compilation_cpu;
  storage->shared_stats.tmp_table_disk_usage += update->shared_stats.tmp_table_disk_usage;
  storage->shared_stats.filesort_disk_usage += update->shared_stats.filesort_disk_usage;
  storage->shared_stats.rows_inserted += update->shared_stats.rows_inserted;
  storage->shared_stats.rows_updated += update->shared_stats.rows_updated;
  storage->shared_stats.rows_deleted += update->shared_stats.rows_deleted;
  storage->shared_stats.rows_read += update->shared_stats.rows_read;
  storage->shared_stats.stmt_cpu_utime += update->shared_stats.stmt_cpu_utime;
  storage->shared_stats.stmt_elapsed_utime
    += update->shared_stats.stmt_elapsed_utime;
  *out = storage;
}

static void sql_stats_merge(SQL_TEXT *base, SQL_TEXT *update, SQL_TEXT **out,
                            SQL_TEXT *storage)
{
  /* SQL_TEXT is the same so simply return the base value. */
  *out = base;
}

static void sql_stats_merge(std::string base, std::string update, std::string *out,
                            std::string *storage)
{
  /* Client attributes are the same so simply return the base value. */
  *out = base;
}

/**
  Update snapshot stats. Also increment size and count of merged values.

  @param base     initial value in snapshot
  @param update   updated value in global stats
  @param count    count of merged entries to increment
  @param size     size of merged entries to increment
*/
static void sql_stats_merge_base(SQL_STATS *base, SQL_STATS *update,
                                 ulonglong &count, ulonglong &size)
{
  /* Use base as storage for the merge. */
  SQL_STATS *out;
  sql_stats_merge(base, update, &out, base);

  size += MD5_HASH_SIZE + sizeof(SQL_STATS);
  ++count;
}

static void sql_stats_merge_base(SQL_TEXT *base, SQL_TEXT *update,
                                 ulonglong &count, ulonglong &size)
{
  /* SQL_TEXT is the same so nothing to merge. */
  size += MD5_HASH_SIZE + sizeof(SQL_TEXT);
}

static void sql_stats_merge_base(std::string base, std::string update,
                                 ulonglong &count, ulonglong &size)
{
  /* Client attributes are the same so nothing to merge. */
  size += MD5_HASH_SIZE + update.length();
}

/**
  @brief Iterator over both snapshot and global stats.

  @details If sql_stats_snapshot is ON then
             iterate over snapshot map;
           else if snapshot doesn't exist or it's marked for drop then
             iterate over global stats;
           else merge snapshot and global stats
             1. iterate over snapshot and look up entries in global map
                  if found then return merged stats else return snapshot entry
             2. swap iteration and lookup maps
             3. iterate over global map and look up entries in snapshot
                  if found then skip entry else return global entry.
*/
template<typename T>
class Stats_iterator
{
  /* Map to iterate over. */
  Stats_map<T> *iterator_map = nullptr;

  /* Map to lookup entries during iteration, optional. */
  Stats_map<T> *lookup_map = nullptr;

  /* Underlying map iterator. */
  typename Stats_map<T>::const_iterator iter;

  /* Merged stats entry. If entry is a pointer, it could point to storage
     below. */
  T merged_value;

  /* Storage for merged stats entry, needed in case the entry is a pointer
     to a separately allocated memory. */
  typename std::remove_pointer<T>::type merged_value_storage;

  /* Has the caller already acquired global stats lock? Default is true
     pretending that lock is already held, so it doesn't get released
     in destructor. */
  bool lock_acquired = true;

  /* Have iterator and lookup maps been swapped for second pass? */
  bool swapped = false;

  /* Whether to return current iterator entry or merged entry. */
  bool use_merged_value = false;

public:
  /* Constructor. */
  Stats_iterator(Stats_map<T> *&snapshot_map, Stats_map<T> *&global_map,
                 THD *thd)
  {
    /* Before starting iteration see if auto snapshot should be created. */
    thd->auto_create_sql_stats_snapshot();

    /* Iterator always takes read lock on snapshot even if snapshot doesn't
       exist. Since in that case it will also take the global stats mutex,
       that would have blocked creation of new snapshot anyway. */
    mysql_rwlock_rdlock(&LOCK_sql_stats_snapshot);

    if (thd->variables.sql_stats_snapshot)
      /* Snapshot is enabled so just iterate over snapshot. */
      iterator_map = snapshot_map;
    else
    {
      lock_acquired = mt_lock(&LOCK_global_sql_stats);

      /* From outside snapshot can only be seen if not marked for deletion. */
      if (snapshot_map && !sql_stats_snapshot.drop_maps)
      {
        /* Snapshot exists and is visible. So it's going to be a two pass
           full join of two maps. See two_map_processing() for details. */
        iterator_map = snapshot_map;
        lookup_map = global_map;
      }
      else
      {
        /* Just iterate over the global stats. */
        iterator_map = global_map;
      }
    }

    iter = iterator_map->cbegin();
    two_map_processing();
  }

  /* Destructor. */
  ~Stats_iterator()
  {
    /* Unlock global stats if needed. */
    mt_unlock(lock_acquired, &LOCK_global_sql_stats);
    mysql_rwlock_unlock(&LOCK_sql_stats_snapshot);
  }

  /* Check if move_to_next() can be called. */
  bool is_valid()
  {
    return iter != iterator_map->cend();
  }

  /* Move iterator to next entry. */
  void move_to_next()
  {
    DBUG_ASSERT(is_valid());
    ++iter;
    two_map_processing();
  }

  /* Get key of current iterator position. */
  const md5_key &get_key()
  {
    DBUG_ASSERT(is_valid());
    return iter->first;
  }

  /* Get value of current iterator position. */
  const T &get_value()
  {
    DBUG_ASSERT(is_valid());
    return use_merged_value ? merged_value : iter->second;
  }

private:
  /* Perform processing for two map join. It is called after the iterator has
     changed position. The following conditions are checked:
      - if still on first map, look up entry in second map and merge two stats
        entries if needed;
      - if reached end of first map, perform swap and start iterating over
        second map;
      - if on second map, skip all entries present in first map until non-dup
        entry is found, or end of map is reached. */
  void two_map_processing()
  {
    /* Extra processing is needed only for the case of two maps. */
    if (lookup_map)
    {
      use_merged_value = false;
      if (!swapped && !is_valid())
      {
        /* Reached the end of first map, swap maps to go through second. */
        std::swap(iterator_map, lookup_map);
        iter = iterator_map->cbegin();
        swapped = true;
      }

      if (!swapped)
      {
        /* Merge duplicates on the first pass. */
        auto update = lookup_map->find(iter->first);
        if (update != lookup_map->end())
        {
          sql_stats_merge(iter->second, update->second, &merged_value,
                          &merged_value_storage);
          use_merged_value = true;
        }
      }
      else
      {
        /* Exclude duplicates on the second pass.
           Note: it is possible to mark entries in the second map as 'seen'
           that were found on the first pass, and avoid the lookups on the
           second pass. However if iteration was interrupted due to some
           error, the 'seen' marks would remain and cause problems for next
           iteration. Versioning marks would solve this problem at the expense
           of more memory used per entry. Since iterating over two maps is
           not frequent, optimizing for memory is more important. */
        while (is_valid() && lookup_map->find(iter->first) != lookup_map->end())
        {
          ++iter;
        }
      }
    }
  }
};

/*
 Pre-conditions to check beefore filling SQL_STATISTICS, SQL_TEXT and
 CLIENT_ATTRIBUTES tables.
*/
static int check_pre_fill_conditions(THD *thd, const char *table_name,
                                     THD::enum_mt_table_name table_name_enum,
                                     const char *mutex_name,
                                     bool check_mt_access_control = true)
{
  int result = 0;

  if (!thd->variables.sql_stats_read_control)
  {
    my_error(ER_IS_TABLE_READ_DISABLED, MYF(0), table_name);
    result = -1;
  }

  if (!result &&
      check_mt_access_control && mt_tables_access_control &&
      check_global_access(thd, PROCESS_ACL))
  {
    result = -1;
  }

  if (!result && thd->get_mt_table_filled(table_name_enum))
  {
    my_error(ER_IS_TABLE_REPEATED_READ, MYF(0),
        table_name, mutex_name);
    result = -1;
  } else {
    thd->set_mt_table_filled(table_name_enum);
  }

  /* Ugly - but prevent query from updating stats intermittently and causing a deadlock*/
  if (thd->should_update_stats) {
    thd->should_update_stats = false;
  }

  return result;
}

/* Fills the SQL_STATISTICS table. */
int fill_sql_stats(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_sql_stats");

  int result = check_pre_fill_conditions(thd, "SQL_STATISTICS", THD::SQL_STATS,
                                         "global_sql_stats");

  if (!result)
  {
    TABLE* table= tables->table;
    Stats_iterator<SQL_STATS *> iter(sql_stats_snapshot.stats,
                                     global_sql_stats.stats, thd);
    MYSQL_TIME time;
    ID_NAME_MAP db_map;
    ID_NAME_MAP user_map;
    fill_invert_map(DB_MAP_NAME, &db_map);
    fill_invert_map(USER_MAP_NAME, &user_map);

    for (; iter.is_valid(); iter.move_to_next())
    {
      int f= 0;
      SQL_STATS *sql_stats = iter.get_value();

      /* skip this one if the statistics are not valid */
      if (!valid_sql_stats(sql_stats))
        continue;

      restore_record(table, s->default_values);

      char sql_id_hex_string[MD5_BUFF_LENGTH];
      array_to_hex(sql_id_hex_string, sql_stats->sql_id, MD5_HASH_SIZE);

      /* SQL ID */
      table->field[f++]->store(sql_id_hex_string, MD5_BUFF_LENGTH,
                               system_charset_info);
      /* PLAN ID */
      if (sql_stats->plan_id_set)
      {
        char plan_id_hex_string[MD5_BUFF_LENGTH];
        array_to_hex(plan_id_hex_string, sql_stats->plan_id, MD5_HASH_SIZE);

        table->field[f]->set_notnull();
        table->field[f++]->store(plan_id_hex_string, MD5_BUFF_LENGTH,
                                 system_charset_info);
      }
      else
        table->field[f++]->set_null();

      char client_id_hex_string[MD5_BUFF_LENGTH];
      array_to_hex(client_id_hex_string, sql_stats->client_id, MD5_HASH_SIZE);

      /* CLIENT ID */
      table->field[f++]->store(client_id_hex_string, MD5_BUFF_LENGTH,
                               system_charset_info);

      const char *a_name;
      /* Table Schema */
      if (!(a_name= get_name(&db_map, sql_stats->db_id)))
        a_name= "UNKNOWN";
      table->field[f++]->store(a_name, strlen(a_name), system_charset_info);

      /* User Name */
      if (!(a_name= get_name(&user_map, sql_stats->user_id)))
        a_name= "UNKNOWN";
      table->field[f++]->store(a_name, strlen(a_name), system_charset_info);

      /* Query Sample Text */
      table->field[f++]->store(sql_stats->query_sample_text,
                              strlen(sql_stats->query_sample_text),
                              system_charset_info);
      /* Query Last Sample Timestamp */
      if (sql_stats->query_sample_seen == 0) {
        table->field[f++]->set_null();
      } else {
        thd->variables.time_zone->gmt_sec_to_TIME(
            &time, (my_time_t)sql_stats->query_sample_seen);
        table->field[f]->set_notnull();
        table->field[f++]->store_time(&time);
      }

      /* Execution Count */
      table->field[f++]->store(sql_stats->count, TRUE);
      /* Skipped Count */
      table->field[f++]->store(sql_stats->skipped_count, TRUE);
      /* Rows inserted */
      table->field[f++]->store(sql_stats->shared_stats.rows_inserted, TRUE);
      /* Rows updated */
      table->field[f++]->store(sql_stats->shared_stats.rows_updated, TRUE);
      /* Rows deleted */
      table->field[f++]->store(sql_stats->shared_stats.rows_deleted, TRUE);
      /* Rows read */
      table->field[f++]->store(sql_stats->shared_stats.rows_read, TRUE);
      /* Rows returned to the client */
      table->field[f++]->store(sql_stats->shared_stats.rows_sent, TRUE);
      /* Total CPU in microseconds */
      table->field[f++]->store(sql_stats->shared_stats.stmt_cpu_utime, TRUE);
      /* Total Elapsed time in microseconds */
      table->field[f++]->store(sql_stats->shared_stats.stmt_elapsed_utime, TRUE);
      /* Bytes written to temp table space */
      table->field[f++]->store(sql_stats->shared_stats.tmp_table_bytes_written, TRUE);
      /* Bytes written to filesort space */
      table->field[f++]->store(sql_stats->shared_stats.filesort_bytes_written, TRUE);
      /* Index dive count */
      table->field[f++]->store(sql_stats->shared_stats.index_dive_count, TRUE);
      /* Index dive CPU */
      table->field[f++]->store(sql_stats->shared_stats.index_dive_cpu, TRUE);
      /* Compilation CPU */
      table->field[f++]->store(sql_stats->shared_stats.compilation_cpu, TRUE);
      /* Tmp table disk usage */
      table->field[f++]->store(sql_stats->shared_stats.tmp_table_disk_usage, TRUE);
      /* Filesort disk usage */
      table->field[f++]->store(sql_stats->shared_stats.filesort_disk_usage, TRUE);

      if (schema_table_store_record(thd, table))
        result = -1;
    }
  }

  DBUG_RETURN(result);
}

/* Fills the SQL_TEXT table. */
int fill_sql_text(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_sql_text");

  int result = check_pre_fill_conditions(thd, "SQL_TEXT", THD::SQL_TEXT,
                                         "global_sql_stats");

  if (!result)
  {
    TABLE* table= tables->table;
    Stats_iterator<SQL_TEXT *> iter(sql_stats_snapshot.text,
                                    global_sql_stats.text, thd);

    for (; iter.is_valid(); iter.move_to_next())
    {
      int f= 0;
      SQL_TEXT *sql_text = iter.get_value();

      restore_record(table, s->default_values);

      char sql_id_hex_string[MD5_BUFF_LENGTH];
      array_to_hex(sql_id_hex_string, iter.get_key().data(),
                   iter.get_key().size());

      /* SQL ID */
      table->field[f++]->store(sql_id_hex_string, MD5_BUFF_LENGTH,
                               system_charset_info);

      /* SQL Type */
      std::string sql_type = sql_cmd_type(sql_text->sql_type);
      table->field[f++]->store(sql_type.c_str(), sql_type.length(),
                               system_charset_info);

      /* SQL Text length */
      table->field[f++]->store(sql_text->sql_text_length, TRUE);

      /* SQL Text */
      table->field[f++]->store(sql_text->sql_text,
                               sql_text->sql_text_arr_len,
                               system_charset_info);

      if (schema_table_store_record(thd, table))
        result = -1;
    }
  }

  DBUG_RETURN(result);
}

int fill_client_attrs(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_client_attrs");

  int result = check_pre_fill_conditions(thd, "CLIENT_ATTRIBUTES",
                                         THD::CLIENT_ATTRS,
                                         "global_sql_stats", false);

  if (!result)
  {
    TABLE* table= tables->table;
    Stats_iterator<std::string> iter(sql_stats_snapshot.client_attrs,
                                     global_sql_stats.client_attrs, thd);

    for (; iter.is_valid(); iter.move_to_next())
    {
      int f= 0;

      restore_record(table, s->default_values);

      char client_id_hex_string[MD5_BUFF_LENGTH];
      array_to_hex(client_id_hex_string, iter.get_key().data(),
                   iter.get_key().size());

      /* CLIENT_ID */
      table->field[f++]->store(client_id_hex_string, MD5_BUFF_LENGTH,
                               system_charset_info);

      /* CLIENT_ATTRIBUTES */
      table->field[f++]->store(iter.get_value().c_str(),
                               std::min(4096, (int)iter.get_value().size()),
                               system_charset_info);

      if (schema_table_store_record(thd, table))
      {
        result = -1;
        break;
      }
    }
  }

  DBUG_RETURN(result);
}

/*
  fill_sql_findings
    Fills the rows returned for statements selecting from SQL_FINDINGS

*/
int fill_sql_findings(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_sql_findings");
  TABLE* table= tables->table;

  int result = 0;
  if (mt_tables_access_control && check_global_access(thd, PROCESS_ACL))
    result = -1;

  bool lock_acquired = mt_lock(&LOCK_global_sql_findings);

  for (auto sql_iter= global_sql_findings_map.cbegin();
       !result && sql_iter != global_sql_findings_map.cend(); ++sql_iter)
  {
    char sql_id_hex_string[MD5_BUFF_LENGTH];
    array_to_hex(sql_id_hex_string, sql_iter->first.data(), sql_iter->first.size());

    for(auto f_iter = sql_iter->second.cbegin();
        !result && f_iter != sql_iter->second.cend(); ++f_iter)
    {
      int f= 0;
      restore_record(table, s->default_values);

      /* SQL ID */
      table->field[f++]->store(sql_id_hex_string, MD5_BUFF_LENGTH,
                               system_charset_info);

      /* CODE */
      table->field[f++]->store(f_iter->code, TRUE);

      /* LEVEL */
      table->field[f++]->store(warning_level_names[f_iter->level].str,
                               std::min((uint)warning_level_names[f_iter->level].length,
                                        sf_max_level_size),
                               system_charset_info);

      /* MESSAGE */
      table->field[f++]->store(f_iter->message.c_str(),
                               std::min((uint)f_iter->message.length(),
                                        sf_max_message_size),
                               system_charset_info);

      /* QUERY_TEXT */
      table->field[f++]->store(f_iter->query_text.c_str(),
                               std::min((uint)f_iter->query_text.length(),
                                        sf_max_query_size),
                               system_charset_info);

      /* COUNT */
      table->field[f++]->store(f_iter->count, TRUE);

      /* LAST_RECORDED */
      table->field[f++]->store(f_iter->last_recorded, TRUE);

      if (schema_table_store_record(thd, table))
        result = -1;
    }
  }
  mt_unlock(lock_acquired, &LOCK_global_sql_findings);

  DBUG_RETURN(result);
}

/**
  @brief Merge current stats map into snapshot stats map.
*/
template<typename T>
static void sql_stats_map_merge(Stats_map<T> *snapshot_map,
                                Stats_map<T> *global_map,
                                ulonglong &count, ulonglong &size)
{
  /* Iterate through updates that will be applied to snapshot. */
  for (auto update = global_map->begin(); update != global_map->end();
      ++update)
  {
    /* base is a pair to iterator and bool indicating insert result. */
    auto base = snapshot_map->insert(*update);
    if (base.second)
    {
      /* Update entry was not found in snapshot so it was inserted (shallow
         copied) into snapshot map. Reset it to avoid pointing to the same
         memory that snapshot entry is now using. This syntax allows both
         setting pointers to nullptr, and also invoke default constructor on
         non-pointer types. */
      update->second = {};
    }
    else
    {
      /* Existing entry was found so merge update entry into snapshot entry. */
      sql_stats_merge_base(base.first->second, update->second, count, size);
    }
  }
}

/**
  @brief Notification that sql_stats_snapshot variable is about to change.

  @retval false  success
  @retval true   out of memory
*/
bool toggle_sql_stats_snapshot(THD *thd)
{
  /* Assume success. */
  bool result = false;
  Sql_stats_maps old_stats;
  mysql_rwlock_wrlock(&LOCK_sql_stats_snapshot);

  /* Validate snapshot state. */
  DBUG_ASSERT(sql_stats_snapshot.is_set() == sql_stats_snapshot.ref_count > 0);

  if (!thd->variables.sql_stats_snapshot)
  {
    /* OFF -> ON transition. If new ref_count is greater than 1 then snapshot
       is already created so this session will just use it. */
    if (++sql_stats_snapshot.ref_count == 1)
    {
      /* Take new snapshot. */
      Sql_stats_maps new_maps;
      if (new_maps.init())
      {
        bool lock_acquired = mt_lock(&LOCK_global_sql_stats);

        /* Move global stats to snapshot. */
        sql_stats_snapshot.move_maps(global_sql_stats);

        /* Set new empty stats maps. */
        global_sql_stats.move_maps(new_maps);

        mt_unlock(lock_acquired, &LOCK_global_sql_stats);

        sql_stats_snapshot_status = TRUE;
      }
      else
      {
        /* We're OOM so cannot enable snapshot. */
        --sql_stats_snapshot.ref_count;
        result = true;
      }
    }
  }
  else
  {
    /* ON -> OFF transition. Only the last user of the snapshot cleans it up
       by either deleting it or merging with global stats. */
    if (--sql_stats_snapshot.ref_count == 0)
    {
      if (sql_stats_snapshot.drop_maps)
        sql_stats_snapshot.cleanup();
      else
      {
        ulonglong dup_count = 0;
        ulonglong dup_size = 0;
        bool lock_acquired = mt_lock(&LOCK_global_sql_stats);

        /* Merge current stats into snapshot. */
        sql_stats_map_merge(sql_stats_snapshot.stats, global_sql_stats.stats,
                            dup_count, dup_size);
        sql_stats_map_merge(sql_stats_snapshot.text, global_sql_stats.text,
                            dup_count, dup_size);
        sql_stats_map_merge(sql_stats_snapshot.client_attrs,
                            global_sql_stats.client_attrs, dup_count, dup_size);

        /* Now snapshot has new entries so add their count and size. */
        sql_stats_snapshot.count += global_sql_stats.count - dup_count;
        sql_stats_snapshot.size += global_sql_stats.size - dup_size;

        /* Save snapshot counters as new counters. */
        sql_stats_count = sql_stats_snapshot.count;
        sql_stats_size = sql_stats_snapshot.size;

        /* Move current stats to be cleaned up after locks are released. */
        old_stats.move_maps(global_sql_stats);

        /* Replace current stats with snapshot. */
        global_sql_stats.move_maps(sql_stats_snapshot);

        mt_unlock(lock_acquired, &LOCK_global_sql_stats);
      }

      sql_stats_snapshot_status = FALSE;
    }
  }

  mysql_rwlock_unlock(&LOCK_sql_stats_snapshot);
  return result;
}

/**
  @brief Handler for FLUSH SQL_STATISTICS.
*/
void flush_sql_statistics(THD *thd)
{
  /*
    see if we should enable sql_stats_snapshot automatically for flush,
    so that it doesn't stall the user workload due to prolonged holding of
    the global sql stats mutex
    */
  thd->auto_create_sql_stats_snapshot();

  if (thd->variables.sql_stats_snapshot)
  {
    mark_sql_stats_snapshot_for_deletion();
  }
  else
  {
    free_global_sql_stats(false /*limits_updated*/);
  }

  thd->release_auto_created_sql_stats_snapshot();
}

/**
  @brief Sql_stats_maps default constructor.
*/
Sql_stats_maps::Sql_stats_maps()
{
  reset_maps();
  ref_count = 0;
  drop_maps = false;
}

/**
  @brief Sql_stats_maps destructor.
*/
Sql_stats_maps::~Sql_stats_maps()
{
  cleanup();
}

/**
  @brief Create the maps.
  @return true if all are successfully created, false otherwise.
*/
bool Sql_stats_maps::init()
{
  stats = new Sql_stats_map;
  text = new Sql_text_map;
  client_attrs = new Client_attrs_map;

  if (!is_set())
    cleanup();

  return is_set();
}

/**
  @brief Check if all maps are set.
  @return true if all maps are set, false otherwise.
*/
bool Sql_stats_maps::is_set()
{
  return stats && text && client_attrs;
}

/**
  @brief Reset map pointers and sizes.
*/
void Sql_stats_maps::reset_maps()
{
  stats = nullptr;
  text = nullptr;
  client_attrs = nullptr;
  size = 0;
  count = 0;
}

/**
  @brief Free the content and delete the maps.
*/
void Sql_stats_maps::cleanup()
{
  if (is_set())
    free_sql_stats_maps(*this);

  delete stats;
  delete text;
  delete client_attrs;

  reset_maps();
  ref_count = 0;
  drop_maps = false;
}

/**
  @brief Move a set of maps into this one.
*/
void Sql_stats_maps::move_maps(Sql_stats_maps &other)
{
  DBUG_ASSERT(!is_set());

  stats = other.stats;
  text = other.text;
  client_attrs = other.client_attrs;
  size = other.size;
  count = other.count;

  other.reset_maps();
}

/*
  Stores the client attribute names
*/
void store_client_attribute_names(char *new_value) {
  std::vector<std::string> new_attr_names = split_into_vector(new_value, ',');

  bool lock_acquired = mt_lock(&LOCK_global_sql_stats);
  client_attribute_names = new_attr_names;
  mt_unlock(lock_acquired, &LOCK_global_sql_stats);
}
