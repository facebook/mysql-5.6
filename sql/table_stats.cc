#include "sql_base.h"
#include "sql_connect.h"
#include "sql_show.h"
#include "my_atomic.h"
#include "mysqld.h"

HASH global_table_stats;

/* ########### begin of encoding service ########### */

static bool names_map_initialized = false;

std::array<NAME_ID_MAP, MAX_MAP_NAME> my_names;

/*
** maximum size of each map:
** - DB:    1024
** - Table:  32K
** - Index: 128K
** These are absolute maximum and when reached will lead to
** no more objects added to their respective maps. Function
** check_name_maps_valid() will return FALSE if any of the
** maps is full.
** When an object is dropped we track it in a soft deleteed
** array until the number of soft deleted objected > 25% of
** the maximum size at which point we do a hard delete of
** those objects that are not referenced in the db instance
** and not just the shard (names are shared across shards).
*/
const std::array<uint, MAX_MAP_NAME> my_names_max_size =
   {(1024), (32*1024), (128*1024)};

/*
** init_names
**
** Initialize the name maps for all object names we care about
** Called at server startup
*/
void init_names()
{
  if (names_map_initialized)
    return;

  for (auto& map : my_names)
  {
    map.current_id = 0;
    mysql_rwlock_init(key_rwlock_NAME_ID_MAP_LOCK_name_id_map,
                      &(map.LOCK_name_id_map));
  }
  names_map_initialized = true;
}

/*
** destroy_names
**
** Destroy the name maps. Called server shutdown
*/
void destroy_names()
{
  if (!names_map_initialized)
    return;

  names_map_initialized = false;
  for (auto& map : my_names)
  {
    /* free the names in the array then clear it */
    for (char *elt : map.names)
      my_free(elt);
    map.names.clear();

    map.map.clear();
    map.current_id=0;

    mysql_rwlock_destroy(&(map.LOCK_name_id_map));
  }
}

/*
** check_name_maps_valid
**
** Check that the name maps are valid, i.e, new names can be added
** It is called in get_id() which returns INVALID_NAME_ID if this
** function returns false.
*/
static bool check_name_maps_valid()
{
  if (!names_map_initialized)
    return false;

  int map_name = DB_MAP_NAME;
  for (const auto& map : my_names)
  {
    DBUG_ASSERT(map_name < MAX_MAP_NAME);
    if (map.current_id >= my_names_max_size[map_name])
      return false;
    map_name++;
  }
  return true;
}

/*
** fill_invert_map
**
** Fill id_map with the invert of map, i.e, stores (name,id) from
** from the input map as (id,name) in the outout map (id_map)
*/
bool fill_invert_map(enum_map_name map_name, ID_NAME_MAP *id_map)
{
  DBUG_ASSERT(map_name < MAX_MAP_NAME);
  NAME_ID_MAP *name_map = &(my_names[map_name]);

  mysql_rwlock_rdlock(&(name_map->LOCK_name_id_map)); /* read */

  for (auto it = name_map->map.begin(); it != name_map->map.end(); it++)
    id_map->insert(std::make_pair(it->second, it->first.c_str()));

  mysql_rwlock_unlock(&(name_map->LOCK_name_id_map));

  return true;
}

/*
** get_id
**
** Returns the ID for a given name (db, table, etc).
** If the name is already in the map then it returns its ID, else
** it'll either reuse the ID of a dropped name or create a new ID
** It returns INVALID_NAME_ID in case of an exception, e.g, if we
** reach the maximum capacity of the map
*/
uint get_id(enum_map_name map_name, const char *name, uint length)
{
  DBUG_ASSERT(map_name < MAX_MAP_NAME);
  NAME_ID_MAP *map = &(my_names[map_name]);
  uint table_id;
  bool write_lock = false;
  bool retry = true;

  while (retry)
  {
    /* we use a read lock first hoping to find a match for the
    ** provided name and retry with write lock in case a match
    ** is not found (write lock is required to update the map)
    */
    if (write_lock)
      mysql_rwlock_wrlock(&(map->LOCK_name_id_map));         /* write */
    else
      mysql_rwlock_rdlock(&(map->LOCK_name_id_map));          /* read */

    retry = false;
    auto iter = map->map.find(name);

    if (iter != map->map.end())       /* found a match: return the ID */
      table_id = iter->second;
    else
    if (!check_name_maps_valid())  /* one of the name maps is invalid */
    {
      my_atomic_add64((longlong*)&object_stats_misses, 1);
      table_id = INVALID_NAME_ID;
    }
    else
    if (write_lock)      /* enter a new entry of we have a write lock */
    {
      assert(length <= 256);
      char *str = (char *) my_malloc(length+1, MYF(MY_WME | MY_ZEROFILL));
      memcpy(str, name, length);
      str[length] = '\0';

      map->names.emplace_back(str);
      map->map.emplace(str, map->current_id);
      table_id = map->current_id++;
    }
    else                /* release the read lock and get a write lock */
    {
      retry = true;        /* we only want to retry with a write lock */
      write_lock = true;
      mysql_rwlock_unlock(&(map->LOCK_name_id_map));
    }
  }

  mysql_rwlock_unlock(&(map->LOCK_name_id_map));

  return table_id;
}

/*
** get_name - get a name from an ID
**  Lookup the inverted map for the specified ID
**  The inverted map is built for every query to return the stats
**  at the beginning of the associated fill_ function. It needs to
**  cleaned at the end.
*/
const char *get_name(ID_NAME_MAP *id_map, uint id)
{
  auto elt = id_map->find(id);
  if (elt == id_map->end())
    return nullptr;
  else
    return elt->second.c_str();
}

/* ########### end of encoding service ########### */

/*
  Update global table statistics for this table and optionally tables
  linked via TABLE::next.

  SYNOPSIS
    update_table_stats()
      tablep - the table for which global/user table stats are updated
      follow_next - when TRUE, update global stats for tables linked
                    via TABLE::next
 */
void update_table_stats(THD *thd, TABLE *tablep, bool follow_next)
{
  for (; tablep; tablep= tablep->next)
  {
    if (tablep->file)
    {
      /* update the table statistics for a given user-table pair */
      tablep->file->update_user_table_stats(thd);
      /* update the table statistics for a given table */
      tablep->file->update_global_table_stats(thd);
    }

    if (!follow_next)
      return;
  }
}

static void
clear_shared_table_stats(SHARED_TABLE_STATS *table_stats)
{
  table_stats->queries_used.clear();
  table_stats->queries_empty.clear();

  table_stats->rows_inserted.clear();
  table_stats->rows_updated.clear();
  table_stats->rows_deleted.clear();
  table_stats->rows_read.clear();
  table_stats->rows_requested.clear();
}

static void
clear_table_stats(TABLE_STATS* table_stats)
{
  clear_shared_table_stats(&(table_stats->shared_stats));

  table_stats->last_admin.clear();
  table_stats->last_non_admin.clear();

  for (int x=0; x < MAX_INDEX_STATS; ++x)
  {
    table_stats->indexes[x].rows_inserted.clear();
    table_stats->indexes[x].rows_updated.clear();
    table_stats->indexes[x].rows_deleted.clear();
    table_stats->indexes[x].rows_read.clear();
    table_stats->indexes[x].rows_requested.clear();
    table_stats->indexes[x].rows_index_first.clear();
    table_stats->indexes[x].rows_index_next.clear();

    table_stats->indexes[x].io_perf_read.init();
  }

  table_stats->n_lock_wait.clear();
  table_stats->n_lock_wait_timeout.clear();
  table_stats->n_lock_deadlock.clear();

  table_stats->rows_index_first.clear();
  table_stats->rows_index_next.clear();

  table_stats->io_perf_read.init();
  table_stats->io_perf_write.init();
  table_stats->io_perf_read_blob.init();
  table_stats->io_perf_read_primary.init();
  table_stats->io_perf_read_secondary.init();
  table_stats->index_inserts.clear();
  table_stats->comment_bytes.clear();

  memset(&table_stats->page_stats, 0, sizeof(table_stats->page_stats));
  memset(&table_stats->comp_stats, 0, sizeof(table_stats->comp_stats));
}

/*
 *   Initialize the index names in table_stats->indexes
 *
 *     SYNOPSIS
 *      set_index_stats_names
 *      table_stats - object to initialize
 *
 *     RETURN VALUE
 *      0 on success, !0 on failure
 *
 *    Stats are stored for at most MAX_INDEX_KEYS and when there are more than
 *    (MAX_INDEX_KEYS-1) indexes then use the last entry for the extra indexes
 *      which gets the name "STATS_OVERFLOW".
 */
static int
set_index_stats_names(TABLE_STATS *table_stats, TABLE *table)
{
  uint idxIter;
  int  rc = 0;

  table_stats->num_indexes= std::min(table->s->keys,(uint)MAX_INDEX_STATS);

  for (idxIter = 0; idxIter < table_stats->num_indexes; ++idxIter)
  {
    uint index_id;
    char const *index_name = table->s->key_info[idxIter].name;

    if (idxIter == (MAX_INDEX_STATS - 1) && table->s->keys > MAX_INDEX_STATS)
      index_name = "STATS_OVERFLOW";
    if ((index_id = get_id(INDEX_MAP_NAME, index_name, strlen(index_name)))
        != INVALID_NAME_ID)
      table_stats->indexes[idxIter].index_id = index_id;
    else
    {
      rc = -1;
      break;
    }
  }

  return rc;
}

/*
  get_table_stats_attributes
   Get the table statistic attributes
   Returns TRUE if successful and FALSE otherwise
  Input:
   db_name       in:  - DB name
   table_name    in:  - table name
   engine_name   in:  - engine name
   db_id        out:  - DB ID
   table_id     out:  - table ID
   hash_key     out:  - hash key
*/
static bool get_table_stats_attributes(
  const char *db_name,
  const char *table_name,
  const char *engine_name,
  uint32_t *db_id,
  uint32_t *table_id,
  uint64_t *hash_key)
{
  DBUG_ASSERT(db_name && table_name && engine_name);

  if (!db_name || !table_name || !engine_name)
  {
    // NO_LINT_DEBUG
    sql_print_error("No key for table or user_table statistics.");
    return false;
  }

  size_t db_name_len    = strlen(db_name);
  size_t table_name_len = strlen(table_name);

  if (db_name_len > NAME_LEN || table_name_len > NAME_LEN)
  {

    // NO_LINT_DEBUG
    sql_print_error("Db or table name too long for table stats :%s:%s:\n",
                    db_name, table_name);
    DBUG_ABORT();
    return false;
  }

  /* skip tables created internally for full text search */
  if (is_fts_table(table_name))
    return false;

  /* get the id's and check they are valid */
  if ((*db_id = get_id(DB_MAP_NAME, db_name, db_name_len)) == INVALID_NAME_ID)
    return false;

  if ((*table_id = get_id(TABLE_MAP_NAME, table_name, table_name_len)) ==
      INVALID_NAME_ID)
    return false;

  *hash_key = ((uint64_t)(*db_id) << 32) | (*table_id);

  return true;
}

/*
  set_table_stats_attributes
   Sets the table statistic attributes (cache key, db, table and engine names)
  Input:
   stats         out: - shared table statistics
   db_id         in:  - DB ID
   table_id      in:  - table ID
   engine_name   in:  - engine name
   hash_key      in:  - hash key
*/
static void set_table_stats_attributes(
    SHARED_TABLE_STATS *stats,
    const uint32_t db_id,
    const uint32_t table_id,
    const char  *engine_name,
    const uint64_t hash_key)
{
  stats->db_id = db_id;
  stats->table_id = table_id;
  stats->engine_name= engine_name;
  stats->hash_key = hash_key;
}

static TABLE_STATS*
get_table_stats_by_name(const char *db_name,
                        const char *table_name,
                        const char *engine_name,
                        TABLE *tbl)
{
  TABLE_STATS* table_stats;
  uint32_t  db_id;
  uint32_t  table_id;
  uint64_t  hash_key;

  if (!names_map_initialized)
    return NULL;

  if (!get_table_stats_attributes(db_name, table_name, engine_name,
                                  &db_id, &table_id, &hash_key))
    return NULL;

  bool table_stats_lock_acquired = lock_global_table_stats();

  // Get or create the TABLE_STATS object for this table.
  if (!(table_stats= (TABLE_STATS *)my_hash_search(&global_table_stats,
                                                   (uchar*)&hash_key,
                                                   sizeof(hash_key))))
  {
    if (!(table_stats= ((TABLE_STATS*)my_malloc(sizeof(TABLE_STATS),
                                                MYF(MY_WME)))))
    {
      sql_print_error("Cannot allocate memory for TABLE_STATISTICS.");
      unlock_global_table_stats(table_stats_lock_acquired);
      return NULL;
    }

    set_table_stats_attributes(&(table_stats->shared_stats), db_id, table_id,
                               engine_name, hash_key);

    table_stats->num_indexes= 0;
    if (tbl && set_index_stats_names(table_stats, tbl))
    {
      sql_print_error("Cannot generate name for index stats.");
      my_free((char*)table_stats);
      unlock_global_table_stats(table_stats_lock_acquired);
      return NULL;
    }

    clear_table_stats(table_stats);
    table_stats->should_update = false;

    if (my_hash_insert(&global_table_stats, (uchar*)table_stats))
    {
      // Out of memory.
      sql_print_error("Inserting table stats failed.");
      my_free((char*)table_stats);
      unlock_global_table_stats(table_stats_lock_acquired);
      return NULL;
    }
  }
  else
  {
    /*
     * Keep things in sync after create or drop index.
     * This does not notice create
     * followed by drop. "reset statistics" will fix that.
     */
    if (tbl && table_stats->num_indexes != std::min(tbl->s->keys,
                                                    (uint)MAX_INDEX_STATS))
    {
      if (set_index_stats_names(table_stats, tbl))
      {
        sql_print_error("Cannot generate name for index stats.");
        unlock_global_table_stats(table_stats_lock_acquired);
        return NULL;
      }
    }
  }

  unlock_global_table_stats(table_stats_lock_acquired);

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
                                      my_bool not_used MY_ATTRIBUTE((unused)))
{
  *length = sizeof(table_stats->shared_stats.hash_key);
  return (uchar*) (&table_stats->shared_stats.hash_key);
}

extern "C" void free_table_stats(TABLE_STATS* table_stats)
{
  my_free((char*)table_stats);
}

void init_global_table_stats(void)
{
  if (my_hash_init(&global_table_stats, &my_charset_bin, max_connections,
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
  bool table_stats_lock_acquired = lock_global_table_stats();

  for (unsigned i = 0; i < global_table_stats.records; ++i) {
    TABLE_STATS *table_stats =
      (TABLE_STATS*)my_hash_element(&global_table_stats, i);

    clear_table_stats(table_stats);
    table_stats->num_indexes= 0;
  }

  unlock_global_table_stats(table_stats_lock_acquired);
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

  {"QUERIES_USED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"QUERIES_EMPTY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},

  {"LAST_ADMIN", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"LAST_NON_ADMIN", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},

  {"ROW_LOCK_WAITS", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROW_LOCK_WAIT_TIMEOUTS", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROW_LOCK_DEADLOCKS", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONG, 0, 0, 0, SKIP_OPEN_TABLE},

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
  {"COMMENT_BYTES", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},

  {"INNODB_PAGES_READ", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"INNODB_PAGES_READ_INDEX", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"INNODB_PAGES_READ_BLOB", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"INNODB_PAGES_WRITTEN", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"INNODB_PAGES_WRITTEN_INDEX", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"INNODB_PAGES_WRITTEN_BLOB", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONG, 0, 0, 0, SKIP_OPEN_TABLE},

  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};

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
                         int n_lock_wait,
                         int n_lock_wait_timeout,
                         int n_lock_deadlock,
                         const char *engine)
{
  TABLE_STATS *stats = get_table_stats_by_name(db, table, engine, NULL);

  if (!stats)
    return;

  if (is_partition) {
    if (stats->should_update) {
      stats->io_perf_read.sum(*r);
      stats->io_perf_write.sum(*w);
      stats->io_perf_read_blob.sum(*r_blob);
      stats->io_perf_read_primary.sum(*r_primary);
      stats->io_perf_read_secondary.sum(*r_secondary);
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
  stats->io_perf_read.set_maybe(*r);
  stats->io_perf_write.set_maybe(*w);
  stats->io_perf_read_blob.set_maybe(*r_blob);
  stats->io_perf_read_primary.set_maybe(*r_primary);
  stats->io_perf_read_secondary.set_maybe(*r_secondary);

  copy_page_stats_with_races(&stats->page_stats, page_stats);

  copy_comp_stats_with_races(&stats->comp_stats, comp_stats);

  stats->n_lock_wait.set_maybe(n_lock_wait);
  stats->n_lock_wait_timeout.set_maybe(n_lock_wait_timeout);
  stats->n_lock_deadlock.set_maybe(n_lock_deadlock);
}

/*
  valid_shared_table_stats
   Returns TRUE if shared table statistics are valid: considered as non
   valid if all the statistics are 0.
   Used in both user and global statistics check functions
  Input:
    stats  in - shared statistics handle
*/
static
bool valid_shared_table_stats(SHARED_TABLE_STATS *stats)
{
  if (stats->rows_inserted.load() == 0 &&
      stats->rows_updated.load() == 0 &&
      stats->rows_deleted.load() == 0 &&
      stats->rows_read.load() == 0 &&
      stats->rows_requested.load() == 0 &&
      stats->queries_used.load() == 0 &&
      stats->queries_empty.load() == 0)
    return false;
  else
    return true;
}

/*
  valid_global_table_stats
   Returns TRUE if global table statistics are valid: considered as non
   valid if all the statistics are 0.
   Checks both the shared (valid_shared_table_stats()) and
   specific statistics.
  Input:
    table_stats  in - global table statistics handle
*/
static
bool valid_global_table_stats(TABLE_STATS *table_stats)
{
  /* check shared statistics */
  if (valid_shared_table_stats(&(table_stats->shared_stats)))
    return true;

  /* check statistics that are specific to TABLE_STATISTICS */
  if (table_stats->n_lock_wait.load() == 0 &&
      table_stats->n_lock_wait_timeout.load() == 0 &&
      table_stats->n_lock_deadlock.load() == 0 &&
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
      table_stats->comment_bytes.load() == 0 &&
      table_stats->page_stats.n_pages_read.load() == 0 &&
      table_stats->page_stats.n_pages_read_index.load() == 0 &&
      table_stats->page_stats.n_pages_read_blob.load() == 0 &&
      table_stats->page_stats.n_pages_written.load() == 0 &&
      table_stats->page_stats.n_pages_written_index.load() == 0 &&
      table_stats->page_stats.n_pages_written_blob.load() == 0)
    return false;
  else
    return true;
}

int fill_table_stats(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_table_stats");
  TABLE* table= tables->table;

  ha_get_table_stats(fill_table_stats_cb);

  bool table_stats_lock_acquired = lock_global_table_stats();

  ID_NAME_MAP db_map;
  ID_NAME_MAP table_map;
  std::array<ID_NAME_MAP*, 2> id_map {&db_map, &table_map};
  fill_invert_map(DB_MAP_NAME, &db_map);
  fill_invert_map(TABLE_MAP_NAME, &table_map);

  for (unsigned i = 0; i < global_table_stats.records; ++i) {
    int f= 0;

    TABLE_STATS *table_stats =
      (TABLE_STATS*)my_hash_element(&global_table_stats, i);

    /* skip this one if the statistics are not valid */
    if (!valid_global_table_stats(table_stats))
      continue;

    restore_record(table, s->default_values);

    if (!fill_shared_table_stats(&(table_stats->shared_stats), table,
                                 &f, &id_map))
      continue;

    table->field[f++]->store(table_stats->last_admin.load(), TRUE);
    table->field[f++]->store(table_stats->last_non_admin.load(), TRUE);

    table->field[f++]->store(table_stats->n_lock_wait.load(), TRUE);
    table->field[f++]->store(table_stats->n_lock_wait_timeout.load(), TRUE);
    table->field[f++]->store(table_stats->n_lock_deadlock.load(), TRUE);

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

    table->field[f++]->store(table_stats->comment_bytes.load(), TRUE);

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
      unlock_global_table_stats(table_stats_lock_acquired);
      DBUG_RETURN(-1);
    }
  }
  unlock_global_table_stats(table_stats_lock_acquired);

  DBUG_RETURN(0);
}

ST_FIELD_INFO index_stats_fields_info[]=
{
  {"TABLE_SCHEMA", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"TABLE_NAME", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"INDEX_NAME", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},

  {"TABLE_ENGINE", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_INSERTED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
                                                         SKIP_OPEN_TABLE},
  {"ROWS_UPDATED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
                                                         SKIP_OPEN_TABLE},
  {"ROWS_DELETED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
                                                         SKIP_OPEN_TABLE},
  {"ROWS_READ", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
                                                         SKIP_OPEN_TABLE},
  {"ROWS_REQUESTED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
                                                         SKIP_OPEN_TABLE},
  {"ROWS_INDEX_FIRST", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
                                                0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_INDEX_NEXT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
                                                0, 0, 0, SKIP_OPEN_TABLE},

  {"IO_READ_BYTES", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
                                                0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_REQUESTS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
                                                0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_SVC_USECS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
                                                0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_SVC_USECS_MAX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
                                                0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_WAIT_USECS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
                                                0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_WAIT_USECS_MAX", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
                                                0, 0, 0, SKIP_OPEN_TABLE},
  {"IO_READ_SLOW_IOS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
                                                0, 0, 0, SKIP_OPEN_TABLE},

  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};


int fill_index_stats(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_index_stats");
  TABLE* table= tables->table;

  bool table_stats_lock_acquired = lock_global_table_stats();

  ID_NAME_MAP db_map;
  ID_NAME_MAP table_map;
  ID_NAME_MAP index_map;
  std::array<ID_NAME_MAP*, 3> id_map {&db_map, &table_map, &index_map};
  fill_invert_map(DB_MAP_NAME,    &db_map);
  fill_invert_map(TABLE_MAP_NAME, &table_map);
  fill_invert_map(INDEX_MAP_NAME, &index_map);

  for (unsigned i = 0; i < global_table_stats.records; ++i) {
    uint ix;

    TABLE_STATS *table_stats =
      (TABLE_STATS*)my_hash_element(&global_table_stats, i);

    SHARED_TABLE_STATS *sts = &(table_stats->shared_stats);

    for (ix=0; ix < table_stats->num_indexes; ++ix)
    {
      INDEX_STATS *index_stats= &(table_stats->indexes[ix]);
      int f= 0;

      if (index_stats->rows_inserted.load() == 0 &&
          index_stats->rows_updated.load() == 0 &&
          index_stats->rows_deleted.load() == 0 &&
          index_stats->rows_read.load() == 0 &&
          index_stats->rows_requested.load() == 0)
      {
        continue;
      }

      restore_record(table, s->default_values);

      const char *a_name;
      /* DB_NAME */
      if (!(a_name = get_name(id_map.at(DB_MAP_NAME), sts->db_id)))
        continue;
      table->field[f++]->store(a_name, strlen(a_name), system_charset_info);

      /* TABLE_NAME */
      if (!(a_name = get_name(id_map.at(TABLE_MAP_NAME), sts->table_id)))
        continue;
      table->field[f++]->store(a_name, strlen(a_name), system_charset_info);

      /* INDEX_NAME */
      if (!(a_name = get_name(id_map.at(INDEX_MAP_NAME), index_stats->index_id)))
        continue;
      table->field[f++]->store(a_name, strlen(a_name), system_charset_info);

      table->field[f++]->store(sts->engine_name,
                               strlen(sts->engine_name),
                               system_charset_info);

      table->field[f++]->store(index_stats->rows_inserted.load(), TRUE);
      table->field[f++]->store(index_stats->rows_updated.load(), TRUE);
      table->field[f++]->store(index_stats->rows_deleted.load(), TRUE);
      table->field[f++]->store(index_stats->rows_read.load(), TRUE);
      table->field[f++]->store(index_stats->rows_requested.load(), TRUE);

      table->field[f++]->store(index_stats->rows_index_first.load(), TRUE);
      table->field[f++]->store(index_stats->rows_index_next.load(), TRUE);

      table->field[f++]->store(index_stats->io_perf_read.bytes.load(), TRUE);
      table->field[f++]->store(index_stats->io_perf_read.requests.load(), TRUE);
      table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                               index_stats->io_perf_read.svc_time.load()),
                               TRUE);
      table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                               index_stats->io_perf_read.svc_time_max.load()),
                               TRUE);
      table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                               index_stats->io_perf_read.wait_time.load()),
                               TRUE);
      table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                               index_stats->io_perf_read.wait_time_max.load()),
                               TRUE);
      table->field[f++]->store(index_stats->io_perf_read.slow_ios.load(), TRUE);


      if (schema_table_store_record(thd, table))
      {
        unlock_global_table_stats(table_stats_lock_acquired);
        DBUG_RETURN(-1);
      }
    }
  }

  unlock_global_table_stats(table_stats_lock_acquired);

  DBUG_RETURN(0);
}

ST_FIELD_INFO user_stats_fields_info[]=
{
  {"USER_NAME", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"BINLOG_BYTES_WRITTEN", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"BINLOG_DISK_READS", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
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
  {"ERRORS_NET_TOTAL", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ERRORS_NET_ER_NET_ERROR_ON_WRITE", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ERRORS_NET_ER_NET_PACKETS_OUT_OF_ORDER", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ERRORS_NET_ER_NET_PACKET_TOO_LARGE", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ERRORS_net_ER_NET_READ_ERROR", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ERRORS_net_ER_NET_READ_INTERRUPTED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ERRORS_net_ER_NET_UNCOMPRESS_ERROR", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ERRORS_net_ER_NET_WRITE_INTERRUPTED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
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
  {"MICROSECONDS_CPU", MY_INT64_NUM_DECIMAL_DIGITS,
    MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"MICROSECONDS_CPU_USER", MY_INT64_NUM_DECIMAL_DIGITS,
    MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"MICROSECONDS_CPU_SYS", MY_INT64_NUM_DECIMAL_DIGITS,
    MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"QUERIES_EMPTY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"QUERY_COMMENT_BYTES", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"RELAY_LOG_BYTES_WRITTEN", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_DELETED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_FETCHED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_INSERTED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_READ", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_UPDATED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_INDEX_FIRST", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"ROWS_INDEX_NEXT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"TRANSACTIONS_COMMIT", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"TRANSACTIONS_ROLLBACK", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"GTID_UNSAFE_CREATE_SELECT", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"GTID_UNSAFE_CREATE_DROP_TEMPORARY_TABLE_IN_TRANSACTION", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"GTID_UNSAFE_NON_TRANSACTIONAL_TABLE", MY_INT32_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
  {"CONNECTIONS_SSL_TOTAL", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, SKIP_OPEN_TABLE},
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
  if (!names_map_initialized)
    return;

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
    /* free successful memory allocations */
    if (new_key)
      my_free(new_key);
    if (old_key)
      my_free(old_key);

    sql_print_error("Memory allocation error in table stats.");
    return;
  }

  strsep(&old_table, "/");
  strsep(&new_table, "/");

  uint32_t  db_id    = get_id(DB_MAP_NAME, old_key, old_table-old_key);
  uint32_t  table_id = get_id(TABLE_MAP_NAME,
                            old_table, old_len-(old_table-old_key));
  /* both DB and table ID should be valid */
  DBUG_ASSERT(db_id != INVALID_NAME_ID && table_id != INVALID_NAME_ID);
  uint64_t  hash_key = ((uint64_t)db_id << 32) | table_id;

  bool table_stats_lock_acquired = lock_global_table_stats();

  stats = (TABLE_STATS*)
    my_hash_search(&global_table_stats, (uchar*)&hash_key, sizeof(hash_key));

  if (stats)
  {
    /* Update the DB ID and/or table ID, and hash key */

    /* db_id can be invalid if new DB name could not be inserted */
    if ((db_id = get_id(DB_MAP_NAME, new_key, new_table-new_key))
        != INVALID_NAME_ID)
      stats->shared_stats.db_id    = db_id;

    /* table_id can be invalid if new table name could not be inserted */
    if (db_id != INVALID_NAME_ID &&
        (table_id=get_id(TABLE_MAP_NAME,new_table, new_len-(new_table-new_key)))
        != INVALID_NAME_ID)
      stats->shared_stats.table_id = table_id;

    /* if both IDs are valid then compute the hash and update the structure */
    if (db_id != INVALID_NAME_ID && table_id != INVALID_NAME_ID)
    {
      stats->shared_stats.hash_key =
        ((uint64_t)stats->shared_stats.db_id << 32) |
        stats->shared_stats.table_id;

      if (my_hash_update(&global_table_stats, (uchar*)stats,
                         (uchar*)&hash_key, sizeof(hash_key)))
      {
        // NO_LINT_DEBUG
        sql_print_error("table_stats_rename: renaming table stats failed");
      }
    }
  }

  unlock_global_table_stats(table_stats_lock_acquired);

  /* rename the table entries for all users (user_table_statistics) */
  rename_user_table_stats(old_name, new_name);

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
  if (!names_map_initialized)
    return;

  old_name = norm_table(old_name);

  char *old_key = my_strdup(old_name, MYF(MY_WME));
  int old_len = strlen(old_name)+1;

  if (!old_key) // Memory allocation error
  {
    // NO_LINT_DEBUG
    sql_print_error("table_stats_delete: memory allocation failed");
    return;
  }

  char *old_table = old_key;
  strsep(&old_table, "/");

  /* skip tables created internally for full text search */
  if (is_fts_table(old_table))
  {
    my_free(old_key);
    return;
  }

  uint32_t  db_id = get_id(DB_MAP_NAME, old_key, old_table-old_key);
  uint32_t  table_id = get_id(TABLE_MAP_NAME,
                            old_table, old_len-(old_table-old_key));

  /* both DB and table ID should be valid */
  DBUG_ASSERT(db_id != INVALID_NAME_ID && table_id != INVALID_NAME_ID);

  if (db_id != INVALID_NAME_ID && table_id != INVALID_NAME_ID)
  {
    uint64_t  hash_key = ((uint64_t)db_id << 32) | (table_id);

    bool table_stats_lock_acquired = lock_global_table_stats();

    TABLE_STATS* old_stats;
    old_stats = (TABLE_STATS*)
      my_hash_search(&global_table_stats, (uchar*)&hash_key, sizeof(hash_key));

    if (old_stats)
      my_hash_delete(&global_table_stats, (uchar*)old_stats);

    unlock_global_table_stats(table_stats_lock_acquired);

    /* delete the entries for all users (user_table_statistics) */
    delete_user_table_stats(old_name);
  }

  my_free(old_key);
}

/*
  It's possible for this mutex to be locked twice by one thread when
  ha_myisam::write_row() errors out during a information schema query.

  We use an error checking mutex here so we can handle this situation.
  More details: https://github.com/facebook/mysql-5.6/issues/132.
*/
bool lock_global_table_stats() {
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
                     (&(&LOCK_global_table_stats)->m_mutex)->thread))
  {
    mysql_mutex_lock(&LOCK_global_table_stats);
    return false;
  }

  return true;
#else
  return mysql_mutex_lock(&LOCK_global_table_stats) == EDEADLK;
#endif
}

void unlock_global_table_stats(bool acquired) {
  /* If lock was already acquired by calling thread, do nothing. */
  if (acquired)
  {
    return;
  }

  /* Otherwise, unlock the mutex */
  mysql_mutex_unlock(&LOCK_global_table_stats);
}

/*
  USER_TABLE_STATISTICS

  This was added to the INFORMATION_SCHEMA to provide user statistics
  per user and table. It provides a subset of the statistics from the
  TABLE_STATISTICS that are relevant for use cases identified so far:
  - rows (inserted, updated, deleted, read, requested)
  - queries (used, empty)
*/
ST_FIELD_INFO user_table_stats_fields_info[]=
{
  {"USER_NAME", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"TABLE_SCHEMA", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"TABLE_NAME", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"TABLE_ENGINE", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},

  {"ROWS_INSERTED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
                                                             SKIP_OPEN_TABLE},
  {"ROWS_UPDATED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
                                                            SKIP_OPEN_TABLE},
  {"ROWS_DELETED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
                                                            SKIP_OPEN_TABLE},
  {"ROWS_READ", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
                                                         SKIP_OPEN_TABLE},
  {"ROWS_REQUESTED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
                                                              SKIP_OPEN_TABLE},

  {"QUERIES_USED", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
                                                            SKIP_OPEN_TABLE},
  {"QUERIES_EMPTY", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
                                                             SKIP_OPEN_TABLE},

  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};

/*
  Service functions to support USER_TABLE_STATISTICS
  - get_key_user_table_stats: get the key and length
  - free the statistics hash table
  - free the statistics structure
  - initialize the statistics structure
  - reset the statistics structure
*/

/*
  get_key_user_table_stats - Get Key User Table Stats
    Returns the key value and length for a user table stats

  Input:
    user_table_stats - user table statistics handle
    length (OUT)     - length of key
    <unused input>
*/
extern "C" uchar *get_key_user_table_stats(
    USER_TABLE_STATS *user_table_stats,
    size_t           *length,
    my_bool not_used MY_ATTRIBUTE((unused)))
{
  *length = sizeof(user_table_stats->shared_stats.hash_key);
  return (uchar*) (&user_table_stats->shared_stats.hash_key);
}

/*
  free_key_user_table_stats - Free User Key Table Stats
    Free a user table statistics entry

  Input:
    user_table_stats - user table stats handle
*/
extern "C" void free_key_user_table_stats(
    USER_TABLE_STATS *user_table_stats)
{
  my_free((char *) user_table_stats);
}

/*
  free_table_stats_for_user - Free Table Statistics for a User
    Frees the hash table used to track table statistics for a user

  Input:
    user_conn - use connection handle
*/
void free_table_stats_for_user(USER_CONN *user_conn)
{
  my_hash_free(&(user_conn->user_table_stats));
}

/*
  init_table_stats_for_user - Initialize Table Statistics for User
    Setup the hash table used to track all the table stats for user
    Initial allocation is 4 entries.

  Input:
    uc   in - user connection handle
*/
void init_table_stats_for_user(
    USER_CONN *uc)
{
  if (my_hash_init(&(uc->user_table_stats), &my_charset_bin, 4,
                   0, 0, (my_hash_get_key)get_key_user_table_stats,
                   (my_hash_free_key)free_key_user_table_stats, 0))
  {
    // NO_LINT_DEBUG
    sql_print_error("Initializing a connection user_table_stats failed.");
    DBUG_ABORT();
  }
}

/*
  clear_user_table_stats - Clear User Table Statistics
   Clears the statistics counters for a user table pair

  Input:
    table_stats - user table statistics handle
*/
static void
clear_user_table_stats(
    USER_TABLE_STATS* table_stats)
{
  /* clear the shared statistics, nothing else */
  clear_shared_table_stats(&(table_stats->shared_stats));
}

/*
  reset_table_stats_for_user - Reset Table Statistics For a User
   Resets the statistics for all the tables tracked for a user

   Input:
     uc  in - user connection handle
*/
void reset_table_stats_for_user(
    USER_CONN *uc)
{
  mysql_mutex_lock(&uc->LOCK_user_table_stats);
  if (!my_hash_inited(&uc->user_table_stats))
    goto end;

  for (uint iter = 0; iter < uc->user_table_stats.records; ++iter)
  {
    USER_TABLE_STATS *user_table_stats =
      (USER_TABLE_STATS *)my_hash_element(&(uc->user_table_stats), iter);

    clear_user_table_stats(user_table_stats);
  }
end:
  mysql_mutex_unlock(&uc->LOCK_user_table_stats);
}

/*
  valid_user_table_stats
   Returns TRUE if user table statistics are valid: considered as non
   valid if all the statistics are 0.
   Checks the shared statistics (valid_shared_table_stats())
  Input:
    table_stats  in - user table statistics handle
*/
bool valid_user_table_stats(USER_TABLE_STATS *table_stats)
{
  return valid_shared_table_stats(&(table_stats->shared_stats));
}

#ifndef EMBEDDED_LIBRARY
/*
  get_user_table_stats_by_name - Get User Table Stats By Name
   Returns the handle on a table statistics stucture for a user

  Input:
    uc          in: - user connection handle
    db_name     in: - database name
    table_name  in: - table name
    engine_name in: - engine name
    tbl         in: - TABLE handle
*/
static USER_TABLE_STATS*
get_user_table_stats_by_name(USER_CONN *uc,
                             const char *db_name,
                             const char *table_name,
                             const char *engine_name,
                             TABLE *tbl)
{
  USER_TABLE_STATS* table_stats = nullptr;

  if (!(UTS_LEVEL_ALL() && names_map_initialized))
    return table_stats;

  mysql_mutex_lock(&uc->LOCK_user_table_stats);

  if (!my_hash_inited(&uc->user_table_stats))
    init_table_stats_for_user(uc);

  uint32_t  db_id;
  uint32_t  table_id;
  uint64_t  hash_key;
  if (!get_table_stats_attributes(db_name, table_name, engine_name,
                                  &db_id, &table_id, &hash_key))
    goto end;

  // Get or create the USER_TABLE_STATS object for this table.
  if (!(table_stats= (USER_TABLE_STATS*)my_hash_search(
                                               &(uc->user_table_stats),
                                               (uchar*)&hash_key,
                                               sizeof(hash_key))))
  {
    if (!(table_stats= ((USER_TABLE_STATS*)my_malloc(sizeof(USER_TABLE_STATS),
                                                     MYF(MY_WME)))))
    {
      // NO_LINT_DEBUG
      sql_print_error("Cannot allocate memory for USER_TABLE_STATISTICS.");
      goto end;
    }

    set_table_stats_attributes(&(table_stats->shared_stats), db_id, table_id,
                               engine_name, hash_key);

    clear_user_table_stats(table_stats);

    if (my_hash_insert(&(uc->user_table_stats), (uchar*)table_stats))
    {
      // Out of memory
      // NO_LINT_DEBUG
      sql_print_error("Inserting table stats failed.");
      my_free((char*)table_stats);
      table_stats = NULL;
      goto end;
    }
  }

end:
  mysql_mutex_unlock(&uc->LOCK_user_table_stats);
  return table_stats;
}
#endif

/*
  get_user_table_stats()
   Return the USER_TABLE_STATS object for a table and user pair

  Input:
   thd            in: THD handle
   table          in: table for which an object is returned
   type_of_db     in: storage engine type

  RETURN VALUE
    USER_TABLE_STATS structure for the requested table and user
    NULL on failure
*/
USER_TABLE_STATS*
get_user_table_stats(THD *thd, TABLE *table, handlerton *engine_type)
{
#ifndef EMBEDDED_LIBRARY
  DBUG_ASSERT(table->s);
  const char *engine_name;
  USER_CONN  *user_conn;

  if (!table->s)
  {
    // NO_LINT_DEBUG
    sql_print_error("No key for user-table stats.");
    return NULL;
  }
  engine_name = ha_resolve_storage_engine_name(engine_type);
  user_conn   = get_user_conn_for_stats(thd);

  if (!user_conn)
    return NULL;

  return get_user_table_stats_by_name(user_conn,
                                      table->s->db.str,
                                      table->s->table_name.str,
                                      engine_name,
                                      table);
#else
  return NULL;
#endif
}

/*
  fill_shared_table_stats
   Fill the shared pieces of statistics for global and table.
   Used in both user and global statistics fill functions
  Input:
    stats   in - shared statistics handle
    table  out - where to store the statistics
    offset out - position of the current column to fill
*/
bool fill_shared_table_stats(
    SHARED_TABLE_STATS *stats,
    TABLE *table,
    int   *offset,
    std::array<ID_NAME_MAP*, 2> *id_map)
{
  int f = *offset;
  const char *a_name;

  /* SCHEMA_NAME */
  if (!(a_name = get_name(id_map->at(DB_MAP_NAME), stats->db_id)))
    return false;
  table->field[f++]->store(a_name, strlen(a_name), system_charset_info);

  /* TABLE_NAME */
  if (!(a_name = get_name(id_map->at(TABLE_MAP_NAME), stats->table_id)))
    return false;
  table->field[f++]->store(a_name, strlen(a_name), system_charset_info);

  /* ENGINE_NAME */
  table->field[f++]->store(stats->engine_name, strlen(stats->engine_name),
                           system_charset_info);

  table->field[f++]->store(stats->rows_inserted.load(), TRUE);
  table->field[f++]->store(stats->rows_updated.load(), TRUE);
  table->field[f++]->store(stats->rows_deleted.load(), TRUE);
  table->field[f++]->store(stats->rows_read.load(), TRUE);
  table->field[f++]->store(stats->rows_requested.load(), TRUE);

  table->field[f++]->store(stats->queries_used.load(), TRUE);
  table->field[f++]->store(stats->queries_empty.load(), TRUE);

  *offset = f;
  return true;
}
