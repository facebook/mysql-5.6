#include "column_statistics_dt.h"
#include "sql_base.h"
#include "sql_show.h"
#include "sql_string.h"
#include "sql_stats.h"
#include "tztime.h"                             // struct Time_zone
#include <mysql/plugin_rim.h>

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
#ifndef DBUG_OFF
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

  Returns FALSE if the number of duplicate executions exceeds the limit
*/
bool register_active_sql(THD *thd, char *query_text, uint query_length)
{
  /* Exit now if any of the conditions that prevent the feature is true
     - feature is off
     - command type is not SELECT
     - EXPLAIN
     - in a transaction
  */
  if (sql_maximum_duplicate_executions == 0  ||
      thd->lex->sql_command != SQLCOM_SELECT ||
      thd->lex->describe                     ||
      thd->in_active_multi_stmt_transaction())
    return true;

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

  thd->mt_key_set(THD::SQL_HASH, sql_hash.data());
  DBUG_ASSERT(thd->mt_key_is_set(THD::SQL_HASH));

  bool ret = true;  // so far did not exceed the max number of dups
  bool lock_acquired = mt_lock(&LOCK_global_active_sql);
  auto iter = global_active_sql.find(sql_hash);
  if (iter == global_active_sql.end())
  {
    global_active_sql.emplace(sql_hash, 1); // its first occurrence
  }
  else
  {
    if (iter->second+1 > sql_maximum_duplicate_executions)
      ret = false;     // one too many
    else
      iter->second++;  // increment the number of duplicates
  }

  mt_unlock(lock_acquired, &LOCK_global_active_sql);
  return ret;
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
  {"QUERY_SAMPLE_TEXT", 4096, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
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
  {"SQL_TEXT", 4096, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},

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

#define WRITE_STATISTICS_DIMENSION_COUNT 4

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
  char md5_hex_buffer[MD5_BUFF_LENGTH];
  std::array<std::string, WRITE_STATISTICS_DIMENSION_COUNT> keys;
  // USER
  keys[0] = thd->get_user_name();
  // CLIENT ID
  array_to_hex(md5_hex_buffer, thd->mt_key_value(THD::CLIENT_ID).data(), MD5_HASH_SIZE);
  keys[1].assign(md5_hex_buffer, MD5_BUFF_LENGTH);
  // SHARD
  keys[2] = thd->get_db_name();
  // SQL ID
  array_to_hex(md5_hex_buffer, thd->mt_key_value(THD::SQL_ID).data(), MD5_HASH_SIZE);
  keys[3].assign(md5_hex_buffer, MD5_BUFF_LENGTH);
  
  // Add/Update the write stats
  for (int i = 0; i<WRITE_STATISTICS_DIMENSION_COUNT; i++) 
  {
    auto iter = time_bucket_stats[i].find(keys[i]);
    if (iter == time_bucket_stats[i].end()) 
    {
      WRITE_STATS ws;
      ws.binlog_bytes_written = binlog_bytes_written;
      ws.cpu_write_time_ms = total_write_time;
      time_bucket_stats[i].insert(std::make_pair(keys[i], ws));
    } else 
    {
      WRITE_STATS & ws = iter->second;
      ws.binlog_bytes_written += binlog_bytes_written;
      ws.cpu_write_time_ms += total_write_time;
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
  bool lock_acquired = mt_lock(&LOCK_global_write_statistics);
  time_t timestamp = my_time(0);
  int time_bucket_key = timestamp - (timestamp % write_stats_frequency);

  auto time_bucket_iter = global_write_statistics_map.begin();
  if (time_bucket_iter == global_write_statistics_map.end() 
    ||  time_bucket_key > time_bucket_iter->first)
  {
    // time_bucket is newer than last registered bucket. need to insert a new one
    while((uint)global_write_statistics_map.size() >= write_stats_count) 
    {
      // reached max time bucket count. Erase the oldest time bucket stats
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
      for(auto stats_iter = time_bucket[type].begin(); stats_iter != time_bucket[type].end(); ++stats_iter) 
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
        table->field[f++]->store(type_string.c_str(), type_string.length(), system_charset_info);

        // value
        table->field[f++]->store(stats_iter->first.c_str(), stats_iter->first.length(), system_charset_info);

        // binlog_bytes_written
        table->field[f++]->store(stats_iter->second.binlog_bytes_written, TRUE);

        // cpu_write_time_ms
        table->field[f++]->store(stats_iter->second.cpu_write_time_ms, TRUE);

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
ulonglong sql_stats_size= 0;

extern ulonglong sql_plans_size;
ulonglong sql_findings_size = 0;


/*
  These are used to determine if existing stats need to be flushed on changing
  the limits.
*/
ulonglong current_max_sql_stats_count= 0;
ulonglong current_max_sql_stats_size= 0;

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
      sql_stats->rows_sent == 0 &&
      sql_stats->tmp_table_bytes_written == 0 &&
      sql_stats->filesort_bytes_written == 0 &&
      sql_stats->index_dive_count == 0 &&
      sql_stats->index_dive_cpu == 0 &&
      sql_stats->compilation_cpu == 0 &&
      sql_stats->tmp_table_disk_usage == 0 &&
      sql_stats->filesort_disk_usage == 0 &&
      sql_stats->shared_stats.rows_inserted == 0 &&
      sql_stats->shared_stats.rows_updated == 0 &&
      sql_stats->shared_stats.rows_deleted == 0 &&
      sql_stats->shared_stats.rows_read == 0 &&
      sql_stats->shared_stats.us_tot == 0)
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

static void set_sql_text_attributes(
  SQL_TEXT *sql_text_struct,
  enum_sql_command sql_type,
  const sql_digest_storage *digest_storage)
{
  sql_text_struct->sql_type = sql_type;
  sql_text_struct->digest_storage.reset(sql_text_struct->token_array_storage,
                                        sizeof(sql_text_struct->token_array_storage));
  // Compute digest just to get length.
  String digest_text;
  compute_digest_text(digest_storage, &digest_text);
  sql_text_struct->sql_text_length = digest_text.length();

  sql_text_struct->digest_storage.copy(digest_storage);
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
  stats->us_tot= thd->sql_cpu;
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
  /*
    thd->sql_cpu is per individual (sub-)query, and is not cumulative.
    So it does not need to be subtracted.
  */
  stats->us_tot= thd->sql_cpu;
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
      my_free(it->second);
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
    finding_vec out: - vector that stores the findings of the statement
                       (key is the warning code)
*/
static void populate_sql_findings(
    THD                    *thd,
    char                   *query_text,
    SQL_FINDING_VEC&        finding_vec)
{
  Diagnostics_area::Sql_condition_iterator it=
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
      sql_find.message.append(err->get_message_text(),
                              std::min((uint)err->get_message_octet_length(),
                                       sf_max_message_size));
      sql_find.query_text.append(query_text,
                                 std::min((uint)strlen(query_text),
                                          sf_max_query_size));
      sql_find.count         = 1;
      sql_find.last_recorded = now;
      finding_vec.push_back(sql_find);

      sql_findings_size += sizeof(SQL_FINDING);
    }
    else
    {
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
void store_sql_findings(THD *thd, char *query_text)
{
  if (sql_findings_control == SQL_INFO_CONTROL_ON &&
      thd->mt_key_is_set(THD::SQL_ID) &&
      thd->lex->select_lex.table_list.elements > 0) // contains at least one table
  {
    bool lock_acquired = mt_lock(&LOCK_global_sql_findings);

    // Lookup finding map for this statement
    auto sql_find_it = global_sql_findings_map.find(thd->mt_key_value(THD::SQL_ID));
    if (sql_find_it == global_sql_findings_map.end())
    {
      /* Check whether we reached the SQL stats limits  */
      if (!is_sql_stats_collection_above_limit())
      {
        // First time a finding is reported for this statement
        SQL_FINDING_VEC finding_vec;
        populate_sql_findings(thd, query_text, finding_vec);

        global_sql_findings_map.insert(
          std::make_pair(thd->mt_key_value(THD::SQL_ID), finding_vec));

        sql_findings_size += MD5_HASH_SIZE;     // for SQL_ID
      }
    }
    else
      populate_sql_findings(thd, query_text, sql_find_it->second);

    mt_unlock(lock_acquired, &LOCK_global_sql_findings);
  }
}

/***********************************************************************
                End - Functions to support SQL findings
************************************************************************/

/*
  update_sql_stats_after_statement
    Updates the SQL stats after every SQL statement.
    It is responsible for getting the SQL digest, storing and updating the
    underlying in-memory structures for SQL_TEXT and SQL_STATISTICS IS tables.
  Input:
    thd    in: - THD
    stats  in: - stats for the current SQL statement execution
*/
void update_sql_stats_after_statement(THD *thd, SHARED_SQL_STATS *stats, char* sub_query)
{
  // Do a light weight limit check without acquiring the lock.
  // There will be another check later while holding the lock.
  if (is_sql_stats_collection_above_limit()) return;

  /* Get the schema and the user name */
  const char *schema= thd->get_db_name();
  const char *user= thd->get_user_name();

  uint32_t db_id= get_id(DB_MAP_NAME, schema, strlen(schema));
  uint32_t user_id= get_id(USER_MAP_NAME, user, strlen(user));
  if (db_id == INVALID_NAME_ID || user_id == INVALID_NAME_ID)
    return;

  /*
    m_digest is just a defensive check and should never be null.

    m_digest_storage could be empty if sql_stats_control was turned on in the
    current statement. Since the variable was OFF during parse, but ON when
    update_sql_stats_after_statement is called, we won't have the digest
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
    if (!(sql_text= ((SQL_TEXT*)my_malloc(sizeof(SQL_TEXT), MYF(MY_WME)))))
    {
      sql_print_error("Cannot allocate memory for SQL_TEXT.");
      mt_unlock(lock_acquired, &LOCK_global_sql_stats);
      return;
    }

    set_sql_text_attributes(sql_text, thd->lex->sql_command,
                            &thd->m_digest->m_digest_storage);

    auto ret= global_sql_stats.text->emplace(thd->mt_key_value(THD::SQL_ID), sql_text);
    if (!ret.second)
    {
      sql_print_error("Failed to insert SQL_TEXT into the hash map.");
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
        (char *)my_malloc(strlen(sub_query) + 1, MYF(MY_WME));
    memcpy(sql_stats->query_sample_text, sub_query, strlen(sub_query) + 1);
    sql_stats->query_sample_seen = time_now;
  }
  /* Update stats */
  sql_stats->count++;
  if (thd->get_stmt_da()->is_error() &&
      thd->get_stmt_da()->sql_errno() == ER_DUPLICATE_STATEMENT_EXECUTION)
    sql_stats->skipped_count++;
  sql_stats->rows_sent += (ulonglong) thd->get_sent_row_count();
  sql_stats->tmp_table_bytes_written += thd->get_tmp_table_bytes_written();
  sql_stats->filesort_bytes_written += thd->get_filesort_bytes_written();
  sql_stats->index_dive_count += thd->get_index_dive_count();
  sql_stats->index_dive_cpu += thd->get_index_dive_cpu();
  sql_stats->compilation_cpu += thd->get_compilation_cpu();
  sql_stats->tmp_table_disk_usage += thd->get_stmt_tmp_table_disk_usage_peak();
  sql_stats->filesort_disk_usage += thd->get_stmt_filesort_disk_usage_peak();

  // Update Row counts
  sql_stats->shared_stats.rows_inserted += stats->rows_inserted;
  sql_stats->shared_stats.rows_updated += stats->rows_updated;
  sql_stats->shared_stats.rows_deleted += stats->rows_deleted;
  sql_stats->shared_stats.rows_read += stats->rows_read;

  // Update CPU stats
  sql_stats->shared_stats.us_tot += stats->us_tot;

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
  storage->rows_sent += update->rows_sent;
  storage->tmp_table_bytes_written += update->tmp_table_bytes_written;
  storage->filesort_bytes_written += update->filesort_bytes_written;
  storage->index_dive_count += update->index_dive_count;
  storage->index_dive_cpu += update->index_dive_cpu;
  storage->compilation_cpu += update->compilation_cpu;
  storage->tmp_table_disk_usage += update->tmp_table_disk_usage;
  storage->filesort_disk_usage += update->filesort_disk_usage;
  storage->shared_stats.rows_inserted += update->shared_stats.rows_inserted;
  storage->shared_stats.rows_updated += update->shared_stats.rows_updated;
  storage->shared_stats.rows_deleted += update->shared_stats.rows_deleted;
  storage->shared_stats.rows_read += update->shared_stats.rows_read;
  storage->shared_stats.us_tot += update->shared_stats.us_tot;
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

/* Fills the SQL_STATISTICS table. */
int fill_sql_stats(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_sql_stats");
  int result = 0;
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
    table->field[f++]->store(sql_stats->rows_sent, TRUE);
    /* Total CPU in microseconds */
    table->field[f++]->store(sql_stats->shared_stats.us_tot, TRUE);
    /* Bytes written to temp table space */
    table->field[f++]->store(sql_stats->tmp_table_bytes_written, TRUE);
    /* Bytes written to filesort space */
    table->field[f++]->store(sql_stats->filesort_bytes_written, TRUE);
    /* Index dive count */
    table->field[f++]->store(sql_stats->index_dive_count, TRUE);
    /* Index dive CPU */
    table->field[f++]->store(sql_stats->index_dive_cpu, TRUE);
    /* Compilation CPU */
    table->field[f++]->store(sql_stats->compilation_cpu, TRUE);
    /* Tmp table disk usage */
    table->field[f++]->store(sql_stats->tmp_table_disk_usage, TRUE);
    /* Filesort disk usage */
    table->field[f++]->store(sql_stats->filesort_disk_usage, TRUE);

    if (schema_table_store_record(thd, table))
    {
      result = -1;
      break;
    }
  }

  DBUG_RETURN(result);
}

/* Fills the SQL_TEXT table. */
int fill_sql_text(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_sql_text");
  int result = 0;
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

    String digest_text;
    compute_digest_text(&sql_text->digest_storage, &digest_text);
    table->field[f++]->store(sql_text->sql_text_length, TRUE);
    /* SQL Text */
    table->field[f++]->store(digest_text.c_ptr(),
                             std::min(digest_text.length(), (uint)4096),
                             system_charset_info);

    if (schema_table_store_record(thd, table))
    {
      result = -1;
      break;
    }
  }

  DBUG_RETURN(result);
}

int fill_client_attrs(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_client_attrs");
  int result = 0;
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

  DBUG_RETURN(result);
}

/*
  fill_sql_findings
    Fills the rows returned for statements selecting from SQL_FINDINGS

*/
int fill_sql_findings(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_sql_text");
  TABLE* table= tables->table;

  bool lock_acquired = mt_lock(&LOCK_global_sql_findings);

  for (auto sql_iter= global_sql_findings_map.cbegin();
       sql_iter != global_sql_findings_map.cend(); ++sql_iter)
  {
    char sql_id_hex_string[MD5_BUFF_LENGTH];
    array_to_hex(sql_id_hex_string, sql_iter->first.data(), sql_iter->first.size());

    for(auto f_iter : sql_iter->second)
    {
      int f= 0;
      restore_record(table, s->default_values);

      /* SQL ID */
      table->field[f++]->store(sql_id_hex_string, MD5_BUFF_LENGTH,
                               system_charset_info);

      /* CODE */
      table->field[f++]->store(f_iter.code, TRUE);

      /* LEVEL */
      table->field[f++]->store(warning_level_names[f_iter.level].str,
                               std::min((uint)warning_level_names[f_iter.level].length,
                                        sf_max_level_size),
                               system_charset_info);

      /* MESSAGE */
      table->field[f++]->store(f_iter.message.c_str(),
                               std::min((uint)f_iter.message.length(),
                                        sf_max_message_size),
                               system_charset_info);

      /* QUERY_TEXT */
      table->field[f++]->store(f_iter.query_text.c_str(),
                               std::min((uint)f_iter.query_text.length(),
                                        sf_max_query_size),
                               system_charset_info);

      /* COUNT */
      table->field[f++]->store(f_iter.count, TRUE);

      /* LAST_RECORDED */
      table->field[f++]->store(f_iter.last_recorded, TRUE);

      if (schema_table_store_record(thd, table))
      {
        mt_unlock(lock_acquired, &LOCK_global_sql_findings);
        DBUG_RETURN(-1);
      }
    }
  }
  mt_unlock(lock_acquired, &LOCK_global_sql_findings);

  DBUG_RETURN(0);
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
  if (thd->variables.sql_stats_snapshot)
  {
    mark_sql_stats_snapshot_for_deletion();
  }
  else
  {
    free_global_sql_stats(false /*limits_updated*/);
  }
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
