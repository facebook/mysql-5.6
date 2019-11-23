#include "sql_base.h"
#include "sql_show.h"
#include "my_atomic.h"
#include "mysqld.h"
#include <stdio.h>

uint max_db_stats_entries;
HASH global_db_stats_hash;
DB_STATS* global_db_stats_array;
static pthread_mutex_t LOCK_global_db_stats;
unsigned char num_db_stats_entries = 0;

static void
clear_db_stats_counters(DB_STATS* db_stats)
{
  db_stats->us_user.clear();
  db_stats->us_sys.clear();
  db_stats->us_tot.clear();
  db_stats->rows_deleted.clear();
  db_stats->rows_inserted.clear();
  db_stats->rows_read.clear();
  db_stats->rows_updated.clear();
}

extern "C" uchar *get_key_db_stats(const uchar *ptr, size_t *length,
                                   my_bool not_used MY_ATTRIBUTE((unused)))
{
  DB_STATS *db_stats = (DB_STATS *)ptr;
  *length = strlen(db_stats->db);
  return (uchar*)db_stats->db;
}

void init_global_db_stats()
{
  pthread_mutex_init(&LOCK_global_db_stats, MY_MUTEX_INIT_FAST);
  global_db_stats_array = (DB_STATS*)my_malloc(
      (max_db_stats_entries + 1) * sizeof(DB_STATS), MYF(MY_WME));
  if (my_hash_init(&global_db_stats_hash, system_charset_info, max_connections,
                   0, 0, (my_hash_get_key)get_key_db_stats,
                   NULL /* free element */, 0)) {
    sql_print_error("Initializing global_db_stats failed.");
  }
}

void free_global_db_stats(void)
{
  my_hash_free(&global_db_stats_hash);
  pthread_mutex_destroy(&LOCK_global_db_stats);
  my_free((char*)global_db_stats_array);
}


/* NULL means the global array is full and we do not keep stats for these
 * databases.
 */
static DB_STATS* get_db_stats_locked(const char* db)
{
  if (!db)
    return NULL;

  DB_STATS* db_stats = (DB_STATS*)my_hash_search(&global_db_stats_hash,
                                                 (uchar*)db,
                                                 strlen(db));
  if (!db_stats) {
    if (num_db_stats_entries == max_db_stats_entries) {
      return NULL;
    }

    db_stats = &global_db_stats_array[++num_db_stats_entries];
    db_stats->index = num_db_stats_entries;
    strcpy(db_stats->db, db);
    clear_db_stats_counters(db_stats);
    my_hash_insert(&global_db_stats_hash, (uchar*)db_stats);
  }
  return db_stats;
}

DB_STATS* get_db_stats(const char *db) {
  // early check for a common case to preclude lock
  if (!db)
    return NULL;

  pthread_mutex_lock(&LOCK_global_db_stats);
  DB_STATS *db_stats = get_db_stats_locked(db);
  pthread_mutex_unlock(&LOCK_global_db_stats);
  return db_stats;
}

/* 0 means the global array is full and we do not keep stats for these
 * databases.
 */
unsigned char get_db_stats_index(const char* db)
{
  pthread_mutex_lock(&LOCK_global_db_stats);
  DB_STATS* db_stats = get_db_stats_locked(db);
  pthread_mutex_unlock(&LOCK_global_db_stats);
  return (db_stats) ? db_stats->index : 0;
}
int fill_db_stats(THD *thd, TABLE_LIST *tables, Item *cond)
{
  unsigned i;
  DBUG_ENTER("fill_db_stats");
  TABLE* table= tables->table;
  unsigned f;
  for (i = 0; i < num_db_stats_entries; ++i) {
    DB_STATS* db_stats = &global_db_stats_array[i + 1];
    f = 0;
    restore_record(table, s->default_values);
    table->field[f++]->store(db_stats->db, strlen(db_stats->db),
                               system_charset_info);
      table->field[f++]->store(0);

    table->field[f++]->store(db_stats->us_user.load(), TRUE);
    table->field[f++]->store(db_stats->us_sys.load(), TRUE);
    table->field[f++]->store(db_stats->us_tot.load(), TRUE);
    table->field[f++]->store(db_stats->rows_deleted.load(), TRUE);
    table->field[f++]->store(db_stats->rows_inserted.load(), TRUE);
    table->field[f++]->store(db_stats->rows_read.load(), TRUE);
    table->field[f++]->store(db_stats->rows_updated.load(), TRUE);

    if (schema_table_store_record(thd, table))
    {
      DBUG_RETURN(-1);
    }
  }
  DBUG_RETURN(0);
}

void reset_global_db_stats()
{
  pthread_mutex_lock(&LOCK_global_db_stats);

  for (unsigned i = 0; i < num_db_stats_entries; ++i) {
    DB_STATS *db_stats = &global_db_stats_array[i + 1];
    clear_db_stats_counters(db_stats);
  }

  pthread_mutex_unlock(&LOCK_global_db_stats);
}

ST_FIELD_INFO db_stats_fields_info[]=
{
  {"DB", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"WORKING_SET_SIZE", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"USER_CPU", MY_INT64_NUM_DECIMAL_DIGITS,
    MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"SYSTEM_CPU", MY_INT64_NUM_DECIMAL_DIGITS,
    MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"CPU", MY_INT64_NUM_DECIMAL_DIGITS,
    MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_DELETED", MY_INT64_NUM_DECIMAL_DIGITS,
    MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_INSERTED", MY_INT64_NUM_DECIMAL_DIGITS,
    MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_READ", MY_INT64_NUM_DECIMAL_DIGITS,
    MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_UPDATED", MY_INT64_NUM_DECIMAL_DIGITS,
    MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};
