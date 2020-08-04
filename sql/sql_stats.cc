#include "column_statistics_dt.h"
#include "sql_base.h"
#include "sql_show.h"
#include "sql_string.h"
#include "tztime.h"                             // struct Time_zone

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

/* Global sql stats hash map to track and update metrics in-memory */
static std::unordered_map<md5_key, SQL_STATS*> global_sql_stats_map;

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

/* Global sql text hash map to track and update metrics in-memory */
static std::unordered_map<md5_key, SQL_TEXT*> global_sql_text_map;

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

static std::unordered_map<md5_key, std::string> global_client_attrs_map;

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


/*
  These are used to determine if existing stats need to be flushed on changing
  the limits.
*/
ulonglong current_max_sql_stats_count= 0;
ulonglong current_max_sql_stats_size= 0;

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

  for (auto it= global_sql_stats_map.begin(); it != global_sql_stats_map.end(); ++it)
  {
    my_free((char *) it->second->query_sample_text);
    my_free((char*)(it->second));
  }
  global_sql_stats_map.clear();

  for (auto it= global_sql_text_map.begin(); it != global_sql_text_map.end(); ++it)
  {
    my_free((char*)(it->second));
  }
  global_sql_text_map.clear();

  global_client_attrs_map.clear();

  sql_stats_count = 0;
  sql_stats_size = 0;

  current_max_sql_stats_count = max_sql_stats_count;
  current_max_sql_stats_size = max_sql_stats_size;

  mt_unlock(lock_acquired, &LOCK_global_sql_stats);
}

/*
  is_sql_stats_collection_above_limit
    Checks whether we reached the counter and size limits for the
    SQL metrics (statistics, text, client attributes, plans)
*/
bool is_sql_stats_collection_above_limit() {
  return (sql_stats_count >= max_sql_stats_count ||
          sql_stats_size + sql_plans_size  >= max_sql_stats_size);
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

      sql_stats_size += sizeof(SQL_FINDING);
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

        sql_stats_size += MD5_HASH_SIZE;     // for SQL_ID
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
  const char *schema= thd->db ? thd->db : "NULL";
  const USER_CONN* uc= thd->get_user_connect();
  const char *user= uc->user ? uc->user : "NULL";

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
  auto client_id_iter = global_client_attrs_map.find(
      thd->mt_key_value(THD::CLIENT_ID));
  if (client_id_iter == global_client_attrs_map.end()) {
    global_client_attrs_map.emplace(thd->mt_key_value(THD::CLIENT_ID),
                                    std::string(thd->client_attrs_string.ptr(),
                                                thd->client_attrs_string.length()));
    sql_stats_size += (MD5_HASH_SIZE + thd->client_attrs_string.length());
  }

  /* Get or create the SQL_TEXT object for this sql statement. */
  auto sql_text_iter= global_sql_text_map.find(thd->mt_key_value(THD::SQL_ID));
  if (sql_text_iter == global_sql_text_map.end())
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

    auto ret= global_sql_text_map.emplace(thd->mt_key_value(THD::SQL_ID), sql_text);
    if (!ret.second)
    {
      sql_print_error("Failed to insert SQL_TEXT into the hash map.");
      my_free((char*)sql_text);
      mt_unlock(lock_acquired, &LOCK_global_sql_stats);
      return;
    }
    sql_stats_size += (MD5_HASH_SIZE + sizeof(SQL_TEXT));
  }

  bool get_sample_query = false;
  /* Get or create the SQL_STATS object for this sql statement. */
  SQL_STATS *sql_stats = nullptr;
  auto sql_stats_iter= global_sql_stats_map.find(sql_stats_cache_key);
  if (sql_stats_iter == global_sql_stats_map.end())
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

    auto ret= global_sql_stats_map.emplace(sql_stats_cache_key, sql_stats);
    if (!ret.second)
    {
      sql_print_error("Failed to insert SQL_STATS into the hash table.");
      my_free((char*)sql_stats);
      mt_unlock(lock_acquired, &LOCK_global_sql_stats);
      return;
    }
    sql_stats_count++;
    sql_stats_size += (MD5_HASH_SIZE + sizeof(SQL_STATS));
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

/* Fills the SQL_STATISTICS table. */
int fill_sql_stats(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_sql_stats");
  TABLE* table= tables->table;

  bool lock_acquired = mt_lock(&LOCK_global_sql_stats);
  MYSQL_TIME time;

  ID_NAME_MAP db_map;
  ID_NAME_MAP user_map;
  fill_invert_map(DB_MAP_NAME, &db_map);
  fill_invert_map(USER_MAP_NAME, &user_map);

  for (auto iter= global_sql_stats_map.cbegin();
      iter != global_sql_stats_map.cend(); ++iter)
  {
    int f= 0;
    SQL_STATS *sql_stats = iter->second;

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
      mt_unlock(lock_acquired, &LOCK_global_sql_stats);
      DBUG_RETURN(-1);
    }
  }
  mt_unlock(lock_acquired, &LOCK_global_sql_stats);

  DBUG_RETURN(0);
}

/* Fills the SQL_TEXT table. */
int fill_sql_text(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_sql_text");
  TABLE* table= tables->table;

  bool lock_acquired = mt_lock(&LOCK_global_sql_stats);

  for (auto iter= global_sql_text_map.cbegin();
      iter != global_sql_text_map.cend(); ++iter)
  {
    int f= 0;
    SQL_TEXT *sql_text = iter->second;

    restore_record(table, s->default_values);

    char sql_id_hex_string[MD5_BUFF_LENGTH];
    array_to_hex(sql_id_hex_string, iter->first.data(), iter->first.size());

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
      mt_unlock(lock_acquired, &LOCK_global_sql_stats);
      DBUG_RETURN(-1);
    }
  }
  mt_unlock(lock_acquired, &LOCK_global_sql_stats);

  DBUG_RETURN(0);
}

int fill_client_attrs(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_client_attrs");
  TABLE* table= tables->table;

  bool lock_acquired = mt_lock(&LOCK_global_sql_stats);

  for (auto iter= global_client_attrs_map.cbegin();
       iter != global_client_attrs_map.cend(); ++iter)
  {
    int f= 0;

    restore_record(table, s->default_values);

    char client_id_hex_string[MD5_BUFF_LENGTH];
    array_to_hex(client_id_hex_string, iter->first.data(), iter->first.size());

    /* CLIENT_ID */
    table->field[f++]->store(client_id_hex_string, MD5_BUFF_LENGTH,
                             system_charset_info);

    /* CLIENT_ATTRIBUTES */
    table->field[f++]->store(iter->second.c_str(),
                             std::min(4096, (int)iter->second.size()),
                             system_charset_info);

    if (schema_table_store_record(thd, table))
    {
      mt_unlock(lock_acquired, &LOCK_global_sql_stats);
      DBUG_RETURN(-1);
    }
  }
  mt_unlock(lock_acquired, &LOCK_global_sql_stats);

  DBUG_RETURN(0);
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
