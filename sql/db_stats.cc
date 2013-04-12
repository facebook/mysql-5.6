#include "sql_base.h"
#include "sql_show.h"
#include "my_atomic.h"
#include "mysqld.h"
#include <stdio.h>

#define MAX_DB_STATS_ENTRIES 255
HASH global_db_stats_hash;
DB_STATS* global_db_stats_array;
static pthread_mutex_t LOCK_global_db_stats;
unsigned char num_db_stats_entries = 0;

extern "C" uchar *get_key_db_stats(const uchar *ptr, size_t *length,
                                   my_bool not_used __attribute__((unused)))
{
  DB_STATS *db_stats = (DB_STATS *)ptr;
  *length = strlen(db_stats->db);
  return (uchar*)db_stats->db;
}

extern "C" void free_db_stats(void *p)
{
  DB_STATS *db_stats = (DB_STATS *)p;
  hyperloglog_destroy(&db_stats->hll);
}

void init_global_db_stats()
{
  pthread_mutex_init(&LOCK_global_db_stats, MY_MUTEX_INIT_FAST);
  global_db_stats_array = (DB_STATS*)my_malloc((MAX_DB_STATS_ENTRIES + 1) * sizeof(DB_STATS), MYF(MY_WME));
  if (my_hash_init(&global_db_stats_hash, system_charset_info, max_connections,
                   0, 0, (my_hash_get_key)get_key_db_stats,
                   free_db_stats, 0)) {
    sql_print_error("Initializing global_db_stats failed.");
  }
}

void free_global_db_stats(void)
{
  my_hash_free(&global_db_stats_hash);
  pthread_mutex_destroy(&LOCK_global_db_stats);
  my_free((char*)global_db_stats_array);
}

/* 0 means the global array is full and we do not keep stats for these
 * databases.
 */
unsigned char get_db_stats_index(const char* db)
{
  pthread_mutex_lock(&LOCK_global_db_stats);
  DB_STATS* db_stats = (DB_STATS*)my_hash_search(&global_db_stats_hash,
                                                 (uchar*)db,
                                                 strlen(db));
  if (!db_stats) {
    if (num_db_stats_entries == MAX_DB_STATS_ENTRIES) {
      pthread_mutex_unlock(&LOCK_global_db_stats);
      return 0;
    }
    db_stats = &global_db_stats_array[++num_db_stats_entries];
    db_stats->index = num_db_stats_entries;
    strcpy(db_stats->db, db);
    hyperloglog_init(&db_stats->hll);
    my_hash_insert(&global_db_stats_hash, (uchar*)db_stats);
  }
  pthread_mutex_unlock(&LOCK_global_db_stats);
  return db_stats->index;
}
int fill_db_stats(THD *thd, TABLE_LIST *tables, Item *cond)
{
  unsigned i;
  DBUG_ENTER("fill_db_stats");
  TABLE* table= tables->table;
  unsigned f;
  uint current_time = (uint)(my_timer_to_seconds(my_timer_now()));
  uint since_time = current_time - thd->variables.working_duration;
  for (i = 0; i < num_db_stats_entries; ++i) {
    DB_STATS* db_stats = &global_db_stats_array[i + 1];
    f = 0;
    restore_record(table, s->default_values);
    table->field[f++]->store(db_stats->db, strlen(db_stats->db),
                               system_charset_info);
    if (!opt_disable_working_set_size) {
      table->field[f++]->store(hyperloglog_query(&db_stats->hll, since_time));
    } else {
      table->field[f++]->store(0);
    }

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
    hyperloglog_reset(&db_stats->hll);
  }

  pthread_mutex_unlock(&LOCK_global_db_stats);
}

void update_global_db_stats_access(unsigned char db_stats_index,
                                   ulonglong space,
                                   ulonglong offset)
{
  if(!opt_disable_working_set_size) {
    hyperloglog_t* hll_to_update;
    ulonglong crc_helper[2];
    ulonglong page_hash;
    uint current_time_in_secs;

    hll_to_update = &global_db_stats_array[db_stats_index].hll;
    crc_helper[0] = space;
    crc_helper[1] = offset;
    page_hash = my_sbox_hash((uchar *)(&crc_helper[0]), 16);
    current_time_in_secs = my_timer_to_seconds(my_timer_now());
    hyperloglog_insert(hll_to_update, page_hash, current_time_in_secs);
  }
}

ST_FIELD_INFO db_stats_fields_info[]=
{
  {"DB", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"WORKING_SET_SIZE", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};
