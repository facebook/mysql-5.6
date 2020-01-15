#include "sql_base.h"
#include "sql_show.h"
#include "sql_string.h"
#include "sql_digest.h"
#include "my_murmur3.h"

#define MD5_BUFF_LENGTH 32

/*
  md5_key is used as the hash key into the SQL_STATISTICS and related tables.

  It needs a hash function for usage in std::unordered_map since the standard
  library doesn't provide a specialization for std::array<>. Just use murmur3
  from mysql.
*/
using md5_key = std::array<unsigned char, MD5_HASH_SIZE>;

namespace std {
  template <>
  struct hash<md5_key>
  {
    std::size_t operator()(const md5_key& k) const
    {
      return murmur3_32(k.data(), k.size(), 0);
    }
  };
}

/*
  SQL_STATISTICS

  This provides statistics for a unique combination of
  sql statement, plan id, table schema and user.
*/
ST_FIELD_INFO sql_stats_fields_info[]=
{
  {"SQL_ID", MD5_BUFF_LENGTH, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"PLAN_ID", MD5_BUFF_LENGTH, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"CLIENT_ID", MD5_BUFF_LENGTH, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"TABLE_SCHEMA", NAME_CHAR_LEN, MYSQL_TYPE_STRING,
      0, 0, 0, SKIP_OPEN_TABLE},
  {"USER_NAME", USERNAME_CHAR_LENGTH, MYSQL_TYPE_STRING,
      0, 0, 0, SKIP_OPEN_TABLE},

  {"EXECUTION_COUNT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},

  {"ROWS_INSERTED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"ROWS_UPDATED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"ROWS_DELETED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
  {"ROWS_READ", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},

  {"TOTAL_CPU", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
      0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},

  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};

/* Global sql stats hash map to track and update metrics in-memory */
std::unordered_map<md5_key, SQL_STATS*> global_sql_stats_map;

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
std::unordered_map<md5_key, SQL_TEXT*> global_sql_text_map;

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

std::unordered_map<md5_key, std::string> global_client_attrs_map;

/*
  These limits control the maximum accumulation of all sql statistics.
  They are controlled by setting the respective system variables.
*/
ulonglong max_sql_stats_count;
ulonglong max_sql_stats_size;

/*
  The current utilization of the sql statistics.
*/
ulonglong sql_stats_count= 0;
ulonglong sql_stats_size= 0;

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
  It's possible for this mutex to be locked twice by one thread when
  ha_myisam::write_row() errors out during a information schema query.

  We use an error checking mutex here so we can handle this situation.
  More details: https://github.com/facebook/mysql-5.6/issues/132.
*/
static bool lock_sql_stats() {
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
                     (&(&LOCK_global_sql_stats)->m_mutex)->thread))
  {
    mysql_mutex_lock(&LOCK_global_sql_stats);
    return false;
  }

  return true;
#else
  return mysql_mutex_lock(&LOCK_global_sql_stats) == EDEADLK;
#endif
}

static void unlock_sql_stats(bool acquired) {
  /* If lock was already acquired by calling thread, do nothing. */
  if (acquired)
  {
    return;
  }

  /* Otherwise, unlock the mutex */
  mysql_mutex_unlock(&LOCK_global_sql_stats);
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
    const unsigned char *sql_id,
    const unsigned char *plan_id,
    const unsigned char *client_id,
    const uint32_t db_id,
    const uint32_t user_id,
    unsigned char *cache_key)
{
  DBUG_ASSERT(sql_id && plan_id && client_id);

  char sql_stats_cache_key[(MD5_HASH_SIZE * 3) + sizeof(db_id) + sizeof(user_id)];
  int offset = 0;
  memcpy(sql_stats_cache_key + offset, sql_id, MD5_HASH_SIZE);
  offset += MD5_HASH_SIZE;
  memcpy(sql_stats_cache_key + offset, plan_id, MD5_HASH_SIZE);
  offset += MD5_HASH_SIZE;
  memcpy(sql_stats_cache_key + offset, client_id, MD5_HASH_SIZE);
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
    const unsigned char *sql_id,
    const unsigned char *plan_id,
    const unsigned char *client_id,
    const uint32_t db_id,
    const uint32_t user_id)
{
  memcpy(stats->sql_id, sql_id, MD5_HASH_SIZE);
  memcpy(stats->plan_id, plan_id, MD5_HASH_SIZE);
  memcpy(stats->client_id, client_id, MD5_HASH_SIZE);
  stats->db_id= db_id;
  stats->user_id= user_id;
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
  bool lock_acquired = lock_sql_stats();

  if (limits_updated) {
    if ((current_max_sql_stats_count <= max_sql_stats_count) &&
        (current_max_sql_stats_size <= max_sql_stats_size)) {
      current_max_sql_stats_count = max_sql_stats_count;
      current_max_sql_stats_size = max_sql_stats_size;
      unlock_sql_stats(lock_acquired);
      return;
    }
  }

  for (auto it= global_sql_stats_map.begin(); it != global_sql_stats_map.end(); ++it)
  {
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

  unlock_sql_stats(lock_acquired);
}

/*
  serialize_client_attrs
    Extracts and serializes client attributes into the buffer
    THD::client_attrs_string.

    This is only calculated once per command (as opposed to per statement),
    and cleared at the end of the command. This is because attributes are
    attached commands, not statements.
*/
static void serialize_client_attrs(THD *thd) {
  if (thd->client_attrs_string.is_empty()) {
    std::vector<std::pair<String, String>> client_attrs;

    // Populate caller
    static const std::string caller = "caller";
    auto it = thd->query_attrs_map.find(caller);
    if (it != thd->query_attrs_map.end()) {
      client_attrs.emplace_back(String(it->first.data(), it->first.size(),
                                       &my_charset_bin),
                                String(it->second.data(), it->second.size(),
                                       &my_charset_bin));
    } else {
      auto it = thd->connection_attrs_map.find(caller);
      if (it != thd->connection_attrs_map.end()) {
        client_attrs.emplace_back(String(it->first.data(), it->first.size(),
                                         &my_charset_bin),
                                  String(it->second.data(), it->second.size(),
                                         &my_charset_bin));
      }
    }

    // Populate async id (inspired from find_async_tag)
    //
    // Search only in first 100 characters to avoid scanning the whole query.
    // The async id is usually near the beginning.
    String query100(thd->query(), MY_MIN(100, thd->query_length()), &my_charset_bin);
    String async_word(C_STRING_WITH_LEN("async-"), &my_charset_bin);

    int pos = query100.strstr(async_word);

    if (pos != -1) {
      pos += async_word.length();
      int epos = pos;

      while(epos < (int)query100.length() && std::isdigit(query100[epos])) {
        epos++;
      }
      client_attrs.emplace_back(String("async_id", &my_charset_bin),
                                String(&query100[pos], epos - pos, &my_charset_bin));
    }

    // Serialize into JSON
    auto& buf = thd->client_attrs_string;
    buf.q_append('{');

    for (size_t i = 0; i < client_attrs.size(); i++) {
      const auto& p = client_attrs[i];

      if (i > 0) {
        buf.q_append(C_STRING_WITH_LEN(", "));
      }
      buf.q_append('\'');
      buf.q_append(p.first.ptr(), MY_MIN(100, p.first.length()));
      buf.q_append(C_STRING_WITH_LEN("' : '"));
      buf.q_append(p.second.ptr(), MY_MIN(100, p.second.length()));
      buf.q_append('\'');
    }
    buf.q_append('}');
  }
}

static bool is_sql_stats_collection_above_limit() {
  return (sql_stats_count >= max_sql_stats_count ||
          sql_stats_size >= max_sql_stats_size);
}

/*
  update_sql_stats_after_statement
    Updates the SQL stats after every SQL statement.
    It is responsible for getting the SQL digest, storing and updating the
    underlying in-memory structures for SQL_TEXT and SQL_STATISTICS IS tables.
  Input:
    thd    in: - THD
    stats  in: - stats for the current SQL statement execution
*/
void update_sql_stats_after_statement(THD *thd, SHARED_SQL_STATS *stats)
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
    available. In that case, just skip stats collection.
  */
  DBUG_ASSERT(thd->m_digest);
  if (!thd->m_digest || thd->m_digest->m_digest_storage.is_empty())
    return;

  md5_key sql_id;
  compute_digest_md5(&thd->m_digest->m_digest_storage, sql_id.data());

  md5_key client_id;
  serialize_client_attrs(thd);
  compute_md5_hash((char *)client_id.data(), thd->client_attrs_string.ptr(),
                   thd->client_attrs_string.length());

  md5_key sql_stats_cache_key;
  if (!set_sql_stats_cache_key(sql_id.data(), sql_id.data(), client_id.data(),
                               db_id, user_id, sql_stats_cache_key.data()))
    return;

  bool lock_acquired = lock_sql_stats();

  // Check again inside the lock
  if (is_sql_stats_collection_above_limit()) return;

  current_max_sql_stats_count = max_sql_stats_count;
  current_max_sql_stats_size = max_sql_stats_size;

  /* Get or create client attributes for this statement. */
  auto client_id_iter = global_client_attrs_map.find(client_id);
  if (client_id_iter == global_client_attrs_map.end()) {
    global_client_attrs_map.emplace(client_id,
                                    std::string(thd->client_attrs_string.ptr(),
                                                thd->client_attrs_string.length()));
    sql_stats_size += (MD5_HASH_SIZE + thd->client_attrs_string.length());
  }

  /* Get or create the SQL_TEXT object for this sql statement. */
  SQL_TEXT *sql_text;
  auto sql_text_iter= global_sql_text_map.find(sql_id);
  if (sql_text_iter == global_sql_text_map.end())
  {
    if (!(sql_text= ((SQL_TEXT*)my_malloc(sizeof(SQL_TEXT), MYF(MY_WME)))))
    {
      sql_print_error("Cannot allocate memory for SQL_TEXT.");
      unlock_sql_stats(lock_acquired);
      return;
    }

    set_sql_text_attributes(sql_text, thd->lex->sql_command,
                            &thd->m_digest->m_digest_storage);

    auto ret= global_sql_text_map.emplace(sql_id, sql_text);
    if (!ret.second)
    {
      sql_print_error("Failed to insert SQL_TEXT into the hash map.");
      my_free((char*)sql_text);
      unlock_sql_stats(lock_acquired);
      return;
    }
    sql_stats_size += (MD5_HASH_SIZE + sizeof(SQL_TEXT));
  }

  /* Get or create the SQL_STATS object for this sql statement. */
  SQL_STATS *sql_stats;
  auto sql_stats_iter= global_sql_stats_map.find(sql_stats_cache_key);
  if (sql_stats_iter == global_sql_stats_map.end())
  {
    if (!(sql_stats= ((SQL_STATS*)my_malloc(sizeof(SQL_STATS), MYF(MY_WME)))))
    {
      sql_print_error("Cannot allocate memory for SQL_STATS.");
      unlock_sql_stats(lock_acquired);
      return;
    }

    /* For now pass in sql_id as plan_id */
    /* TODO (svemuri): Change to the correct plan_id later */
    set_sql_stats_attributes(sql_stats, sql_id.data(), sql_id.data(),
                             client_id.data(), db_id, user_id);
    sql_stats->reset();

    auto ret= global_sql_stats_map.emplace(sql_stats_cache_key, sql_stats);
    if (!ret.second)
    {
      sql_print_error("Failed to insert SQL_STATS into the hash table.");
      my_free((char*)sql_stats);
      unlock_sql_stats(lock_acquired);
      return;
    }
    sql_stats_count++;
    sql_stats_size += (MD5_HASH_SIZE + sizeof(SQL_STATS));
  } else {
    sql_stats= sql_stats_iter->second;
  }

  /* Update stats */
  sql_stats->count++;
  // Update Row counts
  sql_stats->shared_stats.rows_inserted += stats->rows_inserted;
  sql_stats->shared_stats.rows_updated += stats->rows_updated;
  sql_stats->shared_stats.rows_deleted += stats->rows_deleted;
  sql_stats->shared_stats.rows_read += stats->rows_read;
  // Update CPU stats
  sql_stats->shared_stats.us_tot += stats->us_tot;

  unlock_sql_stats(lock_acquired);
}

/* Fills the SQL_STATISTICS table. */
int fill_sql_stats(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_sql_stats");
  TABLE* table= tables->table;

  bool lock_acquired = lock_sql_stats();

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

    char client_id_hex_string[MD5_BUFF_LENGTH];
    array_to_hex(client_id_hex_string, sql_stats->client_id, MD5_HASH_SIZE);

    /* SQL ID */
    table->field[f++]->store(sql_id_hex_string, MD5_BUFF_LENGTH,
                             system_charset_info);
    /* PLAN ID */
    table->field[f++]->store(sql_id_hex_string, MD5_BUFF_LENGTH,
                             system_charset_info);
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

    /* Execution Count */
    table->field[f++]->store(sql_stats->count, TRUE);
    /* Rows inserted */
    table->field[f++]->store(sql_stats->shared_stats.rows_inserted, TRUE);
    /* Rows updated */
    table->field[f++]->store(sql_stats->shared_stats.rows_updated, TRUE);
    /* Rows deleted */
    table->field[f++]->store(sql_stats->shared_stats.rows_deleted, TRUE);
    /* Rows read */
    table->field[f++]->store(sql_stats->shared_stats.rows_read, TRUE);
    /* Total CPU in microseconds */
    table->field[f++]->store(sql_stats->shared_stats.us_tot, TRUE);

    if (schema_table_store_record(thd, table))
    {
      unlock_sql_stats(lock_acquired);
      DBUG_RETURN(-1);
    }
  }
  unlock_sql_stats(lock_acquired);

  DBUG_RETURN(0);
}

/* Fills the SQL_TEXT table. */
int fill_sql_text(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_sql_text");
  TABLE* table= tables->table;

  bool lock_acquired = lock_sql_stats();

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
                             MY_MIN(digest_text.length(), 4096),
                             system_charset_info);

    if (schema_table_store_record(thd, table))
    {
      unlock_sql_stats(lock_acquired);
      DBUG_RETURN(-1);
    }
  }
  unlock_sql_stats(lock_acquired);

  DBUG_RETURN(0);
}

int fill_client_attrs(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_client_attrs");
  TABLE* table= tables->table;

  bool lock_acquired = lock_sql_stats();

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
                             MY_MIN(4096, iter->second.size()),
                             system_charset_info);

    if (schema_table_store_record(thd, table))
    {
      unlock_sql_stats(lock_acquired);
      DBUG_RETURN(-1);
    }
  }
  unlock_sql_stats(lock_acquired);

  DBUG_RETURN(0);
}
