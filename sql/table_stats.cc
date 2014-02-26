#include "sql_base.h"
#include "sql_show.h"
#include "my_atomic.h"

HASH global_table_stats;

/*
  Update global table statistics for this table and optionally tables
  linked via TABLE::next.

  SYNOPSIS
    update_table_stats()
      tablep - the table for which global table stats are updated
      follow_next - when TRUE, update global stats for tables linked
                    via TABLE::next
 */
void update_table_stats(THD *thd, TABLE *tablep, bool follow_next)
{
  for (; tablep; tablep= tablep->next)
  {
    if (tablep->file)
      tablep->file->update_global_table_stats(thd);

    if (!follow_next)
      return;
  }
}

static void
clear_table_stats_counters(TABLE_STATS* table_stats)
{
  table_stats->queries_used.clear();
  table_stats->rows_inserted.clear();
  table_stats->rows_updated.clear();
  table_stats->rows_deleted.clear();
  table_stats->rows_read.clear();
  table_stats->rows_requested.clear();
  table_stats->rows_index_first.clear();
  table_stats->rows_index_next.clear();

  my_io_perf_atomic_init(&table_stats->io_perf_read);
  my_io_perf_atomic_init(&table_stats->io_perf_write);
  my_io_perf_atomic_init(&table_stats->io_perf_read_blob);
  my_io_perf_atomic_init(&table_stats->io_perf_read_primary);
  my_io_perf_atomic_init(&table_stats->io_perf_read_secondary);
  table_stats->index_inserts.clear();
  table_stats->queries_empty.clear();

  memset(&table_stats->page_stats, 0, sizeof(table_stats->page_stats));
  memset(&table_stats->comp_stats, 0, sizeof(table_stats->comp_stats));
}

static TABLE_STATS*
get_table_stats_by_name(const char *db_name,
                        const char *table_name,
                        const char *engine_name,
                        TABLE *tbl)
{
  TABLE_STATS* table_stats;
  char cache_key[NAME_LEN * 2 + 2];
  size_t cache_key_length;

  DBUG_ASSERT(db_name && table_name && engine_name);

  if (!db_name || !table_name || !engine_name)
  {
    sql_print_error("No key for table stats.");
    return NULL;
  }

  size_t db_name_len= strlen(db_name);
  size_t table_name_len= strlen(table_name);

  if (db_name_len > NAME_LEN || table_name_len > NAME_LEN)
  {
    sql_print_error("Db or table name too long for table stats :%s:%s:\n",
                    db_name, table_name);
    DBUG_ABORT();
    return NULL;
  }

  cache_key_length = db_name_len + table_name_len + 2;

  memcpy(cache_key, db_name, db_name_len + 1);
  memcpy(cache_key + db_name_len + 1, table_name, table_name_len + 1);

  mysql_mutex_lock(&LOCK_global_table_stats);

  // Get or create the TABLE_STATS object for this table.
  if (!(table_stats= (TABLE_STATS*)my_hash_search(&global_table_stats,
                                               (uchar*)cache_key,
                                               cache_key_length)))
  {
    if (!(table_stats= ((TABLE_STATS*)my_malloc(sizeof(TABLE_STATS),
                                                MYF(MY_WME)))))
    {
      sql_print_error("Cannot allocate memory for TABLE_STATS.");
      mysql_mutex_unlock(&LOCK_global_table_stats);
      return NULL;
    }

    memcpy(table_stats->hash_key, cache_key, cache_key_length);
    table_stats->hash_key_len= cache_key_length;

    memcpy(table_stats->db, db_name, db_name_len + 1);
    memcpy(table_stats->table, table_name, table_name_len + 1);

    clear_table_stats_counters(table_stats);
    table_stats->engine_name= engine_name;

    if (my_hash_insert(&global_table_stats, (uchar*)table_stats))
    {
      // Out of memory.
      sql_print_error("Inserting table stats failed.");
      my_free((char*)table_stats);
      mysql_mutex_unlock(&LOCK_global_table_stats);
      return NULL;
    }
  }

  mysql_mutex_unlock(&LOCK_global_table_stats);

  return table_stats;
}

/*
  Return the global TABLE_STATS object for a table.

  SYNOPSIS
    get_table_stats()
    table          in: table for which an object is returned
    type_of_db     in: storage engine type

  RETURN VALUE
    TABLE_STATS structure for the requested table
    NULL on failure
*/
TABLE_STATS*
get_table_stats(TABLE *table, handlerton *engine_type)
{
  DBUG_ASSERT(table->s);
  const char* engine_name= ha_resolve_storage_engine_name(engine_type);

  if (!table->s)
  {
    sql_print_error("No key for table stats.");
    return NULL;
  }

  return get_table_stats_by_name(table->s->db.str,
                                 table->s->table_name.str,
                                 engine_name,
                                 table);
}

extern "C" uchar *get_key_table_stats(TABLE_STATS *table_stats, size_t *length,
                                      my_bool not_used __attribute__((unused)))
{
  *length = table_stats->hash_key_len;
  return (uchar*)table_stats->hash_key;
}

extern "C" void free_table_stats(TABLE_STATS* table_stats)
{
  my_free((char*)table_stats);
}

void init_global_table_stats(void)
{
  if (my_hash_init(&global_table_stats, system_charset_info, max_connections,
                0, 0, (my_hash_get_key)get_key_table_stats,
                (my_hash_free_key)free_table_stats, 0)) {
    sql_print_error("Initializing global_table_stats failed.");
    abort();
  }
}

void free_global_table_stats(void)
{
  my_hash_free(&global_table_stats);
}

void reset_global_table_stats()
{
  mysql_mutex_lock(&LOCK_global_table_stats);

  for (unsigned i = 0; i < global_table_stats.records; ++i) {
    TABLE_STATS *table_stats =
      (TABLE_STATS*)my_hash_element(&global_table_stats, i);

    clear_table_stats_counters(table_stats);
  }

  mysql_mutex_unlock(&LOCK_global_table_stats);
}

ST_FIELD_INFO table_stats_fields_info[]=
{
  {"TABLE_SCHEMA", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"TABLE_NAME", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"TABLE_ENGINE", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},

  {"ROWS_INSERTED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_UPDATED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_DELETED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_READ", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_REQUESTED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},

  {"COMPRESSED_PAGE_SIZE", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COMPRESS_PADDING", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COMPRESS_OPS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COMPRESS_OPS_OK", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COMPRESS_PRIMARY_OPS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COMPRESS_PRIMARY_OPS_OK", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COMPRESS_USECS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COMPRESS_OK_USECS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COMPRESS_PRIMARY_USECS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COMPRESS_PRIMARY_OK_USECS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"UNCOMPRESS_OPS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"UNCOMPRESS_USECS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},

  {"ROWS_INDEX_FIRST", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_INDEX_NEXT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},

  {"IO_READ_BYTES", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_REQUESTS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_SVC_USECS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_SVC_USECS_MAX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_WAIT_USECS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_WAIT_USECS_MAX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_SLOW_IOS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},

  {"IO_WRITE_BYTES", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_WRITE_REQUESTS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_WRITE_SVC_USECS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_WRITE_SVC_USECS_MAX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_WRITE_WAIT_USECS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_WRITE_WAIT_USECS_MAX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_WRITE_SLOW_IOS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},

  {"IO_READ_BYTES_BLOB", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_REQUESTS_BLOB", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_SVC_USECS_BLOB", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_SVC_USECS_MAX_BLOB", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_WAIT_USECS_BLOB", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_WAIT_USECS_MAX_BLOB", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_SLOW_IOS_BLOB", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},

  {"IO_READ_BYTES_PRIMARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_REQUESTS_PRIMARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_SVC_USECS_PRIMARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_SVC_USECS_MAX_PRIMARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_WAIT_USECS_PRIMARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_WAIT_USECS_MAX_PRIMARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_SLOW_IOS_PRIMARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},

  {"IO_READ_BYTES_SECONDARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_REQUESTS_SECONDARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_SVC_USECS_SECONDARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_SVC_USECS_MAX_SECONDARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_WAIT_USECS_SECONDARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_WAIT_USECS_MAX_SECONDARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_SLOW_IOS_SECONDARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},

  {"IO_INDEX_INSERTS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"QUERIES_USED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"QUERIES_EMPTY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},

  {"INNODB_PAGES_READ", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"INNODB_PAGES_READ_INDEX", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"INNODB_PAGES_READ_BLOB", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"INNODB_PAGES_WRITTEN", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"INNODB_PAGES_WRITTEN_INDEX", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"INNODB_PAGES_WRITTEN_BLOB", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONG, 0, 0, 0, SKIP_OPEN_TABLE},

  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};

void copy_io_perf_with_races(my_io_perf_atomic_t *out, my_io_perf_t *in)
{
  out->bytes.set_maybe(in->bytes);
  out->requests.set_maybe(in->requests);
  out->svc_time.set_maybe(in->svc_time);
  out->svc_time_max.set_maybe(in->svc_time_max);
  out->wait_time.set_maybe(in->wait_time);
  out->wait_time_max.set_maybe(in->wait_time_max);
  out->slow_ios.set_maybe(in->slow_ios);
}

void copy_page_stats_with_races(page_stats_atomic_t *out, page_stats_t *in)
{
  out->n_pages_read.set_maybe(in->n_pages_read);
  out->n_pages_read_index.set_maybe(in->n_pages_read_index);
  out->n_pages_read_blob.set_maybe(in->n_pages_read_blob);
  out->n_pages_written.set_maybe(in->n_pages_written);
  out->n_pages_written_index.set_maybe(in->n_pages_written_index);
  out->n_pages_written_blob.set_maybe(in->n_pages_written_blob);
}

void copy_comp_stats_with_races(comp_stats_atomic_t *out, comp_stats_t *in)
{
  out->page_size.set_maybe(in->page_size);
  out->padding.set_maybe(in->padding);
  out->compressed.set_maybe(in->compressed);
  out->compressed_ok.set_maybe(in->compressed_ok);
  out->compressed_primary.set_maybe(in->compressed_primary);
  out->compressed_primary_ok.set_maybe(in->compressed_primary_ok);
  out->decompressed.set_maybe(in->decompressed);
  out->compressed_time.set_maybe(in->compressed_time);
  out->compressed_ok_time.set_maybe(in->compressed_ok_time);
  out->decompressed_time.set_maybe(in->decompressed_time);
  out->compressed_primary_time.set_maybe(in->compressed_primary_time);
  out->compressed_primary_ok_time.set_maybe(in->compressed_primary_ok_time);
}

void fill_table_stats_cb(const char *db,
                         const char *table,
                         bool is_partition,
                         my_io_perf_t *r,
                         my_io_perf_t *w,
                         my_io_perf_t *r_blob,
                         my_io_perf_t *r_primary,
                         my_io_perf_t *r_secondary,
                         page_stats_t *page_stats,
                         comp_stats_t *comp_stats,
                         const char *engine)
{
  TABLE_STATS *stats;

  stats= get_table_stats_by_name(db, table, engine, NULL);
  if (!stats)
    return;

  if (is_partition) {
    if (stats->should_update) {
      my_io_perf_sum_atomic_helper(&stats->io_perf_read, r);
      my_io_perf_sum_atomic_helper(&stats->io_perf_write, w);
      my_io_perf_sum_atomic_helper(&stats->io_perf_read_blob, r_blob);
      my_io_perf_sum_atomic_helper(&stats->io_perf_read_primary, r_primary);
      my_io_perf_sum_atomic_helper(&stats->io_perf_read_secondary, r_secondary);
      my_page_stats_sum_atomic(&stats->page_stats, page_stats);
      // page_size would be the same for all partition
      stats->comp_stats.page_size.set_maybe(comp_stats->page_size);
      // padding might be different but we just use one partition to estimate
      stats->comp_stats.padding.set_maybe(comp_stats->padding);
      my_comp_stats_sum_atomic(&stats->comp_stats, comp_stats);
      return;
    } else {
      stats->should_update = true;
    }
  }
  /* These assignments allow for races. That is OK. */
  copy_io_perf_with_races(&stats->io_perf_read, r);
  copy_io_perf_with_races(&stats->io_perf_write, w);
  copy_io_perf_with_races(&stats->io_perf_read_blob, r_blob);
  copy_io_perf_with_races(&stats->io_perf_read_primary, r_primary);
  copy_io_perf_with_races(&stats->io_perf_read_secondary, r_secondary);

  copy_page_stats_with_races(&stats->page_stats, page_stats);

  copy_comp_stats_with_races(&stats->comp_stats, comp_stats);
}

int fill_table_stats(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_table_stats");
  TABLE* table= tables->table;

  ha_get_table_stats(fill_table_stats_cb);

  mysql_mutex_lock(&LOCK_global_table_stats);

  for (unsigned i = 0; i < global_table_stats.records; ++i) {
    int f= 0;

    TABLE_STATS *table_stats =
      (TABLE_STATS*)my_hash_element(&global_table_stats, i);

    if (table_stats->rows_inserted.load() == 0 &&
        table_stats->rows_updated.load() == 0 &&
        table_stats->rows_deleted.load() == 0 &&
        table_stats->rows_read.load() == 0 &&
        table_stats->rows_requested.load() == 0 &&
        table_stats->comp_stats.compressed.load() == 0 &&
        table_stats->comp_stats.compressed_ok.load() == 0 &&
        table_stats->comp_stats.compressed_time.load() == 0 &&
        table_stats->comp_stats.compressed_ok_time.load() == 0 &&
        table_stats->comp_stats.decompressed.load() == 0 &&
        table_stats->comp_stats.decompressed_time.load() == 0 &&
        table_stats->io_perf_read.requests.load() == 0 &&
        table_stats->io_perf_write.requests.load() == 0 &&
        table_stats->io_perf_read_blob.requests.load() == 0 &&
        table_stats->io_perf_read_primary.requests.load() == 0 &&
        table_stats->io_perf_read_secondary.requests.load() == 0 &&
        table_stats->queries_empty.load() == 0 &&
        table_stats->page_stats.n_pages_read.load() == 0 &&
        table_stats->page_stats.n_pages_read_index.load() == 0 &&
        table_stats->page_stats.n_pages_read_blob.load() == 0 &&
        table_stats->page_stats.n_pages_written.load() == 0 &&
        table_stats->page_stats.n_pages_written_index.load() == 0 &&
        table_stats->page_stats.n_pages_written_blob.load() == 0)
    {
      continue;
    }

    restore_record(table, s->default_values);
    table->field[f++]->store(table_stats->db, strlen(table_stats->db),
                           system_charset_info);
    table->field[f++]->store(table_stats->table, strlen(table_stats->table),
                             system_charset_info);

    table->field[f++]->store(table_stats->engine_name,
                             strlen(table_stats->engine_name),
                             system_charset_info);

    table->field[f++]->store(table_stats->rows_inserted.load(), TRUE);
    table->field[f++]->store(table_stats->rows_updated.load(), TRUE);
    table->field[f++]->store(table_stats->rows_deleted.load(), TRUE);
    table->field[f++]->store(table_stats->rows_read.load(), TRUE);
    table->field[f++]->store(table_stats->rows_requested.load(), TRUE);

    table->field[f++]->store(table_stats->comp_stats.page_size.load(), TRUE);
    table->field[f++]->store(table_stats->comp_stats.padding.load(), TRUE);
    table->field[f++]->store(table_stats->comp_stats.compressed.load(), TRUE);
    table->field[f++]->store(
      table_stats->comp_stats.compressed_ok.load(), TRUE);
    table->field[f++]->store(
      table_stats->comp_stats.compressed_primary.load(), TRUE);
    table->field[f++]->store(
      table_stats->comp_stats.compressed_primary_ok.load(), TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
      table_stats->comp_stats.compressed_time.load()), TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
      table_stats->comp_stats.compressed_ok_time.load()), TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
      table_stats->comp_stats.compressed_primary_time.load()), TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
      table_stats->comp_stats.compressed_primary_ok_time.load()), TRUE);
    table->field[f++]->store(
      table_stats->comp_stats.decompressed.load(), TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
      table_stats->comp_stats.decompressed_time.load()), TRUE);

    table->field[f++]->store(table_stats->rows_index_first.load(), TRUE);
    table->field[f++]->store(table_stats->rows_index_next.load(), TRUE);

    table->field[f++]->store(table_stats->io_perf_read.bytes.load(), TRUE);
    table->field[f++]->store(table_stats->io_perf_read.requests.load(), TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             table_stats->io_perf_read.svc_time.load()),
                             TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             table_stats->io_perf_read.svc_time_max.load()),
                             TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             table_stats->io_perf_read.wait_time.load()),
                             TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             table_stats->io_perf_read.wait_time_max.load()),
                             TRUE);
    table->field[f++]->store(table_stats->io_perf_read.slow_ios.load(), TRUE);

    table->field[f++]->store(table_stats->io_perf_write.bytes.load(), TRUE);
    table->field[f++]->store(table_stats->io_perf_write.requests.load(), TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             table_stats->io_perf_write.svc_time.load()),
                             TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             table_stats->io_perf_write.svc_time_max.load()),
                             TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             table_stats->io_perf_write.wait_time.load()),
                             TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             table_stats->io_perf_write.wait_time_max.load()),
                             TRUE);
    table->field[f++]->store(table_stats->io_perf_write.slow_ios.load(), TRUE);

    table->field[f++]->store(table_stats->io_perf_read_blob.bytes.load(), TRUE);
    table->field[f++]->store(table_stats->io_perf_read_blob.requests.load(),
                             TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             table_stats->io_perf_read_blob.svc_time.load()),
                             TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             table_stats->io_perf_read_blob.svc_time_max.load()
                             ), TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             table_stats->io_perf_read_blob.wait_time.load()),
                             TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             table_stats->io_perf_read_blob.wait_time_max.load()
                             ), TRUE);
    table->field[f++]->store(table_stats->io_perf_read_blob.slow_ios.load(),
                             TRUE);

    table->field[f++]->store(
      table_stats->io_perf_read_primary.bytes.load(), TRUE);
    table->field[f++]->store(
      table_stats->io_perf_read_primary.requests.load(), TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
      table_stats->io_perf_read_primary.svc_time.load()), TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
      table_stats->io_perf_read_primary.svc_time_max.load()), TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
      table_stats->io_perf_read_primary.wait_time.load()), TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
      table_stats->io_perf_read_primary.wait_time_max.load()), TRUE);
    table->field[f++]->store(
      table_stats->io_perf_read_primary.slow_ios.load(), TRUE);

    table->field[f++]->store(
      table_stats->io_perf_read_secondary.bytes.load(), TRUE);
    table->field[f++]->store(
      table_stats->io_perf_read_secondary.requests.load(), TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
      table_stats->io_perf_read_secondary.svc_time.load()), TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
      table_stats->io_perf_read_secondary.svc_time_max.load()), TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
      table_stats->io_perf_read_secondary.wait_time.load()), TRUE);
    table->field[f++]->store((ulonglong)my_timer_to_microseconds(
      table_stats->io_perf_read_secondary.wait_time_max.load()), TRUE);
    table->field[f++]->store(
      table_stats->io_perf_read_secondary.slow_ios.load(), TRUE);

    table->field[f++]->store(table_stats->index_inserts.load(), TRUE);
    table->field[f++]->store(table_stats->queries_used.load(), TRUE);
    table->field[f++]->store(table_stats->queries_empty.load(), TRUE);

    table->field[f++]->store(
      table_stats->page_stats.n_pages_read.load(), TRUE);
    table->field[f++]->store(
      table_stats->page_stats.n_pages_read_index.load(), TRUE);
    table->field[f++]->store(
      table_stats->page_stats.n_pages_read_blob.load(), TRUE);
    table->field[f++]->store(
      table_stats->page_stats.n_pages_written.load(), TRUE);
    table->field[f++]->store(
      table_stats->page_stats.n_pages_written_index.load(), TRUE);
    table->field[f++]->store(
      table_stats->page_stats.n_pages_written_blob.load(), TRUE);

    table_stats->should_update = false;

    if (schema_table_store_record(thd, table))
    {
      mysql_mutex_unlock(&LOCK_global_table_stats);
      DBUG_RETURN(-1);
    }
  }
  mysql_mutex_unlock(&LOCK_global_table_stats);

  DBUG_RETURN(0);
}

ST_FIELD_INFO user_stats_fields_info[]=
{
  {"USER_NAME", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"BINLOG_BYTES_WRITTEN", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"BYTES_RECEIVED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"BYTES_SENT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COMMANDS_DDL", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COMMANDS_DELETE", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COMMANDS_HANDLER", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COMMANDS_INSERT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COMMANDS_OTHER", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COMMANDS_SELECT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COMMANDS_TRANSACTION", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COMMANDS_UPDATE", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"CONNECTIONS_CONCURRENT", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"CONNECTIONS_DENIED_MAX_GLOBAL", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"CONNECTIONS_DENIED_MAX_USER", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"CONNECTIONS_LOST", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"CONNECTIONS_TOTAL", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"DISK_READ_BYTES", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"DISK_READ_REQUESTS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"DISK_READ_SVC_USECS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"DISK_READ_WAIT_USECS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"DISK_READ_BYTES_BLOB", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"DISK_READ_REQUESTS_BLOB", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"DISK_READ_SVC_USECS_BLOB", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"DISK_READ_WAIT_USECS_BLOB", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"DISK_READ_BYTES_PRIMARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"DISK_READ_REQUESTS_PRIMARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"DISK_READ_SVC_USECS_PRIMARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"DISK_READ_WAIT_USECS_PRIMARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"DISK_READ_BYTES_SECONDARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"DISK_READ_REQUESTS_SECONDARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"DISK_READ_SVC_USECS_SECONDARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"DISK_READ_WAIT_USECS_SECONDARY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ERRORS_ACCESS_DENIED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ERRORS_TOTAL", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"MICROSECONDS_WALL", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"MICROSECONDS_DDL", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"MICROSECONDS_DELETE", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"MICROSECONDS_HANDLER", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"MICROSECONDS_INSERT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"MICROSECONDS_OTHER", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"MICROSECONDS_SELECT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"MICROSECONDS_TRANSACTION", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"MICROSECONDS_UPDATE", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"QUERIES_EMPTY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_DELETED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_FETCHED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_INSERTED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_READ", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_UPDATED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_INDEX_FIRST", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_INDEX_NEXT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"TRANSACTIONS_COMMIT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"TRANSACTIONS_ROLLBACK", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};

static inline const char *norm_table(const char *name)
{
  const char *ret = name;
  const char *prev = name;
  const char *cur = name;
  while (*cur)
  {
    if (*cur == '/')
    {
      ret = prev;
      prev = cur+1;
    }
    ++cur;
  }
  return ret;
}

/*
  Moves a table's existing stats to a new hash based on that new name.
  If stats already exist for the new name (shouldn't happen), overwrites them.
  This is called when a table is renamed.

  SYNOPSIS
    table_stats_rename()
      old_name - the table's old name, as: "db_name/table_name"
      new_name - the table's new name, as: "db_name/table_name"
 */
void table_stats_rename(const char *old_name, const char *new_name)
{
  old_name = norm_table(old_name);
  new_name = norm_table(new_name);

  TABLE_STATS* stats;
  char *old_key = my_strdup(old_name, MYF(MY_WME));
  int old_len = strlen(old_name)+1;
  char *old_table = old_key;
  char *new_key = my_strdup(new_name, MYF(MY_WME));
  int new_len = strlen(new_name)+1;
  char *new_table = new_key;

  if (!old_key || !new_key) // Memory allocation error
  {
    sql_print_error("Memory allocation error in table stats.");
    return;
  }

  strsep(&old_table, "/");
  strsep(&new_table, "/");

  mysql_mutex_lock(&LOCK_global_table_stats);

  stats = (TABLE_STATS*)
              my_hash_search(&global_table_stats, (uchar*)old_key, old_len);
  if (stats)
  {
    memcpy(stats->hash_key, new_key, new_len);
    stats->hash_key_len = new_len;

    memcpy(stats->db, new_key, new_table - new_key);
    memcpy(stats->table, new_table, new_len - (new_table - new_key));

    if (my_hash_update(&global_table_stats, (uchar*)stats,
                       (uchar*)old_key, old_len))
    {
      sql_print_error("Renaming table stats failed.");
    }
  }

  mysql_mutex_unlock(&LOCK_global_table_stats);
  my_free(new_key);
  my_free(old_key);
}

/*
  Removes a table's existing stats.
  This is called when a table is dropped.

  SYNOPSIS
    table_stats_delete()
      old_name - the table to be dropped, as: "db_name/table_name"
 */
void table_stats_delete(const char *old_name)
{
  old_name = norm_table(old_name);

  TABLE_STATS* old_stats;
  char *old_key = my_strdup(old_name, MYF(MY_WME));
  int old_len = strlen(old_name)+1;

  if (!old_key) // Memory allocation error
  {
    sql_print_error("Memory allocation error in table stats.");
    return;
  }

  {
    char *temp = old_key;
    strsep(&temp, "/");
  }

  mysql_mutex_lock(&LOCK_global_table_stats);

  old_stats = (TABLE_STATS*)
              my_hash_search(&global_table_stats, (uchar*)old_key, old_len);
  if (old_stats)
  {
    my_hash_delete(&global_table_stats, (uchar*)old_stats);
  }

  mysql_mutex_unlock(&LOCK_global_table_stats);
  my_free(old_key);
}
