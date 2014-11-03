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

#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation        // gcc: Class implementation
#endif

#include <mysql/plugin.h>
#include "ha_rocksdb.h"
#include "sql_class.h"
#include "sql_array.h"

#include "my_bit.h"

#include <sstream>

#include "rdb_datadic.h"
#include "rdb_locks.h"
#include "rdb_rowmods.h"

#include "rdb_cf_option.h"
#include "rdb_cf_manager.h"

#include "rocksdb/table.h"
#include "utilities/convenience.h"

/* This is here to get PRIu64, PRId64 */
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>

void dbug_dump_database(rocksdb::DB *db);
static handler *rocksdb_create_handler(handlerton *hton,
                                       TABLE_SHARE *table,
                                       MEM_ROOT *mem_root);
void key_copy(uchar *to_key, uchar *from_record, KEY *key_info,
              uint key_length);

///////////////////////////////////////////////////////////
// Parameters and settings
///////////////////////////////////////////////////////////
extern const longlong ROCKSDB_WRITE_BUFFER_SIZE_DEFAULT;
extern const int ROCKSDB_TARGET_FILE_SIZE_BASE_DEFAULT;
static char * rocksdb_write_buffer_size_str;
Numeric_cf_option write_buffer_size_map;

static char * rocksdb_target_file_size_base_str;
Numeric_cf_option target_file_size_base_map;

static char * rocksdb_db_options;
static char * rocksdb_default_cf_options;

///////////////////////////////////////////////////////////
// Globals
///////////////////////////////////////////////////////////
handlerton *rocksdb_hton;

rocksdb::DB *rdb= NULL;

static std::shared_ptr<rocksdb::Statistics> rocksdb_stats;

rocksdb::ColumnFamilyOptions default_cf_opts;


Column_family_manager cf_manager;
Table_ddl_manager ddl_manager;
LockTable row_locks;

/*
   Hash used to track the number of open tables; variable for example share
   methods
*/
static HASH rocksdb_open_tables;

/* The mutex used to init the hash; variable for example share methods */
mysql_mutex_t rocksdb_mutex;


//////////////////////////////////////////////////////////////////////////////
// Options parse support functions
//////////////////////////////////////////////////////////////////////////////

static int
rocksdb_write_buffer_size_validate(THD* thd,
                                   struct st_mysql_sys_var* var,
                                   void* save,
                                   struct st_mysql_value* value)
{
  /* The option is read-only, it should never be updated */
  DBUG_ASSERT(0);
  return 1;
#if 0
  const char*  param_str;
  char         buff[STRING_BUFFER_USUAL_SIZE];
  int          len= sizeof(buff);

  param_str= value->val_str(value, buff, &len);
  if (param_str != NULL)
  {
    if (parse_multi_number(param_str, &write_buffer_size_map))
    {
      save= (void*)1;
      return 1;
    }
  }
  save= NULL;
  return 0;
#endif
}


static void
rocksdb_write_buffer_size_update(THD* thd,
                                 struct st_mysql_sys_var* var,
                                 void* var_ptr,
                                 const void* save)
{
  /* The option is read-only, it should never be updated */
  DBUG_ASSERT(0);
}


static bool rocksdb_parse_write_buffer_size_arg()
{
  write_buffer_size_map.default_val= ROCKSDB_WRITE_BUFFER_SIZE_DEFAULT;
  if (parse_per_cf_numeric(rocksdb_write_buffer_size_str, &write_buffer_size_map))
  {
    sql_print_error("RocksDB: Invalid value for rocksdb_write_buffer_size: %s",
                    rocksdb_write_buffer_size_str);
    return true;
  }
  else
    return false;
}


static int
rocksdb_target_file_size_base_validate(THD* thd,
                                       struct st_mysql_sys_var* var,
                                       void* save,
                                       struct st_mysql_value* value)
{
  /* The option is read-only, it should never be updated */
  DBUG_ASSERT(0);
  return 1;
}


static void
rocksdb_target_file_size_base_update(THD* thd,
                                     struct st_mysql_sys_var* var,
                                     void* var_ptr,
                                     const void* save)
{
  /* The option is read-only, it should never be updated */
  DBUG_ASSERT(0);
}


static bool rocksdb_parse_target_file_size_base_arg()
{
  target_file_size_base_map.default_val= ROCKSDB_TARGET_FILE_SIZE_BASE_DEFAULT;
  if (parse_per_cf_numeric(rocksdb_target_file_size_base_str,
                           &target_file_size_base_map))
  {
    sql_print_error("RocksDB: Invalid value for rocksdb_target_file_size_base: %s",
                    rocksdb_target_file_size_base_str);
    return true;
  }
  else
    return false;
}

//////////////////////////////////////////////////////////////////////////////
// Options definitions
//////////////////////////////////////////////////////////////////////////////
static long long rocksdb_block_cache_size;

//static long long rocksdb_write_buffer_size;
//static int rocksdb_target_file_size_base;

//TODO: 0 means don't wait at all, and we don't support it yet?
static MYSQL_THDVAR_ULONG(lock_wait_timeout, PLUGIN_VAR_RQCMDARG,
  "Number of seconds to wait for lock",
  NULL, NULL, /*default*/ 1, /*min*/ 1, /*max*/ 1024*1024*1024, 0);

static MYSQL_THDVAR_BOOL(bulk_load, PLUGIN_VAR_RQCMDARG,
  "Use bulk-load mode for inserts", NULL, NULL, FALSE);

static MYSQL_THDVAR_ULONG(max_row_locks, PLUGIN_VAR_RQCMDARG,
  "Maximum number of locks a transaction can have",
  NULL, NULL, /*default*/ 1024*1024*1024, /*min*/ 1, /*max*/ 1024*1024*1024, 0);

static MYSQL_THDVAR_ULONG(bulk_load_size, PLUGIN_VAR_RQCMDARG,
  "Max #records in a batch for bulk-load mode",
  NULL, NULL, /*default*/ 1000, /*min*/ 1, /*max*/ 1024*1024*1024, 0);

static MYSQL_SYSVAR_LONGLONG(block_cache_size, rocksdb_block_cache_size,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "block_cache size for RocksDB",
  NULL, NULL, /* RocksDB's default is 8 MB: */ 8*1024*1024L,
  /* min */ 1024L, /* max */ LONGLONG_MAX, /* Block size */1024L);

static MYSQL_SYSVAR_STR(write_buffer_size, rocksdb_write_buffer_size_str,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "options.write_buffer_size for RocksDB (Can also be set per-column family)",
  rocksdb_write_buffer_size_validate,
  rocksdb_write_buffer_size_update, "4194304" /* default is 4 MB for default CF */);
const longlong ROCKSDB_WRITE_BUFFER_SIZE_DEFAULT=4194304;

static MYSQL_SYSVAR_STR(db_options, rocksdb_db_options,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "db options for RocksDB",
  NULL, NULL, "");

static MYSQL_SYSVAR_STR(default_cf_options, rocksdb_default_cf_options,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "default cf options for RocksDB",
  NULL, NULL, "");

static MYSQL_SYSVAR_STR(target_file_size_base,
  rocksdb_target_file_size_base_str,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "options.target_file_size_base for RocksDB (Can also be set per-column family)",
  rocksdb_target_file_size_base_validate,
  rocksdb_target_file_size_base_update,
  "2097152" /* default is 2 MB for default CF */);
const int ROCKSDB_TARGET_FILE_SIZE_BASE_DEFAULT=2097152;

#if 0
static MYSQL_SYSVAR_INT(target_file_size_base,
  rocksdb_target_file_size_base,
  PLUGIN_VAR_RQCMDARG | PLUGIN_VAR_READONLY,
  "options.target_file_size_base for RocksDB",
  NULL, NULL, /* RocksDB's default is 2 MB: */ 2*1024*1024L,
  /* min */ 1024L, /* max */ INT_MAX, /* Block size */1024L);
#endif

static struct st_mysql_sys_var* rocksdb_system_variables[]= {
  MYSQL_SYSVAR(lock_wait_timeout),
  MYSQL_SYSVAR(max_row_locks),
  MYSQL_SYSVAR(bulk_load),
  MYSQL_SYSVAR(bulk_load_size),

  MYSQL_SYSVAR(block_cache_size),
  MYSQL_SYSVAR(write_buffer_size),
  MYSQL_SYSVAR(db_options),
  MYSQL_SYSVAR(default_cf_options),
  MYSQL_SYSVAR(target_file_size_base),

  NULL
};


///////////////////////////////////////////////////////////////////////////////////////////

/**
  @brief
  Function we use in the creation of our hash to get key.
*/

static uchar* rocksdb_get_key(ROCKSDB_SHARE *share, size_t *length,
                             my_bool not_used __attribute__((unused)))
{
  *length=share->table_name_length;
  return (uchar*) share->table_name;
}

/*
  The following is needed as an argument for thd_enter_cond, irrespectively of
  whether we're compiling with P_S or not.
*/
PSI_stage_info stage_waiting_on_row_lock= { 0, "Waiting for row lock", 0};

#ifdef HAVE_PSI_INTERFACE
static PSI_stage_info *all_rocksdb_stages[]=
{
  & stage_waiting_on_row_lock
};


static PSI_mutex_key ex_key_mutex_example, ex_key_mutex_ROCKSDB_SHARE_mutex;

static PSI_mutex_info all_rocksdb_mutexes[]=
{
  { &ex_key_mutex_example, "rocksdb", PSI_FLAG_GLOBAL},
  { &ex_key_mutex_ROCKSDB_SHARE_mutex, "ROCKSDB_SHARE::mutex", 0}
};

static void init_rocksdb_psi_keys()
{
  const char* category= "rocksdb";
  int count;

  if (PSI_server == NULL)
    return;

  count= array_elements(all_rocksdb_mutexes);
  PSI_server->register_mutex(category, all_rocksdb_mutexes, count);

  count= array_elements(all_rocksdb_stages);
  mysql_stage_register(category, all_rocksdb_stages, count);
}
#endif


/*
  The keys are in form: {index_number} {mem-comparable-key}

  (todo: knowledge about this format is shared between this class and
   RDBSE_KEYDEF)
*/

class Primary_key_comparator : public rocksdb::Comparator
{
public:
  int Compare(const rocksdb::Slice& a, const rocksdb::Slice& b) const
  {
    size_t a_size= a.size();
    size_t b_size= b.size();
    size_t len= (a_size < b_size) ? a_size : b_size;
    int res;

    if ((res= memcmp(a.data(), b.data(), len)))
      return res;

    /* Ok, res== 0 */
    if (a_size != b_size)
    {
      return a_size < b_size? -1 : 1;
    }
    return 0;
  }

  /* The following is not needed by RocksDB, but conceptually should be here: */
  static ulong get_hashnr(const char *key, size_t key_len);
  const char* Name() const { return "RocksDB_SE_v3.0"; }

  //TODO: advanced funcs:
  // - FindShortestSeparator
  // - FindShortSuccessor
  // for now, do-nothing implementations:
  void FindShortestSeparator(std::string* start, const rocksdb::Slice& limit) const {}
  void FindShortSuccessor(std::string* key) const {}
};

Primary_key_comparator rocksdb_pk_comparator;

int compare_mem_comparable_keys(const uchar *a, size_t a_len, const uchar *b, size_t b_len)
{
  rocksdb::Slice a_slice((char*)a, a_len);
  rocksdb::Slice b_slice((char*)b, b_len);
  return rocksdb_pk_comparator.Compare(a_slice, b_slice);
}



/*
  This is a rocksdb connection. Its members represent the current transaction,
  which consists of:
  - the snapshot
  - the changes we've made but are not seeing yet.

  The changes are made to individual tables, which store them here and then
  this object commits them on commit.
*/

class Rdb_transaction
{
public:
  /* The snapshot the transaction is used */
  const rocksdb::Snapshot *snapshot;

  /* The changes made by this transaction (and not yet applied) */
  Row_table changes;

  uint n_writes;

  /*
    These are needed to use LF-Hash. They are allocated per-thread. Logically,
    they are not part of the transaction but it's convenient to have them here.
  */
  LF_PINS *pins;

  /* Row locks taken by this transaction */
  Dynamic_array<Row_lock*> trx_locks;

  int timeout_sec; /* Cached value of @@rocksdb_lock_wait_timeout */
  int max_row_locks;

  void set_params(int timeout_sec_arg, int max_row_locks_arg)
  {
    timeout_sec= timeout_sec_arg;
    max_row_locks= max_row_locks_arg;
  }

  Row_lock *get_lock(const uchar* key, size_t keylen, bool *timed_out)
  {
    Row_lock *lock;
    if (trx_locks.elements() > max_row_locks)
    {
      *timed_out= false;
      return NULL;
    }
    if (!(lock= row_locks.get_lock(pins, key, keylen, timeout_sec)))
    {
      *timed_out= true;
      return NULL;
    }
    return lock;
  }

  void add_lock(Row_lock* lock)
  {
    trx_locks.append(lock);
  }

  void release_last_lock()
  {
    row_locks.release_lock(pins, trx_locks.at(trx_locks.elements() - 1));
    trx_locks.pop();
  }

  void release_locks()
  {
    int size= trx_locks.elements();
    for (int i= 0; i < size; i++)
      row_locks.release_lock(pins, trx_locks.at(i));
    trx_locks.clear();
  }

  bool commit()
  {
    bool res= false;
    flush_batch();
    /* rollback() will delete snapshot, batch and locks */
    rollback();
    return res;
  }

private:

  int flush_batch_intern()
  {
    bool res= false;
    rocksdb::WriteBatch batch;

    if (changes.is_empty())
      return false;

    Row_table_iter iter(&changes);

    for (iter.SeekToFirst(); iter.Valid(); iter.Next())
    {
      if (iter.is_tombstone())
      {
        batch.Delete(iter.cf_handle(), iter.key());
      }
      else
      {
        batch.Put(iter.cf_handle(), iter.key(), iter.value());
      }
    }
    rocksdb::Status s= rdb->Write(rocksdb::WriteOptions(), &batch);
    res= !s.ok(); // we return true when something failed
    return res;
  }

public:
  int flush_batch()
  {
    bool bres= flush_batch_intern();
    changes.reinit();
    n_writes= 0;
    return bres;
  }

  void prepare_for_write()
  {
    /* Currently, we don't do anything here */
  }

  /*
    This must be called when last statement is rolled back, but the transaction
    continues
  */
  void rollback_stmt() { changes.rollback_stmt(); }

  void start_stmt() { changes.start_stmt(); }
  void rollback()
  {
    if (snapshot)
    {
      rdb->ReleaseSnapshot(snapshot);
      snapshot= NULL;
    }

    changes.reinit();
    release_locks();
    n_writes= 0;
  }

  Rdb_transaction() :
    snapshot(NULL), n_writes(0), pins(NULL)
  {
    changes.init();
  }

  ~Rdb_transaction()
  {
    if (snapshot)
    {
      rdb->ReleaseSnapshot(snapshot);
      snapshot= NULL;
    }
    changes.cleanup();
  }
};


static Rdb_transaction* &get_trx_from_thd(THD *thd)
{
  return *(Rdb_transaction**) thd_ha_data(thd, rocksdb_hton);
}


/*
  TODO: maybe, call this in external_lock() and store in ha_rocksdb..
*/

static Rdb_transaction *get_or_create_trx(THD *thd)
{
  Rdb_transaction*& trx= get_trx_from_thd(thd);
  if (trx == NULL)
  {
    if (!(trx= new Rdb_transaction))
      return NULL;
    trx->pins= row_locks.get_pins();
    trx->snapshot= rdb->GetSnapshot();
  }
  else
  {
    if (!trx->snapshot)
      trx->snapshot= rdb->GetSnapshot();
  }
  return trx;
}


static int rocksdb_close_connection(handlerton* hton, THD* thd)
{
  Rdb_transaction*& trx= get_trx_from_thd(thd);
  if (trx && trx->pins)
    row_locks.put_pins(trx->pins);
  delete trx;
  trx= NULL;
  return 0;
}


static int rocksdb_commit(handlerton* hton, THD* thd, bool commit_trx)
{
  if (commit_trx)
  {
    Rdb_transaction*& trx= get_trx_from_thd(thd);
    if (trx)
      trx->commit(); // todo: return error code.
  }
  else
  {
    /*
      Committing a statement within a transaction. We don't need to do anything
      here. trx->start_stmt() will notify Rdb_transaction that another
      statement has started.
    */
  }
  return 0;
}


static int rocksdb_rollback(handlerton* hton, THD* thd, bool rollback_trx)
{
  if (rollback_trx)
  {
    /* Discard the changes made by the transaction */
    Rdb_transaction*& trx= get_trx_from_thd(thd);
    if (trx)
      trx->rollback();
  }
  else
  {
    Rdb_transaction*& trx= get_trx_from_thd(thd);
    if (trx)
      trx->rollback_stmt();
  }
  return 0;
}


/*
  This is called for SHOW ENGINE ROCKSDB STATUS|LOGS|etc.

  For now, produce info about live files (which gives an imprecise idea about
  what column families are there)
*/

static bool rocksdb_show_status(handlerton*		hton,
                                THD*			thd,
                                stat_print_fn*		stat_print,
                                enum ha_stat_type	stat_type)
{
  bool res= false;
  if (stat_type == HA_ENGINE_STATUS)
  {
    std::vector<rocksdb::LiveFileMetaData> metadata_vec;

    rdb->GetLiveFilesMetaData(&metadata_vec);
    std::ostringstream buf;

    for (auto it= metadata_vec.begin(); it!=metadata_vec.end(); it++)
    {
      buf << "cf=" << it->column_family_name;
      buf << " name=" << it->name;
      buf << " size=" << it->size;
      buf << std::endl;
    }

    std::string str= buf.str();
    res= stat_print(thd, rocksdb_hton_name,
                    (uint)strlen(rocksdb_hton_name),
                    STRING_WITH_LEN("live_files"),
                    str.c_str(), str.size());
  }
  return res;
}


void get_cf_options(const std::string &cf_name, rocksdb::ColumnFamilyOptions *opts)
{
  *opts= default_cf_opts;
  int tfsb= target_file_size_base_map.get_val(cf_name.c_str());
  size_t wbs= write_buffer_size_map.get_val(cf_name.c_str());

  opts->write_buffer_size= wbs;
  opts->target_file_size_base= tfsb;
}

/*
  Engine initialization function
*/

static int rocksdb_init_func(void *p)
{
  DBUG_ENTER("rocksdb_init_func");

#ifdef HAVE_PSI_INTERFACE
  init_rocksdb_psi_keys();
#endif

  /* Parse command-line option values */
  if (rocksdb_parse_write_buffer_size_arg() ||
      rocksdb_parse_target_file_size_base_arg())
  {
    DBUG_RETURN(1);
  }

  rocksdb_hton= (handlerton *)p;
  mysql_mutex_init(ex_key_mutex_example, &rocksdb_mutex, MY_MUTEX_INIT_FAST);
  (void) my_hash_init(&rocksdb_open_tables,system_charset_info,32,0,0,
                      (my_hash_get_key) rocksdb_get_key,0,0);

  rocksdb_hton->state=    SHOW_OPTION_YES;
  rocksdb_hton->create=   rocksdb_create_handler;
  rocksdb_hton->close_connection= rocksdb_close_connection;
  rocksdb_hton->commit=   rocksdb_commit;
  rocksdb_hton->rollback= rocksdb_rollback;
  rocksdb_hton->db_type=  DB_TYPE_ROCKSDB;
  rocksdb_hton->show_status= rocksdb_show_status;

  /*
    Don't specify HTON_CAN_RECREATE in flags. re-create is used by TRUNCATE
    TABLE to create an empty table from scratch. RocksDB cannot efficiently
    re-create a table.
  */
  rocksdb_hton->flags= HTON_TEMPORARY_NOT_SUPPORTED |
                       HTON_SUPPORTS_EXTENDED_KEYS;

  DBUG_ASSERT(!mysqld_embedded);

  row_locks.init(compare_mem_comparable_keys,
                 Primary_key_comparator::get_hashnr);

  rocksdb_stats= rocksdb::CreateDBStatistics();

  std::string rocksdb_db_name=  "./rocksdb";

  std::vector<std::string> cf_names;

  rocksdb::DBOptions db_opts;

  db_opts.create_if_missing = true;
  db_opts.statistics= rocksdb_stats;

  rocksdb::Status status;

  status= rocksdb::DB::ListColumnFamilies(db_opts, rocksdb_db_name,
                                          &cf_names);
  if (!status.ok())
  {
    /*
      When we start on an empty datadir, ListColumnFamilies returns IOError,
      and RocksDB doesn't provide any way to check what kind of error it was.
      Checking system errno happens to work right now.
    */
    if (status.IsIOError() && errno == ENOENT)
    {
      sql_print_information("RocksDB: Got ENOENT when listing column families");
      sql_print_information("RocksDB:   assuming that we're creating a new database");
    }
    else
    {
      std::string err_text= status.ToString();
      sql_print_error("RocksDB: Error listing column families: %s", err_text.c_str());
      DBUG_RETURN(1);
    }
  }
  else
    sql_print_information("RocksDB: %ld column families found", cf_names.size());

  std::vector<rocksdb::ColumnFamilyDescriptor> cf_descr;
  std::vector<rocksdb::ColumnFamilyHandle*> cf_handles;

  default_cf_opts.comparator= &rocksdb_pk_comparator;

  default_cf_opts.write_buffer_size= write_buffer_size_map.get_default_val();
  default_cf_opts.target_file_size_base= target_file_size_base_map.get_default_val();

  rocksdb::BlockBasedTableOptions table_options;
  table_options.block_cache = rocksdb::NewLRUCache(rocksdb_block_cache_size);
  default_cf_opts.table_factory.reset(rocksdb::NewBlockBasedTableFactory(table_options));

  if (!rocksdb::GetDBOptionsFromString(
        db_opts,
        std::string(rocksdb_db_options),
        &db_opts)) {
    DBUG_RETURN(1);
  }

  if (!rocksdb::GetColumnFamilyOptionsFromString(
        default_cf_opts,
        std::string(rocksdb_default_cf_options),
        &default_cf_opts)) {
    DBUG_RETURN(1);
  }

  /*
    If there are no column families, we're creating the new database.
    Create one column family named "default".
  */
  if (cf_names.size() == 0)
    cf_names.push_back(DEFAULT_CF_NAME);

  sql_print_information("RocksDB: Column Families at start:");
  for (size_t i = 0; i < cf_names.size(); ++i)
  {
    rocksdb::ColumnFamilyOptions opts;
    get_cf_options(cf_names[i], &opts);

    sql_print_information("  cf=%s", cf_names[i].c_str());
    sql_print_information("    write_buffer_size=%ld", opts.write_buffer_size);
    sql_print_information("    target_file_size_base=%" PRIu64,
                          opts.target_file_size_base);

    cf_descr.push_back(rocksdb::ColumnFamilyDescriptor(cf_names[i], opts));
  }

  rocksdb::Options main_opts(db_opts, default_cf_opts);
  main_opts.env->SetBackgroundThreads(main_opts.max_background_flushes,
                                      rocksdb::Env::Priority::HIGH);
  main_opts.env->SetBackgroundThreads(main_opts.max_background_compactions,
                                      rocksdb::Env::Priority::LOW);
  status= rocksdb::DB::Open(main_opts, rocksdb_db_name, cf_descr,
                            &cf_handles, &rdb);


  if (!status.ok())
  {
    std::string err_text= status.ToString();
    sql_print_error("RocksDB: Error opening instance: %s", err_text.c_str());
    DBUG_RETURN(1);
  }
  cf_manager.init(&cf_names, &cf_handles);

  if (ddl_manager.init(rdb))
    DBUG_RETURN(1);

  sql_print_information("RocksDB instance opened");
  DBUG_RETURN(0);
}


static int rocksdb_done_func(void *p)
{
  int error= 0;
  DBUG_ENTER("rocksdb_done_func");
  if (rocksdb_open_tables.records)
    error= 1;
  my_hash_free(&rocksdb_open_tables);
  mysql_mutex_destroy(&rocksdb_mutex);

  row_locks.cleanup();
  ddl_manager.cleanup();

  cf_manager.cleanup();

  delete rdb;
  rdb= NULL;

  DBUG_RETURN(error);
}


/**
  @brief
  Example of simple lock controls. The "share" it creates is a
  structure we will pass to each rocksdb handler. Do you have to have
  one of these? Well, you have pieces that are used for locking, and
  they are needed to function.
*/

static ROCKSDB_SHARE *get_share(const char *table_name, TABLE *table)
{
  ROCKSDB_SHARE *share;
  uint length;
  char *tmp_name;

  mysql_mutex_lock(&rocksdb_mutex);
  length=(uint) strlen(table_name);

  if (!(share=(ROCKSDB_SHARE*) my_hash_search(&rocksdb_open_tables,
                                              (uchar*) table_name,
                                              length)))
  {
    if (!(share=(ROCKSDB_SHARE *)
          my_multi_malloc(MYF(MY_WME | MY_ZEROFILL),
                          &share, sizeof(*share),
                          &tmp_name, length+1,
                          NullS)))
    {
      mysql_mutex_unlock(&rocksdb_mutex);
      return NULL;
    }

    share->use_count=0;
    share->table_name_length=length;
    share->table_name=tmp_name;
    strmov(share->table_name,table_name);

    if (my_hash_insert(&rocksdb_open_tables, (uchar*) share))
      goto error;
    thr_lock_init(&share->lock);
    //mysql_mutex_init(ex_key_mutex_ROCKSDB_SHARE_mutex,
    //                 &share->mutex, MY_MUTEX_INIT_FAST);
  }
  share->use_count++;
  mysql_mutex_unlock(&rocksdb_mutex);

  return share;

error:
 // mysql_mutex_destroy(&share->mutex);
  my_free(share);

  return NULL;
}


void ha_rocksdb::load_auto_incr_value()
{
  int save_active_index= active_index;
  active_index= table->s->primary_key;
  uint8 save_table_status= table->status;

  // Do a lookup.
  if (!index_last(table->record[0]))
    update_auto_incr_val();

  table->status= save_table_status;
  active_index= save_active_index;

  /*
    Do what ha_rocksdb::index_end() does.
    (Why don't we use index_init/index_end? class handler defines index_init
     as private, for some reason).
  */
  if (scan_it)
  {
    delete scan_it;
    scan_it= NULL;
  }
}


/* Get PK value from table->record[0]. */
void ha_rocksdb::update_auto_incr_val()
{
  Field *pk_field;
  longlong new_val;
  pk_field= table->key_info[table->s->primary_key].key_part[0].field;

  my_bitmap_map *old_map= dbug_tmp_use_all_columns(table, table->read_set);
  new_val= pk_field->val_int() + 1;
  dbug_tmp_restore_column_map(table->read_set, old_map);

  /* TODO: switch to compare-and-swap? */
  mysql_mutex_lock(&tbl_def->mutex);
  if (tbl_def->auto_incr_val < new_val)
    tbl_def->auto_incr_val= new_val;
  mysql_mutex_unlock(&tbl_def->mutex);
}


/**
  @brief
  Free lock controls. We call this whenever we close a table. If the table had
  the last reference to the share, then we free memory associated with it.
*/

static int free_share(ROCKSDB_SHARE *share)
{
  mysql_mutex_lock(&rocksdb_mutex);
  if (!--share->use_count)
  {
    my_hash_delete(&rocksdb_open_tables, (uchar*) share);
    thr_lock_delete(&share->lock);
    //mysql_mutex_destroy(&share->mutex);
    my_free(share);
  }
  mysql_mutex_unlock(&rocksdb_mutex);

  return 0;
}


static handler* rocksdb_create_handler(handlerton *hton,
                                       TABLE_SHARE *table,
                                       MEM_ROOT *mem_root)
{
  return new (mem_root) ha_rocksdb(hton, table);
}


ha_rocksdb::ha_rocksdb(handlerton *hton, TABLE_SHARE *table_arg)
  : handler(hton, table_arg), scan_it(NULL), pk_descr(NULL),
    key_descr(NULL),
    pk_can_be_decoded(false),
    pk_tuple(NULL), pk_packed_tuple(NULL),
    sec_key_packed_tuple(NULL), sec_key_tails(NULL),
    pack_buffer(NULL),
    lock_rows(FALSE),
    keyread_only(FALSE),
    field_enc(NULL)
{}


static const char *ha_rocksdb_exts[] = {
  NullS
};


const char **ha_rocksdb::bas_ext() const
{
  return ha_rocksdb_exts;
}


/*
  Convert record from table->record[0] form into a form that can be written
  into rocksdb.

  @param packed_rec OUT Data slice with record data.
*/

void ha_rocksdb::convert_record_to_storage_format(rocksdb::Slice *packed_rec)
{
  storage_record.length(0);
  /* All NULL bits are initially 0 */
  storage_record.fill(null_bytes_in_rec, 0);

  for (uint i=0; i < table->s->fields; i++)
  {
    Field *field= table->field[i];
    if (field_enc[i].maybe_null())
    {
      char *data= (char*)storage_record.ptr();
      if (field->is_null())
      {
        data[field_enc[i].null_offset] |= field_enc[i].null_mask;
        /* Don't write anything for NULL values */
        continue;
      }
    }

    if (field_enc[i].field_type == MYSQL_TYPE_BLOB)
    {
      Field_blob *blob= (Field_blob*)field;
      /* Get the number of bytes needed to store length*/
      uint length_bytes= blob->pack_length() - portable_sizeof_char_ptr;

      /* Store the length of the value */
      storage_record.append((char*)blob->ptr, length_bytes);

      /* Store the blob value itself */
      char *data_ptr;
      memcpy(&data_ptr, blob->ptr + length_bytes, sizeof(uchar**));
      storage_record.append(data_ptr, blob->get_length());
    }
    else if (field_enc[i].field_type == MYSQL_TYPE_VARCHAR)
    {
      Field_varstring* field_var= (Field_varstring*)field;
      uint data_len;
      /* field_var->length_bytes is 1 or 2 */
      if (field_var->length_bytes == 1)
      {
        data_len= field_var->ptr[0];
      }
      else
      {
        DBUG_ASSERT(field_var->length_bytes==2);
        data_len= uint2korr(field_var->ptr);
      }
      storage_record.append((char*)field_var->ptr,
                            field_var->length_bytes + data_len);
    }
    else
    {
      /* Copy the field data */
      uint len= field->pack_length_in_rec();
      storage_record.append((char*)field->ptr, len);
    }
  }
  *packed_rec= rocksdb::Slice(storage_record.ptr(), storage_record.length());
}


/*
  @param  slice Record in storage format
  @param  buf   Store record in table->record[0] form here
*/
int ha_rocksdb::convert_record_from_storage_format(rocksdb::Slice *slice,
                                                    uchar *buf)
{
  retrieved_record.assign(slice->data(), slice->size());
  return convert_record_from_storage_format(buf);
}


/*
  Unpack the record in this->retrieved_record from storage format into
  buf (which can be table->record[0] or table->record[1])

  If the table has blobs, the unpacked data in buf may keep pointers to the
  data in this->retrieved_record.

  @return
    0      OK
    other  Error inpacking the data
*/

int ha_rocksdb::convert_record_from_storage_format(uchar * buf)
{
  Stream_reader reader(retrieved_record);
  my_ptrdiff_t ptr_diff= buf - table->record[0];

  const char *null_bytes;
  if (!(null_bytes= reader.read(null_bytes_in_rec)))
    return HA_ERR_INTERNAL_ERROR;

  for (uint i=0; i < table->s->fields; i++)
  {
    Field *field= table->field[i];

    int isNull = field_enc[i].maybe_null() &&
      (null_bytes[field_enc[i].null_offset] & field_enc[i].null_mask) != 0;
    if (isNull)
      field->set_null(ptr_diff);
    else
      field->set_notnull(ptr_diff);

    if (field_enc[i].field_type == MYSQL_TYPE_BLOB)
    {
      Field_blob *blob= (Field_blob*)field;
      /* Get the number of bytes needed to store length*/
      uint length_bytes= blob->pack_length() - portable_sizeof_char_ptr;

      blob->move_field_offset(ptr_diff);

      if (isNull)
      {
        memset(blob->ptr, 0, length_bytes + sizeof(uchar**));
        blob->move_field_offset(-ptr_diff);
        /* NULL value means no data is stored */
        continue;
      }

      const char *data_len_str;
      if (!(data_len_str= reader.read(length_bytes)))
      {
        blob->move_field_offset(-ptr_diff);
        return HA_ERR_INTERNAL_ERROR;
      }

      memcpy(blob->ptr, data_len_str, length_bytes);

      uint32 data_len= blob->get_length((uchar*)data_len_str, length_bytes,
                                        table->s->db_low_byte_first);
      const char *blob_ptr;
      if (!(blob_ptr= reader.read(data_len)))
      {
        blob->move_field_offset(-ptr_diff);
        return HA_ERR_INTERNAL_ERROR;
      }

      // set 8-byte pointer to 0, like innodb does (relevant for 32-bit platforms)
      memset(blob->ptr + length_bytes, 0, 8);
      memcpy(blob->ptr + length_bytes, &blob_ptr, sizeof(uchar**));
      blob->move_field_offset(-ptr_diff);
    }
    else if (field_enc[i].field_type == MYSQL_TYPE_VARCHAR)
    {
      if (isNull)
        continue;

      Field_varstring* field_var= (Field_varstring*)field;
      const char *data_len_str;
      if (!(data_len_str= reader.read(field_var->length_bytes)))
        return HA_ERR_INTERNAL_ERROR;

      uint data_len;
      /* field_var->length_bytes is 1 or 2 */
      if (field_var->length_bytes == 1)
      {
        data_len= (uchar)data_len_str[0];
      }
      else
      {
        DBUG_ASSERT(field_var->length_bytes==2);
        data_len= uint2korr(data_len_str);
      }
      if (!(reader.read(data_len)))
        return HA_ERR_INTERNAL_ERROR;
      field_var->move_field_offset(ptr_diff);
      memcpy(field_var->ptr, data_len_str, field_var->length_bytes + data_len);
      field_var->move_field_offset(-ptr_diff);
    }
    else
    {
      if (isNull)
        continue;

      const char *data_bytes;
      uint len= field->pack_length_in_rec();
      if (!(data_bytes= reader.read(len)))
        return HA_ERR_INTERNAL_ERROR;
      field->move_field_offset(ptr_diff);
      memcpy((char*)field->ptr, data_bytes, len);
      field->move_field_offset(-ptr_diff);
    }
  }
  DBUG_ASSERT(reader.remaining_bytes() == 0);

  return 0;
}


static void make_dbname_tablename(StringBuffer<64> *str, TABLE *table_arg)
{
  str->append(table_arg->s->db.str, table_arg->s->db.length);
  str->append('.');
  str->append(table_arg->s->table_name.str, table_arg->s->table_name.length);
  str->c_ptr_safe();
}


/*
  Setup data needed to convert table->record[] to and from record storage format.
*/

void ha_rocksdb::setup_field_converters()
{
  uint i;
  uint null_bytes= 0;
  uchar cur_null_mask=0x1;

  if (!(field_enc= (FIELD_ENCODER*)my_malloc(table->s->fields*sizeof(FIELD_ENCODER), MYF(0))))
    return;

  for (i= 0; i < table->s->fields; i++)
  {
    Field *field= table->field[i];
    field_enc[i].field_type= field->real_type();

    if (field->real_maybe_null())
    {
      field_enc[i].null_mask= cur_null_mask;
      field_enc[i].null_offset=null_bytes;
      if (cur_null_mask == 0x80)
      {
        cur_null_mask= 0x1;
        null_bytes++;
      }
      else
        cur_null_mask= cur_null_mask << 1;
    }
    else
      field_enc[i].null_mask= 0;
  }

  /* Count the last, unfinished NULL-bits byte */
  if (cur_null_mask != 0x1)
    null_bytes++;

  null_bytes_in_rec= null_bytes;
}


int ha_rocksdb::open(const char *name, int mode, uint test_if_locked)
{
  DBUG_ENTER("ha_rocksdb::open");

  if (!(share = get_share(name, table)))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  thr_lock_data_init(&share->lock,&lock,NULL);

  /*
    note: pk_descr may be non-NULL here, when using partitions. It seems,
    it's ok to discard it
  */
  StringBuffer<64> fullname;
  make_dbname_tablename(&fullname, table);
  if (!(tbl_def= ddl_manager.find((uchar*)fullname.c_ptr(), fullname.length())))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  key_descr= tbl_def->key_descr;
  pk_descr= key_descr[table->s->primary_key];

  pk_key_parts= table->key_info[table->s->primary_key].user_defined_key_parts;

  uint key_len= table->key_info[table->s->primary_key].key_length;

  const char *comment=  table->key_info[table->s->primary_key].comment.str;
  const char *idx_name= table->key_info[table->s->primary_key].name;
  rocksdb::ColumnFamilyHandle *cf_handle;
  bool is_auto_cf;
  if (!(cf_handle= cf_manager.get_cf(comment, fullname.c_ptr(), idx_name,
                                     &is_auto_cf)))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  pk_descr->setup(table);  // move this into get_share() ??
  pk_descr->set_cf_handle(cf_handle, is_auto_cf);

  uint packed_key_len= pk_descr->max_storage_fmt_length();

  if (!(pk_tuple= (uchar*)my_malloc(key_len, MYF(0))) ||
      !(pk_packed_tuple= (uchar*)my_malloc(packed_key_len, MYF(0))))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  /* Sometimes, we may use sec_key_packed_tuple for storing packed PK */
  uint max_packed_sec_key_len= packed_key_len;
  for (uint i= 0; i < table->s->keys; i++)
  {
    if (i == table->s->primary_key) /* Primary key was processed above */
      continue;

    comment= table->key_info[i].comment.str;
    idx_name= table->key_info[i].name;
    if (!(cf_handle= cf_manager.get_cf(comment, fullname.c_ptr(), idx_name,
                                       &is_auto_cf)))
      DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

    key_descr[i]->setup(table);  // move this into get_share() ??
    key_descr[i]->set_cf_handle(cf_handle, is_auto_cf);

    uint packed_len= key_descr[i]->max_storage_fmt_length();
    if (packed_len > max_packed_sec_key_len)
      max_packed_sec_key_len= packed_len;
  }

  setup_field_converters();

  if (!(sec_key_packed_tuple= (uchar*)my_malloc(max_packed_sec_key_len,
                                                MYF(0))) ||
      !((sec_key_tails= (uchar*)my_malloc(max_packed_sec_key_len, MYF(0)))) ||
      !((pack_buffer=(uchar*)my_malloc(max_packed_sec_key_len, MYF(0)))))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  info(HA_STATUS_NO_LOCK | HA_STATUS_VARIABLE | HA_STATUS_CONST);

  /* TODO: move the following to where TABLE_SHARE is opened: */
  if (table->found_next_number_field)
    load_auto_incr_value();

  DBUG_RETURN(0);
}


int ha_rocksdb::close(void)
{
  DBUG_ENTER("ha_rocksdb::close");

  pk_descr= NULL;
  key_descr= NULL;

  if (pk_tuple)
    my_free(pk_tuple);
  pk_tuple= NULL;

  if (pk_packed_tuple)
    my_free(pk_packed_tuple);
  pk_packed_tuple= NULL;

  if (sec_key_packed_tuple)
    my_free(sec_key_packed_tuple);
  sec_key_packed_tuple= NULL;

  if (sec_key_tails)
    my_free(sec_key_tails);
  sec_key_tails= NULL;

  if (pack_buffer)
    my_free(pack_buffer);
  pack_buffer= NULL;

  if (field_enc)
    my_free(field_enc);
  field_enc= NULL;

  DBUG_RETURN(free_share(share));
}


const int HA_ERR_ROCKSDB_UNIQUE_NOT_SUPPORTED= HA_ERR_LAST+1;
const int HA_ERR_ROCKSDB_PK_REQUIRED= HA_ERR_LAST+2;
const int HA_ERR_ROCKSDB_TOO_MANY_LOCKS= HA_ERR_LAST+3;

bool ha_rocksdb::get_error_message(int error, String *buf)
{
  if (error == HA_ERR_ROCKSDB_PK_REQUIRED)
    buf->append("Table must have a PRIMARY KEY");
  else if (error == HA_ERR_ROCKSDB_UNIQUE_NOT_SUPPORTED)
    buf->append("Unique indexes are not supported");
  else if (error == HA_ERR_ROCKSDB_TOO_MANY_LOCKS)
    buf->append("Number of locks held reached @@rocksdb_max_row_locks");
  return FALSE; /* not a temporary error */
}


/*
  Create structures needed for storing data in rocksdb. This is called when the
  table is created. The structures will be shared by all TABLE* objects.

  @param
    table_arg  Table with definition
    db_table   "dbname.tablename"
    len        strlen of the above

  @return
    0      - Ok
    other  - error, either given table ddl is not supported by rocksdb or OOM.
*/

int ha_rocksdb::create_key_defs(TABLE *table_arg, const char *db_table, uint len)
{
  uint i;
  DBUG_ENTER("ha_rocksdb::create_key_defs");
  uint n_keys= table_arg->s->keys;

  /* Don't allow to create unique indexes */
/* TODO mdcallag: disable this for now to pretend to support UNIQUE indexes
  for (i= 0; i < table_arg->s->keys; i++)
  {
    if (table_arg->key_info[i].flags & HA_NOSAME &&
        i != table_arg->s->primary_key)
    {
      DBUG_RETURN(HA_ERR_ROCKSDB_UNIQUE_NOT_SUPPORTED);
    }
  }
*/

  /* Create table/key descriptions and put them into the data dictionary */
  if (!(key_descr= (RDBSE_KEYDEF**)my_malloc(sizeof(RDBSE_KEYDEF*) * n_keys,
                                             MYF(MY_ZEROFILL))))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  memset(key_descr, 0, sizeof(RDBSE_KEYDEF*) * n_keys);
  tbl_def= NULL;

  for (i= 0; i < table_arg->s->keys; i++)
  {
    rocksdb::ColumnFamilyHandle* cf_handle;
    /*
      index comment has Column Family name. If there was no comment, we get
      NULL, and it means use the default column family.
    */
    const char *comment= table_arg->key_info[i].comment.str;
    if (looks_like_per_index_cf_typo(comment))
    {
      my_error(ER_NOT_SUPPORTED_YET, MYF(0),
               "column family name looks like a typo of $per_index_cf");
      goto error;
    }
    bool is_auto_cf;
    cf_handle= cf_manager.get_or_create_cf(rdb, comment, db_table,
                                           table_arg->key_info[i].name,
                                           &is_auto_cf);
    if (!cf_handle)
      goto error;

    if (!(key_descr[i]= new RDBSE_KEYDEF(ddl_manager.get_next_number(), i,
                                         cf_handle, is_auto_cf)))
      goto error;
  }
  pk_descr= key_descr[table_arg->s->primary_key];

  if (!(tbl_def= new RDBSE_TABLE_DEF))
    goto error;

  tbl_def->n_keys= n_keys;
  tbl_def->key_descr= key_descr;
  tbl_def->dbname_tablename.append(db_table, len);
  if (ddl_manager.put_and_write(tbl_def, rdb))
    goto error;

  DBUG_RETURN(0);

error:
  /* Delete what we have allocated so far */
  for (i= 0; i < table_arg->s->keys;i++)
    delete key_descr[i];
  delete tbl_def;

  DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
}


static int rocksdb_normalize_tablename(const char *tablename,
                                       StringBuffer<256> *strbuf)
{
  DBUG_ASSERT(tablename[0] == '.' && tablename[1] == '/');
  tablename += 2;

  const char *p= tablename;
  for (; *p != '/'; p++)
  {
    if (*p =='\0')
    {
      DBUG_ASSERT(0); // We were not passed table name?
      return HA_ERR_INTERNAL_ERROR ;
    }
  }
  strbuf->append(tablename, p - tablename);
  strbuf->append('.');
  strbuf->append(p + 1);
  return 0;
}


/**
  @brief
  create() is called to create a database. The variable name will have the name
  of the table.

  @details
  When create() is called you do not need to worry about
  opening the table. Also, the .frm file will have already been
  created so adjusting create_info is not necessary. You can overwrite
  the .frm file at this point if you wish to change the table
  definition, but there are no methods currently provided for doing
  so.

  Called from handle.cc by ha_create_table().

  @see
  ha_create_table() in handle.cc
*/

int ha_rocksdb::create(const char *name, TABLE *table_arg,
                       HA_CREATE_INFO *create_info)
{
  int res;
  StringBuffer<256> strbuf;
  DBUG_ENTER("ha_rocksdb::create");

  /*
    Construct dbname.tablename ourselves, because parititioning
    passes strings like "./test/t14#P#p0" for individual partitions,
    while table_arg->s->table_name has none of that.
  */
  if (rocksdb_normalize_tablename(name, &strbuf))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  /*
    We have HA_REQUIRE_PRIMARY_KEY flag, which guarantees there will be a
    primary key.
    But there is an exception: the query

      ALTER TABLE ... DROP COLUMN primary_key_column

    will attempt to create a table without primary key.
  */
  if (table_arg->s->primary_key == MAX_INDEXES)
  {
    DBUG_RETURN(HA_ERR_ROCKSDB_PK_REQUIRED);
  }

  if ((res= create_key_defs(table_arg, strbuf.ptr(), strbuf.length())))
    DBUG_RETURN(res);

  if (create_info->auto_increment_value)
     tbl_def->auto_incr_val= create_info->auto_increment_value;

  /*
    We only support auto_increment at start of the PRIMARY KEY.
  */
  //Field *field;
  //if ((field= table_arg->next_number_field))
  /* TODO mdcallag: disable this for now to let UNIQUE indexes kind of work
  if ((field= table_arg->found_next_number_field))
  {
    int pk= table_arg->s->primary_key;
    Field *pk_field= table_arg->key_info[pk].key_part[0].field;
    if (field->field_index !=  pk_field->field_index)
      DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }
  */
  DBUG_RETURN(0);
}


/*
  This function is used only when the table has not yet been opened, and
  keyread_allowed bitmap doesn't have the correct values yet.

  See comment it ha_rocksdb::index_flags() for details.
*/

bool ha_rocksdb::check_keyread_allowed(uint inx, uint part, bool all_parts) const
{
  bool res= true;
  KEY *key_info= &table_share->key_info[inx];

  Field_pack_info dummy1;
  res= dummy1.setup(key_info->key_part[part].field);

  if (res && all_parts)
  {
    for (uint i= 0; i < part; i++)
    {
      Field *field;
      if ((field= key_info->key_part[i].field))
      {
        Field_pack_info dummy;
        if (!dummy.setup(field))
        {
          /* Cannot do index-only reads for this column */
          res= false;
          break;
        }
      }
    }
  }

  uint pk= table_share->primary_key;
  if (inx == pk && all_parts &&
      part == table_share->key_info[pk].user_defined_key_parts)
  {
    pk_can_be_decoded= res;
  }

  return res;
}


/*
  @note
    The problem with this function is that SQL layer calls it, when
     - the table has not been yet opened (no ::open() call done)
     - this->table_share already exists, but it is in the process of being
       filled, so some of fields are still NULL.
     - In particular, table_share->key_info[inx].key_part[] is filled only up
       to part #part. Subsequent key parts are not yet filled.

    To complicate things further, SQL layer will call index_flags() with
    all_parts=TRUE. Essentially, we're asked to provide flags for reading
    keyparts whose datatype is not yet known.

    We walk around this problem by using check_keyread_allowed(), which uses
    table_share object and is careful not to step on unitialized data.

    When we get a call with all_parts=TRUE, we try to analyze all parts but
    ignore those that have key_part->field==NULL (these are not initialized
    yet).
*/

ulong ha_rocksdb::index_flags(uint inx, uint part, bool all_parts) const
{
  ulong base_flags= HA_READ_NEXT  |   // doesn't seem to be used
                    HA_READ_ORDER |
                    HA_READ_RANGE |
                    HA_READ_PREV;

  if (check_keyread_allowed(inx, part, all_parts))
    base_flags |= HA_KEYREAD_ONLY;

  /*
    Index-only reads on primary key are the same as table scan for us. Still,
    we need to explicitly "allow" them, otherwise SQL layer will miss some
    plans.
  */
  if (inx == table_share->primary_key)
    base_flags |= HA_KEYREAD_ONLY;

  return base_flags;
}


/*
  Read next index tuple through the secondary index.
*/

int ha_rocksdb::secondary_index_read(int keyno, uchar *buf)
{
  if (scan_it->Valid())
  {
    rocksdb::Slice key= scan_it->key();

    /* Check if we've ran out of records of this index */
    if (key_descr[keyno]->covers_key(key.data(), key.size()))
    {
      int rc;
      //TODO: We could here check if we have ran out of range we're scanning
      uint size= key_descr[keyno]->get_primary_key_tuple(pk_descr, &key,
                                                         (char*) pk_packed_tuple);
      if (size == (uint)-1)
        return HA_ERR_INTERNAL_ERROR;

      last_rowkey.copy((const char*)pk_packed_tuple, size, &my_charset_bin);

      if (keyread_only && !lock_rows)
      {
        rocksdb::Slice value= scan_it->value();
        if (key_descr[keyno]->unpack_record(table, buf, &key, &value))
          return HA_ERR_INTERNAL_ERROR;
        rc= 0;
      }
      else
        rc= get_row_by_rowid(buf, (const char*)pk_packed_tuple, size);

      if (!rc)
      {
        table->status= 0;
        return rc;
      }
    }
  }
  table->status= STATUS_NOT_FOUND;
  return HA_ERR_END_OF_FILE;
}


//void store_key_image_to_rec(Field *field, uchar *ptr, uint len);

int ha_rocksdb::index_read_map(uchar *buf, const uchar *key,
                               key_part_map keypart_map,
                               enum ha_rkey_function find_flag)
{
  int rc= 0;
  DBUG_ENTER("ha_rocksdb::index_read_map");

  uint key_parts= table->key_info[active_index].user_defined_key_parts;
  RDBSE_KEYDEF *kd= key_descr[active_index];

  if (active_index == table->s->primary_key &&
      find_flag == HA_READ_KEY_EXACT &&
      ((keypart_map == ((key_part_map(1) << key_parts) - 1) ||
        keypart_map == HA_WHOLE_KEY)))
  {
    /*
      Equality lookup over primary key, using full tuple.
      This is a special case, use DB::Get.
    */
    uint size= kd->pack_index_tuple(table, pack_buffer, pk_packed_tuple, key,
                                    keypart_map);
    rc= get_row_by_rowid(buf, (const char*)pk_packed_tuple, size);
    DBUG_RETURN(rc);
  }

  uint n_used_parts= my_count_bits(keypart_map);

  uint packed_size;

  packed_size= kd->pack_index_tuple(table, pack_buffer,
                                    sec_key_packed_tuple, key,
                                    keypart_map);

  if (find_flag == HA_READ_PREFIX_LAST_OR_PREV ||
      find_flag == HA_READ_PREFIX_LAST)
  {
    /* See below */
    kd->successor(sec_key_packed_tuple, packed_size);
  }

  rocksdb::Slice slice((char*)sec_key_packed_tuple, packed_size);


  rocksdb::Slice rkey;
  /*
    This will open the iterator and position it at a record that's equal or
    greater than the lookup tuple.
  */
  setup_index_scan(kd->get_cf(), &slice);
  bool move_forward= true;


  switch (find_flag) {
  case HA_READ_KEY_EXACT:
  {
    if (!scan_it->Valid())
      rc= HA_ERR_KEY_NOT_FOUND;
    else
    {
      rkey= scan_it->key();
      if (!kd->covers_key(rkey.data(), rkey.size()) ||
           kd->cmp_full_keys(rkey.data(), rkey.size(),
                             slice.data(), slice.size(), n_used_parts))
      {
        /*
          The record we've got is not from this index, or is not equal to the
          lookup table
        */
        rc= HA_ERR_KEY_NOT_FOUND;
      }
    }
    break;
  }
  case HA_READ_KEY_OR_NEXT:
  {
    if (!scan_it->Valid())
      rc= HA_ERR_KEY_NOT_FOUND;
    else
    {
      rkey= scan_it->key();
      if (!kd->covers_key(rkey.data(), rkey.size()))
      {
        /* The record we've got is not from this index */
        rc= HA_ERR_KEY_NOT_FOUND;
      }
    }
    break;
  }
  case HA_READ_BEFORE_KEY:
  {
    move_forward= false;
    /* We want to read the record that's right *before* the given key.  */
    if (!scan_it->Valid())
    {
      /*
        All the values in the database are smaller than our key. Two cases
         - our index is the last in db. Its last value is a match
         - our index has no records (in that case we will get a record from
           our index and detect it below)
      */
      scan_it->SeekToLast();
    }
    else
    {
      /*
        RocksDB iterator is positioned at "the first key in the source that
        at or past target".
        We need to step one key back, so that we're at the last key that is
        before the target.
        If the passed key is greater than the max. value that is found in the
        table, then iterator is pointing at the *first* record in subsequent
        table/index.
      */
      scan_it->Prev();
    }
    /* fall through */
  }
  case HA_READ_AFTER_KEY:
  {
    bool in_key;
    bool have_row;
    /*
      Walk forward until we've found a record that is not equal to the lookup
      tuple, but still belongs to this index.
    */
    while ((have_row= scan_it->Valid()))
    {
      rkey= scan_it->key();
      if (!(in_key= kd->covers_key(rkey.data(), rkey.size())) ||
          kd->cmp_full_keys(rkey.data(), rkey.size(),
                            slice.data(), slice.size(),
                            n_used_parts))
        break;

      if (move_forward)
        scan_it->Next();
      else
        scan_it->Prev();
    }
    if (!have_row || !in_key)
      rc= HA_ERR_END_OF_FILE;
    break;
  }
  case HA_READ_KEY_OR_PREV:
  {
    if (!scan_it->Valid())
    {
      /*
        We're after the last value in the database. It could be we needed the
        last one.
      */
      scan_it->SeekToLast();
    }
    /* We should see a key that is less-or-equal than specified */
    bool in_key;
    bool have_row;
    while ((have_row= scan_it->Valid()))
    {
      rkey= scan_it->key();
      if (!(in_key= kd->covers_key(rkey.data(), rkey.size())) ||
           kd->cmp_full_keys(rkey.data(), rkey.size(),
                             slice.data(), slice.size(),
                             n_used_parts) <= 0)
        break;
      scan_it->Prev();
    }
    if (!have_row || !in_key)
      rc= HA_ERR_END_OF_FILE;
    break;
  }
  case HA_READ_PREFIX_LAST:
  case HA_READ_PREFIX_LAST_OR_PREV:
  {
    /*
      Given a prefix of (VAL1,VAL2), get the last record that has
      (kp1,kp2)=(VAL1,VAL2).  This cannot be translated directly to RocksDB
      Iterator command.

      We navigate to (VAL1,VAL2+1) and then step one record back.
    */
    if (!scan_it->Valid())
    {
      /*
        We're after the last value in the database. It could be we needed the
        last one.
      */
      scan_it->SeekToLast();
    }
    else
      scan_it->Prev();

    if (scan_it->Valid())
    {
      rc= 0;
      rkey= scan_it->key();
      if (!kd->covers_key(rkey.data(), rkey.size()))
      {
        /* The record we've got is not from this index */
        rc= HA_ERR_KEY_NOT_FOUND;
      }
      else
      if (find_flag == HA_READ_PREFIX_LAST)
      {
        packed_size= kd->pack_index_tuple(table, pack_buffer,
                                          sec_key_packed_tuple, key,
                                          keypart_map);
        /*
          Check if the record has the same search prefix.
        */
        if (kd->cmp_full_keys(rkey.data(), rkey.size(),
                              (const char*)sec_key_packed_tuple, packed_size,
                              n_used_parts))
          rc= HA_ERR_END_OF_FILE;
      }
    }
    else
      rc= HA_ERR_KEY_NOT_FOUND;

    break;
  }
  default:
    DBUG_ASSERT(0);
    break;
  }

  if (rc)
  {
    table->status= STATUS_NOT_FOUND; /* Only not found error is possible */
    DBUG_RETURN(rc);
  }

  skip_scan_it_next_call= FALSE;
  uint pk_size;

  if (active_index == table->s->primary_key)
  {
    pk_size= rkey.size();
    memcpy(pk_packed_tuple, rkey.data(), pk_size);
    last_rowkey.copy(rkey.data(), pk_size, &my_charset_bin);
    if (lock_rows)
    {
      /* We need to put a lock and re-read */
      // TODO: what if we find that the row is gone? Need a re-read?
      rc= get_row_by_rowid(buf, (const char*)pk_packed_tuple, pk_size);
    }
    else
    {
      /* Unpack from the row we've read */
      rocksdb::Slice value= scan_it->value();
      rc= convert_record_from_storage_format(&value, buf);
    }
  }
  else
  {
    pk_size= kd->get_primary_key_tuple(pk_descr, &rkey,
                                       (char*) pk_packed_tuple);
    if (pk_size != (uint)-1)
    {
      last_rowkey.copy((const char*)pk_packed_tuple, pk_size, &my_charset_bin);

      if (keyread_only && !lock_rows)
      {
        /* Get the key columns and primary key value */
        rocksdb::Slice value= scan_it->value();
        if (kd->unpack_record(table, buf, &rkey, &value))
          rc= HA_ERR_INTERNAL_ERROR;
        else
          rc= 0;
      }
      else
        rc= get_row_by_rowid(buf, (const char*)pk_packed_tuple, pk_size);
    }
    else
      rc= HA_ERR_INTERNAL_ERROR;
  }

  if (rc)
  {
    /* the only possible error condition is record-not-found */
    table->status= STATUS_NOT_FOUND;
  }
  else
    table->status= 0;
  DBUG_RETURN(rc);
}


int ha_rocksdb::index_read_last_map(uchar * buf, const uchar * key,
                                    key_part_map keypart_map)
{
  return index_read_map(buf, key, keypart_map, HA_READ_PREFIX_LAST);
}


/*
  Print a value into provided buffer, in hex form.
*/
void hexdump_value(char *strbuf, size_t strbuf_size, const rocksdb::Slice &val)
{
  char *p= strbuf;
  strbuf_size -= 3;
  for (size_t i= 0; i < val.size(); i++)
  {
    uint c= (uint)(uchar)val[i];
    DBUG_ASSERT( c < 0x100);
    int len= snprintf(p, strbuf + strbuf_size - p, "%02x", c);
    DBUG_ASSERT(len == 2);
    p += len;
    if (p >= strbuf + strbuf_size - 10)
    {
      // End the value with '..' if we ran out of buffer space
      p[0]= '.';
      p[1]= '.';
      p[2]=0;
      return;
    }
  }
  *p= 0;
}


int ha_rocksdb::check(THD* thd, HA_CHECK_OPT* check_opt)
{
  uint pk= table->s->primary_key;
  String rowkey_copy;
  String sec_key_copy;

  // Only when debugging: don't use snapshot when reading
  // Rdb_transaction *trx= get_or_create_trx(table->in_use);
  // trx->snapshot= NULL;

  /* For each secondary index, check that we can get a PK value from it */
  sql_print_information("Checking table %s", table->s->table_name.str);
  for (uint keyno= 0; keyno < table->s->keys; keyno++)
  {
    if (keyno != pk)
    {
      extra(HA_EXTRA_KEYREAD);
      ha_index_init(keyno, true);
      ha_rows rows= 0;
      int res;
      sql_print_information("  Checking index %s", table->key_info[keyno].name);
      while (1)
      {
        if (!rows)
          res= index_first(table->record[0]);
        else
          res= index_next(table->record[0]);

        if (res == HA_ERR_END_OF_FILE)
          break;
        if (res)
        {
          // error
          sql_print_error("  .. row %lld: index scan error %d", rows, res);
          goto error;
        }
        rocksdb::Slice key= scan_it->key();
        sec_key_copy.copy(key.data(), key.size(), &my_charset_bin);
        rowkey_copy.copy(last_rowkey.ptr(), last_rowkey.length(),
                         &my_charset_bin);
        if ((res= get_row_by_rowid(table->record[0], rowkey_copy.ptr(),
                                   rowkey_copy.length())))
        {
          sql_print_error("  .. row %lld: failed to fetch row by rowid", rows);
          goto error;
        }
        /* Check if we get the same PK value */
        uint packed_size= pk_descr->pack_record(table, pack_buffer, table->record[0],
                                                pk_packed_tuple, NULL, NULL);
        if (packed_size != rowkey_copy.length() ||
            memcmp(pk_packed_tuple, rowkey_copy.ptr(), packed_size))
        {
          sql_print_error("  .. row %lld: PK value mismatch", rows);
          goto print_and_error;
        }

        /* Check if we get the same secondary key value */
        int tail_size;
        packed_size= key_descr[keyno]->pack_record(table, pack_buffer,
                                                   table->record[0],
                                                   sec_key_packed_tuple,
                                                   sec_key_tails, &tail_size);
        if (packed_size != sec_key_copy.length() ||
            memcmp(sec_key_packed_tuple, sec_key_copy.ptr(), packed_size))
        {
          sql_print_error("  .. row %lld: secondary index value mismatch", rows);
          goto print_and_error;
        }
        rows++;
        continue;

print_and_error:
        {
          char buf[1000];
          hexdump_value(buf, 1000, rocksdb::Slice(rowkey_copy.ptr(),
                                                  rowkey_copy.length()));
          sql_print_error("  rowkey: %s", buf);

          hexdump_value(buf, 1000, rocksdb::Slice(retrieved_record.data(),
                                                  retrieved_record.size()));
          sql_print_error("  record: %s", buf);

          hexdump_value(buf, 1000, rocksdb::Slice(sec_key_copy.ptr(),
                                                  sec_key_copy.length()));
          sql_print_error("  index: %s", buf);

          goto error;
        }
      }
      sql_print_information("  ... %lld records found", rows);
      ha_index_end();
    }
  }
  extra(HA_EXTRA_NO_KEYREAD);

  /*
    TODO: we should check also for PK records that are missing in the secondary
    indexes.
    For that, need to walk through the PK and check that every PK record has a
    proper counterpart in each secondary index.
  */
  return HA_ADMIN_OK;
error:
  ha_index_or_rnd_end();
  extra(HA_EXTRA_NO_KEYREAD);
  return HA_ADMIN_CORRUPT;
}


static void dbug_dump_str(FILE *out, const char *str, int len)
{
  fprintf(out, "\"");
  for (int i= 0; i < len; i++)
  {
    if (str[i] > 32)
      fprintf(out, "%c", str[i]);
    else
      fprintf(out, "\\%d", str[i]);
  }
  fprintf(out, "\"");
}


/*
  Debugging help: dump the whole database into a human-readable file.
  Usage:
    dbug_dump_database(rdb);
*/

void dbug_dump_database(rocksdb::DB *db)
{
  FILE *out= fopen("/tmp/rocksdb.dump", "wt");
  if (!out)
    return;

  rocksdb::Iterator* it = db->NewIterator(rocksdb::ReadOptions());
  for (it->SeekToFirst(); it->Valid(); it->Next()) {
    rocksdb::Slice key= it->key();
    rocksdb::Slice val= it->value();
    dbug_dump_str(out, key.data(), key.size());
    fprintf(out, " -> ");
    dbug_dump_str(out, val.data(), val.size());
    fprintf(out, "\n");
  }
  //assert(it->status().ok());  // Check for any errors found during the scan
  delete it;
  fclose(out);
}


int ha_rocksdb::get_row_by_rowid(uchar *buf, const char *rowid, uint rowid_size)
{
  int rc;
  DBUG_ENTER("ha_rocksdb::get_row_by_rowid");

  rocksdb::Slice key_slice(rowid, rowid_size);

  Rdb_transaction *trx= get_or_create_trx(table->in_use);
  Row_lock *UNINIT_VAR(lock); // protected by 'if (lock_rows)'
  bool timed_out;
  if (lock_rows)
  {
    if (!(lock= trx->get_lock((const uchar*)rowid, rowid_size, &timed_out)))
      DBUG_RETURN(return_lock_failure(timed_out));
  }

  rocksdb::ReadOptions options;
  if (!lock_rows)
    options.snapshot= trx->snapshot;

  bool found;
  if (!trx->changes.Get(key_slice, &retrieved_record, &found))
  {
    rocksdb::Status s= rdb->Get(options, pk_descr->get_cf(), key_slice,
                                &retrieved_record);
    found= !s.IsNotFound();
  }

  if (found)
  {
    if (lock_rows)
      trx->add_lock(lock);
    last_rowkey.copy((const char*)rowid, rowid_size, &my_charset_bin);
    rc= convert_record_from_storage_format(buf);
    table->status= 0;
  }
  else
  {
    if (lock_rows)
      row_locks.release_lock(trx->pins, lock);
    table->status= STATUS_NOT_FOUND;
    rc= HA_ERR_KEY_NOT_FOUND;
  }

  DBUG_RETURN(rc);
}


int ha_rocksdb::index_next(uchar *buf)
{
  return index_next_with_direction(buf, true);
}


int ha_rocksdb::index_prev(uchar *buf)
{
  return index_next_with_direction(buf, false);
}


int ha_rocksdb::index_next_with_direction(uchar *buf, bool move_forward)
{
  int rc;
  DBUG_ENTER("ha_rocksdb::index_next");

  if (active_index == table->s->primary_key)
    rc= rnd_next_with_direction(buf, move_forward);
  else
  {
    if (skip_scan_it_next_call)
      skip_scan_it_next_call= FALSE;
    else
    {
      if (move_forward)
        scan_it->Next();  /* this call cannot fail */
      else
        scan_it->Prev();
    }

    rc= secondary_index_read(active_index, buf);
  }

  DBUG_RETURN(rc);
}


int ha_rocksdb::index_first(uchar *buf)
{
  uchar *key;
  uint key_size;
  int rc;
  DBUG_ENTER("ha_rocksdb::index_first");

  key= (active_index == table->s->primary_key)? pk_packed_tuple :
                                                sec_key_packed_tuple;
  key_descr[active_index]->get_infimum_key(key, &key_size);
  rocksdb::Slice index_key((const char*)key, key_size);

  setup_index_scan(key_descr[active_index]->get_cf(), &index_key);
  skip_scan_it_next_call= TRUE;

  rc= index_next(buf);
  DBUG_RETURN(rc);
}


int ha_rocksdb::index_last(uchar *buf)
{
  uchar *key;
  uint key_size;
  int rc;
  DBUG_ENTER("ha_rocksdb::index_last");

  key= (active_index == table->s->primary_key)? pk_packed_tuple :
                                                sec_key_packed_tuple;
  key_descr[active_index]->get_supremum_key(key, &key_size);
  rocksdb::Slice index_key((const char*)key, key_size);

  setup_index_scan(key_descr[active_index]->get_cf(), &index_key);

  if (!scan_it->Valid())
  {
    /*
      RocksDB keyspace has no records that follow our index. It could be, our
      index is the last in the keyspace. Navigate to last record in the
      keyspace.
    */
    scan_it->SeekToLast();
  }
  else
  {
    /*
      Iterator is pointing at some record right after the end of the index.
      Move one record back.
    */
    scan_it->Prev();
  }
  skip_scan_it_next_call= FALSE;

  if (active_index == table->s->primary_key)
  {
    skip_scan_it_next_call= true;
    rc= rnd_next_with_direction(buf, false);
  }
  else
    rc= secondary_index_read(active_index, buf);

  DBUG_RETURN(rc);
}


void ha_rocksdb::unlock_row()
{
  if (lock_rows)
  {
    Rdb_transaction *trx= get_or_create_trx(table->in_use);
    trx->release_last_lock();
  }
}


//static const char* empty_str="";

int ha_rocksdb::write_row(uchar *buf)
{
  DBUG_ENTER("ha_rocksdb::write_row");
  DBUG_ASSERT(buf == table->record[0]);
  DBUG_ASSERT(lock_rows);

  /* Update AUTO_INCREMENT column */
  //bool using_auto_incr_value= false;

  /*
    Note: "buf == table->record[0]" is copied from innodb. I am not aware of
    any use cases where this condition is not true.
  */
  if (table->next_number_field && buf == table->record[0])
  {
    int err;
    if ((err= update_auto_increment()))
    {
      DBUG_RETURN(err);
    }
    //using_auto_incr_value= true;
  }

  /* Get a packed primary key value */
  uint packed_size= pk_descr->pack_record(table, pack_buffer, buf,
                                          pk_packed_tuple, NULL, NULL);
  rocksdb::Slice key_slice((const char*)pk_packed_tuple, packed_size);

  Rdb_transaction *trx= get_or_create_trx(table->in_use);
  bool do_locking= !THDVAR(table->in_use, bulk_load);
  Row_lock *lock= NULL; // init to shut up the compiler

  if (do_locking)
  {
    /*
      Get a record lock to make sure we do not overwrite somebody's changes
    */
    bool timed_out;
    if (!(lock= trx->get_lock(pk_packed_tuple, packed_size, &timed_out)))
    {
      DBUG_RETURN(return_lock_failure(timed_out));
    }

    /*
      note: we intentionally don't set options.snapshot here. We want to read
      the latest committed data.
    */
    rocksdb::ReadOptions options;

    bool found;
    if (!trx->changes.Get(key_slice, &retrieved_record, &found))
    {
      rocksdb::Status s= rdb->Get(options, pk_descr->get_cf(), key_slice,
                                  &retrieved_record);
      found= !s.IsNotFound();
    }

    if (found)
    {
      /* There is a row with this rowid already */
      row_locks.release_lock(trx->pins, lock);
      DBUG_RETURN(HA_ERR_FOUND_DUPP_KEY);
    }
  }

  /* Ok, we keep a lock. This will prevent concurrent writes with this rowid */
  trx->prepare_for_write();

  if (do_locking)
    trx->add_lock(lock); /* the lock will be released when trx commits */

  /* Write every secondary index */
  for (uint i= 0; i < table->s->keys; i++)
  {
    if (i != table->s->primary_key)
    {
      int packed_size;
      int tail_size;

      packed_size= key_descr[i]->pack_record(table, pack_buffer, buf,
                                             sec_key_packed_tuple,
                                             sec_key_tails, &tail_size);

      rocksdb::Slice secondary_key_slice((char*)sec_key_packed_tuple,
                                         packed_size);
      rocksdb::Slice value_slice((const char*)sec_key_tails, tail_size);
      trx->changes.Put(key_descr[i]->get_cf(), secondary_key_slice, value_slice);
      trx->n_writes++;
    }
  }

  /* Write primary_key -> record */
  if (/*!using_auto_incr_value &&*/ table->next_number_field)
    update_auto_incr_val();

  rocksdb::Slice value_slice;
  convert_record_to_storage_format(&value_slice);

  trx->changes.Put(pk_descr->get_cf(), key_slice, value_slice);
  trx->n_writes++;

  if (!do_locking && trx->n_writes > THDVAR(table->in_use, bulk_load_size))
  {
    /* Bulk insert mode: flush the write batch if it became too big */
    if (trx->flush_batch())
      DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }

  DBUG_RETURN(0);
}


/*
  Open a cursor and position it at the passed record
*/

void ha_rocksdb::setup_index_scan(rocksdb::ColumnFamilyHandle *cf,
                                  rocksdb::Slice *slice)
{
  Rdb_transaction *trx= get_or_create_trx(table->in_use);

  /*
    SQL layer can call rnd_init() multiple times in a row.
    In that case, re-use the iterator, but re-position it at the table start.
  */
  if (!scan_it)
  {
    rocksdb::ReadOptions options;
    if (!lock_rows)
      options.snapshot= trx->snapshot;
    rocksdb::Iterator* rocksdb_it= rdb->NewIterator(options, cf);
    scan_it= new Apply_changes_iter;
    scan_it->init(&trx->changes, rocksdb_it);
  }

  /*
    Seek() will "Position at the first key in the source that at or past target".
    The operation cannot fail.
  */
  scan_it->Seek(*slice);
}


int ha_rocksdb::rnd_init(bool scan)
{
  DBUG_ENTER("ha_rocksdb::rnd_init");

  if (scan)
  {
    uint key_size;
    pk_descr->get_infimum_key(pk_packed_tuple, &key_size);
    rocksdb::Slice table_key((const char*)pk_packed_tuple, key_size);

    setup_index_scan(pk_descr->get_cf(), &table_key);
    skip_scan_it_next_call= true;
  }
  else
  {
    /* We don't need any preparations for rnd_pos() calls. */
  }

  DBUG_RETURN(0);
}


int ha_rocksdb::rnd_next(uchar *buf)
{
  return rnd_next_with_direction(buf, true);
}


/*
  See also secondary_index_read().
*/
int ha_rocksdb::rnd_next_with_direction(uchar *buf, bool move_forward)
{
  int rc;
  DBUG_ENTER("ha_rocksdb::rnd_next");

  if (!scan_it)
  {
    /*
      We can get here when SQL layer has called

        h->index_init(PRIMARY);
        h->index_read_map(full index tuple, HA_READ_KEY_EXACT);

      In this case, we should return EOF.
    */
    table->status= STATUS_NOT_FOUND;
    DBUG_RETURN(HA_ERR_END_OF_FILE);
  }

retry:
  if (skip_scan_it_next_call)
    skip_scan_it_next_call= false;
  else
  {
    if (move_forward)
      scan_it->Next();  /* this call cannot fail */
    else
      scan_it->Prev();  /* this call cannot fail */
  }

  if (scan_it->Valid())
  {
    /* check if we're out of this table */
    rocksdb::Slice key= scan_it->key();
    if (!pk_descr->covers_key(key.data(), key.size()))
    {
      rc= HA_ERR_END_OF_FILE;
      table->status= STATUS_NOT_FOUND;
    }
    else
    {
      if (lock_rows)
      {
        Rdb_transaction *trx= get_or_create_trx(table->in_use);
        /*
          Here is a problem. We've read a row without locking it.
          We could not lock the row before reading it, because we didn't know
          its rowkey.

          Current solution is: lock the row and re-read it.
        */
        Row_lock *lock;
        bool timed_out;
        if (!(lock= trx->get_lock((const uchar*)key.data(), key.size(), &timed_out)))
          DBUG_RETURN(return_lock_failure(timed_out));

        rocksdb::ReadOptions options;
        /* lock_rows==TRUE, so not setting options.snapshot */
        bool found;
        if (!trx->changes.Get(key, &retrieved_record, &found))
        {
          rocksdb::Status s= rdb->Get(options, key, &retrieved_record);
          found= !s.IsNotFound();
        }

        if (!found)
        {
          /* The row is not there already. Try another one */
          row_locks.release_lock(trx->pins, lock);
          goto retry;
        }
        trx->add_lock(lock);

        last_rowkey.copy(key.data(), key.size(), &my_charset_bin);
        rc= convert_record_from_storage_format(buf);
        table->status= 0;
      }
      else
      {
        last_rowkey.copy(key.data(), key.size(), &my_charset_bin);
        rocksdb::Slice value= scan_it->value();
        rc= convert_record_from_storage_format(&value, buf);
        table->status= 0;
      }
    }
  }
  else
    rc= HA_ERR_END_OF_FILE;

  DBUG_RETURN(rc);
}


int ha_rocksdb::rnd_end()
{
  DBUG_ENTER("ha_rocksdb::rnd_end");

  if (scan_it)
  {
    delete scan_it;
    scan_it= NULL;
  }
  DBUG_RETURN(0);
}


int ha_rocksdb::index_end()
{

  if (scan_it)
  {
    delete scan_it;
    scan_it= NULL;
  }

  active_index= MAX_KEY;
  return 0;
}


int ha_rocksdb::delete_all_rows()
{
  DBUG_ENTER("ha_rocksdb::delete_all_rows");
  /*
    We will have to delete records one-by-one anyway. Let the SQL layer do
    it.
  */
  DBUG_RETURN(HA_ERR_WRONG_COMMAND);
}

int ha_rocksdb::truncate()
{
  mysql_mutex_lock(&tbl_def->mutex);
  tbl_def->auto_incr_val= 1;
  mysql_mutex_unlock(&tbl_def->mutex);
  remove_rows(tbl_def);
  return 0;
}

/*
  Delete the row we've last read. Ignore the parameter.
  (Check out InnoDB: row_update_for_mysql() has "UT_NOT_USED(mysql_rec)")
*/
int ha_rocksdb::delete_row(const uchar *buf)
{
  DBUG_ENTER("ha_rocksdb::delete_row");

  rocksdb::Slice key_slice(last_rowkey.ptr(), last_rowkey.length());
  Rdb_transaction *trx= get_or_create_trx(table->in_use);
  trx->prepare_for_write();
  trx->changes.Delete(pk_descr->get_cf(), key_slice);
  DBUG_ASSERT(buf == table->record[0]);

  // Delete the record for every secondary index
  for (uint i= 0; i < table->s->keys; i++)
  {
    if (i != table->s->primary_key)
    {
      int packed_size;
      packed_size= key_descr[i]->pack_record(table, pack_buffer,
                                             buf, sec_key_packed_tuple,
                                             NULL, NULL);
      rocksdb::Slice secondary_key_slice((char*)sec_key_packed_tuple,
                                         packed_size);
      trx->changes.Delete(key_descr[i]->get_cf(), secondary_key_slice);
    }
  }

  DBUG_RETURN(0);
}


int ha_rocksdb::info(uint flag)
{
  DBUG_ENTER("ha_rocksdb::info");

  if (!table)
    return 1;

  if (flag & HA_STATUS_VARIABLE)
  {
    stats.records= 1000;
  }
  if (flag & HA_STATUS_CONST)
  {
    DBUG_ASSERT(table->s->primary_key != MAX_KEY);

    ref_length= pk_descr->max_storage_fmt_length();
  }

  if (flag & HA_STATUS_ERRKEY)
  {
    /*
      Currently we support only primary keys so we know which key had a
      uniqueness violation.
    */
    errkey= 0;
    dup_ref= pk_tuple; //TODO: this should store packed PK.
  }

  if (flag & HA_STATUS_AUTO)
  {
    stats.auto_increment_value= tbl_def->auto_incr_val;
  }

  DBUG_RETURN(0);
}


void ha_rocksdb::position(const uchar *record)
{
  DBUG_ENTER("ha_rocksdb::position");

  memcpy(ref, last_rowkey.ptr(), ref_length);
  DBUG_VOID_RETURN;
}


int ha_rocksdb::rnd_pos(uchar *buf, uchar *pos)
{
  int rc;
  DBUG_ENTER("ha_rocksdb::rnd_pos");

  rc= get_row_by_rowid(buf, (const char*)pos, ref_length);

  DBUG_RETURN(rc);
}


int ha_rocksdb::return_lock_failure(bool timed_out)
{
  THD *thd;
  if ((thd= table->in_use))
  {
    thd_mark_transaction_to_rollback(thd, false);
  }
  return timed_out? HA_ERR_LOCK_WAIT_TIMEOUT : HA_ERR_ROCKSDB_TOO_MANY_LOCKS;
}


int ha_rocksdb::update_row(const uchar *old_data, uchar *new_data)
{
  DBUG_ENTER("ha_rocksdb::update_row");
  DBUG_ASSERT(lock_rows);
  /*
    old_data points to record we're updating. It is the same as the record
    we've just read (for multi-table UPDATE, too, because SQL layer will make
    an rnd_pos() call to re-read the record before calling update_row())
  */
  DBUG_ASSERT(new_data == table->record[0]);

  //psergey-todo: how come we don't get locks for the 'old_data'?  because
  // we've already taken them when reading??

  /* Get a rowkey fo the new record */
  uint packed_size= pk_descr->pack_record(table, pack_buffer, new_data,
                                          pk_packed_tuple,
                                          NULL, NULL);

  rocksdb::Slice key_slice((const char*)pk_packed_tuple, packed_size);
  Rdb_transaction *trx= get_or_create_trx(table->in_use);

  /* Compare rowkey for the new record with rowkey for the old one */
  rocksdb::Slice old_key_slice(last_rowkey.ptr(), last_rowkey.length());
  if (rocksdb_pk_comparator.Compare(key_slice, old_key_slice))
  {
    /* Ok, they are different. We're doing DELETE+PUT */

    /*
      We need to make sure that PUT will not overwrite some existing record.
      Get a lock. on what we're going to write.
    */
    Row_lock *lock;
    bool timed_out;
    if (!(lock= trx->get_lock(pk_packed_tuple, packed_size, &timed_out)))
      DBUG_RETURN(return_lock_failure(timed_out));

    bool found;
    if (!trx->changes.Get(key_slice, &retrieved_record, &found))
    {
      rocksdb::ReadOptions options;
      /* Not setting options.snapshot, we need to check the real contents */
      rocksdb::Status s= rdb->Get(options, key_slice, &retrieved_record);
      found= !s.IsNotFound();
    }

    if (found)
    {
      row_locks.release_lock(trx->pins, lock);
      DBUG_RETURN(HA_ERR_FOUND_DUPP_KEY);
    }

    trx->prepare_for_write();
    trx->add_lock(lock);
    trx->changes.Delete(pk_descr->get_cf(), old_key_slice);
  }

  trx->prepare_for_write();

  /* Update every index */
  for (uint i= 0; i < table->s->keys; i++)
  {
    if (i != table->s->primary_key)
    {
      int packed_size;
      int tail_size;
      // TODO: compare old/new fields, and update only when actually necessary.

      // First, a Delete()
      {
        packed_size= key_descr[i]->pack_record(table, pack_buffer, old_data,
                                               sec_key_packed_tuple,
                                               NULL, NULL);
        rocksdb::Slice secondary_key_slice((char*)sec_key_packed_tuple,
                                           packed_size);
        trx->changes.Delete(key_descr[i]->get_cf(), secondary_key_slice);
      }

      // Then, Put().
      {
        packed_size= key_descr[i]->pack_record(table, pack_buffer, new_data,
                                               sec_key_packed_tuple,
                                               sec_key_tails, &tail_size);
        rocksdb::Slice secondary_key_slice((char*)sec_key_packed_tuple,
                                           packed_size);
        rocksdb::Slice value_slice((const char*)sec_key_tails, tail_size);
        trx->changes.Put(key_descr[i]->get_cf(), secondary_key_slice,
                         value_slice);
      }
    }
  }

  if (table->next_number_field)
    update_auto_incr_val();
  /* Write the table record */
  rocksdb::Slice value_slice;
  convert_record_to_storage_format(&value_slice);
  trx->changes.Put(pk_descr->get_cf(), key_slice, value_slice);

  DBUG_RETURN(0);
}


/* The following function was copied from ha_blackhole::store_lock: */
THR_LOCK_DATA **ha_rocksdb::store_lock(THD *thd,
                                         THR_LOCK_DATA **to,
                                         enum thr_lock_type lock_type)
{
  DBUG_ENTER("ha_rocksdb::store_lock");
  if (lock_type != TL_IGNORE && lock.type == TL_UNLOCK)
  {
    /*
      Here is where we get into the guts of a row level lock.
      If TL_UNLOCK is set
      If we are not doing a LOCK TABLE or DISCARD/IMPORT
      TABLESPACE, then allow multiple writers
    */

    if ((lock_type >= TL_WRITE_CONCURRENT_INSERT &&
         lock_type <= TL_WRITE) && !thd_in_lock_tables(thd)
        && !thd_tablespace_op(thd))
      lock_type = TL_WRITE_ALLOW_WRITE;

    /*
      In queries of type INSERT INTO t1 SELECT ... FROM t2 ...
      MySQL would use the lock TL_READ_NO_INSERT on t2, and that
      would conflict with TL_WRITE_ALLOW_WRITE, blocking all inserts
      to t2. Convert the lock to a normal read lock to allow
      concurrent inserts to t2.
    */

    if (lock_type == TL_READ_NO_INSERT && !thd_in_lock_tables(thd))
      lock_type = TL_READ;

    lock.type= lock_type;
  }
  *to++= &lock;
  DBUG_RETURN(to);
}


int ha_rocksdb::external_lock(THD *thd, int lock_type)
{
  int res= 0;
  DBUG_ENTER("ha_rocksdb::external_lock");
  if (lock_type == F_UNLCK)
  {
    if (!thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN))
    {
      /*
        Do like InnoDB: when we get here, it's time to commit a
        single-statement transaction.

        If the statement involved multiple tables, this code will be executed
        for each of them, but that's ok because non-first trx->commit() calls
        will be no-ops.
      */
      Rdb_transaction* trx= get_or_create_trx(thd);
      res= trx->commit();
      delete scan_it;
      scan_it= NULL;
    }
    lock_rows= FALSE;
  }
  else
  {
    /*
      It's nice to do the following on start of every statement. The problem
      is, handler->start_stmt() is not called for INSERTs.
      So, we put this code here.
    */
    Rdb_transaction* trx= get_or_create_trx(thd);
    trx->set_params(THDVAR(table->in_use, lock_wait_timeout),
                    THDVAR(table->in_use, max_row_locks));

    if (lock_type == F_WRLCK)
    {
      /*
        SQL layer signals us to take a write lock. It does so when starting DML
        statement. We should put locks on the rows we're reading.
      */
      lock_rows= TRUE;
    }

    trans_register_ha(thd, FALSE, rocksdb_hton);
    if (thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN))
    {
      trx->start_stmt();
      trans_register_ha(thd, TRUE, rocksdb_hton);
    }
  }

  DBUG_RETURN(res);
}


int ha_rocksdb::start_stmt(THD *thd, thr_lock_type lock_type)
{
  if (thd_test_options(thd, OPTION_NOT_AUTOCOMMIT | OPTION_BEGIN))
  {
    Rdb_transaction* trx= get_or_create_trx(thd);
    trx->set_params(THDVAR(table->in_use, lock_wait_timeout),
                    THDVAR(table->in_use, max_row_locks));
    trx->start_stmt();
  }
  return 0;
}

/*
  Note: the following function is called when the table is not open. That is,
  this->table==NULL, pk_key_descr==NULL, etc.

  tablename points to line in form "./dbname/tablename".
*/

int ha_rocksdb::delete_table(const char *tablename)
{
  StringBuffer<256> strbuf;
  DBUG_ENTER("ha_rocksdb::delete_table");

  if (rocksdb_normalize_tablename(tablename, &strbuf))
  {
    DBUG_ASSERT(0); // We were not passed table name?
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);
  }


  /* Find the table in the hash */
  RDBSE_TABLE_DEF *tbl;
  if (!(tbl= ddl_manager.find((uchar*)strbuf.c_ptr(), strbuf.length())))
    DBUG_RETURN(HA_ERR_INTERNAL_ERROR);

  remove_rows(tbl);

  /*
    Remove the table entry in data dictionary (this will also remove it from
    the persistent data dictionary).
  */
  ddl_manager.remove(tbl, rdb, true);

  DBUG_RETURN(0);
}


void ha_rocksdb::remove_rows(RDBSE_TABLE_DEF *tbl)
{
  rocksdb::ReadOptions opts;
  opts.total_order_seek= true;
  rocksdb::Iterator* it= rdb->NewIterator(opts);

  char key_buf[MAX_KEY_LENGTH];
  uint key_len;
  /*
    Remove all records in each index.
    (This is is not crash-safe, but it doesn't matter, because bulk row
    deletion will be handled on rocksdb side)
  */
  for (uint i= 0; i < tbl->n_keys ; i++)
  {
    tbl->key_descr[i]->get_infimum_key((uchar*)key_buf, &key_len);

    rocksdb::Slice table_key(key_buf, key_len);
    it->Seek(table_key);
    while (it->Valid())
    {
      rocksdb::Slice key= it->key();
      if (!tbl->key_descr[i]->covers_key(key.data(), key.size()))
        break;
      rdb->Delete(rocksdb::WriteOptions(), key);
      it->Next();
    }
  }
  delete it;
}


int ha_rocksdb::rename_table(const char *from, const char *to)
{
  StringBuffer<256> from_str;
  StringBuffer<256> to_str;

  if (rocksdb_normalize_tablename(from, &from_str))
    return HA_ERR_INTERNAL_ERROR;

  if (rocksdb_normalize_tablename(to, &to_str))
    return HA_ERR_INTERNAL_ERROR;

  /* Check if any index has a per-index column family */
  {
    /*
      We can't check this directly, because
      1. the object that referred to the old table has been close()d.
      2. this ha_rocksdb object has no connection to any table at all, it has
         been just created with (TABLE*)NULL.

      So, we have to find the old table in the ddl_manager (it's there because
      it has been opened by this statement), and check is_auto_cf there.
    */
    RDBSE_TABLE_DEF *tdef;
    if (!(tdef= ddl_manager.find((uchar*)from_str.c_ptr(), from_str.length())))
      return HA_ERR_INTERNAL_ERROR;

    for (uint i= 0; i < tdef->n_keys; i++)
    {
      if (tdef->key_descr[i]->is_auto_cf)
      {
        my_error(ER_NOT_SUPPORTED_YET, MYF(0),
                 "ALTER TABLE on table with per-index CF");
        return HA_ERR_INTERNAL_ERROR;
      }
    }
  }

  if (ddl_manager.rename((uchar*)from_str.ptr(), from_str.length(),
                         (uchar*)to_str.ptr(), to_str.length(), rdb))
    return HA_ERR_INTERNAL_ERROR;

  return 0;
}

/**
  check_if_incompatible_data() called if ALTER TABLE can't detect otherwise
  if new and old definition are compatible

  @details If there are no other explicit signs like changed number of
  fields this function will be called by compare_tables()
  (sql/sql_tables.cc) to decide should we rewrite whole table or only .frm
  file.

*/

bool ha_rocksdb::check_if_incompatible_data(HA_CREATE_INFO *info,
                                            uint table_changes)
{
  DBUG_ENTER("ha_rocksdb::check_if_incompatible_data");
  // this function is needed only for online alter-table
  DBUG_RETURN(COMPATIBLE_DATA_NO);
}


int ha_rocksdb::extra(enum ha_extra_function operation)
{
  DBUG_ENTER("ha_rocksdb::extra");

  switch (operation) {
  case HA_EXTRA_KEYREAD:
    keyread_only= TRUE;
    break;
  case HA_EXTRA_NO_KEYREAD:
    keyread_only= FALSE;
    break;
  case HA_EXTRA_FLUSH:
    /*
      If the table has blobs, then they are part of retrieved_record. This call
      invalidates them.
    */
    retrieved_record.clear();
    break;
  default:
    break;
  }
  DBUG_RETURN(0);
}


ha_rows ha_rocksdb::records_in_range(uint inx, key_range *min_key,
                                     key_range *max_key)
{
  DBUG_ENTER("ha_rocksdb::records_in_range");
  DBUG_RETURN(10);                         // low number to force index usage
}


void ha_rocksdb::update_create_info(HA_CREATE_INFO *create_info)
{
  if (!(create_info->used_fields & HA_CREATE_USED_AUTO))
  {
    create_info->auto_increment_value= tbl_def->auto_incr_val;
  }
}


void ha_rocksdb::get_auto_increment(ulonglong offset, ulonglong increment,
                                    ulonglong nb_desired_values,
                                    ulonglong *first_value,
                                    ulonglong *nb_reserved_values)
{
  if (!nb_desired_values)
    nb_desired_values= 1;

  mysql_mutex_lock(&tbl_def->mutex);
  *first_value= tbl_def->auto_incr_val;
  *nb_reserved_values= nb_desired_values;
  tbl_def->auto_incr_val += nb_desired_values;
  mysql_mutex_unlock(&tbl_def->mutex);
}


typedef struct {
  long bytes_written;
  long bytes_read;
  long number_keys_written;
  long number_keys_read;
  long number_keys_updated;
} ROCKSDBSE_STATUS_VARS;

static ROCKSDBSE_STATUS_VARS rocksdb_counters;

static SHOW_VAR rocksdb_status_variables[]= {
  {"bytes_written",        (char*) &rocksdb_counters.bytes_written, SHOW_LONG},
  {"bytes_read",           (char*) &rocksdb_counters.bytes_read, SHOW_LONG},
  {"number_keys_written",  (char*) &rocksdb_counters.number_keys_written, SHOW_LONG},
  {"number_keys_read",     (char*) &rocksdb_counters.number_keys_read, SHOW_LONG},
  {"number_keys_updated",  (char*) &rocksdb_counters.number_keys_updated, SHOW_LONG},
  {NullS, NullS, SHOW_LONG}
};


void rocksdb_export_status()
{
  ROCKSDBSE_STATUS_VARS &r= rocksdb_counters;
  // TODO: should we protect the below with taking/releasing a mutex, like
  // innodb does?
  r.bytes_written=       rocksdb_stats->getTickerCount(rocksdb::BYTES_WRITTEN);
  r.bytes_read=          rocksdb_stats->getTickerCount(rocksdb::BYTES_READ);
  r.number_keys_written= rocksdb_stats->getTickerCount(rocksdb::NUMBER_KEYS_WRITTEN);
  r.number_keys_read=    rocksdb_stats->getTickerCount(rocksdb::NUMBER_KEYS_READ);
  r.number_keys_updated= rocksdb_stats->getTickerCount(rocksdb::NUMBER_KEYS_UPDATED);
}


static int show_rocksdb_status_vars(THD* thd, SHOW_VAR *var, char* buff)
{
  rocksdb_export_status();
  var->type = SHOW_ARRAY;
  var->value = (char*) &rocksdb_status_variables;
  return(0);
}


static SHOW_VAR rocksdb_status_vars[]=
{
  {"rocksdb", (char*) &show_rocksdb_status_vars, SHOW_FUNC},
  {NullS, NullS, SHOW_LONG}
};


struct st_mysql_storage_engine rocksdb_storage_engine=
{ MYSQL_HANDLERTON_INTERFACE_VERSION };

mysql_declare_plugin(rocksdb_se)
{
  MYSQL_STORAGE_ENGINE_PLUGIN,
  &rocksdb_storage_engine,
  "ROCKSDB",
  "Monty Program Ab",
  "RocksDB storage engine",
  PLUGIN_LICENSE_GPL,
  rocksdb_init_func,                            /* Plugin Init */
  rocksdb_done_func,                            /* Plugin Deinit */
  0x0001,                                       /* version number (0.1) */
  rocksdb_status_vars,                          /* status variables */
  rocksdb_system_variables,                     /* system variables */
  NULL,                                         /* config options */
  0,                                            /* flags */
}
mysql_declare_plugin_end;


/*
  Compute a hash number for a PK value in RowKeyFormat.

  @note
    RowKeyFormat is comparable with memcmp. This means, any hash function will
    work correctly. We use my_charset_bin's hash function.

    Note from Bar: could also use crc32 function.
*/

ulong Primary_key_comparator::get_hashnr(const char *key, size_t key_len)
{
  ulong nr=1, nr2=4;
  my_charset_bin.coll->hash_sort(&my_charset_bin, (const uchar*)key, key_len,
                                 &nr, &nr2);
  return((ulong) nr);
}
