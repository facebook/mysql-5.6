/*
  Copyright (c) 2006, 2013, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; version 2 of the License.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

#include "ha_ndbcluster_glue.h"

#ifdef WITH_NDBCLUSTER_STORAGE_ENGINE
#include "ha_ndbcluster.h"
#include "ha_ndbcluster_connection.h"
#include "ndb_local_connection.h"
#include "ndb_thd.h"
#include "ndb_table_guard.h"
#include "ndb_global_schema_lock.h"
#include "ndb_global_schema_lock_guard.h"

#include "rpl_injector.h"
#include "rpl_filter.h"
#if MYSQL_VERSION_ID > 50600
#include "rpl_slave.h"
#else
#include "slave.h"
#include "log_event.h"
#endif
#include "global_threads.h"
#include "ha_ndbcluster_binlog.h"
#include <ndbapi/NdbDictionary.hpp>
#include <ndbapi/ndb_cluster_connection.hpp>

extern my_bool opt_ndb_log_orig;
extern my_bool opt_ndb_log_bin;
extern my_bool opt_ndb_log_update_as_write;
extern my_bool opt_ndb_log_updated_only;
extern my_bool opt_ndb_log_binlog_index;
extern my_bool opt_ndb_log_apply_status;
extern ulong opt_ndb_extra_logging;
extern st_ndb_slave_state g_ndb_slave_state;

bool ndb_log_empty_epochs(void);

/*
  defines for cluster replication table names
*/
#include "ha_ndbcluster_tables.h"

#include "ndb_dist_priv_util.h"

/*
  Timeout for syncing schema events between
  mysql servers, and between mysql server and the binlog
*/
static const int DEFAULT_SYNC_TIMEOUT= 120;

/*
  Flag showing if the ndb injector thread is running, if so == 1
  -1 if it was started but later stopped for some reason
   0 if never started
*/
static int ndb_binlog_thread_running= 0;
/*
  Flag showing if the ndb binlog should be created, if so == TRUE
  FALSE if not
*/
my_bool ndb_binlog_running= FALSE;
static my_bool ndb_binlog_tables_inited= FALSE;
static my_bool ndb_binlog_is_ready= FALSE;

bool
ndb_binlog_is_read_only(void)
{
  if(!ndb_binlog_tables_inited)
  {
    /* the ndb_* system tables not setup yet */
    return true;
  }

  if (ndb_binlog_running && !ndb_binlog_is_ready)
  {
    /*
      The binlog thread is supposed to write to binlog
      but not ready (still initializing or has lost connection)
    */
    return true;
  }
  return false;
}

/*
  Global reference to the ndb injector thread THD oject

  Has one sole purpose, for setting the in_use table member variable
  in get_share(...)
*/
extern THD * injector_thd; // Declared in ha_ndbcluster.cc

/*
  Global reference to ndb injector thd object.

  Used mainly by the binlog index thread, but exposed to the client sql
  thread for one reason; to setup the events operations for a table
  to enable ndb injector thread receiving events.

  Must therefore always be used with a surrounding
  pthread_mutex_lock(&injector_mutex), when doing create/dropEventOperation
*/
static Ndb *injector_ndb= 0;
static Ndb *schema_ndb= 0;

static int ndbcluster_binlog_inited= 0;
/*
  Flag "ndbcluster_binlog_terminating" set when shutting down mysqld.
  Server main loop should call handlerton function:

  ndbcluster_hton->binlog_func ==
  ndbcluster_binlog_func(...,BFN_BINLOG_END,...) ==
  ndbcluster_binlog_end

  at shutdown, which sets the flag. And then server needs to wait for it
  to complete.  Otherwise binlog will not be complete.

  ndbcluster_hton->panic == ndbcluster_end() will not return until
  ndb binlog is completed
*/
static int ndbcluster_binlog_terminating= 0;

/*
  Mutex and condition used for interacting between client sql thread
  and injector thread
*/
pthread_t ndb_binlog_thread;
pthread_mutex_t injector_mutex;
pthread_cond_t  injector_cond;

/* NDB Injector thread (used for binlog creation) */
static ulonglong ndb_latest_applied_binlog_epoch= 0;
static ulonglong ndb_latest_handled_binlog_epoch= 0;
static ulonglong ndb_latest_received_binlog_epoch= 0;

NDB_SHARE *ndb_apply_status_share= 0;
NDB_SHARE *ndb_schema_share= 0;
pthread_mutex_t ndb_schema_share_mutex;

extern my_bool opt_log_slave_updates;
static my_bool g_ndb_log_slave_updates;

/* Schema object distribution handling */
HASH ndb_schema_objects;
typedef struct st_ndb_schema_object {
  pthread_mutex_t mutex;
  char *key;
  uint key_length;
  uint use_count;
  MY_BITMAP slock_bitmap;
  uint32 slock[256/32]; // 256 bits for lock status of table
  uint32 table_id;
  uint32 table_version;
} NDB_SCHEMA_OBJECT;
static NDB_SCHEMA_OBJECT *ndb_get_schema_object(const char *key,
                                                my_bool create_if_not_exists,
                                                my_bool have_lock);
static void ndb_free_schema_object(NDB_SCHEMA_OBJECT **ndb_schema_object,
                                   bool have_lock);

#ifndef DBUG_OFF
/* purecov: begin deadcode */
static void print_records(TABLE *table, const uchar *record)
{
  for (uint j= 0; j < table->s->fields; j++)
  {
    char buf[40];
    int pos= 0;
    Field *field= table->field[j];
    const uchar* field_ptr= field->ptr - table->record[0] + record;
    int pack_len= field->pack_length();
    int n= pack_len < 10 ? pack_len : 10;

    for (int i= 0; i < n && pos < 20; i++)
    {
      pos+= sprintf(&buf[pos]," %x", (int) (uchar) field_ptr[i]);
    }
    buf[pos]= 0;
    DBUG_PRINT("info",("[%u]field_ptr[0->%d]: %s", j, n, buf));
  }
}
/* purecov: end */
#else
#define print_records(a,b)
#endif


#ifndef DBUG_OFF
static void dbug_print_table(const char *info, TABLE *table)
{
  if (table == 0)
  {
    DBUG_PRINT("info",("%s: (null)", info));
    return;
  }
  DBUG_PRINT("info",
             ("%s: %s.%s s->fields: %d  "
              "reclength: %lu  rec_buff_length: %u  record[0]: 0x%lx  "
              "record[1]: 0x%lx",
              info,
              table->s->db.str,
              table->s->table_name.str,
              table->s->fields,
              table->s->reclength,
              table->s->rec_buff_length,
              (long) table->record[0],
              (long) table->record[1]));

  for (unsigned int i= 0; i < table->s->fields; i++) 
  {
    Field *f= table->field[i];
    DBUG_PRINT("info",
               ("[%d] \"%s\"(0x%lx:%s%s%s%s%s%s) type: %d  pack_length: %d  "
                "ptr: 0x%lx[+%d]  null_bit: %u  null_ptr: 0x%lx[+%d]",
                i,
                f->field_name,
                (long) f->flags,
                (f->flags & PRI_KEY_FLAG)  ? "pri"       : "attr",
                (f->flags & NOT_NULL_FLAG) ? ""          : ",nullable",
                (f->flags & UNSIGNED_FLAG) ? ",unsigned" : ",signed",
                (f->flags & ZEROFILL_FLAG) ? ",zerofill" : "",
                (f->flags & BLOB_FLAG)     ? ",blob"     : "",
                (f->flags & BINARY_FLAG)   ? ",binary"   : "",
                f->real_type(),
                f->pack_length(),
                (long) f->ptr, (int) (f->ptr - table->record[0]),
                f->null_bit,
                (long) f->null_ptr,
                (int) ((uchar*) f->null_ptr - table->record[0])));
    if (f->type() == MYSQL_TYPE_BIT)
    {
      Field_bit *g= (Field_bit*) f;
      DBUG_PRINT("MYSQL_TYPE_BIT",("field_length: %d  bit_ptr: 0x%lx[+%d] "
                                   "bit_ofs: %d  bit_len: %u",
                                   g->field_length, (long) g->bit_ptr,
                                   (int) ((uchar*) g->bit_ptr -
                                          table->record[0]),
                                   g->bit_ofs, g->bit_len));
    }
  }
}
#else
#define dbug_print_table(a,b)
#endif


static inline void
print_warning_list(const char* prefix, THD* thd)
{
  Diagnostics_area::Sql_condition_iterator
   it(thd->get_stmt_da()->sql_conditions());

  const Sql_condition *err;
  while ((err= it++))
  {
    sql_print_warning("%s: (%d)%s",
                      prefix,
                      err->get_sql_errno(),
                      err->get_message_text());
  }
}


static void run_query(THD *thd, char *buf, char *end,
                      const int *no_print_error)
{
  /*
    NOTE! Don't use this function for new implementation, backward
    compat. only
  */

  Ndb_local_connection mysqld(thd);

  /*
    Run the query, suppress some errors from being printed
    to log and ignore any error returned
  */
  (void)mysqld.raw_run_query(buf, (end - buf),
                             no_print_error);
}

static void
ndbcluster_binlog_close_table(THD *thd, NDB_SHARE *share)
{
  DBUG_ENTER("ndbcluster_binlog_close_table");
  Ndb_event_data *event_data= share->event_data;
  if (event_data)
  {
    delete event_data;
    share->event_data= 0;
  }
  DBUG_VOID_RETURN;
}


/*
  Open a shadow table for the table given in share.
  - The shadow table is (mainly) used when an event is
    recieved from the data nodes which need to be written
    to the binlog injector.
*/

static int
ndb_binlog_open_shadow_table(THD *thd, NDB_SHARE *share)
{
  int error;
  DBUG_ASSERT(share->event_data == 0);
  Ndb_event_data *event_data= share->event_data= new Ndb_event_data(share);
  DBUG_ENTER("ndb_binlog_open_shadow_table");

  MEM_ROOT **root_ptr=
    my_pthread_getspecific_ptr(MEM_ROOT**, THR_MALLOC);
  MEM_ROOT *old_root= *root_ptr;
  init_sql_alloc(&event_data->mem_root, 1024, 0);
  *root_ptr= &event_data->mem_root;

  TABLE_SHARE *shadow_table_share=
    (TABLE_SHARE*)alloc_root(&event_data->mem_root, sizeof(TABLE_SHARE));
  TABLE *shadow_table=
    (TABLE*)alloc_root(&event_data->mem_root, sizeof(TABLE));

  init_tmp_table_share(thd, shadow_table_share,
                       share->db, 0,
                       share->table_name,
                       share->key);
  if ((error= open_table_def(thd, shadow_table_share, 0)) ||
      (error= open_table_from_share(thd, shadow_table_share, "", 0,
                                    (uint) (OPEN_FRM_FILE_ONLY | DELAYED_OPEN | READ_ALL),
                                    0, shadow_table,
#ifdef NDB_WITHOUT_ONLINE_ALTER
                                    false
#else
                                    OTM_OPEN
#endif
                                    )))
  {
    DBUG_PRINT("error", ("failed to open shadow table, error: %d my_errno: %d",
                         error, my_errno));
    free_table_share(shadow_table_share);
    delete event_data;
    share->event_data= 0;
    *root_ptr= old_root;
    DBUG_RETURN(error);
  }
  event_data->shadow_table= shadow_table;

  mysql_mutex_lock(&LOCK_open);
  assign_new_table_id(shadow_table_share);
  mysql_mutex_unlock(&LOCK_open);

  shadow_table->in_use= injector_thd;
  
  shadow_table->s->db.str= share->db;
  shadow_table->s->db.length= strlen(share->db);
  shadow_table->s->table_name.str= share->table_name;
  shadow_table->s->table_name.length= strlen(share->table_name);
  /* We can't use 'use_all_columns()' as the file object is not setup yet */
  shadow_table->column_bitmaps_set_no_signal(&shadow_table->s->all_set,
                                             &shadow_table->s->all_set);
#ifndef DBUG_OFF
  dbug_print_table("table", shadow_table);
#endif
  *root_ptr= old_root;
  DBUG_RETURN(0);
}


/*
  Initialize the binlog part of the NDB_SHARE
*/
int ndbcluster_binlog_init_share(THD *thd, NDB_SHARE *share, TABLE *_table)
{
  MEM_ROOT *mem_root= &share->mem_root;
  int do_event_op= ndb_binlog_running;
  int error= 0;
  DBUG_ENTER("ndbcluster_binlog_init_share");

  share->connect_count= g_ndb_cluster_connection->get_connect_count();
#ifdef HAVE_NDB_BINLOG
  share->m_cfn_share= NULL;
#endif

  share->op= 0;
  share->new_op= 0;
  share->event_data= 0;

  if (!ndb_schema_share &&
      strcmp(share->db, NDB_REP_DB) == 0 &&
      strcmp(share->table_name, NDB_SCHEMA_TABLE) == 0)
    do_event_op= 1;
  else if (!ndb_apply_status_share &&
           strcmp(share->db, NDB_REP_DB) == 0 &&
           strcmp(share->table_name, NDB_APPLY_TABLE) == 0)
    do_event_op= 1;

  if (Ndb_dist_priv_util::is_distributed_priv_table(share->db,
                                                    share->table_name))
  {
    do_event_op= 0;
  }

  {
    int i, no_nodes= g_ndb_cluster_connection->no_db_nodes();
    share->subscriber_bitmap= (MY_BITMAP*)
      alloc_root(mem_root, no_nodes * sizeof(MY_BITMAP));
    for (i= 0; i < no_nodes; i++)
    {
      bitmap_init(&share->subscriber_bitmap[i],
                  (Uint32*)alloc_root(mem_root, max_ndb_nodes/8),
                  max_ndb_nodes, FALSE);
      bitmap_clear_all(&share->subscriber_bitmap[i]);
    }
  }

  if (!do_event_op)
  {
    if (_table)
    {
      if (_table->s->primary_key == MAX_KEY)
        share->flags|= NSF_HIDDEN_PK;
      if (_table->s->blob_fields != 0)
        share->flags|= NSF_BLOB_FLAG;
    }
    else
    {
      share->flags|= NSF_NO_BINLOG;
    }
    DBUG_RETURN(error);
  }
  while (1) 
  {
    if ((error= ndb_binlog_open_shadow_table(thd, share)))
      break;
    if (share->event_data->shadow_table->s->primary_key == MAX_KEY)
      share->flags|= NSF_HIDDEN_PK;
    if (share->event_data->shadow_table->s->blob_fields != 0)
      share->flags|= NSF_BLOB_FLAG;
    break;
  }
  DBUG_RETURN(error);
}

/*****************************************************************
  functions called from master sql client threads
****************************************************************/

/*
  called in mysql_show_binlog_events and reset_logs to make sure we wait for
  all events originating from this mysql server to arrive in the binlog

  Wait for the last epoch in which the last transaction is a part of.

  Wait a maximum of 30 seconds.
*/
static void ndbcluster_binlog_wait(THD *thd)
{
  if (ndb_binlog_running)
  {
    DBUG_ENTER("ndbcluster_binlog_wait");
    ulonglong wait_epoch= ndb_get_latest_trans_gci();
    /*
      cluster not connected or no transactions done
      so nothing to wait for
    */
    if (!wait_epoch)
      DBUG_VOID_RETURN;
    const char *save_info= thd ? thd->proc_info : 0;
    int count= 30;
    if (thd)
      thd->proc_info= "Waiting for ndbcluster binlog update to "
	"reach current position";
    pthread_mutex_lock(&injector_mutex);
    while (!(thd && thd->killed) && count && ndb_binlog_running &&
           (ndb_latest_handled_binlog_epoch == 0 ||
            ndb_latest_handled_binlog_epoch < wait_epoch))
    {
      count--;
      struct timespec abstime;
      set_timespec(abstime, 1);
      pthread_cond_timedwait(&injector_cond, &injector_mutex, &abstime);
    }
    pthread_mutex_unlock(&injector_mutex);
    if (thd)
      thd->proc_info= save_info;
    DBUG_VOID_RETURN;
  }
}

/*
 Called from MYSQL_BIN_LOG::reset_logs in log.cc when binlog is emptied
*/
static int ndbcluster_reset_logs(THD *thd)
{
  if (!ndb_binlog_running)
    return 0;

  /* only reset master should reset logs */
  if (!((thd->lex->sql_command == SQLCOM_RESET) &&
        (thd->lex->type & REFRESH_MASTER)))
    return 0;

  DBUG_ENTER("ndbcluster_reset_logs");

  /*
    Wait for all events originating from this mysql server has
    reached the binlog before continuing to reset
  */
  ndbcluster_binlog_wait(thd);

  /*
    Truncate mysql.ndb_binlog_index table, if table does not
    exist ignore the error as it is a "consistent" behavior
  */
  Ndb_local_connection mysqld(thd);
  const bool ignore_no_such_table = true;
  if(mysqld.truncate_table(STRING_WITH_LEN("mysql"),
                           STRING_WITH_LEN("ndb_binlog_index"),
                           ignore_no_such_table))
  {
    // Failed to truncate table
    DBUG_RETURN(1);
  }
  DBUG_RETURN(0);
}

/*
  Setup THD object
  'Inspired' from ha_ndbcluster.cc : ndb_util_thread_func
*/
static THD *
setup_thd(char * stackptr)
{
  DBUG_ENTER("setup_thd");
  THD * thd= new THD; /* note that contructor of THD uses DBUG_ */
  if (thd == 0)
  {
    DBUG_RETURN(0);
  }
  THD_CHECK_SENTRY(thd);

  thd->thread_id= 0;
  thd->thread_stack= stackptr; /* remember where our stack is */
  if (thd->store_globals())
  {
    thd->cleanup();
    delete thd;
    DBUG_RETURN(0);
  }

  lex_start(thd);

  thd->init_for_queries();
  thd_set_command(thd, COM_DAEMON);
  thd->system_thread= SYSTEM_THREAD_NDBCLUSTER_BINLOG;
#ifndef NDB_THD_HAS_NO_VERSION
  thd->version= refresh_version;
#endif
  thd->client_capabilities= 0;
  thd->lex->start_transaction_opt= 0;
  thd->security_ctx->skip_grants();

  CHARSET_INFO *charset_connection= get_charset_by_csname("utf8",
                                                          MY_CS_PRIMARY,
                                                          MYF(MY_WME));
  thd->variables.character_set_client= charset_connection;
  thd->variables.character_set_results= charset_connection;
  thd->variables.collation_connection= charset_connection;
  thd->update_charset();
  DBUG_RETURN(thd);
}

/*
  Called from MYSQL_BIN_LOG::purge_logs in log.cc when the binlog "file"
  is removed
*/

static int
ndbcluster_binlog_index_purge_file(THD *thd, const char *file)
{
  int error = 0;
  THD * save_thd= thd;
  DBUG_ENTER("ndbcluster_binlog_index_purge_file");
  DBUG_PRINT("enter", ("file: %s", file));

  if (!ndb_binlog_running || (thd && thd->slave_thread))
    DBUG_RETURN(0);

  /**
   * This function really really needs a THD object,
   *   new/delete one if not available...yuck!
   */
  if (thd == 0)
  {
    if ((thd = setup_thd((char*)&save_thd)) == 0)
    {
      /**
       * TODO return proper error code here,
       * BUT! return code is not (currently) checked in
       *      log.cc : purge_index_entry() so we settle for warning printout
       */
      sql_print_warning("NDB: Unable to purge "
                        NDB_REP_DB "." NDB_REP_TABLE
                        " File=%s (failed to setup thd)", file);
      DBUG_RETURN(0);
    }
  }

  /*
    delete rows from mysql.ndb_binlog_index table for the given
    filename, if table does not exist ignore the error as it
    is a "consistent" behavior
  */
  Ndb_local_connection mysqld(thd);
  const bool ignore_no_such_table = true;
  if(mysqld.delete_rows(STRING_WITH_LEN("mysql"),
                        STRING_WITH_LEN("ndb_binlog_index"),
                        ignore_no_such_table,
                        "File='", file, "'", NULL))
  {
    // Failed to delete rows from table
    error = 1;
  }

  if (save_thd == 0)
  {
    thd->cleanup();
    delete thd;
  }

  DBUG_RETURN(error);
}


#ifndef NDB_WITHOUT_DIST_PRIV
// Determine if privilege tables are distributed, ie. stored in NDB
static bool
priv_tables_are_in_ndb(THD *thd)
{
  bool distributed= false;
  Ndb_dist_priv_util dist_priv;
  DBUG_ENTER("ndbcluster_distributed_privileges");

  Ndb *ndb= check_ndb_in_thd(thd);
  if (!ndb)
    DBUG_RETURN(false); // MAGNUS, error message?

  if (ndb->setDatabaseName(dist_priv.database()) != 0)
    DBUG_RETURN(false);

  const char* table_name;
  while((table_name= dist_priv.iter_next_table()))
  {
    DBUG_PRINT("info", ("table_name: %s", table_name));
    Ndb_table_guard ndbtab_g(ndb->getDictionary(), table_name);
    const NDBTAB *ndbtab= ndbtab_g.get_table();
    if (ndbtab)
    {
      distributed= true;
    }
    else if (distributed)
    {
      sql_print_error("NDB: Inconsistency detected in distributed "
                      "privilege tables. Table '%s.%s' is not distributed",
                      dist_priv.database(), table_name);
      DBUG_RETURN(false);
    }
  }
  DBUG_RETURN(distributed);
}
#endif

static void
ndbcluster_binlog_log_query(handlerton *hton, THD *thd, enum_binlog_command binlog_command,
                            const char *query, uint query_length,
                            const char *db, const char *table_name)
{
  DBUG_ENTER("ndbcluster_binlog_log_query");
  DBUG_PRINT("enter", ("db: %s  table_name: %s  query: %s",
                       db, table_name, query));
  enum SCHEMA_OP_TYPE type;
  int log= 0;
  uint32 table_id= 0, table_version= 0;
  /*
    Since databases aren't real ndb schema object
    they don't have any id/version

    But since that id/version is used to make sure that event's on SCHEMA_TABLE
    is correct, we set random numbers
  */
  table_id = (uint32)rand();
  table_version = (uint32)rand();
  switch (binlog_command)
  {
  case LOGCOM_CREATE_TABLE:
    type= SOT_CREATE_TABLE;
    DBUG_ASSERT(FALSE);
    break;
  case LOGCOM_ALTER_TABLE:
    type= SOT_ALTER_TABLE_COMMIT;
    //log= 1;
    break;
  case LOGCOM_RENAME_TABLE:
    type= SOT_RENAME_TABLE;
    DBUG_ASSERT(FALSE);
    break;
  case LOGCOM_DROP_TABLE:
    type= SOT_DROP_TABLE;
    DBUG_ASSERT(FALSE);
    break;
  case LOGCOM_CREATE_DB:
    type= SOT_CREATE_DB;
    log= 1;
    break;
  case LOGCOM_ALTER_DB:
    type= SOT_ALTER_DB;
    log= 1;
    break;
  case LOGCOM_DROP_DB:
    type= SOT_DROP_DB;
    DBUG_ASSERT(FALSE);
    break;
#ifndef NDB_WITHOUT_DIST_PRIV
  case LOGCOM_CREATE_USER:
    type= SOT_CREATE_USER;
    if (priv_tables_are_in_ndb(thd))
    {
      DBUG_PRINT("info", ("Privilege tables have been distributed, logging statement"));
      log= 1;
    }
    break;
  case LOGCOM_DROP_USER:
    type= SOT_DROP_USER;
    if (priv_tables_are_in_ndb(thd))
    {
      DBUG_PRINT("info", ("Privilege tables have been distributed, logging statement"));
      log= 1;
    }
    break;
  case LOGCOM_RENAME_USER:
    type= SOT_RENAME_USER;
    if (priv_tables_are_in_ndb(thd))
    {
      DBUG_PRINT("info", ("Privilege tables have been distributed, logging statement"));
      log= 1;
    }
    break;
  case LOGCOM_GRANT:
    type= SOT_GRANT;
    if (priv_tables_are_in_ndb(thd))
    {
      DBUG_PRINT("info", ("Privilege tables have been distributed, logging statement"));
      log= 1;
    }
    break;
  case LOGCOM_REVOKE:
    type= SOT_REVOKE;
    if (priv_tables_are_in_ndb(thd))
    {
      DBUG_PRINT("info", ("Privilege tables have been distributed, logging statement"));
      log= 1;
    }
    break;
#endif
  }
  if (log)
  {
    ndbcluster_log_schema_op(thd, query, query_length,
                             db, table_name, table_id, table_version, type,
                             NULL, NULL);
  }
  DBUG_VOID_RETURN;
}


/*
  End use of the NDB Cluster binlog
   - wait for binlog thread to shutdown
*/

int ndbcluster_binlog_end(THD *thd)
{
  DBUG_ENTER("ndbcluster_binlog_end");

  if (ndb_util_thread_running > 0)
  {
    /*
      Wait for util thread to die (as this uses the injector mutex)
      There is a very small change that ndb_util_thread dies and the
      following mutex is freed before it's accessed. This shouldn't
      however be a likely case as the ndbcluster_binlog_end is supposed to
      be called before ndb_cluster_end().
    */
    sql_print_information("Stopping Cluster Utility thread");
    pthread_mutex_lock(&LOCK_ndb_util_thread);
    /* Ensure mutex are not freed if ndb_cluster_end is running at same time */
    ndb_util_thread_running++;
    ndbcluster_terminating= 1;
    pthread_cond_signal(&COND_ndb_util_thread);
    while (ndb_util_thread_running > 1)
      pthread_cond_wait(&COND_ndb_util_ready, &LOCK_ndb_util_thread);
    ndb_util_thread_running--;
    pthread_mutex_unlock(&LOCK_ndb_util_thread);
  }

  if (ndb_index_stat_thread_running > 0)
  {
    /*
      Index stats thread blindly imitates util thread.  Following actually
      fixes some "[Warning] Plugin 'ndbcluster' will be forced to shutdown".
    */
    sql_print_information("Stopping Cluster Index Stats thread");
    pthread_mutex_lock(&LOCK_ndb_index_stat_thread);
    /* Ensure mutex are not freed if ndb_cluster_end is running at same time */
    ndb_index_stat_thread_running++;
    ndbcluster_terminating= 1;
    pthread_cond_signal(&COND_ndb_index_stat_thread);
    while (ndb_index_stat_thread_running > 1)
      pthread_cond_wait(&COND_ndb_index_stat_ready, &LOCK_ndb_index_stat_thread);
    ndb_index_stat_thread_running--;
    pthread_mutex_unlock(&LOCK_ndb_index_stat_thread);
  }

  if (ndbcluster_binlog_inited)
  {
    ndbcluster_binlog_inited= 0;
    if (ndb_binlog_thread_running)
    {
      /* wait for injector thread to finish */
      ndbcluster_binlog_terminating= 1;
      pthread_mutex_lock(&injector_mutex);
      pthread_cond_signal(&injector_cond);
      while (ndb_binlog_thread_running > 0)
        pthread_cond_wait(&injector_cond, &injector_mutex);
      pthread_mutex_unlock(&injector_mutex);
    }
    pthread_mutex_destroy(&injector_mutex);
    pthread_cond_destroy(&injector_cond);
    pthread_mutex_destroy(&ndb_schema_share_mutex);
  }

  DBUG_RETURN(0);
}

/*****************************************************************
  functions called from slave sql client threads
****************************************************************/
static void ndbcluster_reset_slave(THD *thd)
{
  int error = 0;
  if (!ndb_binlog_running)
    return;

  DBUG_ENTER("ndbcluster_reset_slave");

  /*
    delete all rows from mysql.ndb_apply_status table
    - if table does not exist ignore the error as it
      is a consistent behavior
  */
  Ndb_local_connection mysqld(thd);
  const bool ignore_no_such_table = true;
  if(mysqld.delete_rows(STRING_WITH_LEN("mysql"),
                        STRING_WITH_LEN("ndb_apply_status"),
                        ignore_no_such_table,
                        NULL))
  {
    // Failed to delete rows from table
    error = 1;
  }

  g_ndb_slave_state.atResetSlave();

  // pending fix for bug#59844 will make this function return int
  DBUG_VOID_RETURN;
}

/*
  Initialize the binlog part of the ndb handlerton
*/

/**
  Upon the sql command flush logs, we need to ensure that all outstanding
  ndb data to be logged has made it to the binary log to get a deterministic
  behavior on the rotation of the log.
 */
static bool ndbcluster_flush_logs(handlerton *hton, ulonglong target_lsn)
{
  if (target_lsn == 0)
    ndbcluster_binlog_wait(current_thd);
  return FALSE;
}


static int ndbcluster_binlog_func(handlerton *hton, THD *thd, 
                                  enum_binlog_func fn, 
                                  void *arg)
{
  DBUG_ENTER("ndbcluster_binlog_func");
  int res= 0;
  switch(fn)
  {
  case BFN_RESET_LOGS:
    res= ndbcluster_reset_logs(thd);
    break;
  case BFN_RESET_SLAVE:
    ndbcluster_reset_slave(thd);
    break;
  case BFN_BINLOG_WAIT:
    ndbcluster_binlog_wait(thd);
    break;
  case BFN_BINLOG_END:
    res= ndbcluster_binlog_end(thd);
    break;
  case BFN_BINLOG_PURGE_FILE:
    res= ndbcluster_binlog_index_purge_file(thd, (const char *)arg);
    break;
  }
  DBUG_RETURN(res);
}

void ndbcluster_binlog_init_handlerton()
{
  handlerton *h= ndbcluster_hton;
  h->flush_logs=       ndbcluster_flush_logs;
  h->binlog_func=      ndbcluster_binlog_func;
  h->binlog_log_query= ndbcluster_binlog_log_query;
}


/*
  Convert db and table name into a key to use for searching
  the ndbcluster_open_tables hash
*/
static size_t
ndb_open_tables__create_key(char* key_buf, size_t key_buf_length,
                            const char* db, size_t db_length,
                            const char* table, size_t table_length)
{
  size_t key_length =  my_snprintf(key_buf, key_buf_length,
                                   "./%*s/%*s", db_length, db,
                                   table_length, table) - 1;
  assert(key_length > 0);
  assert(key_length < key_buf_length);

  return key_length;
}


/*
  Check if table with given name is open, ie. is
  in ndbcluster_open_tables hash
*/
static bool
ndb_open_tables__is_table_open(const char* db, size_t db_length,
                               const char* table, size_t table_length)
{
  char key[FN_REFLEN + 1];
  size_t key_length = ndb_open_tables__create_key(key, sizeof(key),
                                                  db, db_length,
                                                  table, table_length);
  DBUG_ENTER("ndb_open_tables__is_table_open");
  DBUG_PRINT("enter", ("db: '%s', table: '%s', key: '%s'",
                       db, table, key));

  pthread_mutex_lock(&ndbcluster_mutex);
  bool result = my_hash_search(&ndbcluster_open_tables,
                               (const uchar*)key,
                               key_length) != NULL;
  pthread_mutex_unlock(&ndbcluster_mutex);

  DBUG_PRINT("exit", ("result: %d", result));
  DBUG_RETURN(result);
}


static bool
ndbcluster_check_ndb_schema_share()
{
  return ndb_open_tables__is_table_open(STRING_WITH_LEN("mysql"),
                                        STRING_WITH_LEN("ndb_schema"));
}


static bool
ndbcluster_check_ndb_apply_status_share()
{
  return ndb_open_tables__is_table_open(STRING_WITH_LEN("mysql"),
                                        STRING_WITH_LEN("ndb_apply_status"));
}


static bool
create_cluster_sys_table(THD *thd, const char* db, size_t db_length,
                         const char* table, size_t table_length,
                         const char* create_definitions,
                         const char* create_options)
{
  if (ndb_open_tables__is_table_open(db, db_length, table, table_length))
    return false;

  if (g_ndb_cluster_connection->get_no_ready() <= 0)
    return false;

  if (opt_ndb_extra_logging)
    sql_print_information("NDB: Creating %s.%s", db, table);

  Ndb_local_connection mysqld(thd);

  /*
    Check if table exists in MySQL "dictionary"(i.e on disk)
    if so, remove it since there is none in Ndb
  */
  {
    char path[FN_REFLEN + 1];
    build_table_filename(path, sizeof(path) - 1,
                         db, table, reg_ext, 0);
    if (my_delete(path, MYF(0)) == 0)
    {
      /*
        The .frm file existed and was deleted from disk.
        It's possible that someone has tried to use it and thus
        it might have been inserted in the table definition cache.
        It must be flushed to avoid that it exist only in the
        table definition cache.
      */
      if (opt_ndb_extra_logging)
        sql_print_information("NDB: Flushing %s.%s", db, table);

      /* Flush mysql.ndb_apply_status table, ignore all errors */
      (void)mysqld.flush_table(db, db_length,
                               table, table_length);
    }
  }

  const bool create_if_not_exists = true;
  const bool res = mysqld.create_sys_table(db, db_length,
                                           table, table_length,
                                           create_if_not_exists,
                                           create_definitions,
                                           create_options);
  return res;
}


static bool
ndb_apply_table__create(THD *thd)
{
  DBUG_ENTER("ndb_apply_table__create");

  /* NOTE! Updating this table schema must be reflected in ndb_restore */
  const bool res =
    create_cluster_sys_table(thd,
                             STRING_WITH_LEN("mysql"),
                             STRING_WITH_LEN("ndb_apply_status"),
                             // table_definition
                             "server_id INT UNSIGNED NOT NULL,"
                             "epoch BIGINT UNSIGNED NOT NULL, "
                             "log_name VARCHAR(255) BINARY NOT NULL, "
                             "start_pos BIGINT UNSIGNED NOT NULL, "
                             "end_pos BIGINT UNSIGNED NOT NULL, "
                             "PRIMARY KEY USING HASH (server_id)",
                             // table_options
                             "ENGINE=NDB CHARACTER SET latin1");
  DBUG_RETURN(res);
}


static bool
ndb_schema_table__create(THD *thd)
{
  DBUG_ENTER("ndb_schema_table__create");

  /* NOTE! Updating this table schema must be reflected in ndb_restore */
  const bool res =
    create_cluster_sys_table(thd,
                             STRING_WITH_LEN("mysql"),
                             STRING_WITH_LEN("ndb_schema"),
                             // table_definition
                             "db VARBINARY(63) NOT NULL,"
                             "name VARBINARY(63) NOT NULL,"
                             "slock BINARY(32) NOT NULL,"
                             "query BLOB NOT NULL,"
                             "node_id INT UNSIGNED NOT NULL,"
                             "epoch BIGINT UNSIGNED NOT NULL,"
                             "id INT UNSIGNED NOT NULL,"
                             "version INT UNSIGNED NOT NULL,"
                             "type INT UNSIGNED NOT NULL,"
                             "PRIMARY KEY USING HASH (db,name)",
                             // table_options
                             "ENGINE=NDB CHARACTER SET latin1");
  DBUG_RETURN(res);
}

class Thd_ndb_options_guard
{
public:
  Thd_ndb_options_guard(Thd_ndb *thd_ndb)
    : m_val(thd_ndb->options), m_save_val(thd_ndb->options) {}
  ~Thd_ndb_options_guard() { m_val= m_save_val; }
  void set(uint32 flag) { m_val|= flag; }
private:
  uint32 &m_val;
  uint32 m_save_val;
};

extern int ndb_setup_complete;
extern pthread_cond_t COND_ndb_setup_complete;

/*
   ndb_notify_tables_writable
   
   Called to notify any waiting threads that Ndb tables are
   now writable
*/ 
static void ndb_notify_tables_writable()
{
  pthread_mutex_lock(&ndbcluster_mutex);
  ndb_setup_complete= 1;
  pthread_cond_broadcast(&COND_ndb_setup_complete);
  pthread_mutex_unlock(&ndbcluster_mutex);
}

/*
  Ndb has no representation of the database schema objects.
  The mysql.ndb_schema table contains the latest schema operations
  done via a mysqld, and thus reflects databases created/dropped/altered
  while a mysqld was disconnected.  This function tries to recover
  the correct state w.r.t created databases using the information in
  that table.


*/
static int ndbcluster_find_all_databases(THD *thd)
{
  Ndb *ndb= check_ndb_in_thd(thd);
  Thd_ndb *thd_ndb= get_thd_ndb(thd);
  Thd_ndb_options_guard thd_ndb_options(thd_ndb);
  NDBDICT *dict= ndb->getDictionary();
  NdbTransaction *trans= NULL;
  NdbError ndb_error;
  int retries= 100;
  int retry_sleep= 30; /* 30 milliseconds, transaction */
  DBUG_ENTER("ndbcluster_find_all_databases");

  /*
    Function should only be called while ndbcluster_global_schema_lock
    is held, to ensure that ndb_schema table is not being updated while
    scanning.
  */
  if (!thd_ndb->has_required_global_schema_lock("ndbcluster_find_all_databases"))
    DBUG_RETURN(1);

  ndb->setDatabaseName(NDB_REP_DB);
  thd_ndb_options.set(TNO_NO_LOG_SCHEMA_OP);
  thd_ndb_options.set(TNO_NO_LOCK_SCHEMA_OP);
  while (1)
  {
    char db_buffer[FN_REFLEN];
    char *db= db_buffer+1;
    char name[FN_REFLEN];
    char query[64000];
    Ndb_table_guard ndbtab_g(dict, NDB_SCHEMA_TABLE);
    const NDBTAB *ndbtab= ndbtab_g.get_table();
    NdbScanOperation *op;
    NdbBlob *query_blob_handle;
    int r= 0;
    if (ndbtab == NULL)
    {
      ndb_error= dict->getNdbError();
      goto error;
    }
    trans= ndb->startTransaction();
    if (trans == NULL)
    {
      ndb_error= ndb->getNdbError();
      goto error;
    }
    op= trans->getNdbScanOperation(ndbtab);
    if (op == NULL)
    {
      ndb_error= trans->getNdbError();
      goto error;
    }

    op->readTuples(NdbScanOperation::LM_Read,
                   NdbScanOperation::SF_TupScan, 1);
    
    r|= op->getValue("db", db_buffer) == NULL;
    r|= op->getValue("name", name) == NULL;
    r|= (query_blob_handle= op->getBlobHandle("query")) == NULL;
    r|= query_blob_handle->getValue(query, sizeof(query));

    if (r)
    {
      ndb_error= op->getNdbError();
      goto error;
    }

    if (trans->execute(NdbTransaction::NoCommit))
    {
      ndb_error= trans->getNdbError();
      goto error;
    }

    while ((r= op->nextResult()) == 0)
    {
      unsigned db_len= db_buffer[0];
      unsigned name_len= name[0];
      /*
        name_len == 0 means no table name, hence the row
        is for a database
      */
      if (db_len > 0 && name_len == 0)
      {
        /* database found */
        db[db_len]= 0;

	/* find query */
        Uint64 query_length= 0;
        if (query_blob_handle->getLength(query_length))
        {
          ndb_error= query_blob_handle->getNdbError();
          goto error;
        }
        query[query_length]= 0;
        build_table_filename(name, sizeof(name), db, "", "", 0);
        int database_exists= !my_access(name, F_OK);
        if (strncasecmp("CREATE", query, 6) == 0)
        {
          /* Database should exist */
          if (!database_exists)
          {
            /* create missing database */
            sql_print_information("NDB: Discovered missing database '%s'", db);
            const int no_print_error[1]= {0};
            run_query(thd, query, query + query_length,
                      no_print_error);
          }
        }
        else if (strncasecmp("ALTER", query, 5) == 0)
        {
          /* Database should exist */
          if (!database_exists)
          {
            /* create missing database */
            sql_print_information("NDB: Discovered missing database '%s'", db);
            const int no_print_error[1]= {0};
            name_len= my_snprintf(name, sizeof(name), "CREATE DATABASE %s", db);
            run_query(thd, name, name + name_len,
                      no_print_error);
            run_query(thd, query, query + query_length,
                      no_print_error);
          }
        }
        else if (strncasecmp("DROP", query, 4) == 0)
        {
          /* Database should not exist */
          if (database_exists)
          {
            /* drop missing database */
            sql_print_information("NDB: Discovered reamining database '%s'", db);
          }
        }
      }
    }
    if (r == -1)
    {
      ndb_error= op->getNdbError();
      goto error;
    }
    ndb->closeTransaction(trans);
    trans= NULL;
    DBUG_RETURN(0); // success
  error:
    if (trans)
    {
      ndb->closeTransaction(trans);
      trans= NULL;
    }
    if (ndb_error.status == NdbError::TemporaryError && !thd->killed)
    {
      if (retries--)
      {
        sql_print_warning("NDB: ndbcluster_find_all_databases retry: %u - %s",
                          ndb_error.code,
                          ndb_error.message);
        do_retry_sleep(retry_sleep);
        continue; // retry
      }
    }
    if (!thd->killed)
    {
      sql_print_error("NDB: ndbcluster_find_all_databases fail: %u - %s",
                      ndb_error.code,
                      ndb_error.message);
    }

    DBUG_RETURN(1); // not temp error or too many retries
  }
}

bool
ndb_binlog_setup(THD *thd)
{
  if (ndb_binlog_tables_inited)
    return true; // Already setup -> OK

  Ndb_global_schema_lock_guard global_schema_lock_guard(thd);
  if (global_schema_lock_guard.lock(false, false))
    return false;
  if (!ndb_schema_share &&
      ndbcluster_check_ndb_schema_share() == 0)
  {
    ndb_create_table_from_engine(thd, NDB_REP_DB, NDB_SCHEMA_TABLE);
    if (!ndb_schema_share)
    {
      ndb_schema_table__create(thd);
      // always make sure we create the 'schema' first
      if (!ndb_schema_share)
        return false;
    }
  }
  if (!ndb_apply_status_share &&
      ndbcluster_check_ndb_apply_status_share() == 0)
  {
    ndb_create_table_from_engine(thd, NDB_REP_DB, NDB_APPLY_TABLE);
    if (!ndb_apply_status_share)
    {
      ndb_apply_table__create(thd);
      if (!ndb_apply_status_share)
        return false;
    }
  }

  if (ndbcluster_find_all_databases(thd))
  {
    return false;
  }

  if (!ndbcluster_find_all_files(thd))
  {
    ndb_binlog_tables_inited= TRUE;
    if (ndb_binlog_tables_inited &&
        ndb_binlog_running && ndb_binlog_is_ready)
    {
      if (opt_ndb_extra_logging)
        sql_print_information("NDB Binlog: ndb tables writable");
      close_cached_tables(NULL, NULL, TRUE, FALSE, FALSE);
      
      /* 
         Signal any waiting thread that ndb table setup is
         now complete
      */
      ndb_notify_tables_writable();
    }
    /* Signal injector thread that all is setup */
    pthread_cond_signal(&injector_cond);

    return true; // Setup completed -> OK
  }
  return false;
}

/*
  Defines and struct for schema table.
  Should reflect table definition above.
*/
#define SCHEMA_DB_I 0u
#define SCHEMA_NAME_I 1u
#define SCHEMA_SLOCK_I 2u
#define SCHEMA_QUERY_I 3u
#define SCHEMA_NODE_ID_I 4u
#define SCHEMA_EPOCH_I 5u
#define SCHEMA_ID_I 6u
#define SCHEMA_VERSION_I 7u
#define SCHEMA_TYPE_I 8u
#define SCHEMA_SIZE 9u
#define SCHEMA_SLOCK_SIZE 32u

struct Cluster_schema
{
  uchar db_length;
  char db[64];
  uchar name_length;
  char name[64];
  uchar slock_length;
  uint32 slock[SCHEMA_SLOCK_SIZE/4];
  unsigned short query_length;
  char *query;
  Uint64 epoch;
  uint32 node_id;
  uint32 id;
  uint32 version;
  uint32 type;
  uint32 any_value;
};

static void
print_could_not_discover_error(THD *thd,
                               const Cluster_schema *schema)
{
  sql_print_error("NDB Binlog: Could not discover table '%s.%s' from "
                  "binlog schema event '%s' from node %d. "
                  "my_errno: %d",
                   schema->db, schema->name, schema->query,
                   schema->node_id, my_errno);
  print_warning_list("NDB Binlog", thd);
}


/*
  Transfer schema table data into corresponding struct
*/
static void ndbcluster_get_schema(Ndb_event_data *event_data,
                                  Cluster_schema *s)
{
  TABLE *table= event_data->shadow_table;
  Field **field;
  /* unpack blob values */
  uchar* blobs_buffer= 0;
  uint blobs_buffer_size= 0;
  my_bitmap_map *old_map= dbug_tmp_use_all_columns(table, table->read_set);
  {
    ptrdiff_t ptrdiff= 0;
    int ret= get_ndb_blobs_value(table, event_data->ndb_value[0],
                                 blobs_buffer, blobs_buffer_size,
                                 ptrdiff);
    if (ret != 0)
    {
      my_free(blobs_buffer, MYF(MY_ALLOW_ZERO_PTR));
      DBUG_PRINT("info", ("blob read error"));
      DBUG_ASSERT(FALSE);
    }
  }
  /* db varchar 1 length uchar */
  field= table->field;
  s->db_length= *(uint8*)(*field)->ptr;
  DBUG_ASSERT(s->db_length <= (*field)->field_length);
  DBUG_ASSERT((*field)->field_length + 1 == sizeof(s->db));
  memcpy(s->db, (*field)->ptr + 1, s->db_length);
  s->db[s->db_length]= 0;
  /* name varchar 1 length uchar */
  field++;
  s->name_length= *(uint8*)(*field)->ptr;
  DBUG_ASSERT(s->name_length <= (*field)->field_length);
  DBUG_ASSERT((*field)->field_length + 1 == sizeof(s->name));
  memcpy(s->name, (*field)->ptr + 1, s->name_length);
  s->name[s->name_length]= 0;
  /* slock fixed length */
  field++;
  s->slock_length= (*field)->field_length;
  DBUG_ASSERT((*field)->field_length == sizeof(s->slock));
  memcpy(s->slock, (*field)->ptr, s->slock_length);
  /* query blob */
  field++;
  {
    Field_blob *field_blob= (Field_blob*)(*field);
    uint blob_len= field_blob->get_length((*field)->ptr);
    uchar *blob_ptr= 0;
    field_blob->get_ptr(&blob_ptr);
    DBUG_ASSERT(blob_len == 0 || blob_ptr != 0);
    s->query_length= blob_len;
    s->query= sql_strmake((char*) blob_ptr, blob_len);
  }
  /* node_id */
  field++;
  s->node_id= (Uint32)((Field_long *)*field)->val_int();
  /* epoch */
  field++;
  s->epoch= ((Field_long *)*field)->val_int();
  /* id */
  field++;
  s->id= (Uint32)((Field_long *)*field)->val_int();
  /* version */
  field++;
  s->version= (Uint32)((Field_long *)*field)->val_int();
  /* type */
  field++;
  s->type= (Uint32)((Field_long *)*field)->val_int();
  /* free blobs buffer */
  my_free(blobs_buffer, MYF(MY_ALLOW_ZERO_PTR));
  dbug_tmp_restore_column_map(table->read_set, old_map);
}

/*
  helper function to pack a ndb varchar
*/
char *ndb_pack_varchar(const NDBCOL *col, char *buf,
                       const char *str, int sz)
{
  switch (col->getArrayType())
  {
    case NDBCOL::ArrayTypeFixed:
      memcpy(buf, str, sz);
      break;
    case NDBCOL::ArrayTypeShortVar:
      *(uchar*)buf= (uchar)sz;
      memcpy(buf + 1, str, sz);
      break;
    case NDBCOL::ArrayTypeMediumVar:
      int2store(buf, sz);
      memcpy(buf + 2, str, sz);
      break;
  }
  return buf;
}

/*
  acknowledge handling of schema operation
*/
static int
ndbcluster_update_slock(THD *thd,
                        const char *db,
                        const char *table_name,
                        uint32 table_id,
                        uint32 table_version)
{
  DBUG_ENTER("ndbcluster_update_slock");
  if (!ndb_schema_share)
  {
    DBUG_RETURN(0);
  }

  const NdbError *ndb_error= 0;
  uint32 node_id= g_ndb_cluster_connection->node_id();
  Ndb *ndb= check_ndb_in_thd(thd);
  char save_db[FN_HEADLEN];
  strcpy(save_db, ndb->getDatabaseName());

  char tmp_buf[FN_REFLEN];
  NDBDICT *dict= ndb->getDictionary();
  ndb->setDatabaseName(NDB_REP_DB);
  Ndb_table_guard ndbtab_g(dict, NDB_SCHEMA_TABLE);
  const NDBTAB *ndbtab= ndbtab_g.get_table();
  NdbTransaction *trans= 0;
  int retries= 100;
  int retry_sleep= 30; /* 30 milliseconds, transaction */
  const NDBCOL *col[SCHEMA_SIZE];
  unsigned sz[SCHEMA_SIZE];

  MY_BITMAP slock;
  uint32 bitbuf[SCHEMA_SLOCK_SIZE/4];
  bitmap_init(&slock, bitbuf, sizeof(bitbuf)*8, false);

  if (ndbtab == 0)
  {
    if (dict->getNdbError().code != 4009)
      abort();
    DBUG_RETURN(0);
  }

  {
    uint i;
    for (i= 0; i < SCHEMA_SIZE; i++)
    {
      col[i]= ndbtab->getColumn(i);
      if (i != SCHEMA_QUERY_I)
      {
        sz[i]= col[i]->getLength();
        DBUG_ASSERT(sz[i] <= sizeof(tmp_buf));
      }
    }
  }

  while (1)
  {
    if ((trans= ndb->startTransaction()) == 0)
      goto err;
    {
      NdbOperation *op= 0;
      int r= 0;

      /* read the bitmap exlusive */
      r|= (op= trans->getNdbOperation(ndbtab)) == 0;
      DBUG_ASSERT(r == 0);
      r|= op->readTupleExclusive();
      DBUG_ASSERT(r == 0);
    
      /* db */
      ndb_pack_varchar(col[SCHEMA_DB_I], tmp_buf, db, strlen(db));
      r|= op->equal(SCHEMA_DB_I, tmp_buf);
      DBUG_ASSERT(r == 0);
      /* name */
      ndb_pack_varchar(col[SCHEMA_NAME_I], tmp_buf, table_name,
                       strlen(table_name));
      r|= op->equal(SCHEMA_NAME_I, tmp_buf);
      DBUG_ASSERT(r == 0);
      /* slock */
      r|= op->getValue(SCHEMA_SLOCK_I, (char*)slock.bitmap) == 0;
      DBUG_ASSERT(r == 0);
    }
    if (trans->execute(NdbTransaction::NoCommit))
      goto err;

    if (opt_ndb_extra_logging > 19)
    {
      uint32 copy[SCHEMA_SLOCK_SIZE/4];
      memcpy(copy, bitbuf, sizeof(copy));
      bitmap_clear_bit(&slock, node_id);
      sql_print_information("NDB: reply to %s.%s(%u/%u) from %x%x to %x%x",
                            db, table_name,
                            table_id, table_version,
                            copy[0], copy[1],
                            slock.bitmap[0],
                            slock.bitmap[1]);
    }
    else
    {
      bitmap_clear_bit(&slock, node_id);
    }

    {
      NdbOperation *op= 0;
      int r= 0;

      /* now update the tuple */
      r|= (op= trans->getNdbOperation(ndbtab)) == 0;
      DBUG_ASSERT(r == 0);
      r|= op->updateTuple();
      DBUG_ASSERT(r == 0);

      /* db */
      ndb_pack_varchar(col[SCHEMA_DB_I], tmp_buf, db, strlen(db));
      r|= op->equal(SCHEMA_DB_I, tmp_buf);
      DBUG_ASSERT(r == 0);
      /* name */
      ndb_pack_varchar(col[SCHEMA_NAME_I], tmp_buf, table_name,
                       strlen(table_name));
      r|= op->equal(SCHEMA_NAME_I, tmp_buf);
      DBUG_ASSERT(r == 0);
      /* slock */
      r|= op->setValue(SCHEMA_SLOCK_I, (char*)slock.bitmap);
      DBUG_ASSERT(r == 0);
      /* node_id */
      r|= op->setValue(SCHEMA_NODE_ID_I, node_id);
      DBUG_ASSERT(r == 0);
      /* type */
      r|= op->setValue(SCHEMA_TYPE_I, (uint32)SOT_CLEAR_SLOCK);
      DBUG_ASSERT(r == 0);
    }
    if (trans->execute(NdbTransaction::Commit, 
                       NdbOperation::DefaultAbortOption, 1 /*force send*/) == 0)
    {
      DBUG_PRINT("info", ("node %d cleared lock on '%s.%s'",
                          node_id, db, table_name));
      dict->forceGCPWait(1);
      break;
    }
  err:
    const NdbError *this_error= trans ?
      &trans->getNdbError() : &ndb->getNdbError();
    if (this_error->status == NdbError::TemporaryError && !thd->killed)
    {
      if (retries--)
      {
        if (trans)
          ndb->closeTransaction(trans);
        do_retry_sleep(retry_sleep);
        continue; // retry
      }
    }
    ndb_error= this_error;
    break;
  }

  if (ndb_error)
  {
    char buf[1024];
    my_snprintf(buf, sizeof(buf), "Could not release lock on '%s.%s'",
                db, table_name);
    push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                        ER_GET_ERRMSG, ER(ER_GET_ERRMSG),
                        ndb_error->code, ndb_error->message, buf);
  }
  if (trans)
    ndb->closeTransaction(trans);
  ndb->setDatabaseName(save_db);
  DBUG_RETURN(0);
}


/*
  log query in schema table
*/
static void ndb_report_waiting(const char *key,
                               int the_time,
                               const char *op,
                               const char *obj,
                               const MY_BITMAP * map)
{
  ulonglong ndb_latest_epoch= 0;
  const char *proc_info= "<no info>";
  pthread_mutex_lock(&injector_mutex);
  if (injector_ndb)
    ndb_latest_epoch= injector_ndb->getLatestGCI();
  if (injector_thd)
    proc_info= injector_thd->proc_info;
  pthread_mutex_unlock(&injector_mutex);
  if (map == 0)
  {
    sql_print_information("NDB %s:"
                          " waiting max %u sec for %s %s."
                          "  epochs: (%u/%u,%u/%u,%u/%u)"
                          "  injector proc_info: %s"
                          ,key, the_time, op, obj
                          ,(uint)(ndb_latest_handled_binlog_epoch >> 32)
                          ,(uint)(ndb_latest_handled_binlog_epoch)
                          ,(uint)(ndb_latest_received_binlog_epoch >> 32)
                          ,(uint)(ndb_latest_received_binlog_epoch)
                          ,(uint)(ndb_latest_epoch >> 32)
                          ,(uint)(ndb_latest_epoch)
                          ,proc_info
                          );
  }
  else
  {
    sql_print_information("NDB %s:"
                          " waiting max %u sec for %s %s."
                          "  epochs: (%u/%u,%u/%u,%u/%u)"
                          "  injector proc_info: %s map: %x%x"
                          ,key, the_time, op, obj
                          ,(uint)(ndb_latest_handled_binlog_epoch >> 32)
                          ,(uint)(ndb_latest_handled_binlog_epoch)
                          ,(uint)(ndb_latest_received_binlog_epoch >> 32)
                          ,(uint)(ndb_latest_received_binlog_epoch)
                          ,(uint)(ndb_latest_epoch >> 32)
                          ,(uint)(ndb_latest_epoch)
                          ,proc_info
                          ,map->bitmap[0]
                          ,map->bitmap[1]
                          );
  }
}

static
const char*
get_schema_type_name(uint type)
{
  switch(type){
  case SOT_DROP_TABLE:
    return "DROP_TABLE";
  case SOT_CREATE_TABLE:
    return "CREATE_TABLE";
  case SOT_RENAME_TABLE_NEW:
    return "RENAME_TABLE_NEW";
  case SOT_ALTER_TABLE_COMMIT:
    return "ALTER_TABLE_COMMIT";
  case SOT_DROP_DB:
    return "DROP_DB";
  case SOT_CREATE_DB:
    return "CREATE_DB";
  case SOT_ALTER_DB:
    return "ALTER_DB";
  case SOT_CLEAR_SLOCK:
    return "CLEAR_SLOCK";
  case SOT_TABLESPACE:
    return "TABLESPACE";
  case SOT_LOGFILE_GROUP:
    return "LOGFILE_GROUP";
  case SOT_RENAME_TABLE:
    return "RENAME_TABLE";
  case SOT_TRUNCATE_TABLE:
    return "TRUNCATE_TABLE";
  case SOT_RENAME_TABLE_PREPARE:
    return "RENAME_TABLE_PREPARE";
  case SOT_ONLINE_ALTER_TABLE_PREPARE:
    return "ONLINE_ALTER_TABLE_PREPARE";
  case SOT_ONLINE_ALTER_TABLE_COMMIT:
    return "ONLINE_ALTER_TABLE_COMMIT";
  case SOT_CREATE_USER:
    return "CREATE_USER";
  case SOT_DROP_USER:
    return "DROP_USER";
  case SOT_RENAME_USER:
    return "RENAME_USER";
  case SOT_GRANT:
    return "GRANT";
  case SOT_REVOKE:
    return "REVOKE";
  }
  return "<unknown>";
}

extern void update_slave_api_stats(Ndb*);

int ndbcluster_log_schema_op(THD *thd,
                             const char *query, int query_length,
                             const char *db, const char *table_name,
                             uint32 ndb_table_id,
                             uint32 ndb_table_version,
                             enum SCHEMA_OP_TYPE type,
                             const char *new_db, const char *new_table_name)
{
  DBUG_ENTER("ndbcluster_log_schema_op");
  Thd_ndb *thd_ndb= get_thd_ndb(thd);
  if (!thd_ndb)
  {
    if (!(thd_ndb= Thd_ndb::seize(thd)))
    {
      sql_print_error("Could not allocate Thd_ndb object");
      DBUG_RETURN(1);
    }
    thd_set_thd_ndb(thd, thd_ndb);
  }

  DBUG_PRINT("enter",
             ("query: %s  db: %s  table_name: %s  thd_ndb->options: %d",
              query, db, table_name, thd_ndb->options));
  if (!ndb_schema_share || thd_ndb->options & TNO_NO_LOG_SCHEMA_OP)
  {
    if (thd->slave_thread)
      update_slave_api_stats(thd_ndb->ndb);

    DBUG_RETURN(0);
  }

  char tmp_buf2[FN_REFLEN];
  char quoted_table1[2 + 2 * FN_REFLEN + 1];
  char quoted_db1[2 + 2 * FN_REFLEN + 1];
  char quoted_db2[2 + 2 * FN_REFLEN + 1];
  char quoted_table2[2 + 2 * FN_REFLEN + 1];
  int id_length= 0;
  const char *type_str;
  int also_internal= 0;
  uint32 log_type= (uint32)type;
  switch (type)
  {
  case SOT_DROP_TABLE:
    /* drop database command, do not log at drop table */
    if (thd->lex->sql_command ==  SQLCOM_DROP_DB)
      DBUG_RETURN(0);
    /* redo the drop table query as is may contain several tables */
    query= tmp_buf2;
    id_length= my_strmov_quoted_identifier (thd, (char *) quoted_table1,
                                            table_name, 0);
    quoted_table1[id_length]= '\0';
    id_length= my_strmov_quoted_identifier (thd, (char *) quoted_db1,
                                            db, 0);
    quoted_db1[id_length]= '\0';
    query_length= (uint) (strxmov(tmp_buf2, "drop table ", quoted_db1, ".",
                                  quoted_table1, NullS) - tmp_buf2);
    type_str= "drop table";
    break;
  case SOT_RENAME_TABLE_PREPARE:
    type_str= "rename table prepare";
    also_internal= 1;
    break;
  case SOT_RENAME_TABLE:
    /* redo the rename table query as is may contain several tables */
    query= tmp_buf2;
    id_length= my_strmov_quoted_identifier (thd, (char *) quoted_db1,
                                            db, 0);
    quoted_db1[id_length]= '\0';
    id_length= my_strmov_quoted_identifier (thd, (char *) quoted_table1,
                                            table_name, 0);
    quoted_table1[id_length]= '\0';
    id_length= my_strmov_quoted_identifier (thd, (char *) quoted_db2,
                                            new_db, 0);
    quoted_db2[id_length]= '\0';
    id_length= my_strmov_quoted_identifier (thd, (char *) quoted_table2,
                                            new_table_name, 0);
    quoted_table2[id_length]= '\0';
    query_length= (uint) (strxmov(tmp_buf2, "rename table ",
                                  quoted_db1, ".", quoted_table1, " to ",
                                  quoted_db2, ".", quoted_table2, NullS) - tmp_buf2);
    type_str= "rename table";
    break;
  case SOT_CREATE_TABLE:
    type_str= "create table";
    break;
  case SOT_ALTER_TABLE_COMMIT:
    type_str= "alter table";
    also_internal= 1;
    break;
  case SOT_ONLINE_ALTER_TABLE_PREPARE:
    type_str= "online alter table prepare";
    also_internal= 1;
    break;
  case SOT_ONLINE_ALTER_TABLE_COMMIT:
    type_str= "online alter table commit";
    also_internal= 1;
    break;
  case SOT_DROP_DB:
    type_str= "drop db";
    break;
  case SOT_CREATE_DB:
    type_str= "create db";
    break;
  case SOT_ALTER_DB:
    type_str= "alter db";
    break;
  case SOT_TABLESPACE:
    type_str= "tablespace";
    break;
  case SOT_LOGFILE_GROUP:
    type_str= "logfile group";
    break;
  case SOT_TRUNCATE_TABLE:
    type_str= "truncate table";
    break;
  case SOT_CREATE_USER:
    type_str= "create user";
    break;
  case SOT_DROP_USER:
    type_str= "drop user";
    break;
  case SOT_RENAME_USER:
    type_str= "rename user";
    break;
  case SOT_GRANT:
    type_str= "grant/revoke";
    break;
  case SOT_REVOKE:
    type_str= "revoke all";
    break;
  default:
    abort(); /* should not happen, programming error */
  }

  NDB_SCHEMA_OBJECT *ndb_schema_object;
  {
    char key[FN_REFLEN + 1];
    build_table_filename(key, sizeof(key) - 1, db, table_name, "", 0);
    ndb_schema_object= ndb_get_schema_object(key, TRUE, FALSE);
    ndb_schema_object->table_id= ndb_table_id;
    ndb_schema_object->table_version= ndb_table_version;
  }

  const NdbError *ndb_error= 0;
  uint32 node_id= g_ndb_cluster_connection->node_id();
  Uint64 epoch= 0;
  {
    int i;
    int no_storage_nodes= g_ndb_cluster_connection->no_db_nodes();

    /* begin protect ndb_schema_share */
    pthread_mutex_lock(&ndb_schema_share_mutex);
    if (ndb_schema_share == 0)
    {
      pthread_mutex_unlock(&ndb_schema_share_mutex);
      if (ndb_schema_object)
        ndb_free_schema_object(&ndb_schema_object, FALSE);
      DBUG_RETURN(0);    
    }
    pthread_mutex_lock(&ndb_schema_share->mutex);
    for (i= 0; i < no_storage_nodes; i++)
    {
      bitmap_union(&ndb_schema_object->slock_bitmap,
                   &ndb_schema_share->subscriber_bitmap[i]);
    }
    pthread_mutex_unlock(&ndb_schema_share->mutex);
    pthread_mutex_unlock(&ndb_schema_share_mutex);
    /* end protect ndb_schema_share */

    if (also_internal)
      bitmap_set_bit(&ndb_schema_object->slock_bitmap, node_id);
    else
      bitmap_clear_bit(&ndb_schema_object->slock_bitmap, node_id);

    DBUG_DUMP("schema_subscribers", (uchar*)&ndb_schema_object->slock,
              no_bytes_in_map(&ndb_schema_object->slock_bitmap));
  }

  Ndb *ndb= thd_ndb->ndb;
  char save_db[FN_REFLEN];
  strcpy(save_db, ndb->getDatabaseName());

  char tmp_buf[FN_REFLEN];
  NDBDICT *dict= ndb->getDictionary();
  ndb->setDatabaseName(NDB_REP_DB);
  Ndb_table_guard ndbtab_g(dict, NDB_SCHEMA_TABLE);
  const NDBTAB *ndbtab= ndbtab_g.get_table();
  NdbTransaction *trans= 0;
  int retries= 100;
  int retry_sleep= 30; /* 30 milliseconds, transaction */
  const NDBCOL *col[SCHEMA_SIZE];
  unsigned sz[SCHEMA_SIZE];

  if (ndbtab == 0)
  {
    if (strcmp(NDB_REP_DB, db) != 0 ||
        strcmp(NDB_SCHEMA_TABLE, table_name))
    {
      ndb_error= &dict->getNdbError();
    }
    goto end;
  }

  {
    uint i;
    for (i= 0; i < SCHEMA_SIZE; i++)
    {
      col[i]= ndbtab->getColumn(i);
      if (i != SCHEMA_QUERY_I)
      {
        sz[i]= col[i]->getLength();
        DBUG_ASSERT(sz[i] <= sizeof(tmp_buf));
      }
    }
  }

  while (1)
  {
    const char *log_db= db;
    const char *log_tab= table_name;
    const char *log_subscribers= (char*)ndb_schema_object->slock;
    if ((trans= ndb->startTransaction()) == 0)
      goto err;
    while (1)
    {
      NdbOperation *op= 0;
      int r= 0;
      r|= (op= trans->getNdbOperation(ndbtab)) == 0;
      DBUG_ASSERT(r == 0);
      r|= op->writeTuple();
      DBUG_ASSERT(r == 0);
      
      /* db */
      ndb_pack_varchar(col[SCHEMA_DB_I], tmp_buf, log_db, strlen(log_db));
      r|= op->equal(SCHEMA_DB_I, tmp_buf);
      DBUG_ASSERT(r == 0);
      /* name */
      ndb_pack_varchar(col[SCHEMA_NAME_I], tmp_buf, log_tab,
                       strlen(log_tab));
      r|= op->equal(SCHEMA_NAME_I, tmp_buf);
      DBUG_ASSERT(r == 0);
      /* slock */
      DBUG_ASSERT(sz[SCHEMA_SLOCK_I] ==
                  no_bytes_in_map(&ndb_schema_object->slock_bitmap));
      r|= op->setValue(SCHEMA_SLOCK_I, log_subscribers);
      DBUG_ASSERT(r == 0);
      /* query */
      {
        NdbBlob *ndb_blob= op->getBlobHandle(SCHEMA_QUERY_I);
        DBUG_ASSERT(ndb_blob != 0);
        uint blob_len= query_length;
        const char* blob_ptr= query;
        r|= ndb_blob->setValue(blob_ptr, blob_len);
        DBUG_ASSERT(r == 0);
      }
      /* node_id */
      r|= op->setValue(SCHEMA_NODE_ID_I, node_id);
      DBUG_ASSERT(r == 0);
      /* epoch */
      r|= op->setValue(SCHEMA_EPOCH_I, epoch);
      DBUG_ASSERT(r == 0);
      /* id */
      r|= op->setValue(SCHEMA_ID_I, ndb_table_id);
      DBUG_ASSERT(r == 0);
      /* version */
      r|= op->setValue(SCHEMA_VERSION_I, ndb_table_version);
      DBUG_ASSERT(r == 0);
      /* type */
      r|= op->setValue(SCHEMA_TYPE_I, log_type);
      DBUG_ASSERT(r == 0);
      /* any value */
      Uint32 anyValue = 0;
      if (! thd->slave_thread)
      {
        /* Schema change originating from this MySQLD, check SQL_LOG_BIN
         * variable and pass 'setting' to all logging MySQLDs via AnyValue  
         */
        if (thd_options(thd) & OPTION_BIN_LOG) /* e.g. SQL_LOG_BIN == on */
        {
          DBUG_PRINT("info", ("Schema event for binlogging"));
          ndbcluster_anyvalue_set_normal(anyValue);
        }
        else
        {
          DBUG_PRINT("info", ("Schema event not for binlogging")); 
          ndbcluster_anyvalue_set_nologging(anyValue);
        }
      }
      else
      {
        /* 
           Slave propagating replicated schema event in ndb_schema
           In case replicated serverId is composite 
           (server-id-bits < 31) we copy it into the 
           AnyValue as-is
           This is for 'future', as currently Schema operations
           do not have composite AnyValues.
           In future it may be useful to support *not* mapping composite
           AnyValues to/from Binlogged server-ids.
        */
        DBUG_PRINT("info", ("Replicated schema event with original server id %d",
                            thd->server_id));
        anyValue = thd_unmasked_server_id(thd);
      }

#ifndef DBUG_OFF
      /*
        MySQLD will set the user-portion of AnyValue (if any) to all 1s
        This tests code filtering ServerIds on the value of server-id-bits.
      */
      const char* p = getenv("NDB_TEST_ANYVALUE_USERDATA");
      if (p != 0  && *p != 0 && *p != '0' && *p != 'n' && *p != 'N')
      {
        dbug_ndbcluster_anyvalue_set_userbits(anyValue);
      }
#endif  

      r|= op->setAnyValue(anyValue);
      DBUG_ASSERT(r == 0);
      break;
    }
    if (trans->execute(NdbTransaction::Commit, NdbOperation::DefaultAbortOption,
                       1 /* force send */) == 0)
    {
      DBUG_PRINT("info", ("logged: %s", query));
      dict->forceGCPWait(1);
      break;
    }
err:
    const NdbError *this_error= trans ?
      &trans->getNdbError() : &ndb->getNdbError();
    if (this_error->status == NdbError::TemporaryError && !thd->killed)
    {
      if (retries--)
      {
        if (trans)
          ndb->closeTransaction(trans);
        do_retry_sleep(retry_sleep);
        continue; // retry
      }
    }
    ndb_error= this_error;
    break;
  }
end:
  if (ndb_error)
    push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                        ER_GET_ERRMSG, ER(ER_GET_ERRMSG),
                        ndb_error->code,
                        ndb_error->message,
                        "Could not log query '%s' on other mysqld's");
          
  if (trans)
    ndb->closeTransaction(trans);
  ndb->setDatabaseName(save_db);

  if (opt_ndb_extra_logging > 19)
  {
    sql_print_information("NDB: distributed %s.%s(%u/%u) type: %s(%u) query: \'%s\' to %x%x",
                          db,
                          table_name,
                          ndb_table_id,
                          ndb_table_version,
                          get_schema_type_name(log_type),
                          log_type,
                          query,
                          ndb_schema_object->slock_bitmap.bitmap[0],
                          ndb_schema_object->slock_bitmap.bitmap[1]);
  }

  /*
    Wait for other mysqld's to acknowledge the table operation
  */
  if (ndb_error == 0 && !bitmap_is_clear_all(&ndb_schema_object->slock_bitmap))
  {
    int max_timeout= DEFAULT_SYNC_TIMEOUT;
    pthread_mutex_lock(&ndb_schema_object->mutex);
    while (1)
    {
      struct timespec abstime;
      int i;
      int no_storage_nodes= g_ndb_cluster_connection->no_db_nodes();
      set_timespec(abstime, 1);
      int ret= pthread_cond_timedwait(&injector_cond,
                                      &ndb_schema_object->mutex,
                                      &abstime);
      if (thd->killed)
        break;

      /* begin protect ndb_schema_share */
      pthread_mutex_lock(&ndb_schema_share_mutex);
      if (ndb_schema_share == 0)
      {
        pthread_mutex_unlock(&ndb_schema_share_mutex);
        break;
      }
      MY_BITMAP servers;
      bitmap_init(&servers, 0, 256, FALSE);
      bitmap_clear_all(&servers);
      bitmap_set_bit(&servers, node_id); // "we" are always alive
      pthread_mutex_lock(&ndb_schema_share->mutex);
      for (i= 0; i < no_storage_nodes; i++)
      {
        /* remove any unsubscribed from schema_subscribers */
        MY_BITMAP *tmp= &ndb_schema_share->subscriber_bitmap[i];
        bitmap_union(&servers, tmp);
      }
      pthread_mutex_unlock(&ndb_schema_share->mutex);
      pthread_mutex_unlock(&ndb_schema_share_mutex);
      /* end protect ndb_schema_share */

      /* remove any unsubscribed from ndb_schema_object->slock */
      bitmap_intersect(&ndb_schema_object->slock_bitmap, &servers);
      bitmap_free(&servers);

      if (bitmap_is_clear_all(&ndb_schema_object->slock_bitmap))
        break;

      if (ret)
      {
        max_timeout--;
        if (max_timeout == 0)
        {
          sql_print_error("NDB %s: distributing %s timed out. Ignoring...",
                          type_str, ndb_schema_object->key);
          DBUG_ASSERT(false);
          break;
        }
        if (opt_ndb_extra_logging)
          ndb_report_waiting(type_str, max_timeout,
                             "distributing", ndb_schema_object->key,
                             &ndb_schema_object->slock_bitmap);
      }
    }
    pthread_mutex_unlock(&ndb_schema_object->mutex);
  }
  else if (ndb_error)
  {
    sql_print_error("NDB %s: distributing %s err: %u",
                    type_str, ndb_schema_object->key,
                    ndb_error->code);
  }
  else if (opt_ndb_extra_logging > 19)
  {
    sql_print_information("NDB %s: not waiting for distributing %s",
                          type_str, ndb_schema_object->key);
  }

  if (ndb_schema_object)
    ndb_free_schema_object(&ndb_schema_object, FALSE);

  if (opt_ndb_extra_logging > 19)
  {
    sql_print_information("NDB: distribution of %s.%s(%u/%u) type: %s(%u) query: \'%s\'"
                          " - complete!",
                          db,
                          table_name,
                          ndb_table_id,
                          ndb_table_version,
                          get_schema_type_name(log_type),
                          log_type,
                          query);
  }

  if (thd->slave_thread)
    update_slave_api_stats(ndb);

  DBUG_RETURN(0);
}

/*
  Handle _non_ data events from the storage nodes
*/

int
ndb_handle_schema_change(THD *thd, Ndb *is_ndb, NdbEventOperation *pOp,
                         Ndb_event_data *event_data)
{
  DBUG_ENTER("ndb_handle_schema_change");
  NDB_SHARE *share= event_data->share;
  TABLE *shadow_table= event_data->shadow_table;
  const char *tabname= shadow_table->s->table_name.str;
  const char *dbname= shadow_table->s->db.str;
  bool do_close_cached_tables= FALSE;
  bool is_remote_change= !ndb_has_node_id(pOp->getReqNodeId());

  if (pOp->getEventType() == NDBEVENT::TE_ALTER)
  {
    DBUG_RETURN(0);
  }
  DBUG_ASSERT(pOp->getEventType() == NDBEVENT::TE_DROP ||
              pOp->getEventType() == NDBEVENT::TE_CLUSTER_FAILURE);
  {
    Thd_ndb *thd_ndb= get_thd_ndb(thd);
    Ndb *ndb= thd_ndb->ndb;
    NDBDICT *dict= ndb->getDictionary();
    ndb->setDatabaseName(dbname);
    Ndb_table_guard ndbtab_g(dict, tabname);
    const NDBTAB *ev_tab= pOp->getTable();
    const NDBTAB *cache_tab= ndbtab_g.get_table();
    if (cache_tab &&
        cache_tab->getObjectId() == ev_tab->getObjectId() &&
        cache_tab->getObjectVersion() <= ev_tab->getObjectVersion())
      ndbtab_g.invalidate();
  }

  pthread_mutex_lock(&share->mutex);
  DBUG_ASSERT(share->state == NSS_DROPPED || 
              share->op == pOp || share->new_op == pOp);
  if (share->new_op)
  {
    share->new_op= 0;
  }
  if (share->op)
  {
    share->op= 0;
  }
  // either just us or drop table handling as well
      
  /* Signal ha_ndbcluster::delete/rename_table that drop is done */
  pthread_mutex_unlock(&share->mutex);
  (void) pthread_cond_signal(&injector_cond);

  pthread_mutex_lock(&ndbcluster_mutex);
  /* ndb_share reference binlog free */
  DBUG_PRINT("NDB_SHARE", ("%s binlog free  use_count: %u",
                           share->key, share->use_count));
  free_share(&share, TRUE);
  if (is_remote_change && share && share->state != NSS_DROPPED)
  {
    DBUG_PRINT("info", ("remote change"));
    share->state= NSS_DROPPED;
    if (share->use_count != 1)
    {
      /* open handler holding reference */
      /* wait with freeing create ndb_share to below */
      do_close_cached_tables= TRUE;
    }
    else
    {
      /* ndb_share reference create free */
      DBUG_PRINT("NDB_SHARE", ("%s create free  use_count: %u",
                               share->key, share->use_count));
      free_share(&share, TRUE);
      share= 0;
    }
  }
  else
    share= 0;
  pthread_mutex_unlock(&ndbcluster_mutex);

  if (event_data)
  {
    delete event_data;
    pOp->setCustomData(NULL);
  }

  pthread_mutex_lock(&injector_mutex);
  is_ndb->dropEventOperation(pOp);
  pOp= 0;
  pthread_mutex_unlock(&injector_mutex);

  if (do_close_cached_tables)
  {
    TABLE_LIST table_list;
    memset(&table_list, 0, sizeof(table_list));
    table_list.db= (char *)dbname;
    table_list.alias= table_list.table_name= (char *)tabname;
    close_cached_tables(thd, &table_list, FALSE, FALSE, FALSE);
    /* ndb_share reference create free */
    DBUG_PRINT("NDB_SHARE", ("%s create free  use_count: %u",
                             share->key, share->use_count));
    free_share(&share);
  }
  DBUG_RETURN(0);
}

static void ndb_binlog_query(THD *thd, Cluster_schema *schema)
{
  /* any_value == 0 means local cluster sourced change that
   * should be logged
   */
  if (ndbcluster_anyvalue_is_reserved(schema->any_value))
  {
    /* Originating SQL node did not want this query logged */
    if (!ndbcluster_anyvalue_is_nologging(schema->any_value))
      sql_print_warning("NDB: unknown value for binlog signalling 0x%X, "
                        "query not logged",
                        schema->any_value);
    return;
  }
  
  Uint32 queryServerId = ndbcluster_anyvalue_get_serverid(schema->any_value);
  /* 
     Start with serverId as received AnyValue, in case it's a composite
     (server_id_bits < 31).
     This is for 'future', as currently schema ops do not have composite
     AnyValues.
     In future it may be useful to support *not* mapping composite
     AnyValues to/from Binlogged server-ids.
  */
  Uint32 loggedServerId = schema->any_value;

  if (queryServerId)
  {
    /* 
       AnyValue has non-zero serverId, must be a query applied by a slave 
       mysqld.
       TODO : Assert that we are running in the Binlog injector thread?
    */
    if (! g_ndb_log_slave_updates)
    {
      /* This MySQLD does not log slave updates */
      return;
    }
  }
  else
  {
    /* No ServerId associated with this query, mark it as ours */
    ndbcluster_anyvalue_set_serverid(loggedServerId, ::server_id);
  }

  uint32 thd_server_id_save= thd->server_id;
  DBUG_ASSERT(sizeof(thd_server_id_save) == sizeof(thd->server_id));
  char *thd_db_save= thd->db;
  thd->server_id = loggedServerId;
  thd->db= schema->db;
  int errcode = query_error_code(thd, thd->killed == THD::NOT_KILLED);
  thd->binlog_query(THD::STMT_QUERY_TYPE, schema->query,
                    schema->query_length, FALSE,
#ifdef NDB_THD_BINLOG_QUERY_HAS_DIRECT
                    TRUE,
#endif
                    schema->name[0] == 0 || thd->db[0] == 0,
                    errcode);
  thd->server_id= thd_server_id_save;
  thd->db= thd_db_save;
}

static int
ndb_binlog_thread_handle_schema_event(THD *thd, Ndb *s_ndb,
                                      NdbEventOperation *pOp,
                                      List<Cluster_schema> 
                                      *post_epoch_log_list,
                                      List<Cluster_schema> 
                                      *post_epoch_unlock_list,
                                      MEM_ROOT *mem_root)
{
  DBUG_ENTER("ndb_binlog_thread_handle_schema_event");
  Ndb_event_data *event_data= (Ndb_event_data *) pOp->getCustomData();
  NDB_SHARE *tmp_share= event_data->share;
  if (tmp_share && ndb_schema_share == tmp_share)
  {
    NDBEVENT::TableEvent ev_type= pOp->getEventType();
    DBUG_PRINT("enter", ("%s.%s  ev_type: %d",
                         tmp_share->db, tmp_share->table_name, ev_type));
    if (ev_type == NDBEVENT::TE_UPDATE ||
        ev_type == NDBEVENT::TE_INSERT)
    {
      Thd_ndb *thd_ndb= get_thd_ndb(thd);
      Ndb *ndb= thd_ndb->ndb;
      NDBDICT *dict= ndb->getDictionary();
      Thd_ndb_options_guard thd_ndb_options(thd_ndb);
      Cluster_schema *schema= (Cluster_schema *)
        sql_alloc(sizeof(Cluster_schema));
      MY_BITMAP slock;
      bitmap_init(&slock, schema->slock, 8*SCHEMA_SLOCK_SIZE, FALSE);
      uint node_id= g_ndb_cluster_connection->node_id();
      {
        ndbcluster_get_schema(event_data, schema);
        schema->any_value= pOp->getAnyValue();
      }
      enum SCHEMA_OP_TYPE schema_type= (enum SCHEMA_OP_TYPE)schema->type;
      DBUG_PRINT("info",
                 ("%s.%s: log query_length: %d  query: '%s'  type: %d",
                  schema->db, schema->name,
                  schema->query_length, schema->query,
                  schema_type));

      if (opt_ndb_extra_logging > 19)
      {
        sql_print_information("NDB: got schema event on %s.%s(%u/%u) query: '%s' type: %s(%d) node: %u slock: %x%x",
                              schema->db, schema->name,
                              schema->id, schema->version,
                              schema->query,
                              get_schema_type_name(schema_type),
                              schema_type,
                              schema->node_id,
                              slock.bitmap[0], slock.bitmap[1]);
      }

      if ((schema->db[0] == 0) && (schema->name[0] == 0))
        DBUG_RETURN(0);
      switch (schema_type)
      {
      case SOT_CLEAR_SLOCK:
        /*
          handle slock after epoch is completed to ensure that
          schema events get inserted in the binlog after any data
          events
        */
        post_epoch_log_list->push_back(schema, mem_root);
        DBUG_RETURN(0);
      case SOT_ALTER_TABLE_COMMIT:
        // fall through
      case SOT_RENAME_TABLE_PREPARE:
        // fall through
      case SOT_ONLINE_ALTER_TABLE_PREPARE:
        // fall through
      case SOT_ONLINE_ALTER_TABLE_COMMIT:
        post_epoch_log_list->push_back(schema, mem_root);
        post_epoch_unlock_list->push_back(schema, mem_root);
        DBUG_RETURN(0);
        break;
      default:
        break;
      }

      if (schema->node_id != node_id)
      {
        int log_query= 0, post_epoch_unlock= 0;
        char errmsg[MYSQL_ERRMSG_SIZE];

        switch (schema_type)
        {
        case SOT_RENAME_TABLE:
          // fall through
        case SOT_RENAME_TABLE_NEW:
        {
          uint end= my_snprintf(&errmsg[0], MYSQL_ERRMSG_SIZE,
                                "NDB Binlog: Skipping renaming locally "
                                "defined table '%s.%s' from binlog schema "
                                "event '%s' from node %d. ",
                                schema->db, schema->name, schema->query,
                                schema->node_id);
          errmsg[end]= '\0';
        }
        // fall through
        case SOT_DROP_TABLE:
          if (schema_type == SOT_DROP_TABLE)
          {
            uint end= my_snprintf(&errmsg[0], MYSQL_ERRMSG_SIZE,
                                  "NDB Binlog: Skipping dropping locally "
                                  "defined table '%s.%s' from binlog schema "
                                  "event '%s' from node %d. ",
                                  schema->db, schema->name, schema->query,
                                  schema->node_id);
            errmsg[end]= '\0';
          }
          if (! ndbcluster_check_if_local_table(schema->db, schema->name))
          {
            thd_ndb_options.set(TNO_NO_LOCK_SCHEMA_OP);
            const int no_print_error[2]=
              {ER_BAD_TABLE_ERROR, 0}; /* ignore missing table */
            run_query(thd, schema->query,
                      schema->query + schema->query_length,
                      no_print_error);
            /* binlog dropping table after any table operations */
            post_epoch_log_list->push_back(schema, mem_root);
            /* acknowledge this query _after_ epoch completion */
            post_epoch_unlock= 1;
          }
          else
          {
            /* Tables exists as a local table, leave it */
            DBUG_PRINT("info", ("%s", errmsg));
            sql_print_error("%s", errmsg);
            log_query= 1;
          }
          // Fall through
	case SOT_TRUNCATE_TABLE:
        {
          char key[FN_REFLEN + 1];
          build_table_filename(key, sizeof(key) - 1,
                               schema->db, schema->name, "", 0);
          /* ndb_share reference temporary, free below */
          NDB_SHARE *share= get_share(key, 0, FALSE, FALSE);
          if (share)
          {
            DBUG_PRINT("NDB_SHARE", ("%s temporary  use_count: %u",
                                     share->key, share->use_count));
          }
          // invalidation already handled by binlog thread
          if (!share || !share->op)
          {
            {
              ndb->setDatabaseName(schema->db);
              Ndb_table_guard ndbtab_g(dict, schema->name);
              ndbtab_g.invalidate();
            }
            TABLE_LIST table_list;
            memset(&table_list, 0, sizeof(table_list));
            table_list.db= schema->db;
            table_list.alias= table_list.table_name= schema->name;
            close_cached_tables(thd, &table_list, FALSE, FALSE, FALSE);
          }
          /* ndb_share reference temporary free */
          if (share)
          {
            DBUG_PRINT("NDB_SHARE", ("%s temporary free  use_count: %u",
                                     share->key, share->use_count));
            free_share(&share);
          }
        }
        if (schema_type != SOT_TRUNCATE_TABLE)
          break;
        // fall through
        case SOT_CREATE_TABLE:
          thd_ndb_options.set(TNO_NO_LOCK_SCHEMA_OP);
          if (ndbcluster_check_if_local_table(schema->db, schema->name))
          {
            DBUG_PRINT("info", ("NDB Binlog: Skipping locally defined table '%s.%s'",
                                schema->db, schema->name));
            sql_print_error("NDB Binlog: Skipping locally defined table '%s.%s' from "
                            "binlog schema event '%s' from node %d. ",
                            schema->db, schema->name, schema->query,
                            schema->node_id);
          }
          else if (ndb_create_table_from_engine(thd, schema->db, schema->name))
          {
            print_could_not_discover_error(thd, schema);
          }
          log_query= 1;
          break;
        case SOT_DROP_DB:
          /* Drop the database locally if it only contains ndb tables */
          thd_ndb_options.set(TNO_NO_LOCK_SCHEMA_OP);
          if (! ndbcluster_check_if_local_tables_in_db(thd, schema->db))
          {
            const int no_print_error[1]= {0};
            run_query(thd, schema->query,
                      schema->query + schema->query_length,
                      no_print_error);
            /* binlog dropping database after any table operations */
            post_epoch_log_list->push_back(schema, mem_root);
            /* acknowledge this query _after_ epoch completion */
            post_epoch_unlock= 1;
          }
          else
          {
            /* Database contained local tables, leave it */
            sql_print_error("NDB Binlog: Skipping drop database '%s' since it contained local tables "
                            "binlog schema event '%s' from node %d. ",
                            schema->db, schema->query,
                            schema->node_id);
            log_query= 1;
          }
          break;
        case SOT_CREATE_DB:
          if (opt_ndb_extra_logging > 9)
            sql_print_information("SOT_CREATE_DB %s", schema->db);
          
          /* fall through */
        case SOT_ALTER_DB:
        {
          thd_ndb_options.set(TNO_NO_LOCK_SCHEMA_OP);
          const int no_print_error[1]= {0};
          run_query(thd, schema->query,
                    schema->query + schema->query_length,
                    no_print_error);
          log_query= 1;
          break;
        }
        case SOT_CREATE_USER:
        case SOT_DROP_USER:
        case SOT_RENAME_USER:
        case SOT_GRANT:
        case SOT_REVOKE:
        {
          if (opt_ndb_extra_logging > 9)
            sql_print_information("Got dist_priv event: %s, "
                                  "flushing privileges",
                                  get_schema_type_name(schema_type));

          thd_ndb_options.set(TNO_NO_LOCK_SCHEMA_OP);
          const int no_print_error[1]= {0};
          char *cmd= (char *) "flush privileges";
          run_query(thd, cmd,
                    cmd + strlen(cmd),
                    no_print_error);
          log_query= 1;
	  break;
        }
        case SOT_TABLESPACE:
        case SOT_LOGFILE_GROUP:
          log_query= 1;
          break;
        case SOT_ALTER_TABLE_COMMIT:
        case SOT_RENAME_TABLE_PREPARE:
        case SOT_ONLINE_ALTER_TABLE_PREPARE:
        case SOT_ONLINE_ALTER_TABLE_COMMIT:
        case SOT_CLEAR_SLOCK:
          abort();
        }
        if (log_query && ndb_binlog_running)
          ndb_binlog_query(thd, schema);
        /* signal that schema operation has been handled */
        DBUG_DUMP("slock", (uchar*) schema->slock, schema->slock_length);
        if (bitmap_is_set(&slock, node_id))
        {
          if (post_epoch_unlock)
            post_epoch_unlock_list->push_back(schema, mem_root);
          else
            ndbcluster_update_slock(thd, schema->db, schema->name,
                                    schema->id, schema->version);
        }
      }
      DBUG_RETURN(0);
    }
    /*
      the normal case of UPDATE/INSERT has already been handled
    */
    switch (ev_type)
    {
    case NDBEVENT::TE_DELETE:
      // skip
      break;
    case NDBEVENT::TE_CLUSTER_FAILURE:
      if (opt_ndb_extra_logging)
        sql_print_information("NDB Binlog: cluster failure for %s at epoch %u/%u.",
                              ndb_schema_share->key,
                              (uint)(pOp->getGCI() >> 32),
                              (uint)(pOp->getGCI()));
      // fall through
    case NDBEVENT::TE_DROP:
      if (opt_ndb_extra_logging &&
          ndb_binlog_tables_inited && ndb_binlog_running)
        sql_print_information("NDB Binlog: ndb tables initially "
                              "read only on reconnect.");

      /* begin protect ndb_schema_share */
      pthread_mutex_lock(&ndb_schema_share_mutex);
      /* ndb_share reference binlog extra free */
      DBUG_PRINT("NDB_SHARE", ("%s binlog extra free  use_count: %u",
                               ndb_schema_share->key,
                               ndb_schema_share->use_count));
      free_share(&ndb_schema_share);
      ndb_schema_share= 0;
      ndb_binlog_tables_inited= FALSE;
      ndb_binlog_is_ready= FALSE;
      pthread_mutex_unlock(&ndb_schema_share_mutex);
      /* end protect ndb_schema_share */

      close_cached_tables(NULL, NULL, FALSE, FALSE, FALSE);
      // fall through
    case NDBEVENT::TE_ALTER:
      ndb_handle_schema_change(thd, s_ndb, pOp, event_data);
      break;
    case NDBEVENT::TE_NODE_FAILURE:
    {
      uint8 node_id= g_node_id_map[pOp->getNdbdNodeId()];
      DBUG_ASSERT(node_id != 0xFF);
      pthread_mutex_lock(&tmp_share->mutex);
      bitmap_clear_all(&tmp_share->subscriber_bitmap[node_id]);
      DBUG_PRINT("info",("NODE_FAILURE UNSUBSCRIBE[%d]", node_id));
      if (opt_ndb_extra_logging)
      {
        sql_print_information("NDB Binlog: Node: %d, down,"
                              " Subscriber bitmask %x%x",
                              pOp->getNdbdNodeId(),
                              tmp_share->subscriber_bitmap[node_id].bitmap[1],
                              tmp_share->subscriber_bitmap[node_id].bitmap[0]);
      }
      pthread_mutex_unlock(&tmp_share->mutex);
      (void) pthread_cond_signal(&injector_cond);
      break;
    }
    case NDBEVENT::TE_SUBSCRIBE:
    {
      uint8 node_id= g_node_id_map[pOp->getNdbdNodeId()];
      uint8 req_id= pOp->getReqNodeId();
      DBUG_ASSERT(req_id != 0 && node_id != 0xFF);
      pthread_mutex_lock(&tmp_share->mutex);
      bitmap_set_bit(&tmp_share->subscriber_bitmap[node_id], req_id);
      DBUG_PRINT("info",("SUBSCRIBE[%d] %d", node_id, req_id));
      if (opt_ndb_extra_logging)
      {
        sql_print_information("NDB Binlog: Node: %d, subscribe from node %d,"
                              " Subscriber bitmask %x%x",
                              pOp->getNdbdNodeId(),
                              req_id,
                              tmp_share->subscriber_bitmap[node_id].bitmap[1],
                              tmp_share->subscriber_bitmap[node_id].bitmap[0]);
      }
      pthread_mutex_unlock(&tmp_share->mutex);
      (void) pthread_cond_signal(&injector_cond);
      break;
    }
    case NDBEVENT::TE_UNSUBSCRIBE:
    {
      uint8 node_id= g_node_id_map[pOp->getNdbdNodeId()];
      uint8 req_id= pOp->getReqNodeId();
      DBUG_ASSERT(req_id != 0 && node_id != 0xFF);
      pthread_mutex_lock(&tmp_share->mutex);
      bitmap_clear_bit(&tmp_share->subscriber_bitmap[node_id], req_id);
      DBUG_PRINT("info",("UNSUBSCRIBE[%d] %d", node_id, req_id));
      if (opt_ndb_extra_logging)
      {
        sql_print_information("NDB Binlog: Node: %d, unsubscribe from node %d,"
                              " Subscriber bitmask %x%x",
                              pOp->getNdbdNodeId(),
                              req_id,
                              tmp_share->subscriber_bitmap[node_id].bitmap[1],
                              tmp_share->subscriber_bitmap[node_id].bitmap[0]);
      }
      pthread_mutex_unlock(&tmp_share->mutex);
      (void) pthread_cond_signal(&injector_cond);
      break;
    }
    default:
      sql_print_error("NDB Binlog: unknown non data event %d for %s. "
                      "Ignoring...", (unsigned) ev_type, tmp_share->key);
    }
  }
  DBUG_RETURN(0);
}

/*
  process any operations that should be done after
  the epoch is complete
*/
static void
ndb_binlog_thread_handle_schema_event_post_epoch(THD *thd,
                                                 List<Cluster_schema>
                                                 *post_epoch_log_list,
                                                 List<Cluster_schema>
                                                 *post_epoch_unlock_list)
{
  if (post_epoch_log_list->elements == 0)
    return;
  DBUG_ENTER("ndb_binlog_thread_handle_schema_event_post_epoch");
  Cluster_schema *schema;
  Thd_ndb *thd_ndb= get_thd_ndb(thd);
  Ndb *ndb= thd_ndb->ndb;
  NDBDICT *dict= ndb->getDictionary();
  while ((schema= post_epoch_log_list->pop()))
  {
    Thd_ndb_options_guard thd_ndb_options(thd_ndb);
    DBUG_PRINT("info",
               ("%s.%s: log query_length: %d  query: '%s'  type: %d",
                schema->db, schema->name,
                schema->query_length, schema->query,
                schema->type));
    int log_query= 0;
    {
      enum SCHEMA_OP_TYPE schema_type= (enum SCHEMA_OP_TYPE)schema->type;
      char key[FN_REFLEN + 1];
      build_table_filename(key, sizeof(key) - 1, schema->db, schema->name, "", 0);
      if (schema_type == SOT_CLEAR_SLOCK)
      {
        pthread_mutex_lock(&ndbcluster_mutex);
        NDB_SCHEMA_OBJECT *ndb_schema_object=
          (NDB_SCHEMA_OBJECT*) my_hash_search(&ndb_schema_objects,
                                              (const uchar*) key, strlen(key));
        if (ndb_schema_object &&
            (ndb_schema_object->table_id == schema->id &&
             ndb_schema_object->table_version == schema->version))
        {
          pthread_mutex_lock(&ndb_schema_object->mutex);
          if (opt_ndb_extra_logging > 19)
          {
            sql_print_information("NDB: CLEAR_SLOCK key: %s(%u/%u) from"
                                  " %x%x to %x%x",
                                  key, schema->id, schema->version,
                                  ndb_schema_object->slock[0],
                                  ndb_schema_object->slock[1],
                                  schema->slock[0],
                                  schema->slock[1]);
          }
          memcpy(ndb_schema_object->slock, schema->slock,
                 sizeof(ndb_schema_object->slock));
          DBUG_DUMP("ndb_schema_object->slock_bitmap.bitmap",
                    (uchar*)ndb_schema_object->slock_bitmap.bitmap,
                    no_bytes_in_map(&ndb_schema_object->slock_bitmap));
          pthread_mutex_unlock(&ndb_schema_object->mutex);
          pthread_cond_signal(&injector_cond);
        }
        else if (opt_ndb_extra_logging > 19)
        {
          if (ndb_schema_object == 0)
          {
            sql_print_information("NDB: Discarding event...no obj: %s (%u/%u)",
                                  key, schema->id, schema->version);
          }
          else
          {
            sql_print_information("NDB: Discarding event...key: %s "
                                  "non matching id/version [%u/%u] != [%u/%u]",
                                  key,
                                  ndb_schema_object->table_id,
                                  ndb_schema_object->table_version,
                                  schema->id,
                                  schema->version);
          }
        }
        pthread_mutex_unlock(&ndbcluster_mutex);
        continue;
      }
      /* ndb_share reference temporary, free below */
      NDB_SHARE *share= get_share(key, 0, FALSE, FALSE);
      if (share)
      {
        DBUG_PRINT("NDB_SHARE", ("%s temporary  use_count: %u",
                                 share->key, share->use_count));
      }
      switch (schema_type)
      {
      case SOT_DROP_DB:
        log_query= 1;
        break;
      case SOT_DROP_TABLE:
        if (opt_ndb_extra_logging > 9)
          sql_print_information("SOT_DROP_TABLE %s.%s", schema->db, schema->name);
        log_query= 1;
        {
          ndb->setDatabaseName(schema->db);
          Ndb_table_guard ndbtab_g(dict, schema->name);
          ndbtab_g.invalidate();
        }
        {
          TABLE_LIST table_list;
          memset(&table_list, 0, sizeof(table_list));
          table_list.db= schema->db;
          table_list.alias= table_list.table_name= schema->name;
          close_cached_tables(thd, &table_list, FALSE, FALSE, FALSE);
        }
        break;
      case SOT_RENAME_TABLE:
        if (opt_ndb_extra_logging > 9)
          sql_print_information("SOT_RENAME_TABLE %s.%s", schema->db, schema->name);
        log_query= 1;
        if (share)
        {
          ndbcluster_rename_share(thd, share);
        }
        break;
      case SOT_RENAME_TABLE_PREPARE:
        if (opt_ndb_extra_logging > 9)
          sql_print_information("SOT_RENAME_TABLE_PREPARE %s.%s -> %s",
                                schema->db, schema->name, schema->query);
        if (share &&
            schema->node_id != g_ndb_cluster_connection->node_id())
          ndbcluster_prepare_rename_share(share, schema->query);
        break;
      case SOT_ALTER_TABLE_COMMIT:
        if (opt_ndb_extra_logging > 9)
          sql_print_information("SOT_ALTER_TABLE_COMMIT %s.%s", schema->db, schema->name);
        if (schema->node_id == g_ndb_cluster_connection->node_id())
          break;
        log_query= 1;
        {
          ndb->setDatabaseName(schema->db);
          Ndb_table_guard ndbtab_g(dict, schema->name);
          ndbtab_g.invalidate();
        }
        {
          TABLE_LIST table_list;
          memset(&table_list, 0, sizeof(table_list));
          table_list.db= schema->db;
          table_list.alias= table_list.table_name= schema->name;
          close_cached_tables(thd, &table_list, FALSE, FALSE, FALSE);
        }
        if (share)
        {
          if (share->op)
          {
            Ndb_event_data *event_data= (Ndb_event_data *) share->op->getCustomData();
            if (event_data)
              delete event_data;
            share->op->setCustomData(NULL);
            {
              Mutex_guard injector_mutex_g(injector_mutex);
              injector_ndb->dropEventOperation(share->op);
            }
            share->op= 0;
            free_share(&share);
          }
          free_share(&share);
        }

        if (share)
        {
          /*
            Free the share pointer early, ndb_create_table_from_engine()
            may delete what share is pointing to as a sideeffect
          */
          DBUG_PRINT("NDB_SHARE", ("%s early free, use_count: %u",
                                   share->key, share->use_count));
          free_share(&share);
          share= 0;
        }

        thd_ndb_options.set(TNO_NO_LOCK_SCHEMA_OP);
        if (ndbcluster_check_if_local_table(schema->db, schema->name) &&
           !Ndb_dist_priv_util::is_distributed_priv_table(schema->db,
                                                          schema->name))
        {
          sql_print_error("NDB Binlog: Skipping locally defined table '%s.%s' "
                          "from binlog schema event '%s' from node %d.",
                          schema->db, schema->name, schema->query,
                          schema->node_id);
        }
        else if (ndb_create_table_from_engine(thd, schema->db, schema->name))
        {
          print_could_not_discover_error(thd, schema);
        }
        break;

      case SOT_ONLINE_ALTER_TABLE_PREPARE:
      {
        if (opt_ndb_extra_logging > 9)
          sql_print_information("SOT_ONLINE_ALTER_TABLE_PREPARE %s.%s", schema->db, schema->name);
        int error= 0;
        ndb->setDatabaseName(schema->db);
        {
          Ndb_table_guard ndbtab_g(dict, schema->name);
          ndbtab_g.get_table();
          ndbtab_g.invalidate();
        }
        Ndb_table_guard ndbtab_g(dict, schema->name);
        const NDBTAB *ndbtab= ndbtab_g.get_table();
        /*
          Refresh local frm file and dictionary cache if
          remote on-line alter table
        */
        TABLE_LIST table_list;
        memset(&table_list, 0, sizeof(table_list));
        table_list.db= (char *)schema->db;
        table_list.alias= table_list.table_name= (char *)schema->name;
        close_cached_tables(thd, &table_list, TRUE, FALSE, FALSE);

        if (schema->node_id != g_ndb_cluster_connection->node_id())
        {
          char key[FN_REFLEN];
          uchar *data= 0, *pack_data= 0;
          size_t length, pack_length;
 
          DBUG_PRINT("info", ("Detected frm change of table %s.%s",
                              schema->db, schema->name));
          log_query= 1;
          build_table_filename(key, FN_LEN-1, schema->db, schema->name, NullS, 0);
          /*
            If the there is no local table shadowing the altered table and 
            it has an frm that is different than the one on disk then 
            overwrite it with the new table definition
          */
          if (!ndbcluster_check_if_local_table(schema->db, schema->name) &&
              readfrm(key, &data, &length) == 0 &&
              packfrm(data, length, &pack_data, &pack_length) == 0 &&
              cmp_frm(ndbtab, pack_data, pack_length))
          {
            DBUG_DUMP("frm", (uchar*) ndbtab->getFrmData(), 
                      ndbtab->getFrmLength());
            my_free((char*)data, MYF(MY_ALLOW_ZERO_PTR));
            data= NULL;
            if ((error= unpackfrm(&data, &length,
                                  (const uchar*) ndbtab->getFrmData())) ||
                (error= writefrm(key, data, length)))
            {
              sql_print_error("NDB: Failed write frm for %s.%s, error %d",
                              schema->db, schema->name, error);
            }
          }
          my_free((char*)data, MYF(MY_ALLOW_ZERO_PTR));
          my_free((char*)pack_data, MYF(MY_ALLOW_ZERO_PTR));
        }
        if (share)
        {
          if (opt_ndb_extra_logging > 9)
            sql_print_information("NDB Binlog: handeling online alter/rename");

          pthread_mutex_lock(&share->mutex);
          ndbcluster_binlog_close_table(thd, share);

          if ((error= ndb_binlog_open_shadow_table(thd, share)))
            sql_print_error("NDB Binlog: Failed to re-open shadow table %s.%s",
                            schema->db, schema->name);
          if (error)
            pthread_mutex_unlock(&share->mutex);
        }
        if (!error && share)
        {
          if (share->event_data->shadow_table->s->primary_key == MAX_KEY)
            share->flags|= NSF_HIDDEN_PK;
          /*
            Refresh share->flags to handle added BLOB columns
          */
          if (share->event_data->shadow_table->s->blob_fields != 0)
            share->flags|= NSF_BLOB_FLAG;

          /*
            Start subscribing to data changes to the new table definition
          */
          String event_name(INJECTOR_EVENT_LEN);
          ndb_rep_event_name(&event_name, schema->db, schema->name,
                             get_binlog_full(share));
          NdbEventOperation *tmp_op= share->op;
          share->new_op= 0;
          share->op= 0;

          if (ndbcluster_create_event_ops(thd, share, ndbtab, event_name.c_ptr()))
          {
            sql_print_error("NDB Binlog:"
                            "FAILED CREATE (DISCOVER) EVENT OPERATIONS Event: %s",
                            event_name.c_ptr());
          }
          else
          {
            share->new_op= share->op;
          }
          share->op= tmp_op;
          pthread_mutex_unlock(&share->mutex);

          if (opt_ndb_extra_logging > 9)
            sql_print_information("NDB Binlog: handeling online alter/rename done");
        }
        break;
      }
      case SOT_ONLINE_ALTER_TABLE_COMMIT:
      {
        if (opt_ndb_extra_logging > 9)
          sql_print_information("SOT_ONLINE_ALTER_TABLE_COMMIT %s.%s", schema->db, schema->name);
        if (share)
        {
          pthread_mutex_lock(&share->mutex);
          if (share->op && share->new_op)
          {
            Ndb_event_data *event_data= (Ndb_event_data *) share->op->getCustomData();
            if (event_data)
              delete event_data;
            share->op->setCustomData(NULL);
            {
              Mutex_guard injector_mutex_g(injector_mutex);
              injector_ndb->dropEventOperation(share->op);
            }
            share->op= share->new_op;
            share->new_op= 0;
            free_share(&share);
          }
          pthread_mutex_unlock(&share->mutex);
        }
        break;
      }
      case SOT_RENAME_TABLE_NEW:
        if (opt_ndb_extra_logging > 9)
          sql_print_information("SOT_RENAME_TABLE_NEW %s.%s", schema->db, schema->name);
        log_query= 1;
        if (ndb_binlog_running && (!share || !share->op))
        {
          /*
            we need to free any share here as command below
            may need to call handle_trailing_share
          */
          if (share)
          {
            /* ndb_share reference temporary free */
            DBUG_PRINT("NDB_SHARE", ("%s temporary free  use_count: %u",
                                     share->key, share->use_count));
            free_share(&share);
            share= 0;
          }
          thd_ndb_options.set(TNO_NO_LOCK_SCHEMA_OP);
          if (ndbcluster_check_if_local_table(schema->db, schema->name))
          {
            DBUG_PRINT("info", ("NDB Binlog: Skipping locally defined table '%s.%s'",
                                schema->db, schema->name));
            sql_print_error("NDB Binlog: Skipping locally defined table '%s.%s' from "
                            "binlog schema event '%s' from node %d. ",
                            schema->db, schema->name, schema->query,
                            schema->node_id);
          }
          else if (ndb_create_table_from_engine(thd, schema->db, schema->name))
          {
            print_could_not_discover_error(thd, schema);
          }
        }
        break;
      default:
        DBUG_ASSERT(FALSE);
      }
      if (share)
      {
        /* ndb_share reference temporary free */
        DBUG_PRINT("NDB_SHARE", ("%s temporary free  use_count: %u",
                                 share->key, share->use_count));
        free_share(&share);
        share= 0;
      }
    }
    if (ndb_binlog_running && log_query)
      ndb_binlog_query(thd, schema);
  }
  while ((schema= post_epoch_unlock_list->pop()))
  {
    ndbcluster_update_slock(thd, schema->db, schema->name,
                            schema->id, schema->version);
  }
  DBUG_VOID_RETURN;
}

/*
  Timer class for doing performance measurements
*/

/*********************************************************************
  Internal helper functions for handeling of the cluster replication tables
  - ndb_binlog_index
  - ndb_apply_status
*********************************************************************/

/*
  struct to hold the data to be inserted into the
  ndb_binlog_index table
*/
struct ndb_binlog_index_row {
  ulonglong epoch;
  const char *master_log_file;
  ulonglong master_log_pos;
  ulong n_inserts;
  ulong n_updates;
  ulong n_deletes;
  ulong n_schemaops;

  ulong orig_server_id;
  ulonglong orig_epoch;

  ulong gci;

  struct ndb_binlog_index_row *next;
};


/*
  Open the ndb_binlog_index table for writing
*/
static int
ndb_binlog_index_table__open(THD *thd,
                             TABLE **ndb_binlog_index)
{
  const char *save_proc_info=
    thd_proc_info(thd, "Opening " NDB_REP_DB "." NDB_REP_TABLE);

  TABLE_LIST tables;
  tables.init_one_table(STRING_WITH_LEN(NDB_REP_DB),    // db
                        STRING_WITH_LEN(NDB_REP_TABLE), // name
                        NDB_REP_TABLE,                  // alias
                        TL_WRITE);                      // for write

  /* Only allow real table to be opened */
  tables.required_type= FRMTYPE_TABLE;

  const bool derived = false;
  const uint flags =
    MYSQL_LOCK_IGNORE_TIMEOUT; /* Wait for lock "infinitely" */
  if (open_and_lock_tables(thd, &tables, derived, flags))
  {
    if (thd->killed)
      sql_print_error("NDB Binlog: Opening ndb_binlog_index: killed");
    else
      sql_print_error("NDB Binlog: Opening ndb_binlog_index: %d, '%s'",
                      thd->get_stmt_da()->sql_errno(),
                      thd->get_stmt_da()->message());
    thd_proc_info(thd, save_proc_info);
    return -1;
  }
  *ndb_binlog_index= tables.table;
  thd_proc_info(thd, save_proc_info);
  return 0;
}


/*
  Write rows to the ndb_binlog_index table
*/
static int
ndb_binlog_index_table__write_rows(THD *thd,
                                   ndb_binlog_index_row *row)
{
  int error= 0;
  ndb_binlog_index_row *first= row;
  TABLE *ndb_binlog_index= 0;

  /*
    Assume this function is not called with an error set in thd
    (but clear for safety in release version)
   */
  assert(!thd->is_error());
  thd->clear_error();

  /*
    Turn of binlogging to prevent the table changes to be written to
    the binary log.
  */
  tmp_disable_binlog(thd);

  if (ndb_binlog_index_table__open(thd, &ndb_binlog_index))
  {
    sql_print_error("NDB Binlog: Unable to open ndb_binlog_index table");
    error= -1;
    goto add_ndb_binlog_index_err;
  }

  // Set all columns to be written
  ndb_binlog_index->use_all_columns();

  do
  {
    ulonglong epoch= 0, orig_epoch= 0;
    uint orig_server_id= 0;

    // Intialize ndb_binlog_index->record[0]
    empty_record(ndb_binlog_index);

    ndb_binlog_index->field[0]->store(first->master_log_pos, true);
    ndb_binlog_index->field[1]->store(first->master_log_file,
                                      strlen(first->master_log_file),
                                      &my_charset_bin);
    ndb_binlog_index->field[2]->store(epoch= first->epoch, true);
    if (ndb_binlog_index->s->fields > 7)
    {
      ndb_binlog_index->field[3]->store(row->n_inserts, true);
      ndb_binlog_index->field[4]->store(row->n_updates, true);
      ndb_binlog_index->field[5]->store(row->n_deletes, true);
      ndb_binlog_index->field[6]->store(row->n_schemaops, true);
      ndb_binlog_index->field[7]->store(orig_server_id= row->orig_server_id, true);
      ndb_binlog_index->field[8]->store(orig_epoch= row->orig_epoch, true);
      ndb_binlog_index->field[9]->store(first->gci, true);
      row= row->next;
    }
    else
    {
      while ((row= row->next))
      {
        first->n_inserts+= row->n_inserts;
        first->n_updates+= row->n_updates;
        first->n_deletes+= row->n_deletes;
        first->n_schemaops+= row->n_schemaops;
      }
      ndb_binlog_index->field[3]->store((ulonglong)first->n_inserts, true);
      ndb_binlog_index->field[4]->store((ulonglong)first->n_updates, true);
      ndb_binlog_index->field[5]->store((ulonglong)first->n_deletes, true);
      ndb_binlog_index->field[6]->store((ulonglong)first->n_schemaops, true);
    }

    if ((error= ndb_binlog_index->file->ha_write_row(ndb_binlog_index->record[0])))
    {
      char tmp[128];
      if (ndb_binlog_index->s->fields > 7)
        my_snprintf(tmp, sizeof(tmp), "%u/%u,%u,%u/%u",
                    uint(epoch >> 32), uint(epoch),
                    orig_server_id,
                    uint(orig_epoch >> 32), uint(orig_epoch));

      else
        my_snprintf(tmp, sizeof(tmp), "%u/%u", uint(epoch >> 32), uint(epoch));
      sql_print_error("NDB Binlog: Writing row (%s) to ndb_binlog_index: %d",
                      tmp, error);
      error= -1;
      goto add_ndb_binlog_index_err;
    }
  } while (row);

add_ndb_binlog_index_err:
  /*
    Explicitly commit or rollback the writes(although we normally
    use a non transactional engine for the ndb_binlog_index table)
  */
  thd->get_stmt_da()->set_overwrite_status(true);
  thd->is_error() ? trans_rollback_stmt(thd) : trans_commit_stmt(thd);
  thd->get_stmt_da()->set_overwrite_status(false);

  // Close the tables this thread has opened
  close_thread_tables(thd);

  /*
    There should be no need for rolling back transaction due to deadlock
    (since ndb_binlog_index is non transactional).
  */
  DBUG_ASSERT(! thd->transaction_rollback_request);

  // Release MDL locks on the opened table
  thd->mdl_context.release_transactional_locks();

  reenable_binlog(thd);
  return error;
}

/*********************************************************************
  Functions for start, stop, wait for ndbcluster binlog thread
*********************************************************************/

pthread_handler_t ndb_binlog_thread_func(void *arg);

int ndbcluster_binlog_start()
{
  DBUG_ENTER("ndbcluster_binlog_start");

  if (::server_id == 0)
  {
    sql_print_warning("NDB: server id set to zero - changes logged to "
                      "bin log with server id zero will be logged with "
                      "another server id by slave mysqlds");
  }

  /* 
     Check that ServerId is not using the reserved bit or bits reserved
     for application use
  */
  if ((::server_id & 0x1 << 31) ||                             // Reserved bit
      !ndbcluster_anyvalue_is_serverid_in_range(::server_id))  // server_id_bits
  {
    sql_print_error("NDB: server id provided is too large to be represented in "
                    "opt_server_id_bits or is reserved");
    DBUG_RETURN(-1);
  }

  pthread_mutex_init(&injector_mutex, MY_MUTEX_INIT_FAST);
  pthread_cond_init(&injector_cond, NULL);
  pthread_mutex_init(&ndb_schema_share_mutex, MY_MUTEX_INIT_FAST);

  /* Create injector thread */
  if (pthread_create(&ndb_binlog_thread, &connection_attrib,
                     ndb_binlog_thread_func, 0))
  {
    DBUG_PRINT("error", ("Could not create ndb injector thread"));
    pthread_cond_destroy(&injector_cond);
    pthread_mutex_destroy(&injector_mutex);
    DBUG_RETURN(-1);
  }

  ndbcluster_binlog_inited= 1;

  /* Wait for the injector thread to start */
  pthread_mutex_lock(&injector_mutex);
  while (!ndb_binlog_thread_running)
    pthread_cond_wait(&injector_cond, &injector_mutex);
  pthread_mutex_unlock(&injector_mutex);

  if (ndb_binlog_thread_running < 0)
    DBUG_RETURN(-1);

  DBUG_RETURN(0);
}


/**************************************************************
  Internal helper functions for creating/dropping ndb events
  used by the client sql threads
**************************************************************/
void
ndb_rep_event_name(String *event_name,const char *db, const char *tbl,
                   my_bool full)
{
  if (full)
    event_name->set_ascii("REPLF$", 6);
  else
    event_name->set_ascii("REPL$", 5);
  event_name->append(db);
#ifdef NDB_WIN32
  /*
   * Some bright spark decided that we should sometimes have backslashes.
   * This causes us pain as the event is db/table and not db\table so trying
   * to drop db\table when we meant db/table ends in the event lying around
   * after drop table, leading to all sorts of pain.
  */
  String backslash_sep(1);
  backslash_sep.set_ascii("\\",1);

  int bsloc;
  if((bsloc= event_name->strstr(backslash_sep,0))!=-1)
	  event_name->replace(bsloc, 1, "/", 1);
#endif
  if (tbl)
  {
    event_name->append('/');
    event_name->append(tbl);
  }
  DBUG_PRINT("info", ("ndb_rep_event_name: %s", event_name->c_ptr()));
}

#ifdef HAVE_NDB_BINLOG
static void 
set_binlog_flags(NDB_SHARE *share,
                 Ndb_binlog_type ndb_binlog_type)
{
  DBUG_ENTER("set_binlog_flags");
  switch (ndb_binlog_type)
  {
  case NBT_NO_LOGGING:
    DBUG_PRINT("info", ("NBT_NO_LOGGING"));
    set_binlog_nologging(share);
    DBUG_VOID_RETURN;
  case NBT_DEFAULT:
    DBUG_PRINT("info", ("NBT_DEFAULT"));
    if (opt_ndb_log_updated_only)
    {
      set_binlog_updated_only(share);
    }
    else
    {
      set_binlog_full(share);
    }
    if (opt_ndb_log_update_as_write)
    {
      set_binlog_use_write(share);
    }
    else
    {
      set_binlog_use_update(share);
    }
    break;
  case NBT_UPDATED_ONLY:
    DBUG_PRINT("info", ("NBT_UPDATED_ONLY"));
    set_binlog_updated_only(share);
    set_binlog_use_write(share);
    break;
  case NBT_USE_UPDATE:
    DBUG_PRINT("info", ("NBT_USE_UPDATE"));
  case NBT_UPDATED_ONLY_USE_UPDATE:
    DBUG_PRINT("info", ("NBT_UPDATED_ONLY_USE_UPDATE"));
    set_binlog_updated_only(share);
    set_binlog_use_update(share);
    break;
  case NBT_FULL:
    DBUG_PRINT("info", ("NBT_FULL"));
    set_binlog_full(share);
    set_binlog_use_write(share);
    break;
  case NBT_FULL_USE_UPDATE:
    DBUG_PRINT("info", ("NBT_FULL_USE_UPDATE"));
    set_binlog_full(share);
    set_binlog_use_update(share);
    break;
  }
  set_binlog_logging(share);
  DBUG_VOID_RETURN;
}


inline void slave_reset_conflict_fn(NDB_SHARE *share)
{
  NDB_CONFLICT_FN_SHARE *cfn_share= share->m_cfn_share;
  if (cfn_share)
  {
    memset(cfn_share, 0, sizeof(*cfn_share));
  }
}

static uint
slave_check_resolve_col_type(const NDBTAB *ndbtab,
                             uint field_index)
{
  DBUG_ENTER("slave_check_resolve_col_type");
  const NDBCOL *c= ndbtab->getColumn(field_index);
  uint sz= 0;
  switch (c->getType())
  {
  case  NDBCOL::Unsigned:
    sz= sizeof(Uint32);
    DBUG_PRINT("info", ("resolve column Uint32 %u",
                        field_index));
    break;
  case  NDBCOL::Bigunsigned:
    sz= sizeof(Uint64);
    DBUG_PRINT("info", ("resolve column Uint64 %u",
                        field_index));
    break;
  default:
    DBUG_PRINT("info", ("resolve column %u has wrong type",
                        field_index));
    break;
  }
  DBUG_RETURN(sz);
}

static int
slave_set_resolve_fn(THD *thd, NDB_SHARE *share,
                     const NDBTAB *ndbtab, uint field_index,
                     uint resolve_col_sz,
                     const st_conflict_fn_def* conflict_fn,
                     TABLE *table,
                     uint8 flags)
{
  DBUG_ENTER("slave_set_resolve_fn");

  Thd_ndb *thd_ndb= get_thd_ndb(thd);
  Ndb *ndb= thd_ndb->ndb;
  NDBDICT *dict= ndb->getDictionary();
  NDB_CONFLICT_FN_SHARE *cfn_share= share->m_cfn_share;
  if (cfn_share == NULL)
  {
    share->m_cfn_share= cfn_share= (NDB_CONFLICT_FN_SHARE*)
      alloc_root(&share->mem_root, sizeof(NDB_CONFLICT_FN_SHARE));
    slave_reset_conflict_fn(share);
  }
  cfn_share->m_conflict_fn= conflict_fn;

  /* Calculate resolve col stuff (if relevant) */
  cfn_share->m_resolve_size= resolve_col_sz;
  cfn_share->m_resolve_column= field_index;
  cfn_share->m_resolve_offset= (uint16)(table->field[field_index]->ptr -
                                        table->record[0]);
  cfn_share->m_flags = flags;

  {
    /* get exceptions table */
    char ex_tab_name[FN_REFLEN];
    strxnmov(ex_tab_name, sizeof(ex_tab_name), share->table_name,
             lower_case_table_names ? NDB_EXCEPTIONS_TABLE_SUFFIX_LOWER :
             NDB_EXCEPTIONS_TABLE_SUFFIX, NullS);
    ndb->setDatabaseName(share->db);
    Ndb_table_guard ndbtab_g(dict, ex_tab_name);
    const NDBTAB *ex_tab= ndbtab_g.get_table();
    if (ex_tab)
    {
      const int fixed_cols= 4;
      bool ok=
        ex_tab->getNoOfColumns() >= fixed_cols &&
        ex_tab->getNoOfPrimaryKeys() == 4 &&
        /* server id */
        ex_tab->getColumn(0)->getType() == NDBCOL::Unsigned &&
        ex_tab->getColumn(0)->getPrimaryKey() &&
        /* master_server_id */
        ex_tab->getColumn(1)->getType() == NDBCOL::Unsigned &&
        ex_tab->getColumn(1)->getPrimaryKey() &&
        /* master_epoch */
        ex_tab->getColumn(2)->getType() == NDBCOL::Bigunsigned &&
        ex_tab->getColumn(2)->getPrimaryKey() &&
        /* count */
        ex_tab->getColumn(3)->getType() == NDBCOL::Unsigned &&
        ex_tab->getColumn(3)->getPrimaryKey();
      if (ok)
      {
        int ncol= ndbtab->getNoOfColumns();
        int nkey= ndbtab->getNoOfPrimaryKeys();
        int i, k;
        for (i= k= 0; i < ncol && k < nkey; i++)
        {
          const NdbDictionary::Column* col= ndbtab->getColumn(i);
          if (col->getPrimaryKey())
          {
            const NdbDictionary::Column* ex_col=
              ex_tab->getColumn(fixed_cols + k);
            ok=
              ex_col != NULL &&
              col->getType() == ex_col->getType() &&
              col->getLength() == ex_col->getLength() &&
              col->getNullable() == ex_col->getNullable();
            if (!ok)
              break;
            cfn_share->m_offset[k]=
              (uint16)(table->field[i]->ptr - table->record[0]);
            k++;
          }
        }
        if (ok)
        {
          cfn_share->m_ex_tab= ex_tab;
          cfn_share->m_pk_cols= nkey;
          ndbtab_g.release();
          if (opt_ndb_extra_logging)
            sql_print_information("NDB Slave: Table %s.%s logging exceptions to %s.%s",
                                  table->s->db.str,
                                  table->s->table_name.str,
                                  table->s->db.str,
                                  ex_tab_name);
        }
        else
          sql_print_warning("NDB Slave: exceptions table %s has wrong "
                            "definition (column %d)",
                            ex_tab_name, fixed_cols + k);
      }
      else
        sql_print_warning("NDB Slave: exceptions table %s has wrong "
                          "definition (initial %d columns)",
                          ex_tab_name, fixed_cols);
    }
  }
  DBUG_RETURN(0);
}

/**
  CFT_NDB_OLD

  To perform conflict detection, an interpreted program is used to read
  the timestamp stored locally and compare to what was on the master.
  If timestamp is not equal, an error for this operation (9998) will be raised,
  and new row will not be applied. The error codes for the operations will
  be checked on return.  For this to work is is vital that the operation
  is run with ignore error option.

  As an independent feature, phase 2 also saves the
  conflicts into the table's exceptions table.
*/
int
row_conflict_fn_old(NDB_CONFLICT_FN_SHARE* cfn_share,
                    enum_conflicting_op_type op_type,
                    const uchar* old_data,
                    const uchar* new_data,
                    const MY_BITMAP* write_set,
                    NdbInterpretedCode* code)
{
  DBUG_ENTER("row_conflict_fn_old");
  uint32 resolve_column= cfn_share->m_resolve_column;
  uint32 resolve_size= cfn_share->m_resolve_size;
  const uchar* field_ptr = old_data + cfn_share->m_resolve_offset;

  assert((resolve_size == 4) || (resolve_size == 8));

  if (unlikely(!bitmap_is_set(write_set, resolve_column)))
  {
    sql_print_information("NDB Slave: missing data for %s",
                          cfn_share->m_conflict_fn->name);
    DBUG_RETURN(1);
  }

  const uint label_0= 0;
  const Uint32 RegOldValue= 1, RegCurrentValue= 2;
  int r;

  DBUG_PRINT("info",
             ("Adding interpreted filter, existing value must eq event old value"));
  /*
   * read old value from record
   */
  union {
    uint32 old_value_32;
    uint64 old_value_64;
  };
  {
    if (resolve_size == 4)
    {
      memcpy(&old_value_32, field_ptr, resolve_size);
      DBUG_PRINT("info", ("  old_value_32: %u", old_value_32));
    }
    else
    {
      memcpy(&old_value_64, field_ptr, resolve_size);
      DBUG_PRINT("info", ("  old_value_64: %llu",
                          (unsigned long long) old_value_64));
    }
  }

  /*
   * Load registers RegOldValue and RegCurrentValue
   */
  if (resolve_size == 4)
    r= code->load_const_u32(RegOldValue, old_value_32);
  else
    r= code->load_const_u64(RegOldValue, old_value_64);
  DBUG_ASSERT(r == 0);
  r= code->read_attr(RegCurrentValue, resolve_column);
  DBUG_ASSERT(r == 0);
  /*
   * if RegOldValue == RegCurrentValue goto label_0
   * else raise error for this row
   */
  r= code->branch_eq(RegOldValue, RegCurrentValue, label_0);
  DBUG_ASSERT(r == 0);
  r= code->interpret_exit_nok(error_conflict_fn_violation);
  DBUG_ASSERT(r == 0);
  r= code->def_label(label_0);
  DBUG_ASSERT(r == 0);
  r= code->interpret_exit_ok();
  DBUG_ASSERT(r == 0);
  r= code->finalise();
  DBUG_ASSERT(r == 0);
  DBUG_RETURN(r);
}

int
row_conflict_fn_max_update_only(NDB_CONFLICT_FN_SHARE* cfn_share,
                                enum_conflicting_op_type op_type,
                                const uchar* old_data,
                                const uchar* new_data,
                                const MY_BITMAP* write_set,
                                NdbInterpretedCode* code)
{
  DBUG_ENTER("row_conflict_fn_max_update_only");
  uint32 resolve_column= cfn_share->m_resolve_column;
  uint32 resolve_size= cfn_share->m_resolve_size;
  const uchar* field_ptr = new_data + cfn_share->m_resolve_offset;

  assert((resolve_size == 4) || (resolve_size == 8));

  if (unlikely(!bitmap_is_set(write_set, resolve_column)))
  {
    sql_print_information("NDB Slave: missing data for %s",
                          cfn_share->m_conflict_fn->name);
    DBUG_RETURN(1);
  }

  const uint label_0= 0;
  const Uint32 RegNewValue= 1, RegCurrentValue= 2;
  int r;

  DBUG_PRINT("info",
             ("Adding interpreted filter, existing value must be lt event new"));
  /*
   * read new value from record
   */
  union {
    uint32 new_value_32;
    uint64 new_value_64;
  };
  {
    if (resolve_size == 4)
    {
      memcpy(&new_value_32, field_ptr, resolve_size);
      DBUG_PRINT("info", ("  new_value_32: %u", new_value_32));
    }
    else
    {
      memcpy(&new_value_64, field_ptr, resolve_size);
      DBUG_PRINT("info", ("  new_value_64: %llu",
                          (unsigned long long) new_value_64));
    }
  }
  /*
   * Load registers RegNewValue and RegCurrentValue
   */
  if (resolve_size == 4)
    r= code->load_const_u32(RegNewValue, new_value_32);
  else
    r= code->load_const_u64(RegNewValue, new_value_64);
  DBUG_ASSERT(r == 0);
  r= code->read_attr(RegCurrentValue, resolve_column);
  DBUG_ASSERT(r == 0);
  /*
   * if RegNewValue > RegCurrentValue goto label_0
   * else raise error for this row
   */
  r= code->branch_gt(RegNewValue, RegCurrentValue, label_0);
  DBUG_ASSERT(r == 0);
  r= code->interpret_exit_nok(error_conflict_fn_violation);
  DBUG_ASSERT(r == 0);
  r= code->def_label(label_0);
  DBUG_ASSERT(r == 0);
  r= code->interpret_exit_ok();
  DBUG_ASSERT(r == 0);
  r= code->finalise();
  DBUG_ASSERT(r == 0);
  DBUG_RETURN(r);
}

/**
  CFT_NDB_MAX

  To perform conflict resolution, an interpreted program is used to read
  the timestamp stored locally and compare to what is going to be applied.
  If timestamp is lower, an error for this operation (9999) will be raised,
  and new row will not be applied. The error codes for the operations will
  be checked on return.  For this to work is is vital that the operation
  is run with ignore error option.

  Note that for delete, this algorithm reverts to the OLD algorithm.
*/
int
row_conflict_fn_max(NDB_CONFLICT_FN_SHARE* cfn_share,
                    enum_conflicting_op_type op_type,
                    const uchar* old_data,
                    const uchar* new_data,
                    const MY_BITMAP* write_set,
                    NdbInterpretedCode* code)
{
  switch(op_type)
  {
  case WRITE_ROW:
    abort();
    return 1;
  case UPDATE_ROW:
    return row_conflict_fn_max_update_only(cfn_share,
                                           op_type,
                                           old_data,
                                           new_data,
                                           write_set,
                                           code);
  case DELETE_ROW:
    /* Can't use max of new image, as there's no new image
     * for DELETE
     * Use OLD instead
     */
    return row_conflict_fn_old(cfn_share,
                               op_type,
                               old_data,
                               new_data,
                               write_set,
                               code);
  default:
    abort();
    return 1;
  }
}


/**
  CFT_NDB_MAX_DEL_WIN

  To perform conflict resolution, an interpreted program is used to read
  the timestamp stored locally and compare to what is going to be applied.
  If timestamp is lower, an error for this operation (9999) will be raised,
  and new row will not be applied. The error codes for the operations will
  be checked on return.  For this to work is is vital that the operation
  is run with ignore error option.

  In this variant, replicated DELETEs alway succeed - no filter is added
  to them.
*/

int
row_conflict_fn_max_del_win(NDB_CONFLICT_FN_SHARE* cfn_share,
                            enum_conflicting_op_type op_type,
                            const uchar* old_data,
                            const uchar* new_data,
                            const MY_BITMAP* write_set,
                            NdbInterpretedCode* code)
{
  switch(op_type)
  {
  case WRITE_ROW:
    abort();
    return 1;
  case UPDATE_ROW:
    return row_conflict_fn_max_update_only(cfn_share,
                                           op_type,
                                           old_data,
                                           new_data,
                                           write_set,
                                           code);
  case DELETE_ROW:
    /* This variant always lets a received DELETE_ROW
     * succeed.
     */
    return 1;
  default:
    abort();
    return 1;
  }
};


/**
  CFT_NDB_EPOCH

*/

int
row_conflict_fn_epoch(NDB_CONFLICT_FN_SHARE* cfn_share,
                      enum_conflicting_op_type op_type,
                      const uchar* old_data,
                      const uchar* new_data,
                      const MY_BITMAP* write_set,
                      NdbInterpretedCode* code)
{
  DBUG_ENTER("row_conflict_fn_epoch");
  switch(op_type)
  {
  case WRITE_ROW:
    abort();
    DBUG_RETURN(1);
  case UPDATE_ROW:
  case DELETE_ROW:
  {
    const uint label_0= 0;
    const Uint32
      RegAuthor= 1, RegZero= 2,
      RegMaxRepEpoch= 1, RegRowEpoch= 2;
    int r;

    r= code->load_const_u32(RegZero, 0);
    assert(r == 0);
    r= code->read_attr(RegAuthor, NdbDictionary::Column::ROW_AUTHOR);
    assert(r == 0);
    /* If last author was not local, assume no conflict */
    r= code->branch_ne(RegZero, RegAuthor, label_0);
    assert(r == 0);

    /*
     * Load registers RegMaxRepEpoch and RegRowEpoch
     */
    r= code->load_const_u64(RegMaxRepEpoch, g_ndb_slave_state.max_rep_epoch);
    assert(r == 0);
    r= code->read_attr(RegRowEpoch, NdbDictionary::Column::ROW_GCI64);
    assert(r == 0);

    /*
     * if RegRowEpoch <= RegMaxRepEpoch goto label_0
     * else raise error for this row
     */
    r= code->branch_le(RegRowEpoch, RegMaxRepEpoch, label_0);
    assert(r == 0);
    r= code->interpret_exit_nok(error_conflict_fn_violation);
    assert(r == 0);
    r= code->def_label(label_0);
    assert(r == 0);
    r= code->interpret_exit_ok();
    assert(r == 0);
    r= code->finalise();
    assert(r == 0);
    DBUG_RETURN(r);
  }
  default:
    abort();
    DBUG_RETURN(1);
  }
};

static const st_conflict_fn_arg_def resolve_col_args[]=
{
  /* Arg type              Optional */
  { CFAT_COLUMN_NAME,      false },
  { CFAT_END,              false }
};

static const st_conflict_fn_arg_def epoch_fn_args[]=
{
  /* Arg type              Optional */
  { CFAT_EXTRA_GCI_BITS,   true  },
  { CFAT_END,              false }
};

static const st_conflict_fn_def conflict_fns[]=
{
  { "NDB$MAX_DELETE_WIN", CFT_NDB_MAX_DEL_WIN,
    &resolve_col_args[0], row_conflict_fn_max_del_win },
  { "NDB$MAX",            CFT_NDB_MAX,
    &resolve_col_args[0], row_conflict_fn_max         },
  { "NDB$OLD",            CFT_NDB_OLD,
    &resolve_col_args[0], row_conflict_fn_old         },
  { "NDB$EPOCH",          CFT_NDB_EPOCH,
    &epoch_fn_args[0],    row_conflict_fn_epoch       }
};

static unsigned n_conflict_fns=
  sizeof(conflict_fns) / sizeof(struct st_conflict_fn_def);


int
parse_conflict_fn_spec(const char* conflict_fn_spec,
                       const st_conflict_fn_def** conflict_fn,
                       st_conflict_fn_arg* args,
                       Uint32* max_args,
                       const TABLE* table,
                       char *msg, uint msg_len)
{
  DBUG_ENTER("parse_conflict_fn_spec");

  Uint32 no_args = 0;
  const char *ptr= conflict_fn_spec;
  const char *error_str= "unknown conflict resolution function";
  /* remove whitespace */
  while (*ptr == ' ' && *ptr != '\0') ptr++;

  DBUG_PRINT("info", ("parsing %s", conflict_fn_spec));

  for (unsigned i= 0; i < n_conflict_fns; i++)
  {
    const st_conflict_fn_def &fn= conflict_fns[i];

    uint len= strlen(fn.name);
    if (strncmp(ptr, fn.name, len))
      continue;

    DBUG_PRINT("info", ("found function %s", fn.name));

    /* skip function name */
    ptr+= len;

    /* remove whitespace */
    while (*ptr == ' ' && *ptr != '\0') ptr++;

    /* next '(' */
    if (*ptr != '(')
    {
      error_str= "missing '('";
      DBUG_PRINT("info", ("parse error %s", error_str));
      break;
    }
    ptr++;

    /* find all arguments */
    for (;;)
    {
      if (no_args >= *max_args)
      {
        error_str= "too many arguments";
        DBUG_PRINT("info", ("parse error %s", error_str));
        break;
      }

      /* expected type */
      enum enum_conflict_fn_arg_type type=
        conflict_fns[i].arg_defs[no_args].arg_type;

      /* remove whitespace */
      while (*ptr == ' ' && *ptr != '\0') ptr++;

      if (type == CFAT_END)
      {
        args[no_args].type= type;
        error_str= NULL;
        break;
      }

      /* arg */
      /* Todo : Should support comma as an arg separator? */
      const char *start_arg= ptr;
      while (*ptr != ')' && *ptr != ' ' && *ptr != '\0') ptr++;
      const char *end_arg= ptr;

      bool optional_arg = conflict_fns[i].arg_defs[no_args].optional;
      /* any arg given? */
      if (start_arg == end_arg)
      {
        if (!optional_arg)
        {
          error_str= "missing function argument";
          DBUG_PRINT("info", ("parse error %s", error_str));
          break;
        }
        else
        {
          /* Arg was optional, and not present
           * Must be at end of args, finish parsing
           */
          args[no_args].type= CFAT_END;
          error_str= NULL;
          break;
        }
      }

      uint len= end_arg - start_arg;
      args[no_args].type=    type;
      args[no_args].ptr=     start_arg;
      args[no_args].len=     len;
      args[no_args].fieldno= (uint32)-1;
 
      DBUG_PRINT("info", ("found argument %s %u", start_arg, len));

      bool arg_processing_error = false;
      switch (type)
      {
      case CFAT_COLUMN_NAME:
      {
        /* find column in table */
        DBUG_PRINT("info", ("searching for %s %u", start_arg, len));
        TABLE_SHARE *table_s= table->s;
        for (uint j= 0; j < table_s->fields; j++)
        {
          Field *field= table_s->field[j];
          if (strncmp(start_arg, field->field_name, len) == 0 &&
              field->field_name[len] == '\0')
          {
            DBUG_PRINT("info", ("found %s", field->field_name));
            args[no_args].fieldno= j;
            break;
          }
        }
        break;
      }
      case CFAT_EXTRA_GCI_BITS:
      {
        /* Map string to number and check it's in range etc */
        char* end_of_arg = (char*) end_arg;
        Uint32 bits = strtoul(start_arg, &end_of_arg, 0);
        DBUG_PRINT("info", ("Using %u as the number of extra bits", bits));

        if (bits > 31)
        {
          arg_processing_error= true;
          error_str= "Too many extra Gci bits";
          DBUG_PRINT("info", ("%s", error_str));
          break;
        }
        /* Num bits seems ok */
        args[no_args].extraGciBits = bits;
        break;
      }
      case CFAT_END:
        abort();
      }

      if (arg_processing_error)
        break;
      no_args++;
    }

    if (error_str)
      break;

    /* remove whitespace */
    while (*ptr == ' ' && *ptr != '\0') ptr++;

    /* next ')' */
    if (*ptr != ')')
    {
      error_str= "missing ')'";
      break;
    }
    ptr++;

    /* remove whitespace */
    while (*ptr == ' ' && *ptr != '\0') ptr++;

    /* garbage in the end? */
    if (*ptr != '\0')
    {
      error_str= "garbage in the end";
      break;
    }

    /* Update ptrs to conflict fn + # of args */
    *conflict_fn = &conflict_fns[i];
    *max_args = no_args;

    DBUG_RETURN(0);
  }
  /* parse error */
  my_snprintf(msg, msg_len, "%s, %s at '%s'",
           conflict_fn_spec, error_str, ptr);
  DBUG_PRINT("info", ("%s", msg));
  DBUG_RETURN(-1);
}

static int
setup_conflict_fn(THD *thd, NDB_SHARE *share,
                  const NDBTAB *ndbtab,
                  char *msg, uint msg_len,
                  TABLE *table,
                  const st_conflict_fn_def* conflict_fn,
                  const st_conflict_fn_arg* args,
                  const Uint32 num_args)
{
  DBUG_ENTER("setup_conflict_fn");

  /* setup the function */
  switch (conflict_fn->type)
  {
  case CFT_NDB_MAX:
  case CFT_NDB_OLD:
  case CFT_NDB_MAX_DEL_WIN:
  {
    if (num_args != 1)
    {
      my_snprintf(msg, msg_len,
                  "Incorrect arguments to conflict function");
      DBUG_PRINT("info", ("%s", msg));
      DBUG_RETURN(-1);
    }

    uint resolve_col_sz= 0;

    if (0 == (resolve_col_sz =
              slave_check_resolve_col_type(ndbtab, args[0].fieldno)))
    {
      /* wrong data type */
      slave_reset_conflict_fn(share);
      my_snprintf(msg, msg_len,
                  "column '%s' has wrong datatype",
                  table->s->field[args[0].fieldno]->field_name);
      DBUG_PRINT("info", ("%s", msg));
      DBUG_RETURN(-1);
    }

    if (slave_set_resolve_fn(thd, share, ndbtab,
                             args[0].fieldno, resolve_col_sz,
                             conflict_fn, table, CFF_NONE))
    {
      my_snprintf(msg, msg_len,
                  "unable to setup conflict resolution using column '%s'",
                  table->s->field[args[0].fieldno]->field_name);
      DBUG_PRINT("info", ("%s", msg));
      DBUG_RETURN(-1);
    }
    if (opt_ndb_extra_logging)
    {
       sql_print_information("NDB Slave: Table %s.%s using conflict_fn %s on attribute %s.",
                             table->s->db.str,
                             table->s->table_name.str,
                             conflict_fn->name,
                             table->s->field[args[0].fieldno]->field_name);
    }
    break;
  }
  case CFT_NDB_EPOCH:
  {
    if (num_args > 1)
    {
      my_snprintf(msg, msg_len,
                  "Too many arguments to conflict function");
      DBUG_PRINT("info", ("%s", msg));
      DBUG_RETURN(-1);
    }

    /* Check that table doesn't have Blobs as we don't support that */
    if (share->flags & NSF_BLOB_FLAG)
    {
      my_snprintf(msg, msg_len, "Table has Blob column(s), not suitable for NDB$EPOCH.");
      DBUG_PRINT("info", ("%s", msg));
      DBUG_RETURN(-1);
    }

    /* Check that table has required extra meta-columns */
    /* Todo : Could warn if extra gcibits is insufficient to
     * represent SavePeriod/EpochPeriod
     */
    if (ndbtab->getExtraRowGciBits() == 0)
      sql_print_information("Ndb Slave : CFT_NDB_EPOCH, low epoch resolution");

    if (ndbtab->getExtraRowAuthorBits() == 0)
    {
      my_snprintf(msg, msg_len, "No extra row author bits in table.");
      DBUG_PRINT("info", ("%s", msg));
      DBUG_RETURN(-1);
    }

    if (slave_set_resolve_fn(thd, share, ndbtab,
                             0, // field_no
                             0, // resolve_col_sz
                             conflict_fn, table, CFF_REFRESH_ROWS))
    {
      my_snprintf(msg, msg_len,
                  "unable to setup conflict resolution");
      DBUG_PRINT("info", ("%s", msg));
      DBUG_RETURN(-1);
    }
    if (opt_ndb_extra_logging)
    {
      sql_print_information("NDB Slave: Table %s.%s using conflict_fn %s.",
                            table->s->db.str,
                            table->s->table_name.str,
                            conflict_fn->name);
    }
    break;
  }
  case CFT_NUMBER_OF_CFTS:
  case CFT_NDB_UNDEF:
    abort();
  }
  DBUG_RETURN(0);
}

static const char *ndb_rep_db= NDB_REP_DB;
static const char *ndb_replication_table= NDB_REPLICATION_TABLE;
static const char *nrt_db= "db";
static const char *nrt_table_name= "table_name";
static const char *nrt_server_id= "server_id";
static const char *nrt_binlog_type= "binlog_type";
static const char *nrt_conflict_fn= "conflict_fn";

/*
   ndbcluster_read_replication_table

   This function reads the information for the supplied table from
   the mysql.ndb_replication table.
   Where there is no information (or no table), defaults are
   returned.
*/
int
ndbcluster_read_replication_table(THD *thd, Ndb *ndb,
                                  const char* db,
                                  const char* table_name,
                                  uint server_id,
                                  Uint32* binlog_flags,
                                  char** conflict_fn_spec,
                                  char* conflict_fn_buffer,
                                  Uint32 conflict_fn_buffer_len)
{
  DBUG_ENTER("ndbcluster_read_replication_table");
  NdbError ndberror;
  int error= 0;
  const char *error_str= "<none>";

  ndb->setDatabaseName(ndb_rep_db);
  NDBDICT *dict= ndb->getDictionary();
  Ndb_table_guard ndbtab_g(dict, ndb_replication_table);
  const NDBTAB *reptab= ndbtab_g.get_table();
  if (reptab == NULL &&
      (dict->getNdbError().classification == NdbError::SchemaError ||
       dict->getNdbError().code == 4009))
  {
    DBUG_PRINT("info", ("No %s.%s table", ndb_rep_db, ndb_replication_table));
    *binlog_flags= NBT_DEFAULT;
    *conflict_fn_spec= NULL;
    DBUG_RETURN(0);
  }
  const NDBCOL
    *col_db, *col_table_name, *col_server_id, *col_binlog_type, *col_conflict_fn;
  char tmp_buf[FN_REFLEN];
  uint retries= 100;
  int retry_sleep= 30; /* 30 milliseconds, transaction */
  if (reptab == NULL)
  {
    ndberror= dict->getNdbError();
    goto err;
  }
  if (reptab->getNoOfPrimaryKeys() != 3)
  {
    error= -2;
    error_str= "Wrong number of primary key parts, expected 3";
    goto err;
  }
  error= -1;
  col_db= reptab->getColumn(error_str= nrt_db);
  if (col_db == NULL ||
      !col_db->getPrimaryKey() ||
      col_db->getType() != NDBCOL::Varbinary)
    goto err;
  col_table_name= reptab->getColumn(error_str= nrt_table_name);
  if (col_table_name == NULL ||
      !col_table_name->getPrimaryKey() ||
      col_table_name->getType() != NDBCOL::Varbinary)
    goto err;
  col_server_id= reptab->getColumn(error_str= nrt_server_id);
  if (col_server_id == NULL ||
      !col_server_id->getPrimaryKey() ||
      col_server_id->getType() != NDBCOL::Unsigned)
    goto err;
  col_binlog_type= reptab->getColumn(error_str= nrt_binlog_type);
  if (col_binlog_type == NULL ||
      col_binlog_type->getPrimaryKey() ||
      col_binlog_type->getType() != NDBCOL::Unsigned)
    goto err;
  col_conflict_fn= reptab->getColumn(error_str= nrt_conflict_fn);
  if (col_conflict_fn == NULL)
  {
    col_conflict_fn= NULL;
  }
  else if (col_conflict_fn->getPrimaryKey() ||
           col_conflict_fn->getType() != NDBCOL::Varbinary)
    goto err;

  error= 0;
  for (;;)
  {
    NdbTransaction *trans= ndb->startTransaction();
    if (trans == NULL)
    {
      ndberror= ndb->getNdbError();
      break;
    }
    NdbRecAttr *col_binlog_type_rec_attr[2];
    NdbRecAttr *col_conflict_fn_rec_attr[2]= {NULL, NULL};
    uint32 ndb_binlog_type[2];
    const uint sz= 256;
    char ndb_conflict_fn_buf[2*sz];
    char *ndb_conflict_fn[2]= {ndb_conflict_fn_buf, ndb_conflict_fn_buf+sz};
    NdbOperation *op[2];
    uint32 i, id= 0;
    /* Read generic row (server_id==0) and specific row (server_id == our id)
     * from ndb_replication.
     * Specific overrides generic, if present
     */
    for (i= 0; i < 2; i++)
    {
      NdbOperation *_op;
      DBUG_PRINT("info", ("reading[%u]: %s,%s,%u", i, db, table_name, id));
      if ((_op= trans->getNdbOperation(reptab)) == NULL) abort();
      if (_op->readTuple(NdbOperation::LM_CommittedRead)) abort();
      ndb_pack_varchar(col_db, tmp_buf, db, strlen(db));
      if (_op->equal(col_db->getColumnNo(), tmp_buf)) abort();
      ndb_pack_varchar(col_table_name, tmp_buf, table_name, strlen(table_name));
      if (_op->equal(col_table_name->getColumnNo(), tmp_buf)) abort();
      if (_op->equal(col_server_id->getColumnNo(), id)) abort();
      if ((col_binlog_type_rec_attr[i]=
           _op->getValue(col_binlog_type, (char *)&(ndb_binlog_type[i]))) == 0) abort();
      /* optional columns */
      if (col_conflict_fn)
      {
        if ((col_conflict_fn_rec_attr[i]=
             _op->getValue(col_conflict_fn, ndb_conflict_fn[i])) == 0) abort();
      }
      id= server_id;
      op[i]= _op;
    }

    if (trans->execute(NdbTransaction::Commit,
                       NdbOperation::AO_IgnoreError))
    {
      if (ndb->getNdbError().status == NdbError::TemporaryError)
      {
        if (retries--)
        {
          if (trans)
            ndb->closeTransaction(trans);
          do_retry_sleep(retry_sleep);
          continue;
        }
      }
      ndberror= trans->getNdbError();
      ndb->closeTransaction(trans);
      break;
    }
    for (i= 0; i < 2; i++)
    {
      if (op[i]->getNdbError().code)
      {
        if (op[i]->getNdbError().classification == NdbError::NoDataFound)
        {
          col_binlog_type_rec_attr[i]= NULL;
          col_conflict_fn_rec_attr[i]= NULL;
          DBUG_PRINT("info", ("not found row[%u]", i));
          continue;
        }
        ndberror= op[i]->getNdbError();
        break;
      }
      DBUG_PRINT("info", ("found row[%u]", i));
    }
    if (col_binlog_type_rec_attr[1] == NULL ||
        col_binlog_type_rec_attr[1]->isNULL())
    {
      /* No specific value, use generic */
      col_binlog_type_rec_attr[1]= col_binlog_type_rec_attr[0];
      ndb_binlog_type[1]= ndb_binlog_type[0];
    }
    if (col_conflict_fn_rec_attr[1] == NULL ||
        col_conflict_fn_rec_attr[1]->isNULL())
    {
      /* No specific value, use generic */
      col_conflict_fn_rec_attr[1]= col_conflict_fn_rec_attr[0];
      ndb_conflict_fn[1]= ndb_conflict_fn[0];
    }

    if (col_binlog_type_rec_attr[1] == NULL ||
        col_binlog_type_rec_attr[1]->isNULL())
    {
      DBUG_PRINT("info", ("No binlog flag value, using default"));
      /* No value */
      *binlog_flags= NBT_DEFAULT;
    }
    else
    {
      DBUG_PRINT("info", ("Taking binlog flag value from the table"));
      *binlog_flags= (enum Ndb_binlog_type) ndb_binlog_type[1];
    }

    if (col_conflict_fn_rec_attr[1] == NULL ||
        col_conflict_fn_rec_attr[1]->isNULL())
    {
      /* No conflict function */
      *conflict_fn_spec = NULL;
    }
    else
    {
      const char* conflict_fn = ndb_conflict_fn[1];
      uint len= 0;
      switch (col_conflict_fn->getArrayType())
      {
      case NDBCOL::ArrayTypeShortVar:
        len= *(uchar*)conflict_fn;
        conflict_fn++;
        break;
      case NDBCOL::ArrayTypeMediumVar:
        len= uint2korr(conflict_fn);
        conflict_fn+= 2;
        break;
      default:
        abort();
      }
      if ((len + 1) > conflict_fn_buffer_len)
      {
        ndb->closeTransaction(trans);
        error= -2;
        error_str= "Conflict function specification too long.";
        goto err;
      }
      memcpy(conflict_fn_buffer, conflict_fn, len);
      conflict_fn_buffer[len] = '\0';
      *conflict_fn_spec = conflict_fn_buffer;
    }

    DBUG_PRINT("info", ("Retrieved Binlog flags : %u and function spec : %s",
                        *binlog_flags, (*conflict_fn_spec != NULL ?*conflict_fn_spec:
                                       "NULL")));

    ndb->closeTransaction(trans);

    DBUG_RETURN(0);
  }

err:
  DBUG_PRINT("info", ("error %d, error_str %s, ndberror.code %u",
                      error, error_str, ndberror.code));
  if (error < 0)
  {
    char msg[FN_REFLEN];
    switch (error)
    {
      case -1:
        my_snprintf(msg, sizeof(msg),
                 "Missing or wrong type for column '%s'", error_str);
        break;
      case -2:
        my_snprintf(msg, sizeof(msg), "%s", error_str);
        break;
      default:
        abort();
    }
    push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                        ER_NDB_REPLICATION_SCHEMA_ERROR,
                        ER(ER_NDB_REPLICATION_SCHEMA_ERROR),
                        msg);
  }
  else
  {
    char msg[FN_REFLEN];
    my_snprintf(tmp_buf, sizeof(tmp_buf), "ndberror %u", ndberror.code);
    my_snprintf(msg, sizeof(msg), "Unable to retrieve %s.%s, logging and "
             "conflict resolution may not function as intended (%s)",
             ndb_rep_db, ndb_replication_table, tmp_buf);
    push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                        ER_ILLEGAL_HA_CREATE_OPTION,
                        ER(ER_ILLEGAL_HA_CREATE_OPTION),
                        ndbcluster_hton_name, msg);  
  }
  *binlog_flags= NBT_DEFAULT;
  *conflict_fn_spec= NULL;

  if (ndberror.code && opt_ndb_extra_logging)
    print_warning_list("NDB", thd);
  DBUG_RETURN(ndberror.code);
}

/*
  ndbcluster_get_binlog_replication_info

  This function retrieves the data for the given table
  from the ndb_replication table.

  If the table is not found, or the table does not exist,
  then defaults are returned.
*/
int
ndbcluster_get_binlog_replication_info(THD *thd, Ndb *ndb,
                                       const char* db,
                                       const char* table_name,
                                       uint server_id,
                                       const TABLE *table,
                                       Uint32* binlog_flags,
                                       const st_conflict_fn_def** conflict_fn,
                                       st_conflict_fn_arg* args,
                                       Uint32* num_args)
{
  DBUG_ENTER("ndbcluster_get_binlog_replication_info");

  /* Override for ndb_apply_status when logging */
  if (opt_ndb_log_apply_status)
  {
    if (strcmp(db, NDB_REP_DB) == 0 &&
        strcmp(table_name, NDB_APPLY_TABLE) == 0)
    {
      /*
        Ensure that we get all columns from ndb_apply_status updates
        by forcing FULL event type
        Also, ensure that ndb_apply_status events are always logged as
        WRITES.
      */
      DBUG_PRINT("info", ("ndb_apply_status defaulting to FULL, USE_WRITE"));
      sql_print_information("NDB : ndb-log-apply-status forcing "
                            "%s.%s to FULL USE_WRITE",
                            NDB_REP_DB, NDB_APPLY_TABLE);
      *binlog_flags = NBT_FULL;
      *conflict_fn = NULL;
      *num_args = 0;
      DBUG_RETURN(0);
    }
  }

  const Uint32 MAX_CONFLICT_FN_SPEC_LEN = 256;
  char conflict_fn_buffer[MAX_CONFLICT_FN_SPEC_LEN];
  char* conflict_fn_spec;

  if (ndbcluster_read_replication_table(thd,
                                        ndb,
                                        db,
                                        table_name,
                                        server_id,
                                        binlog_flags,
                                        &conflict_fn_spec,
                                        conflict_fn_buffer,
                                        MAX_CONFLICT_FN_SPEC_LEN) != 0)
  {
    DBUG_RETURN(ER_NDB_REPLICATION_SCHEMA_ERROR);
  }

  if (table != NULL)
  {
    if (conflict_fn_spec != NULL)
    {
      char tmp_buf[FN_REFLEN];

      if (parse_conflict_fn_spec(conflict_fn_spec,
                                 conflict_fn,
                                 args,
                                 num_args,
                                 table,
                                 tmp_buf,
                                 sizeof(tmp_buf)) != 0)
      {
        push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                            ER_CONFLICT_FN_PARSE_ERROR,
                            ER(ER_CONFLICT_FN_PARSE_ERROR),
                            tmp_buf);
        DBUG_RETURN(ER_CONFLICT_FN_PARSE_ERROR);
      }
    }
    else
    {
      /* No conflict function specified */
      conflict_fn= NULL;
      num_args= 0;
    }
  }

  DBUG_RETURN(0);
}

int
ndbcluster_apply_binlog_replication_info(THD *thd,
                                         NDB_SHARE *share,
                                         const NDBTAB* ndbtab,
                                         TABLE* table,
                                         const st_conflict_fn_def* conflict_fn,
                                         const st_conflict_fn_arg* args,
                                         Uint32 num_args,
                                         bool do_set_binlog_flags,
                                         Uint32 binlog_flags)
{
  DBUG_ENTER("ndbcluster_apply_binlog_replication_info");
  char tmp_buf[FN_REFLEN];

  if (do_set_binlog_flags)
  {
    DBUG_PRINT("info", ("Setting binlog flags to %u", binlog_flags));
    set_binlog_flags(share, (enum Ndb_binlog_type)binlog_flags);
  }

  if (conflict_fn != NULL)
  {
    if (setup_conflict_fn(thd, share,
                          ndbtab,
                          tmp_buf, sizeof(tmp_buf),
                          table,
                          conflict_fn,
                          args,
                          num_args) != 0)
    {
      push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                          ER_CONFLICT_FN_PARSE_ERROR,
                          ER(ER_CONFLICT_FN_PARSE_ERROR),
                          tmp_buf);
      DBUG_RETURN(-1);
    }
  }
  else
  {
    /* No conflict function specified */
    slave_reset_conflict_fn(share);
  }

  DBUG_RETURN(0);
}

int
ndbcluster_read_binlog_replication(THD *thd, Ndb *ndb,
                                   NDB_SHARE *share,
                                   const NDBTAB *ndbtab,
                                   uint server_id,
                                   TABLE *table,
                                   bool do_set_binlog_flags)
{
  DBUG_ENTER("ndbcluster_read_binlog_replication");
  Uint32 binlog_flags;
  const st_conflict_fn_def* conflict_fn= NULL;
  st_conflict_fn_arg args[MAX_CONFLICT_ARGS];
  Uint32 num_args = MAX_CONFLICT_ARGS;

  if ((ndbcluster_get_binlog_replication_info(thd, ndb,
                                              share->db,
                                              share->table_name,
                                              server_id,
                                              table,
                                              &binlog_flags,
                                              &conflict_fn,
                                              args,
                                              &num_args) != 0) ||
      (ndbcluster_apply_binlog_replication_info(thd,
                                                share,
                                                ndbtab,
                                                table,
                                                conflict_fn,
                                                args,
                                                num_args,
                                                do_set_binlog_flags,
                                                binlog_flags) != 0))
  {
    DBUG_RETURN(-1);
  }

  DBUG_RETURN(0);
}
#endif /* HAVE_NDB_BINLOG */

bool
ndbcluster_check_if_local_table(const char *dbname, const char *tabname)
{
  char key[FN_REFLEN + 1];
  char ndb_file[FN_REFLEN + 1];

  DBUG_ENTER("ndbcluster_check_if_local_table");
  build_table_filename(key, FN_LEN-1, dbname, tabname, reg_ext, 0);
  build_table_filename(ndb_file, FN_LEN-1, dbname, tabname, ha_ndb_ext, 0);
  /* Check that any defined table is an ndb table */
  DBUG_PRINT("info", ("Looking for file %s and %s", key, ndb_file));
  if ((! my_access(key, F_OK)) && my_access(ndb_file, F_OK))
  {
    DBUG_PRINT("info", ("table file %s not on disk, local table", ndb_file));   
  
  
    DBUG_RETURN(true);
  }

  DBUG_RETURN(false);
}

bool
ndbcluster_check_if_local_tables_in_db(THD *thd, const char *dbname)
{
  DBUG_ENTER("ndbcluster_check_if_local_tables_in_db");
  DBUG_PRINT("info", ("Looking for files in directory %s", dbname));
  LEX_STRING *tabname;
  List<LEX_STRING> files;
  char path[FN_REFLEN + 1];

  build_table_filename(path, sizeof(path) - 1, dbname, "", "", 0);
  if (find_files(thd, &files, dbname, path, NullS, 0) != FIND_FILES_OK)
  {
    thd->clear_error();
    DBUG_PRINT("info", ("Failed to find files"));
    DBUG_RETURN(true);
  }
  DBUG_PRINT("info",("found: %d files", files.elements));
  while ((tabname= files.pop()))
  {
    DBUG_PRINT("info", ("Found table %s", tabname->str));
    if (ndbcluster_check_if_local_table(dbname, tabname->str))
      DBUG_RETURN(true);
  }
  
  DBUG_RETURN(false);
}

/*
  Common function for setting up everything for logging a table at
  create/discover.
*/
int ndbcluster_create_binlog_setup(THD *thd, Ndb *ndb, const char *key,
                                   uint key_len,
                                   const char *db,
                                   const char *table_name,
                                   TABLE * table)
{
  int do_event_op= ndb_binlog_running;
  DBUG_ENTER("ndbcluster_create_binlog_setup");
  DBUG_PRINT("enter",("key: %s  key_len: %d  %s.%s",
                      key, key_len, db, table_name));
  DBUG_ASSERT(! IS_NDB_BLOB_PREFIX(table_name));
  DBUG_ASSERT(strlen(key) == key_len);

  pthread_mutex_lock(&ndbcluster_mutex);
  NDB_SHARE * share = get_share(key, table, TRUE, TRUE);
  if (share == 0)
  {
    /**
     * Failed to create share
     */
    pthread_mutex_unlock(&ndbcluster_mutex);
    DBUG_RETURN(-1);
  }
  pthread_mutex_unlock(&ndbcluster_mutex);

  pthread_mutex_lock(&share->mutex);
  if (get_binlog_nologging(share) || share->op != 0 || share->new_op != 0)
  {
    pthread_mutex_unlock(&share->mutex);
    free_share(&share);
    DBUG_RETURN(0); // replication already setup, or should not
  }

  if (Ndb_dist_priv_util::is_distributed_priv_table(db, table_name))
  {
    // The distributed privilege tables are distributed by writing
    // the CREATE USER, GRANT, REVOKE etc. to ndb_schema -> no need
    // to listen to events from this table
    DBUG_PRINT("info", ("Skipping binlogging of table %s/%s", db, table_name));
    do_event_op= 0;
  }

  if (!ndb_schema_share &&
      strcmp(share->db, NDB_REP_DB) == 0 &&
      strcmp(share->table_name, NDB_SCHEMA_TABLE) == 0)
    do_event_op= 1;
  else if (!ndb_apply_status_share &&
           strcmp(share->db, NDB_REP_DB) == 0 &&
           strcmp(share->table_name, NDB_APPLY_TABLE) == 0)
    do_event_op= 1;

  if (!do_event_op)
  {
    set_binlog_nologging(share);
    pthread_mutex_unlock(&share->mutex);
    DBUG_RETURN(0);
  }

  while (share && !IS_TMP_PREFIX(table_name))
  {
    /*
      ToDo make sanity check of share so that the table is actually the same
      I.e. we need to do open file from frm in this case
      Currently awaiting this to be fixed in the 4.1 tree in the general
      case
    */

    /* Create the event in NDB */
    ndb->setDatabaseName(db);

    NDBDICT *dict= ndb->getDictionary();
    Ndb_table_guard ndbtab_g(dict, table_name);
    const NDBTAB *ndbtab= ndbtab_g.get_table();
    if (ndbtab == 0)
    {
      if (opt_ndb_extra_logging)
        sql_print_information("NDB Binlog: Failed to get table %s from ndb: "
                              "%s, %d", key, dict->getNdbError().message,
                              dict->getNdbError().code);
      break; // error
    }
#ifdef HAVE_NDB_BINLOG
    /*
     */
    ndbcluster_read_binlog_replication(thd, ndb, share, ndbtab,
                                       ::server_id, NULL, TRUE);
#endif
    /*
      check if logging turned off for this table
    */
    if (get_binlog_nologging(share))
    {
      if (opt_ndb_extra_logging)
        sql_print_information("NDB Binlog: NOT logging %s", share->key);
      pthread_mutex_unlock(&share->mutex);
      DBUG_RETURN(0);
    }

    String event_name(INJECTOR_EVENT_LEN);
    ndb_rep_event_name(&event_name, db, table_name, get_binlog_full(share));
    /*
      event should have been created by someone else,
      but let's make sure, and create if it doesn't exist
    */
    const NDBEVENT *ev= dict->getEvent(event_name.c_ptr());
    if (!ev)
    {
      if (ndbcluster_create_event(thd, ndb, ndbtab, event_name.c_ptr(), share))
      {
        sql_print_error("NDB Binlog: "
                        "FAILED CREATE (DISCOVER) TABLE Event: %s",
                        event_name.c_ptr());
        break; // error
      }
      if (opt_ndb_extra_logging)
        sql_print_information("NDB Binlog: "
                              "CREATE (DISCOVER) TABLE Event: %s",
                              event_name.c_ptr());
    }
    else
    {
      delete ev;
      if (opt_ndb_extra_logging)
        sql_print_information("NDB Binlog: DISCOVER TABLE Event: %s",
                              event_name.c_ptr());
    }

    /*
      create the event operations for receiving logging events
    */
    if (ndbcluster_create_event_ops(thd, share,
                                    ndbtab, event_name.c_ptr()))
    {
      sql_print_error("NDB Binlog:"
                      "FAILED CREATE (DISCOVER) EVENT OPERATIONS Event: %s",
                      event_name.c_ptr());
      /* a warning has been issued to the client */
      break;
    }
    pthread_mutex_unlock(&share->mutex);
    DBUG_RETURN(0);
  }

  pthread_mutex_unlock(&share->mutex);
  free_share(&share);
  DBUG_RETURN(-1);
}

int
ndbcluster_create_event(THD *thd, Ndb *ndb, const NDBTAB *ndbtab,
                        const char *event_name, NDB_SHARE *share,
                        int push_warning)
{
  DBUG_ENTER("ndbcluster_create_event");
  DBUG_PRINT("info", ("table=%s version=%d event=%s share=%s",
                      ndbtab->getName(), ndbtab->getObjectVersion(),
                      event_name, share ? share->key : "(nil)"));
  DBUG_ASSERT(! IS_NDB_BLOB_PREFIX(ndbtab->getName()));
  if (!share)
  {
    DBUG_PRINT("info", ("share == NULL"));
    DBUG_RETURN(0);
  }
  if (get_binlog_nologging(share))
  {
    if (opt_ndb_extra_logging && ndb_binlog_running)
      sql_print_information("NDB Binlog: NOT logging %s", share->key);
    DBUG_PRINT("info", ("share->flags & NSF_NO_BINLOG, flags: %x %d",
                        share->flags, share->flags & NSF_NO_BINLOG));
    DBUG_RETURN(0);
  }

  ndb->setDatabaseName(share->db);
  NDBDICT *dict= ndb->getDictionary();
  NDBEVENT my_event(event_name);
  my_event.setTable(*ndbtab);
  my_event.addTableEvent(NDBEVENT::TE_ALL);
  if (share->flags & NSF_HIDDEN_PK)
  {
    if (share->flags & NSF_BLOB_FLAG)
    {
      sql_print_error("NDB Binlog: logging of table %s "
                      "with BLOB attribute and no PK is not supported",
                      share->key);
      if (push_warning)
        push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                            ER_ILLEGAL_HA_CREATE_OPTION,
                            ER(ER_ILLEGAL_HA_CREATE_OPTION),
                            ndbcluster_hton_name,
                            "Binlog of table with BLOB attribute and no PK");

      share->flags|= NSF_NO_BINLOG;
      DBUG_RETURN(-1);
    }
    /* No primary key, subscribe for all attributes */
    my_event.setReport((NDBEVENT::EventReport)
                       (NDBEVENT::ER_ALL | NDBEVENT::ER_DDL));
    DBUG_PRINT("info", ("subscription all"));
  }
  else
  {
    if (strcmp(share->db, NDB_REP_DB) == 0 &&
        strcmp(share->table_name, NDB_SCHEMA_TABLE) == 0)
    {
      /**
       * ER_SUBSCRIBE is only needed on NDB_SCHEMA_TABLE
       */
      my_event.setReport((NDBEVENT::EventReport)
                         (NDBEVENT::ER_ALL |
                          NDBEVENT::ER_SUBSCRIBE |
                          NDBEVENT::ER_DDL));
      DBUG_PRINT("info", ("subscription all and subscribe"));
    }
    else
    {
      if (get_binlog_full(share))
      {
        my_event.setReport((NDBEVENT::EventReport)
                           (NDBEVENT::ER_ALL | NDBEVENT::ER_DDL));
        DBUG_PRINT("info", ("subscription all"));
      }
      else
      {
        my_event.setReport((NDBEVENT::EventReport)
                           (NDBEVENT::ER_UPDATED | NDBEVENT::ER_DDL));
        DBUG_PRINT("info", ("subscription only updated"));
      }
    }
  }
  if (share->flags & NSF_BLOB_FLAG)
    my_event.mergeEvents(TRUE);

  /* add all columns to the event */
  int n_cols= ndbtab->getNoOfColumns();
  for(int a= 0; a < n_cols; a++)
    my_event.addEventColumn(a);

  if (dict->createEvent(my_event)) // Add event to database
  {
    if (dict->getNdbError().classification != NdbError::SchemaObjectExists)
    {
      /*
        failed, print a warning
      */
      if (push_warning > 1)
        push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                            ER_GET_ERRMSG, ER(ER_GET_ERRMSG),
                            dict->getNdbError().code,
                            dict->getNdbError().message, "NDB");
      sql_print_error("NDB Binlog: Unable to create event in database. "
                      "Event: %s  Error Code: %d  Message: %s", event_name,
                      dict->getNdbError().code, dict->getNdbError().message);
      DBUG_RETURN(-1);
    }

    /*
      try retrieving the event, if table version/id matches, we will get
      a valid event.  Otherwise we have a trailing event from before
    */
    const NDBEVENT *ev;
    if ((ev= dict->getEvent(event_name)))
    {
      delete ev;
      DBUG_RETURN(0);
    }

    /*
      trailing event from before; an error, but try to correct it
    */
    if (dict->getNdbError().code == NDB_INVALID_SCHEMA_OBJECT &&
        dict->dropEvent(my_event.getName(), 1))
    {
      if (push_warning > 1)
        push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                            ER_GET_ERRMSG, ER(ER_GET_ERRMSG),
                            dict->getNdbError().code,
                            dict->getNdbError().message, "NDB");
      sql_print_error("NDB Binlog: Unable to create event in database. "
                      " Attempt to correct with drop failed. "
                      "Event: %s Error Code: %d Message: %s",
                      event_name,
                      dict->getNdbError().code,
                      dict->getNdbError().message);
      DBUG_RETURN(-1);
    }

    /*
      try to add the event again
    */
    if (dict->createEvent(my_event))
    {
      if (push_warning > 1)
        push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                            ER_GET_ERRMSG, ER(ER_GET_ERRMSG),
                            dict->getNdbError().code,
                            dict->getNdbError().message, "NDB");
      sql_print_error("NDB Binlog: Unable to create event in database. "
                      " Attempt to correct with drop ok, but create failed. "
                      "Event: %s Error Code: %d Message: %s",
                      event_name,
                      dict->getNdbError().code,
                      dict->getNdbError().message);
      DBUG_RETURN(-1);
    }
#ifdef NDB_BINLOG_EXTRA_WARNINGS
    push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                        ER_GET_ERRMSG, ER(ER_GET_ERRMSG),
                        0, "NDB Binlog: Removed trailing event",
                        "NDB");
#endif
  }

  DBUG_RETURN(0);
}

inline int is_ndb_compatible_type(Field *field)
{
  return
    !(field->flags & BLOB_FLAG) &&
    field->type() != MYSQL_TYPE_BIT &&
    field->pack_length() != 0;
}

/*
  - create eventOperations for receiving log events
  - setup ndb recattrs for reception of log event data
  - "start" the event operation

  used at create/discover of tables
*/
int
ndbcluster_create_event_ops(THD *thd, NDB_SHARE *share,
                            const NDBTAB *ndbtab, const char *event_name)
{
  /*
    we are in either create table or rename table so table should be
    locked, hence we can work with the share without locks
  */

  DBUG_ENTER("ndbcluster_create_event_ops");
  DBUG_PRINT("enter", ("table: %s event: %s", ndbtab->getName(), event_name));
  DBUG_ASSERT(! IS_NDB_BLOB_PREFIX(ndbtab->getName()));

  DBUG_ASSERT(share != 0);

  if (get_binlog_nologging(share))
  {
    DBUG_PRINT("info", ("share->flags & NSF_NO_BINLOG, flags: %x",
                        share->flags));
    DBUG_RETURN(0);
  }

  // Don't allow event ops to be created on distributed priv tables
  // they are distributed via ndb_schema
  assert(!Ndb_dist_priv_util::is_distributed_priv_table(share->db,
                                                        share->table_name));

  Ndb_event_data *event_data= share->event_data;
  int do_ndb_schema_share= 0, do_ndb_apply_status_share= 0;
#ifdef HAVE_NDB_BINLOG
  uint len= strlen(share->table_name);
#endif
  if (!ndb_schema_share && strcmp(share->db, NDB_REP_DB) == 0 &&
      strcmp(share->table_name, NDB_SCHEMA_TABLE) == 0)
    do_ndb_schema_share= 1;
  else if (!ndb_apply_status_share && strcmp(share->db, NDB_REP_DB) == 0 &&
           strcmp(share->table_name, NDB_APPLY_TABLE) == 0)
    do_ndb_apply_status_share= 1;
  else
#ifdef HAVE_NDB_BINLOG
    if (!binlog_filter->db_ok(share->db) ||
        !ndb_binlog_running ||
        (len >= sizeof(NDB_EXCEPTIONS_TABLE_SUFFIX) &&
         strcmp(share->table_name+len-sizeof(NDB_EXCEPTIONS_TABLE_SUFFIX)+1,
                lower_case_table_names ? NDB_EXCEPTIONS_TABLE_SUFFIX_LOWER :
                NDB_EXCEPTIONS_TABLE_SUFFIX) == 0))
#endif
  {
    share->flags|= NSF_NO_BINLOG;
    DBUG_RETURN(0);
  }

  if (share->op)
  {
    event_data= (Ndb_event_data *) share->op->getCustomData();
    assert(event_data->share == share);
    assert(share->event_data == 0);

    DBUG_ASSERT(share->use_count > 1);
    sql_print_error("NDB Binlog: discover reusing old ev op");
    /* ndb_share reference ToDo free */
    DBUG_PRINT("NDB_SHARE", ("%s ToDo free  use_count: %u",
                             share->key, share->use_count));
    free_share(&share); // old event op already has reference
    DBUG_RETURN(0);
  }

  DBUG_ASSERT(event_data != 0);
  TABLE *table= event_data->shadow_table;

  int retries= 100;
  /*
    100 milliseconds, temporary error on schema operation can
    take some time to be resolved
  */
  int retry_sleep= 100;
  while (1)
  {
    Mutex_guard injector_mutex_g(injector_mutex);
    Ndb *ndb= injector_ndb;
    if (do_ndb_schema_share)
      ndb= schema_ndb;

    if (ndb == 0)
      DBUG_RETURN(-1);

    NdbEventOperation* op;
    if (do_ndb_schema_share)
      op= ndb->createEventOperation(event_name);
    else
    {
      // set injector_ndb database/schema from table internal name
      int ret= ndb->setDatabaseAndSchemaName(ndbtab);
      assert(ret == 0); NDB_IGNORE_VALUE(ret);
      op= ndb->createEventOperation(event_name);
      // reset to catch errors
      ndb->setDatabaseName("");
    }
    if (!op)
    {
      sql_print_error("NDB Binlog: Creating NdbEventOperation failed for"
                      " %s",event_name);
      push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                          ER_GET_ERRMSG, ER(ER_GET_ERRMSG),
                          ndb->getNdbError().code,
                          ndb->getNdbError().message,
                          "NDB");
      DBUG_RETURN(-1);
    }

    if (share->flags & NSF_BLOB_FLAG)
      op->mergeEvents(TRUE); // currently not inherited from event

    uint n_columns= ndbtab->getNoOfColumns();
    uint n_fields= table->s->fields;
    uint val_length= sizeof(NdbValue) * n_columns;

    /*
       Allocate memory globally so it can be reused after online alter table
    */
    if (my_multi_malloc(MYF(MY_WME),
                        &event_data->ndb_value[0],
                        val_length,
                        &event_data->ndb_value[1],
                        val_length,
                        NULL) == 0)
    {
      DBUG_PRINT("info", ("Failed to allocate records for event operation"));
      DBUG_RETURN(-1);
    }

    for (uint j= 0; j < n_columns; j++)
    {
      const char *col_name= ndbtab->getColumn(j)->getName();
      NdbValue attr0, attr1;
      if (j < n_fields)
      {
        Field *f= table->field[j];
        if (is_ndb_compatible_type(f))
        {
          DBUG_PRINT("info", ("%s compatible", col_name));
          attr0.rec= op->getValue(col_name, (char*) f->ptr);
          attr1.rec= op->getPreValue(col_name,
                                     (f->ptr - table->record[0]) +
                                     (char*) table->record[1]);
        }
        else if (! (f->flags & BLOB_FLAG))
        {
          DBUG_PRINT("info", ("%s non compatible", col_name));
          attr0.rec= op->getValue(col_name);
          attr1.rec= op->getPreValue(col_name);
        }
        else
        {
          DBUG_PRINT("info", ("%s blob", col_name));
          DBUG_ASSERT(share->flags & NSF_BLOB_FLAG);
          attr0.blob= op->getBlobHandle(col_name);
          attr1.blob= op->getPreBlobHandle(col_name);
          if (attr0.blob == NULL || attr1.blob == NULL)
          {
            sql_print_error("NDB Binlog: Creating NdbEventOperation"
                            " blob field %u handles failed (code=%d) for %s",
                            j, op->getNdbError().code, event_name);
            push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                                ER_GET_ERRMSG, ER(ER_GET_ERRMSG),
                                op->getNdbError().code,
                                op->getNdbError().message,
                                "NDB");
            ndb->dropEventOperation(op);
            DBUG_RETURN(-1);
          }
        }
      }
      else
      {
        DBUG_PRINT("info", ("%s hidden key", col_name));
        attr0.rec= op->getValue(col_name);
        attr1.rec= op->getPreValue(col_name);
      }
      event_data->ndb_value[0][j].ptr= attr0.ptr;
      event_data->ndb_value[1][j].ptr= attr1.ptr;
      DBUG_PRINT("info", ("&event_data->ndb_value[0][%d]: 0x%lx  "
                          "event_data->ndb_value[0][%d]: 0x%lx",
                          j, (long) &event_data->ndb_value[0][j],
                          j, (long) attr0.ptr));
      DBUG_PRINT("info", ("&event_data->ndb_value[1][%d]: 0x%lx  "
                          "event_data->ndb_value[1][%d]: 0x%lx",
                          j, (long) &event_data->ndb_value[0][j],
                          j, (long) attr1.ptr));
    }
    op->setCustomData((void *) event_data); // set before execute
    share->event_data= 0;                   // take over event data
    share->op= op; // assign op in NDB_SHARE

    if (op->execute())
    {
      share->op= NULL;
      retries--;
      if (op->getNdbError().status != NdbError::TemporaryError &&
          op->getNdbError().code != 1407)
        retries= 0;
      if (retries == 0)
      {
        push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                            ER_GET_ERRMSG, ER(ER_GET_ERRMSG), 
                            op->getNdbError().code, op->getNdbError().message,
                            "NDB");
        sql_print_error("NDB Binlog: ndbevent->execute failed for %s; %d %s",
                        event_name,
                        op->getNdbError().code, op->getNdbError().message);
      }
      share->event_data= event_data;
      op->setCustomData(NULL);
      ndb->dropEventOperation(op);
      if (retries && !thd->killed)
      {
        do_retry_sleep(retry_sleep);
        continue;
      }
      DBUG_RETURN(-1);
    }
    break;
  }

  /* ndb_share reference binlog */
  get_share(share);
  DBUG_PRINT("NDB_SHARE", ("%s binlog  use_count: %u",
                           share->key, share->use_count));
  if (do_ndb_apply_status_share)
  {
    /* ndb_share reference binlog extra */
    ndb_apply_status_share= get_share(share);
    DBUG_PRINT("NDB_SHARE", ("%s binlog extra  use_count: %u",
                             share->key, share->use_count));
    (void) pthread_cond_signal(&injector_cond);
  }
  else if (do_ndb_schema_share)
  {
    /* ndb_share reference binlog extra */
    ndb_schema_share= get_share(share);
    DBUG_PRINT("NDB_SHARE", ("%s binlog extra  use_count: %u",
                             share->key, share->use_count));
    (void) pthread_cond_signal(&injector_cond);
  }

  DBUG_PRINT("info",("%s share->op: 0x%lx  share->use_count: %u",
                     share->key, (long) share->op, share->use_count));

  if (opt_ndb_extra_logging)
    sql_print_information("NDB Binlog: logging %s (%s,%s)", share->key,
                          get_binlog_full(share) ? "FULL" : "UPDATED",
                          get_binlog_use_update(share) ? "USE_UPDATE" : "USE_WRITE");
  DBUG_RETURN(0);
}

int
ndbcluster_drop_event(THD *thd, Ndb *ndb, NDB_SHARE *share,
                      const char *type_str,
                      const char *dbname,
                      const char *tabname)
{
  DBUG_ENTER("ndbcluster_drop_event");
  /*
    There might be 2 types of events setup for the table, we cannot know
    which ones are supposed to be there as they may have been created
    differently for different mysqld's.  So we drop both
  */
  for (uint i= 0; i < 2; i++)
  {
    NDBDICT *dict= ndb->getDictionary();
    String event_name(INJECTOR_EVENT_LEN);
    ndb_rep_event_name(&event_name, dbname, tabname, i);
    
    if (!dict->dropEvent(event_name.c_ptr()))
      continue;

    if (dict->getNdbError().code != 4710 &&
        dict->getNdbError().code != 1419)
    {
      /* drop event failed for some reason, issue a warning */
      push_warning_printf(thd, Sql_condition::WARN_LEVEL_WARN,
                          ER_GET_ERRMSG, ER(ER_GET_ERRMSG),
                          dict->getNdbError().code,
                          dict->getNdbError().message, "NDB");
      /* error is not that the event did not exist */
      sql_print_error("NDB Binlog: Unable to drop event in database. "
                      "Event: %s Error Code: %d Message: %s",
                      event_name.c_ptr(),
                      dict->getNdbError().code,
                      dict->getNdbError().message);
      /* ToDo; handle error? */
      if (share && share->op &&
          share->op->getState() == NdbEventOperation::EO_EXECUTING &&
          dict->getNdbError().mysql_code != HA_ERR_NO_CONNECTION)
      {
        DBUG_ASSERT(FALSE);
        DBUG_RETURN(-1);
      }
    }
  }
  DBUG_RETURN(0);
}

/*
  when entering the calling thread should have a share lock id share != 0
  then the injector thread will have  one as well, i.e. share->use_count == 0
  (unless it has already dropped... then share->op == 0)
*/

int
ndbcluster_handle_drop_table(THD *thd, Ndb *ndb, NDB_SHARE *share,
                             const char *type_str,
                             const char * dbname, const char * tabname)
{
  DBUG_ENTER("ndbcluster_handle_drop_table");

  if (dbname && tabname)
  {
    if (ndbcluster_drop_event(thd, ndb, share, type_str, dbname, tabname))
      DBUG_RETURN(-1);
  }

  if (share == 0 || share->op == 0)
  {
    DBUG_RETURN(0);
  }

/*
  Syncronized drop between client thread and injector thread is
  neccessary in order to maintain ordering in the binlog,
  such that the drop occurs _after_ any inserts/updates/deletes.

  The penalty for this is that the drop table becomes slow.

  This wait is however not strictly neccessary to produce a binlog
  that is usable.  However the slave does not currently handle
  these out of order, thus we are keeping the SYNC_DROP_ defined
  for now.
*/
  const char *save_proc_info= thd->proc_info;
#define SYNC_DROP_
#ifdef SYNC_DROP_
  thd->proc_info= "Syncing ndb table schema operation and binlog";
  pthread_mutex_lock(&share->mutex);
  int max_timeout= DEFAULT_SYNC_TIMEOUT;
  while (share->op)
  {
    struct timespec abstime;
    set_timespec(abstime, 1);
    int ret= pthread_cond_timedwait(&injector_cond,
                                    &share->mutex,
                                    &abstime);
    if (thd->killed ||
        share->op == 0)
      break;
    if (ret)
    {
      max_timeout--;
      if (max_timeout == 0)
      {
        sql_print_error("NDB %s: %s timed out. Ignoring...",
                        type_str, share->key);
        DBUG_ASSERT(false);
        break;
      }
      if (opt_ndb_extra_logging)
        ndb_report_waiting(type_str, max_timeout,
                           type_str, share->key, 0);
    }
  }
  pthread_mutex_unlock(&share->mutex);
#else
  pthread_mutex_lock(&share->mutex);
  share->op= 0;
  pthread_mutex_unlock(&share->mutex);
#endif
  thd->proc_info= save_proc_info;

  DBUG_RETURN(0);
}


/********************************************************************
  Internal helper functions for differentd events from the stoarage nodes
  used by the ndb injector thread
********************************************************************/

/*
  Unpack a record read from NDB 

  SYNOPSIS
    ndb_unpack_record()
    buf                 Buffer to store read row

  NOTE
    The data for each row is read directly into the
    destination buffer. This function is primarily 
    called in order to check if any fields should be 
    set to null.
*/

static void ndb_unpack_record(TABLE *table, NdbValue *value,
                              MY_BITMAP *defined, uchar *buf)
{
  Field **p_field= table->field, *field= *p_field;
  my_ptrdiff_t row_offset= (my_ptrdiff_t) (buf - table->record[0]);
  my_bitmap_map *old_map= dbug_tmp_use_all_columns(table, table->write_set);
  DBUG_ENTER("ndb_unpack_record");

  /*
    Set the filler bits of the null byte, since they are
    not touched in the code below.
    
    The filler bits are the MSBs in the last null byte
  */ 
  if (table->s->null_bytes > 0)
       buf[table->s->null_bytes - 1]|= 256U - (1U <<
					       table->s->last_null_bit_pos);
  /*
    Set null flag(s)
  */
  for ( ; field;
       p_field++, value++, field= *p_field)
  {
    field->set_notnull(row_offset);       
    if ((*value).ptr)
    {
      if (!(field->flags & BLOB_FLAG))
      {
        int is_null= (*value).rec->isNULL();
        if (is_null)
        {
          if (is_null > 0)
          {
            DBUG_PRINT("info",("[%u] NULL", field->field_index));
            field->set_null(row_offset);
          }
          else
          {
            DBUG_PRINT("info",("[%u] UNDEFINED", field->field_index));
            bitmap_clear_bit(defined, field->field_index);
          }
        }
        else if (field->type() == MYSQL_TYPE_BIT)
        {
          Field_bit *field_bit= static_cast<Field_bit*>(field);

          /*
            Move internal field pointer to point to 'buf'.  Calling
            the correct member function directly since we know the
            type of the object.
           */
          field_bit->Field_bit::move_field_offset(row_offset);
          if (field->pack_length() < 5)
          {
            DBUG_PRINT("info", ("bit field H'%.8X", 
                                (*value).rec->u_32_value()));
            field_bit->Field_bit::store((longlong) (*value).rec->u_32_value(),
                                        TRUE);
          }
          else
          {
            DBUG_PRINT("info", ("bit field H'%.8X%.8X",
                                *(Uint32 *)(*value).rec->aRef(),
                                *((Uint32 *)(*value).rec->aRef()+1)));
#ifdef WORDS_BIGENDIAN
            /* lsw is stored first */
            Uint32 *buf= (Uint32 *)(*value).rec->aRef();
            field_bit->Field_bit::store((((longlong)*buf)
                                         & 0x00000000FFFFFFFFLL)
                                        |
                                        ((((longlong)*(buf+1)) << 32)
                                         & 0xFFFFFFFF00000000LL),
                                        TRUE);
#else
            field_bit->Field_bit::store((longlong)
                                        (*value).rec->u_64_value(), TRUE);
#endif
          }
          /*
            Move back internal field pointer to point to original
            value (usually record[0]).
           */
          field_bit->Field_bit::move_field_offset(-row_offset);
          DBUG_PRINT("info",("[%u] SET",
                             (*value).rec->getColumn()->getColumnNo()));
          DBUG_DUMP("info", (const uchar*) field->ptr, field->pack_length());
        }
        else
        {
          DBUG_PRINT("info",("[%u] SET",
                             (*value).rec->getColumn()->getColumnNo()));
          DBUG_DUMP("info", (const uchar*) field->ptr, field->pack_length());
        }
      }
      else
      {
        NdbBlob *ndb_blob= (*value).blob;
        uint col_no= field->field_index;
        int isNull;
        ndb_blob->getDefined(isNull);
        if (isNull == 1)
        {
          DBUG_PRINT("info",("[%u] NULL", col_no));
          field->set_null(row_offset);
        }
        else if (isNull == -1)
        {
          DBUG_PRINT("info",("[%u] UNDEFINED", col_no));
          bitmap_clear_bit(defined, col_no);
        }
        else
        {
#ifndef DBUG_OFF
          // pointer vas set in get_ndb_blobs_value
          Field_blob *field_blob= (Field_blob*)field;
          uchar* ptr;
          field_blob->get_ptr(&ptr, row_offset);
          uint32 len= field_blob->get_length(row_offset);
          DBUG_PRINT("info",("[%u] SET ptr: 0x%lx  len: %u",
                             col_no, (long) ptr, len));
#endif
        }
      }
    }
  }
  dbug_tmp_restore_column_map(table->write_set, old_map);
  DBUG_VOID_RETURN;
}

/*
  Handle error states on events from the storage nodes
*/
static int
ndb_binlog_thread_handle_error(Ndb *ndb,
                               NdbEventOperation *pOp)
{
  Ndb_event_data *event_data= (Ndb_event_data *) pOp->getCustomData();
  NDB_SHARE *share= event_data->share;
  DBUG_ENTER("ndb_binlog_thread_handle_error");

  int overrun= pOp->isOverrun();
  if (overrun)
  {
    /*
      ToDo: this error should rather clear the ndb_binlog_index...
      and continue
    */
    sql_print_error("NDB Binlog: Overrun in event buffer, "
                    "this means we have dropped events. Cannot "
                    "continue binlog for %s", share->key);
    pOp->clearError();
    DBUG_RETURN(-1);
  }

  if (!pOp->isConsistent())
  {
    /*
      ToDo: this error should rather clear the ndb_binlog_index...
      and continue
    */
    sql_print_error("NDB Binlog: Not Consistent. Cannot "
                    "continue binlog for %s. Error code: %d"
                    " Message: %s", share->key,
                    pOp->getNdbError().code,
                    pOp->getNdbError().message);
    pOp->clearError();
    DBUG_RETURN(-1);
  }
  sql_print_error("NDB Binlog: unhandled error %d for table %s",
                  pOp->hasError(), share->key);
  pOp->clearError();
  DBUG_RETURN(0);
}

static int
ndb_binlog_thread_handle_non_data_event(THD *thd,
                                        NdbEventOperation *pOp,
                                        ndb_binlog_index_row &row)
{
  Ndb_event_data *event_data= (Ndb_event_data *) pOp->getCustomData();
  NDB_SHARE *share= event_data->share;
  NDBEVENT::TableEvent type= pOp->getEventType();

  switch (type)
  {
  case NDBEVENT::TE_CLUSTER_FAILURE:
    if (opt_ndb_extra_logging)
      sql_print_information("NDB Binlog: cluster failure for %s at epoch %u/%u.",
                            share->key,
                            (uint)(pOp->getGCI() >> 32),
                            (uint)(pOp->getGCI()));
    if (ndb_apply_status_share == share)
    {
      if (opt_ndb_extra_logging &&
          ndb_binlog_tables_inited && ndb_binlog_running)
        sql_print_information("NDB Binlog: ndb tables initially "
                              "read only on reconnect.");
      /* ndb_share reference binlog extra free */
      DBUG_PRINT("NDB_SHARE", ("%s binlog extra free  use_count: %u",
                               share->key, share->use_count));
      free_share(&ndb_apply_status_share);
      ndb_apply_status_share= 0;
      ndb_binlog_tables_inited= FALSE;
    }
    DBUG_PRINT("error", ("CLUSTER FAILURE EVENT: "
                        "%s  received share: 0x%lx  op: 0x%lx  share op: 0x%lx  "
                        "new_op: 0x%lx",
                         share->key, (long) share, (long) pOp,
                         (long) share->op, (long) share->new_op));
    break;
  case NDBEVENT::TE_DROP:
    if (ndb_apply_status_share == share)
    {
      if (opt_ndb_extra_logging &&
          ndb_binlog_tables_inited && ndb_binlog_running)
        sql_print_information("NDB Binlog: ndb tables initially "
                              "read only on reconnect.");
      /* ndb_share reference binlog extra free */
      DBUG_PRINT("NDB_SHARE", ("%s binlog extra free  use_count: %u",
                               share->key, share->use_count));
      free_share(&ndb_apply_status_share);
      ndb_apply_status_share= 0;
      ndb_binlog_tables_inited= FALSE;
    }
    /* ToDo: remove printout */
    if (opt_ndb_extra_logging)
      sql_print_information("NDB Binlog: drop table %s.", share->key);
    // fall through
  case NDBEVENT::TE_ALTER:
    row.n_schemaops++;
    DBUG_PRINT("info", ("TABLE %s  EVENT: %s  received share: 0x%lx  op: 0x%lx  "
                        "share op: 0x%lx  new_op: 0x%lx",
                        type == NDBEVENT::TE_DROP ? "DROP" : "ALTER",
                        share->key, (long) share, (long) pOp,
                        (long) share->op, (long) share->new_op));
    break;
  case NDBEVENT::TE_NODE_FAILURE:
    /* fall through */
  case NDBEVENT::TE_SUBSCRIBE:
    /* fall through */
  case NDBEVENT::TE_UNSUBSCRIBE:
    /* ignore */
    return 0;
  default:
    sql_print_error("NDB Binlog: unknown non data event %d for %s. "
                    "Ignoring...", (unsigned) type, share->key);
    return 0;
  }

  ndb_handle_schema_change(thd, injector_ndb, pOp, event_data);
  return 0;
}

/*
  Handle data events from the storage nodes
*/
inline ndb_binlog_index_row *
ndb_find_binlog_index_row(ndb_binlog_index_row **rows,
                          uint orig_server_id, int flag)
{
  ndb_binlog_index_row *row= *rows;
  if (opt_ndb_log_orig)
  {
    ndb_binlog_index_row *first= row, *found_id= 0;
    for (;;)
    {
      if (row->orig_server_id == orig_server_id)
      {
        /* */
        if (!flag || !row->orig_epoch)
          return row;
        if (!found_id)
          found_id= row;
      }
      if (row->orig_server_id == 0)
        break;
      row= row->next;
      if (row == NULL)
      {
        row= (ndb_binlog_index_row*)sql_alloc(sizeof(ndb_binlog_index_row));
        memset(row, 0, sizeof(ndb_binlog_index_row));
        row->next= first;
        *rows= row;
        if (found_id)
        {
          /*
            If we found index_row with same server id already
            that row will contain the current stats.
            Copy stats over to new and reset old.
          */
          row->n_inserts= found_id->n_inserts;
          row->n_updates= found_id->n_updates;
          row->n_deletes= found_id->n_deletes;
          found_id->n_inserts= 0;
          found_id->n_updates= 0;
          found_id->n_deletes= 0;
        }
        /* keep track of schema ops only on "first" index_row */
        row->n_schemaops= first->n_schemaops;
        first->n_schemaops= 0;
        break;
      }
    }
    row->orig_server_id= orig_server_id;
  }
  return row;
}

static int
ndb_binlog_thread_handle_data_event(Ndb *ndb, NdbEventOperation *pOp,
                                    ndb_binlog_index_row **rows,
                                    injector::transaction &trans,
                                    unsigned &trans_row_count,
                                    unsigned &trans_slave_row_count)
{
  Ndb_event_data *event_data= (Ndb_event_data *) pOp->getCustomData();
  TABLE *table= event_data->shadow_table;
  NDB_SHARE *share= event_data->share;
  if (pOp != share->op)
  {
    return 0;
  }

  uint32 anyValue= pOp->getAnyValue();
  if (ndbcluster_anyvalue_is_reserved(anyValue))
  {
    if (!ndbcluster_anyvalue_is_nologging(anyValue))
      sql_print_warning("NDB: unknown value for binlog signalling 0x%X, "
                        "event not logged",
                        anyValue);
    return 0;
  }
  uint32 originating_server_id= ndbcluster_anyvalue_get_serverid(anyValue);
  bool log_this_slave_update = g_ndb_log_slave_updates;
  bool count_this_event = true;

  if (share == ndb_apply_status_share)
  {
    /* 
       Note that option values are read without synchronisation w.r.t. 
       thread setting option variable or epoch boundaries.
    */
    if (opt_ndb_log_apply_status ||
        opt_ndb_log_orig)
    {
      Uint32 ndb_apply_status_logging_server_id= originating_server_id;
      Uint32 ndb_apply_status_server_id= 0;
      Uint64 ndb_apply_status_epoch= 0;
      bool event_has_data = false;

      switch(pOp->getEventType())
      {
      case NDBEVENT::TE_INSERT:
        // fall through
      case NDBEVENT::TE_UPDATE:
        event_has_data = true;
        break;
      case NDBEVENT::TE_DELETE:
        break;
      default:
        /* We should REALLY never get here */
        abort();
      }
      
      if (likely( event_has_data ))
      {
        /* unpack data to fetch orig_server_id and orig_epoch */
        uint n_fields= table->s->fields;
        MY_BITMAP b;
        uint32 bitbuf[128 / (sizeof(uint32) * 8)];
        bitmap_init(&b, bitbuf, n_fields, FALSE);
        bitmap_set_all(&b);
        ndb_unpack_record(table, event_data->ndb_value[0], &b, table->record[0]);
        ndb_apply_status_server_id= (uint)((Field_long *)table->field[0])->val_int();
        ndb_apply_status_epoch= ((Field_longlong *)table->field[1])->val_int();
        
        if (opt_ndb_log_apply_status)
        {
          /* 
             Determine if event came from our immediate Master server
             Ignore locally manually sourced and reserved events 
          */
          if ((ndb_apply_status_logging_server_id != 0) &&
              (! ndbcluster_anyvalue_is_reserved(ndb_apply_status_logging_server_id)))
          {
            bool isFromImmediateMaster = (ndb_apply_status_server_id ==
                                          ndb_apply_status_logging_server_id);
            
            if (isFromImmediateMaster)
            {
              /* 
                 We log this event with our server-id so that it 
                 propagates back to the originating Master (our
                 immediate Master)
              */
              assert(ndb_apply_status_logging_server_id != ::server_id);
              
              originating_server_id= 0; /* Will be set to our ::serverid below */
            }
          }
        }

        if (opt_ndb_log_orig)
        {
          /* store */
          ndb_binlog_index_row *row= ndb_find_binlog_index_row
            (rows, ndb_apply_status_server_id, 1);
          row->orig_epoch= ndb_apply_status_epoch;
        }
      }
    } // opt_ndb_log_apply_status || opt_ndb_log_orig)

    if (opt_ndb_log_apply_status)
    {
      /* We are logging ndb_apply_status changes
       * Don't count this event as making an epoch non-empty
       * Log this event in the Binlog
       */
      count_this_event = false;
      log_this_slave_update = true;
    }
    else
    {
      /* Not logging ndb_apply_status updates, discard this event now */
      return 0;
    }
  }
  
  if (originating_server_id == 0)
    originating_server_id= ::server_id;
  else 
  {
    /* Track that we received a replicated row event */
    if (likely( count_this_event ))
      trans_slave_row_count++;
    
    if (!log_this_slave_update)
    {
      /*
        This event comes from a slave applier since it has an originating
        server id set. Since option to log slave updates is not set, skip it.
      */
      return 0;
    }
  }

  /* 
     Start with logged_server_id as AnyValue in case it's a composite
     (server_id_bits < 31).  This way any user-values are passed-through
     to the Binlog in the high bits of the event's Server Id.
     In future it may be useful to support *not* mapping composite
     AnyValues to/from Binlogged server-ids.
  */
  uint32 logged_server_id= anyValue;
  ndbcluster_anyvalue_set_serverid(logged_server_id, originating_server_id);

  DBUG_ASSERT(trans.good());
  DBUG_ASSERT(table != 0);

  dbug_print_table("table", table);

  uint n_fields= table->s->fields;
  DBUG_PRINT("info", ("Assuming %u columns for table %s",
                      n_fields, table->s->table_name.str));
  MY_BITMAP b;
  /* Potential buffer for the bitmap */
  uint32 bitbuf[128 / (sizeof(uint32) * 8)];
  const bool own_buffer = n_fields <= sizeof(bitbuf) * 8;
  bitmap_init(&b, own_buffer ? bitbuf : NULL, n_fields, FALSE); 
  bitmap_set_all(&b);

  /*
   row data is already in table->record[0]
   As we told the NdbEventOperation to do this
   (saves moving data about many times)
  */

  /*
    for now malloc/free blobs buffer each time
    TODO if possible share single permanent buffer with handlers
   */
  uchar* blobs_buffer[2] = { 0, 0 };
  uint blobs_buffer_size[2] = { 0, 0 };

  ndb_binlog_index_row *row=
    ndb_find_binlog_index_row(rows, originating_server_id, 0);

  switch(pOp->getEventType())
  {
  case NDBEVENT::TE_INSERT:
    if (likely( count_this_event ))
    {
      row->n_inserts++;
      trans_row_count++;
    }
    DBUG_PRINT("info", ("INSERT INTO %s.%s",
                        table->s->db.str, table->s->table_name.str));
    {
      int ret;
      if (share->flags & NSF_BLOB_FLAG)
      {
        my_ptrdiff_t ptrdiff= 0;
        ret = get_ndb_blobs_value(table, event_data->ndb_value[0],
                                  blobs_buffer[0],
                                  blobs_buffer_size[0],
                                  ptrdiff);
        assert(ret == 0);
      }
      ndb_unpack_record(table, event_data->ndb_value[0], &b, table->record[0]);
      ret = trans.write_row(logged_server_id,
                            injector::transaction::table(table, true),
                            &b, n_fields, table->record[0]);
      assert(ret == 0);
    }
    break;
  case NDBEVENT::TE_DELETE:
    if (likely( count_this_event ))
    {
      row->n_deletes++;
      trans_row_count++;
    }
    DBUG_PRINT("info",("DELETE FROM %s.%s",
                       table->s->db.str, table->s->table_name.str));
    {
      /*
        table->record[0] contains only the primary key in this case
        since we do not have an after image
      */
      int n;
      if (!get_binlog_full(share) && table->s->primary_key != MAX_KEY)
        n= 0; /*
                use the primary key only as it save time and space and
                it is the only thing needed to log the delete
              */
      else
        n= 1; /*
                we use the before values since we don't have a primary key
                since the mysql server does not handle the hidden primary
                key
              */

      int ret;
      if (share->flags & NSF_BLOB_FLAG)
      {
        my_ptrdiff_t ptrdiff= table->record[n] - table->record[0];
        ret = get_ndb_blobs_value(table, event_data->ndb_value[n],
                                  blobs_buffer[n],
                                  blobs_buffer_size[n],
                                  ptrdiff);
        assert(ret == 0);
      }
      ndb_unpack_record(table, event_data->ndb_value[n], &b, table->record[n]);
      DBUG_EXECUTE("info", print_records(table, table->record[n]););
      ret = trans.delete_row(logged_server_id,
                             injector::transaction::table(table, true),
                             &b, n_fields, table->record[n]);
      assert(ret == 0);
    }
    break;
  case NDBEVENT::TE_UPDATE:
    if (likely( count_this_event ))
    {
      row->n_updates++;
      trans_row_count++;
    }
    DBUG_PRINT("info", ("UPDATE %s.%s",
                        table->s->db.str, table->s->table_name.str));
    {
      int ret;
      if (share->flags & NSF_BLOB_FLAG)
      {
        my_ptrdiff_t ptrdiff= 0;
        ret = get_ndb_blobs_value(table, event_data->ndb_value[0],
                                  blobs_buffer[0],
                                  blobs_buffer_size[0],
                                  ptrdiff);
        assert(ret == 0);
      }
      ndb_unpack_record(table, event_data->ndb_value[0],
                        &b, table->record[0]);
      DBUG_EXECUTE("info", print_records(table, table->record[0]););
      if (table->s->primary_key != MAX_KEY &&
          !get_binlog_use_update(share)) 
      {
        /*
          since table has a primary key, we can do a write
          using only after values
        */
        ret = trans.write_row(logged_server_id,
                              injector::transaction::table(table, true),
                              &b, n_fields, table->record[0]);// after values
        assert(ret == 0);
      }
      else
      {
        /*
          mysql server cannot handle the ndb hidden key and
          therefore needs the before image as well
        */
        if (share->flags & NSF_BLOB_FLAG)
        {
          my_ptrdiff_t ptrdiff= table->record[1] - table->record[0];
          ret = get_ndb_blobs_value(table, event_data->ndb_value[1],
                                    blobs_buffer[1],
                                    blobs_buffer_size[1],
                                    ptrdiff);
          assert(ret == 0);
        }
        ndb_unpack_record(table, event_data->ndb_value[1], &b, table->record[1]);
        DBUG_EXECUTE("info", print_records(table, table->record[1]););
        ret = trans.update_row(logged_server_id,
                               injector::transaction::table(table, true),
                               &b, n_fields,
                               table->record[1], // before values
                               table->record[0]);// after values
        assert(ret == 0);
      }
    }
    break;
  default:
    /* We should REALLY never get here. */
    DBUG_PRINT("info", ("default - uh oh, a brain exploded."));
    break;
  }

  if (share->flags & NSF_BLOB_FLAG)
  {
    my_free(blobs_buffer[0], MYF(MY_ALLOW_ZERO_PTR));
    my_free(blobs_buffer[1], MYF(MY_ALLOW_ZERO_PTR));
  }

  if (!own_buffer)
  {
    bitmap_free(&b);
  }

  return 0;
}

//#define RUN_NDB_BINLOG_TIMER
#ifdef RUN_NDB_BINLOG_TIMER
class Timer
{
public:
  Timer() { start(); }
  void start() { gettimeofday(&m_start, 0); }
  void stop() { gettimeofday(&m_stop, 0); }
  ulong elapsed_ms()
  {
    return (ulong)
      (((longlong) m_stop.tv_sec - (longlong) m_start.tv_sec) * 1000 +
       ((longlong) m_stop.tv_usec -
        (longlong) m_start.tv_usec + 999) / 1000);
  }
private:
  struct timeval m_start,m_stop;
};
#endif

/****************************************************************
  Injector thread main loop
****************************************************************/

static uchar *
ndb_schema_objects_get_key(NDB_SCHEMA_OBJECT *schema_object,
                           size_t *length,
                           my_bool not_used __attribute__((unused)))
{
  *length= schema_object->key_length;
  return (uchar*) schema_object->key;
}

static NDB_SCHEMA_OBJECT *ndb_get_schema_object(const char *key,
                                                my_bool create_if_not_exists,
                                                my_bool have_lock)
{
  NDB_SCHEMA_OBJECT *ndb_schema_object;
  uint length= (uint) strlen(key);
  DBUG_ENTER("ndb_get_schema_object");
  DBUG_PRINT("enter", ("key: '%s'", key));

  if (!have_lock)
    pthread_mutex_lock(&ndbcluster_mutex);
  while (!(ndb_schema_object=
           (NDB_SCHEMA_OBJECT*) my_hash_search(&ndb_schema_objects,
                                               (const uchar*) key,
                                               length)))
  {
    if (!create_if_not_exists)
    {
      DBUG_PRINT("info", ("does not exist"));
      break;
    }
    if (!(ndb_schema_object=
          (NDB_SCHEMA_OBJECT*) my_malloc(sizeof(*ndb_schema_object) + length + 1,
                                         MYF(MY_WME | MY_ZEROFILL))))
    {
      DBUG_PRINT("info", ("malloc error"));
      break;
    }
    ndb_schema_object->key= (char *)(ndb_schema_object+1);
    memcpy(ndb_schema_object->key, key, length + 1);
    ndb_schema_object->key_length= length;
    if (my_hash_insert(&ndb_schema_objects, (uchar*) ndb_schema_object))
    {
      my_free((uchar*) ndb_schema_object, 0);
      break;
    }
    pthread_mutex_init(&ndb_schema_object->mutex, MY_MUTEX_INIT_FAST);
    bitmap_init(&ndb_schema_object->slock_bitmap, ndb_schema_object->slock,
                sizeof(ndb_schema_object->slock)*8, FALSE);
    bitmap_clear_all(&ndb_schema_object->slock_bitmap);
    break;
  }
  if (ndb_schema_object)
  {
    ndb_schema_object->use_count++;
    DBUG_PRINT("info", ("use_count: %d", ndb_schema_object->use_count));
  }
  if (!have_lock)
    pthread_mutex_unlock(&ndbcluster_mutex);
  DBUG_RETURN(ndb_schema_object);
}


static void ndb_free_schema_object(NDB_SCHEMA_OBJECT **ndb_schema_object,
                                   bool have_lock)
{
  DBUG_ENTER("ndb_free_schema_object");
  DBUG_PRINT("enter", ("key: '%s'", (*ndb_schema_object)->key));
  if (!have_lock)
    pthread_mutex_lock(&ndbcluster_mutex);
  if (!--(*ndb_schema_object)->use_count)
  {
    DBUG_PRINT("info", ("use_count: %d", (*ndb_schema_object)->use_count));
    my_hash_delete(&ndb_schema_objects, (uchar*) *ndb_schema_object);
    pthread_mutex_destroy(&(*ndb_schema_object)->mutex);
    my_free((uchar*) *ndb_schema_object, MYF(0));
    *ndb_schema_object= 0;
  }
  else
  {
    DBUG_PRINT("info", ("use_count: %d", (*ndb_schema_object)->use_count));
  }
  if (!have_lock)
    pthread_mutex_unlock(&ndbcluster_mutex);
  DBUG_VOID_RETURN;
}


static void
remove_event_operations(Ndb* ndb)
{
  DBUG_ENTER("remove_event_operations");
  NdbEventOperation *op;
  while ((op= ndb->getEventOperation()))
  {
    DBUG_ASSERT(!IS_NDB_BLOB_PREFIX(op->getEvent()->getTable()->getName()));
    DBUG_PRINT("info", ("removing event operation on %s",
                        op->getEvent()->getName()));

    Ndb_event_data *event_data= (Ndb_event_data *) op->getCustomData();
    DBUG_ASSERT(event_data);

    NDB_SHARE *share= event_data->share;
    DBUG_ASSERT(share != NULL);
    DBUG_ASSERT(share->op == op || share->new_op == op);

    delete event_data;
    op->setCustomData(NULL);

    pthread_mutex_lock(&share->mutex);
    share->op= 0;
    share->new_op= 0;
    pthread_mutex_unlock(&share->mutex);

    DBUG_PRINT("NDB_SHARE", ("%s binlog free  use_count: %u",
                             share->key, share->use_count));
    free_share(&share);

    ndb->dropEventOperation(op);
  }
  DBUG_VOID_RETURN;
}

extern long long g_event_data_count;
extern long long g_event_nondata_count;
extern long long g_event_bytes_count;

void updateInjectorStats(Ndb* schemaNdb, Ndb* dataNdb)
{
  /* Update globals to sum of totals from each listening
   * Ndb object
   */
  g_event_data_count = 
    schemaNdb->getClientStat(Ndb::DataEventsRecvdCount) + 
    dataNdb->getClientStat(Ndb::DataEventsRecvdCount);
  g_event_nondata_count = 
    schemaNdb->getClientStat(Ndb::NonDataEventsRecvdCount) + 
    dataNdb->getClientStat(Ndb::NonDataEventsRecvdCount);
  g_event_bytes_count = 
    schemaNdb->getClientStat(Ndb::EventBytesRecvdCount) + 
    dataNdb->getClientStat(Ndb::EventBytesRecvdCount);
}

enum Binlog_thread_state
{
  BCCC_running= 0,
  BCCC_exit= 1,
  BCCC_restart= 2
};

extern ulong opt_ndb_report_thresh_binlog_epoch_slip;
extern ulong opt_ndb_report_thresh_binlog_mem_usage;

pthread_handler_t
ndb_binlog_thread_func(void *arg)
{
  THD *thd; /* needs to be first for thread_stack */
  Ndb *i_ndb= 0;
  Ndb *s_ndb= 0;
  Thd_ndb *thd_ndb=0;
  injector *inj= injector::instance();
  uint incident_id= 0;
  Binlog_thread_state do_ndbcluster_binlog_close_connection;

  /**
   * If we get error after having reported incident
   *   but before binlog started...we do "Restarting Cluster Binlog"
   *   in that case, don't report incident again
   */
  bool do_incident = true;

#ifdef RUN_NDB_BINLOG_TIMER
  Timer main_timer;
#endif

  pthread_mutex_lock(&injector_mutex);
  /*
    Set up the Thread
  */
  my_thread_init();
  DBUG_ENTER("ndb_binlog_thread");

  thd= new THD; /* note that contructor of THD uses DBUG_ */
  THD_CHECK_SENTRY(thd);

  /* We need to set thd->thread_id before thd->store_globals, or it will
     set an invalid value for thd->variables.pseudo_thread_id.
  */
  mysql_mutex_lock(&LOCK_thread_count);
  thd->thread_id= thread_id++;
  mysql_mutex_unlock(&LOCK_thread_count);

  thd->thread_stack= (char*) &thd; /* remember where our stack is */
  if (thd->store_globals())
  {
    thd->cleanup();
    delete thd;
    ndb_binlog_thread_running= -1;
    pthread_mutex_unlock(&injector_mutex);
    pthread_cond_signal(&injector_cond);

    DBUG_LEAVE;                               // Must match DBUG_ENTER()
    my_thread_end();
    pthread_exit(0);
    return NULL;                              // Avoid compiler warnings
  }
  lex_start(thd);

  thd->init_for_queries();
  thd_set_command(thd, COM_DAEMON);
  thd->system_thread= SYSTEM_THREAD_NDBCLUSTER_BINLOG;
#ifndef NDB_THD_HAS_NO_VERSION
  thd->version= refresh_version;
#endif
  thd->client_capabilities= 0;
  thd->security_ctx->skip_grants();
  my_net_init(&thd->net, 0);

  // Ndb binlog thread always use row format
  thd->set_current_stmt_binlog_format_row();

  /*
    Set up ndb binlog
  */
  sql_print_information("Starting Cluster Binlog Thread");

  pthread_detach_this_thread();
  thd->real_id= pthread_self();
  thd->capture_system_thread_id();
  mysql_mutex_lock(&LOCK_thread_count);
  add_global_thread(thd);
  mysql_mutex_unlock(&LOCK_thread_count);
  thd->lex->start_transaction_opt= 0;


restart_cluster_failure:
  int have_injector_mutex_lock= 0;
  do_ndbcluster_binlog_close_connection= BCCC_exit;

  if (!(thd_ndb= Thd_ndb::seize(thd)))
  {
    sql_print_error("Could not allocate Thd_ndb object");
    ndb_binlog_thread_running= -1;
    pthread_mutex_unlock(&injector_mutex);
    pthread_cond_signal(&injector_cond);
    goto err;
  }

  if (!(s_ndb= new Ndb(g_ndb_cluster_connection, NDB_REP_DB)) ||
      s_ndb->init())
  {
    sql_print_error("NDB Binlog: Getting Schema Ndb object failed");
    ndb_binlog_thread_running= -1;
    pthread_mutex_unlock(&injector_mutex);
    pthread_cond_signal(&injector_cond);
    goto err;
  }

  // empty database
  if (!(i_ndb= new Ndb(g_ndb_cluster_connection, "")) ||
      i_ndb->init())
  {
    sql_print_error("NDB Binlog: Getting Ndb object failed");
    ndb_binlog_thread_running= -1;
    pthread_mutex_unlock(&injector_mutex);
    pthread_cond_signal(&injector_cond);
    goto err;
  }

  /* init hash for schema object distribution */
  (void) my_hash_init(&ndb_schema_objects, system_charset_info, 32, 0, 0,
                      (my_hash_get_key)ndb_schema_objects_get_key, 0, 0);

  /*
    Expose global reference to our ndb object.

    Used by both sql client thread and binlog thread to interact
    with the storage
    pthread_mutex_lock(&injector_mutex);
  */
  injector_thd= thd;
  injector_ndb= i_ndb;
  schema_ndb= s_ndb;

  if (opt_bin_log && opt_ndb_log_bin)
  {
    ndb_binlog_running= TRUE;
  }

  /* Thread start up completed  */
  ndb_binlog_thread_running= 1;
  pthread_mutex_unlock(&injector_mutex);
  pthread_cond_signal(&injector_cond);

  /*
    wait for mysql server to start (so that the binlog is started
    and thus can receive the first GAP event)
  */
  mysql_mutex_lock(&LOCK_server_started);
  while (!mysqld_server_started)
  {
    struct timespec abstime;
    set_timespec(abstime, 1);
    mysql_cond_timedwait(&COND_server_started, &LOCK_server_started,
                         &abstime);
    if (ndbcluster_terminating)
    {
      mysql_mutex_unlock(&LOCK_server_started);
      goto err;
    }
  }
  mysql_mutex_unlock(&LOCK_server_started);
  /*
    Main NDB Injector loop
  */
  while (do_incident && ndb_binlog_running)
  {
    /*
      check if it is the first log, if so we do not insert a GAP event
      as there is really no log to have a GAP in
    */
    if (incident_id == 0)
    {
      LOG_INFO log_info;
      mysql_bin_log.get_current_log(&log_info);
      int len=  strlen(log_info.log_file_name);
      uint no= 0;
      if ((sscanf(log_info.log_file_name + len - 6, "%u", &no) == 1) &&
          no == 1)
      {
        /* this is the fist log, so skip GAP event */
        break;
      }
    }

    /*
      Always insert a GAP event as we cannot know what has happened
      in the cluster while not being connected.
    */
    LEX_STRING const msg[2]=
      {
        { C_STRING_WITH_LEN("mysqld startup")    },
        { C_STRING_WITH_LEN("cluster disconnect")}
      };
    int ret = inj->record_incident(thd, INCIDENT_LOST_EVENTS,
                                   msg[incident_id]);
    assert(ret == 0); NDB_IGNORE_VALUE(ret);
    do_incident = false; // Don't report incident again, unless we get started
    break;
  }
  incident_id= 1;
  {
    thd->proc_info= "Waiting for ndbcluster to start";

    pthread_mutex_lock(&injector_mutex);
    while (!ndb_schema_share ||
           (ndb_binlog_running && !ndb_apply_status_share) ||
           !ndb_binlog_tables_inited)
    {
      if (!thd_ndb->valid_ndb())
      {
        /*
          Cluster has gone away before setup was completed.
          Keep lock on injector_mutex to prevent further
          usage of the injector_ndb, and restart binlog
          thread to get rid of any garbage on the ndb objects
        */
        have_injector_mutex_lock= 1;
        do_ndbcluster_binlog_close_connection= BCCC_restart;
        goto err;
      }
      /* ndb not connected yet */
      struct timespec abstime;
      set_timespec(abstime, 1);
      pthread_cond_timedwait(&injector_cond, &injector_mutex, &abstime);
      if (ndbcluster_binlog_terminating)
      {
        pthread_mutex_unlock(&injector_mutex);
        goto err;
      }
    }
    pthread_mutex_unlock(&injector_mutex);

    DBUG_ASSERT(ndbcluster_hton->slot != ~(uint)0);
    thd_set_thd_ndb(thd, thd_ndb);
    thd_ndb->options|= TNO_NO_LOG_SCHEMA_OP;
    thd->query_id= 0; // to keep valgrind quiet
  }

  {
    // wait for the first event
    thd->proc_info= "Waiting for first event from ndbcluster";
    int schema_res, res;
    Uint64 schema_gci;
    do
    {
      DBUG_PRINT("info", ("Waiting for the first event"));

      if (ndbcluster_binlog_terminating)
        goto err;

      schema_res= s_ndb->pollEvents(100, &schema_gci);
    } while (schema_gci == 0 || ndb_latest_received_binlog_epoch == schema_gci);
    if (ndb_binlog_running)
    {
      Uint64 gci= i_ndb->getLatestGCI();
      while (gci < schema_gci || gci == ndb_latest_received_binlog_epoch)
      {
        if (ndbcluster_binlog_terminating)
          goto err;
        res= i_ndb->pollEvents(10, &gci);
      }
      if (gci > schema_gci)
      {
        schema_gci= gci;
      }
    }
    // now check that we have epochs consistant with what we had before the restart
    DBUG_PRINT("info", ("schema_res: %d  schema_gci: %u/%u", schema_res,
                        (uint)(schema_gci >> 32),
                        (uint)(schema_gci)));
    {
      i_ndb->flushIncompleteEvents(schema_gci);
      s_ndb->flushIncompleteEvents(schema_gci);
      if (schema_gci < ndb_latest_handled_binlog_epoch)
      {
        sql_print_error("NDB Binlog: cluster has been restarted --initial or with older filesystem. "
                        "ndb_latest_handled_binlog_epoch: %u/%u, while current epoch: %u/%u. "
                        "RESET MASTER should be issued. Resetting ndb_latest_handled_binlog_epoch.",
                        (uint)(ndb_latest_handled_binlog_epoch >> 32),
                        (uint)(ndb_latest_handled_binlog_epoch),
                        (uint)(schema_gci >> 32),
                        (uint)(schema_gci));
        ndb_set_latest_trans_gci(0);
        ndb_latest_handled_binlog_epoch= 0;
        ndb_latest_applied_binlog_epoch= 0;
        ndb_latest_received_binlog_epoch= 0;
      }
      else if (ndb_latest_applied_binlog_epoch > 0)
      {
        sql_print_warning("NDB Binlog: cluster has reconnected. "
                          "Changes to the database that occured while "
                          "disconnected will not be in the binlog");
      }
      if (opt_ndb_extra_logging)
      {
        sql_print_information("NDB Binlog: starting log at epoch %u/%u",
                              (uint)(schema_gci >> 32),
                              (uint)(schema_gci));
      }
    }
  }
  /*
    binlog thread is ready to receive events
    - client threads may now start updating data, i.e. tables are
    no longer read only
  */
  ndb_binlog_is_ready= TRUE;

  if (opt_ndb_extra_logging)
    sql_print_information("NDB Binlog: ndb tables writable");
  close_cached_tables((THD*) 0, (TABLE_LIST*) 0, FALSE, FALSE, FALSE);

  /* 
     Signal any waiting thread that ndb table setup is
     now complete
  */
  ndb_notify_tables_writable();

  {
    static char db[]= "";
    thd->db= db;
  }
  do_incident = true; // If we get disconnected again...do incident report
  do_ndbcluster_binlog_close_connection= BCCC_running;
  for ( ; !((ndbcluster_binlog_terminating ||
             do_ndbcluster_binlog_close_connection) &&
            ndb_latest_handled_binlog_epoch >= ndb_get_latest_trans_gci()) &&
          do_ndbcluster_binlog_close_connection != BCCC_restart; )
  {
#ifndef DBUG_OFF
    if (do_ndbcluster_binlog_close_connection)
    {
      DBUG_PRINT("info", ("do_ndbcluster_binlog_close_connection: %d, "
                          "ndb_latest_handled_binlog_epoch: %u/%u, "
                          "*get_latest_trans_gci(): %u/%u",
                          do_ndbcluster_binlog_close_connection,
                          (uint)(ndb_latest_handled_binlog_epoch >> 32),
                          (uint)(ndb_latest_handled_binlog_epoch),
                          (uint)(ndb_get_latest_trans_gci() >> 32),
                          (uint)(ndb_get_latest_trans_gci())));
    }
#endif
#ifdef RUN_NDB_BINLOG_TIMER
    main_timer.stop();
    sql_print_information("main_timer %ld ms",  main_timer.elapsed_ms());
    main_timer.start();
#endif

    /*
      now we don't want any events before next gci is complete
    */
    thd->proc_info= "Waiting for event from ndbcluster";
    thd->set_time();
    
    /* wait for event or 1000 ms */
    Uint64 gci= 0, schema_gci;
    int res= 0, tot_poll_wait= 1000;
    if (ndb_binlog_running)
    {
      res= i_ndb->pollEvents(tot_poll_wait, &gci);
      tot_poll_wait= 0;
    }
    int schema_res= s_ndb->pollEvents(tot_poll_wait, &schema_gci);
    ndb_latest_received_binlog_epoch= gci;

    while (gci > schema_gci && schema_res >= 0)
    {
      static char buf[64];
      thd->proc_info= "Waiting for schema epoch";
      my_snprintf(buf, sizeof(buf), "%s %u/%u(%u/%u)", thd->proc_info,
                  (uint)(schema_gci >> 32),
                  (uint)(schema_gci),
                  (uint)(gci >> 32),
                  (uint)(gci));
      thd->proc_info= buf;
      schema_res= s_ndb->pollEvents(10, &schema_gci);
    }

    if ((ndbcluster_binlog_terminating ||
         do_ndbcluster_binlog_close_connection) &&
        (ndb_latest_handled_binlog_epoch >= ndb_get_latest_trans_gci() ||
         !ndb_binlog_running))
      break; /* Shutting down server */

    MEM_ROOT **root_ptr=
      my_pthread_getspecific_ptr(MEM_ROOT**, THR_MALLOC);
    MEM_ROOT *old_root= *root_ptr;
    MEM_ROOT mem_root;
    init_sql_alloc(&mem_root, 4096, 0);
    List<Cluster_schema> post_epoch_log_list;
    List<Cluster_schema> post_epoch_unlock_list;
    *root_ptr= &mem_root;

    if (unlikely(schema_res > 0))
    {
      thd->proc_info= "Processing events from schema table";
      g_ndb_log_slave_updates= opt_log_slave_updates;
      s_ndb->
        setReportThreshEventGCISlip(opt_ndb_report_thresh_binlog_epoch_slip);
      s_ndb->
        setReportThreshEventFreeMem(opt_ndb_report_thresh_binlog_mem_usage);
      NdbEventOperation *pOp= s_ndb->nextEvent();
      while (pOp != NULL)
      {
        if (!pOp->hasError())
        {
          ndb_binlog_thread_handle_schema_event(thd, s_ndb, pOp,
                                                &post_epoch_log_list,
                                                &post_epoch_unlock_list,
                                                &mem_root);
          DBUG_PRINT("info", ("s_ndb first: %s", s_ndb->getEventOperation() ?
                              s_ndb->getEventOperation()->getEvent()->getTable()->getName() :
                              "<empty>"));
          DBUG_PRINT("info", ("i_ndb first: %s", i_ndb->getEventOperation() ?
                              i_ndb->getEventOperation()->getEvent()->getTable()->getName() :
                              "<empty>"));
          if (i_ndb->getEventOperation() == NULL &&
              s_ndb->getEventOperation() == NULL &&
              do_ndbcluster_binlog_close_connection == BCCC_running)
          {
            DBUG_PRINT("info", ("do_ndbcluster_binlog_close_connection= BCCC_restart"));
            do_ndbcluster_binlog_close_connection= BCCC_restart;
            if (ndb_latest_received_binlog_epoch < ndb_get_latest_trans_gci() && ndb_binlog_running)
            {
              sql_print_error("NDB Binlog: latest transaction in epoch %u/%u not in binlog "
                              "as latest received epoch is %u/%u",
                              (uint)(ndb_get_latest_trans_gci() >> 32),
                              (uint)(ndb_get_latest_trans_gci()),
                              (uint)(ndb_latest_received_binlog_epoch >> 32),
                              (uint)(ndb_latest_received_binlog_epoch));
            }
          }
        }
        else
          sql_print_error("NDB: error %lu (%s) on handling "
                          "binlog schema event",
                          (ulong) pOp->getNdbError().code,
                          pOp->getNdbError().message);
        pOp= s_ndb->nextEvent();
      }
      updateInjectorStats(s_ndb, i_ndb);
    }

    if (!ndb_binlog_running)
    {
      /*
        Just consume any events, not used if no binlogging
        e.g. node failure events
      */
      Uint64 tmp_gci;
      if (i_ndb->pollEvents(0, &tmp_gci))
      {
        NdbEventOperation *pOp;
        while ((pOp= i_ndb->nextEvent()))
        {
          if ((unsigned) pOp->getEventType() >=
              (unsigned) NDBEVENT::TE_FIRST_NON_DATA_EVENT)
          {
            ndb_binlog_index_row row;
            ndb_binlog_thread_handle_non_data_event(thd, pOp, row);
          }
        }
        if (i_ndb->getEventOperation() == NULL &&
            s_ndb->getEventOperation() == NULL &&
            do_ndbcluster_binlog_close_connection == BCCC_running)
        {
          DBUG_PRINT("info", ("do_ndbcluster_binlog_close_connection= BCCC_restart"));
          do_ndbcluster_binlog_close_connection= BCCC_restart;
        }
      }
      updateInjectorStats(s_ndb, i_ndb);
    }
    else if (res > 0 ||
             (ndb_log_empty_epochs() &&
              gci > ndb_latest_handled_binlog_epoch))
    {
      DBUG_PRINT("info", ("pollEvents res: %d", res));
      thd->proc_info= "Processing events";
      uchar apply_status_buf[512];
      TABLE *apply_status_table= NULL;
      if (ndb_apply_status_share)
      {
        /*
          We construct the buffer to write the apply status binlog
          event here, as the table->record[0] buffer is referenced
          by the apply status event operation, and will be filled
          with data at the nextEvent call if the first event should
          happen to be from the apply status table
        */
        Ndb_event_data *event_data= ndb_apply_status_share->event_data;
        if (!event_data)
        {
          DBUG_ASSERT(ndb_apply_status_share->op);
          event_data= 
            (Ndb_event_data *) ndb_apply_status_share->op->getCustomData();
          DBUG_ASSERT(event_data);
        }
        apply_status_table= event_data->shadow_table;

        /* 
           Intialize apply_status_table->record[0] 
        */
        empty_record(apply_status_table);

        apply_status_table->field[0]->store((longlong)::server_id, true);
        /*
          gci is added later, just before writing to binlog as gci
          is unknown here
        */
        apply_status_table->field[2]->store("", 0, &my_charset_bin);
        apply_status_table->field[3]->store((longlong)0, true);
        apply_status_table->field[4]->store((longlong)0, true);
        DBUG_ASSERT(sizeof(apply_status_buf) >= apply_status_table->s->reclength);
        memcpy(apply_status_buf, apply_status_table->record[0],
               apply_status_table->s->reclength);
      }
      NdbEventOperation *pOp= i_ndb->nextEvent();
      ndb_binlog_index_row _row;
      ndb_binlog_index_row *rows= &_row;
      injector::transaction trans;
      unsigned trans_row_count= 0;
      unsigned trans_slave_row_count= 0;
      if (!pOp)
      {
        /*
          Must be an empty epoch since the condition
          (ndb_log_empty_epochs() &&
           gci > ndb_latest_handled_binlog_epoch)
          must be true we write empty epoch into
          ndb_binlog_index
        */
        DBUG_PRINT("info", ("Writing empty epoch for gci %llu", gci));
        DBUG_PRINT("info", ("Initializing transaction"));
        inj->new_trans(thd, &trans);
        rows= &_row;
        memset(&_row, 0, sizeof(_row));
        thd->variables.character_set_client= &my_charset_latin1;
        goto commit_to_binlog;
      }
      while (pOp != NULL)
      {
        rows= &_row;
#ifdef RUN_NDB_BINLOG_TIMER
        Timer gci_timer, write_timer;
        int event_count= 0;
        gci_timer.start();
#endif
        gci= pOp->getGCI();
        DBUG_PRINT("info", ("Handling gci: %u/%u",
                            (uint)(gci >> 32),
                            (uint)(gci)));
        // sometimes get TE_ALTER with invalid table
        DBUG_ASSERT(pOp->getEventType() == NdbDictionary::Event::TE_ALTER ||
                    ! IS_NDB_BLOB_PREFIX(pOp->getEvent()->getTable()->getName()));
        DBUG_ASSERT(gci <= ndb_latest_received_binlog_epoch);

        /* initialize some variables for this epoch */
        g_ndb_log_slave_updates= opt_log_slave_updates;
        i_ndb->
          setReportThreshEventGCISlip(opt_ndb_report_thresh_binlog_epoch_slip);
        i_ndb->setReportThreshEventFreeMem(opt_ndb_report_thresh_binlog_mem_usage);

        memset(&_row, 0, sizeof(_row));
        thd->variables.character_set_client= &my_charset_latin1;
        DBUG_PRINT("info", ("Initializing transaction"));
        inj->new_trans(thd, &trans);
        trans_row_count= 0;
        trans_slave_row_count= 0;
        // pass table map before epoch
        {
          Uint32 iter= 0;
          const NdbEventOperation *gci_op;
          Uint32 event_types;

          if (!i_ndb->isConsistentGCI(gci))
          {
            char errmsg[64];
            uint end= sprintf(&errmsg[0],
                              "Detected missing data in GCI %llu, "
                              "inserting GAP event", gci);
            errmsg[end]= '\0';
            DBUG_PRINT("info",
                       ("Detected missing data in GCI %llu, "
                        "inserting GAP event", gci));
            LEX_STRING const msg= { C_STRING_WITH_LEN(errmsg) };
            inj->record_incident(thd, INCIDENT_LOST_EVENTS, msg);
          }
          while ((gci_op= i_ndb->getGCIEventOperations(&iter, &event_types))
                 != NULL)
          {
            Ndb_event_data *event_data=
              (Ndb_event_data *) gci_op->getCustomData();
            NDB_SHARE *share= (event_data)?event_data->share:NULL;
            DBUG_PRINT("info", ("per gci_op: 0x%lx  share: 0x%lx  event_types: 0x%x",
                                (long) gci_op, (long) share, event_types));
            // workaround for interface returning TE_STOP events
            // which are normally filtered out below in the nextEvent loop
            if ((event_types & ~NdbDictionary::Event::TE_STOP) == 0)
            {
              DBUG_PRINT("info", ("Skipped TE_STOP on table %s",
                                  gci_op->getEvent()->getTable()->getName()));
              continue;
            }
            // this should not happen
            if (share == NULL || event_data->shadow_table == NULL)
            {
              DBUG_PRINT("info", ("no share or table %s!",
                                  gci_op->getEvent()->getTable()->getName()));
              continue;
            }
            if (share == ndb_apply_status_share)
            {
              // skip this table, it is handled specially
              continue;
            }
            TABLE *table= event_data->shadow_table;
#ifndef DBUG_OFF
            const LEX_STRING &name= table->s->table_name;
#endif
            if ((event_types & (NdbDictionary::Event::TE_INSERT |
                                NdbDictionary::Event::TE_UPDATE |
                                NdbDictionary::Event::TE_DELETE)) == 0)
            {
              DBUG_PRINT("info", ("skipping non data event table: %.*s",
                                  (int) name.length, name.str));
              continue;
            }
            if (!trans.good())
            {
              DBUG_PRINT("info",
                         ("Found new data event, initializing transaction"));
              inj->new_trans(thd, &trans);
            }
            DBUG_PRINT("info", ("use_table: %.*s, cols %u",
                                (int) name.length, name.str,
                                table->s->fields));
            injector::transaction::table tbl(table, true);
            int ret = trans.use_table(::server_id, tbl);
            assert(ret == 0); NDB_IGNORE_VALUE(ret);
          }
        }
        if (trans.good())
        {
          if (apply_status_table)
          {
#ifndef DBUG_OFF
            const LEX_STRING& name= apply_status_table->s->table_name;
            DBUG_PRINT("info", ("use_table: %.*s",
                                (int) name.length, name.str));
#endif
            injector::transaction::table tbl(apply_status_table, true);
            int ret = trans.use_table(::server_id, tbl);
            assert(ret == 0); NDB_IGNORE_VALUE(ret);

            /* add the gci to the record */
            Field *field= apply_status_table->field[1];
            my_ptrdiff_t row_offset=
              (my_ptrdiff_t) (apply_status_buf - apply_status_table->record[0]);
            field->move_field_offset(row_offset);
            field->store((longlong)gci, true);
            field->move_field_offset(-row_offset);

            trans.write_row(::server_id,
                            injector::transaction::table(apply_status_table,
                                                         true),
                            &apply_status_table->s->all_set,
                            apply_status_table->s->fields,
                            apply_status_buf);
          }
          else
          {
            sql_print_error("NDB: Could not get apply status share");
          }
        }
#ifdef RUN_NDB_BINLOG_TIMER
        write_timer.start();
#endif
        do
        {
#ifdef RUN_NDB_BINLOG_TIMER
          event_count++;
#endif
          if (pOp->hasError() &&
              ndb_binlog_thread_handle_error(i_ndb, pOp) < 0)
            goto err;

#ifndef DBUG_OFF
          {
            Ndb_event_data *event_data=
              (Ndb_event_data *) pOp->getCustomData();
            NDB_SHARE *share= (event_data)?event_data->share:NULL;
            DBUG_PRINT("info",
                       ("EVENT TYPE: %d  GCI: %u/%u last applied: %u/%u  "
                        "share: 0x%lx (%s.%s)", pOp->getEventType(),
                        (uint)(gci >> 32),
                        (uint)(gci),
                        (uint)(ndb_latest_applied_binlog_epoch >> 32),
                        (uint)(ndb_latest_applied_binlog_epoch),
                        (long) share,
                        share ? share->db :  "'NULL'",
                        share ? share->table_name : "'NULL'"));
            DBUG_ASSERT(share != 0);
          }
          // assert that there is consistancy between gci op list
          // and event list
          {
            Uint32 iter= 0;
            const NdbEventOperation *gci_op;
            Uint32 event_types;
            while ((gci_op= i_ndb->getGCIEventOperations(&iter, &event_types))
                   != NULL)
            {
              if (gci_op == pOp)
                break;
            }
            DBUG_ASSERT(gci_op == pOp);
            DBUG_ASSERT((event_types & pOp->getEventType()) != 0);
          }
#endif
          if ((unsigned) pOp->getEventType() <
              (unsigned) NDBEVENT::TE_FIRST_NON_DATA_EVENT)
            ndb_binlog_thread_handle_data_event(i_ndb, pOp, &rows, trans, trans_row_count, trans_slave_row_count);
          else
          {
            ndb_binlog_thread_handle_non_data_event(thd, pOp, *rows);
            DBUG_PRINT("info", ("s_ndb first: %s", s_ndb->getEventOperation() ?
                                s_ndb->getEventOperation()->getEvent()->getTable()->getName() :
                                "<empty>"));
            DBUG_PRINT("info", ("i_ndb first: %s", i_ndb->getEventOperation() ?
                                i_ndb->getEventOperation()->getEvent()->getTable()->getName() :
                                "<empty>"));
            if (i_ndb->getEventOperation() == NULL &&
                s_ndb->getEventOperation() == NULL &&
                do_ndbcluster_binlog_close_connection == BCCC_running)
            {
              DBUG_PRINT("info", ("do_ndbcluster_binlog_close_connection= BCCC_restart"));
              do_ndbcluster_binlog_close_connection= BCCC_restart;
              if (ndb_latest_received_binlog_epoch < ndb_get_latest_trans_gci() && ndb_binlog_running)
              {
                sql_print_error("NDB Binlog: latest transaction in epoch %lu not in binlog "
                                "as latest received epoch is %lu",
                                (ulong) ndb_get_latest_trans_gci(),
                                (ulong) ndb_latest_received_binlog_epoch);
              }
            }
          }

          pOp= i_ndb->nextEvent();
        } while (pOp && pOp->getGCI() == gci);

         updateInjectorStats(s_ndb, i_ndb);
        
        /*
          note! pOp is not referring to an event in the next epoch
          or is == 0
        */
#ifdef RUN_NDB_BINLOG_TIMER
        write_timer.stop();
#endif

        while (trans.good())
        {
          if (!ndb_log_empty_epochs())
          {
            /*
              If 
                - We did not add any 'real' rows to the Binlog AND
                - We did not apply any slave row updates, only
                  ndb_apply_status updates
              THEN
                Don't write the Binlog transaction which just
                contains ndb_apply_status updates.
                (For cicular rep with log_apply_status, ndb_apply_status
                updates will propagate while some related, real update
                is propagating)
            */
            if ((trans_row_count == 0) &&
                (! (opt_ndb_log_apply_status &&
                    trans_slave_row_count) ))
            {
              /* nothing to commit, rollback instead */
              if (int r= trans.rollback())
              {
                sql_print_error("NDB Binlog: "
                                "Error during ROLLBACK of GCI %u/%u. Error: %d",
                                uint(gci >> 32), uint(gci), r);
                /* TODO: Further handling? */
              }
              break;
            }
          }
      commit_to_binlog:
          thd->proc_info= "Committing events to binlog";
          injector::transaction::binlog_pos start= trans.start_pos();
          if (int r= trans.commit())
          {
            sql_print_error("NDB Binlog: "
                            "Error during COMMIT of GCI. Error: %d",
                            r);
            /* TODO: Further handling? */
          }
          rows->gci= (Uint32)(gci >> 32); // Expose gci hi/lo
          rows->epoch= gci;
          rows->master_log_file= start.file_name();
          rows->master_log_pos= start.file_pos();

          DBUG_PRINT("info", ("COMMIT gci: %lu", (ulong) gci));
          if (opt_ndb_log_binlog_index)
          {
            if (ndb_binlog_index_table__write_rows(thd, rows))
            {
              /* 
                 Writing to ndb_binlog_index failed, check if we are
                 being killed and retry
              */
              if (thd->killed)
              {
                (void) mysql_mutex_lock(&LOCK_thread_count);
                volatile THD::killed_state killed= thd->killed;
                /* We are cleaning up, allow for flushing last epoch */
                thd->killed= THD::NOT_KILLED;
                ndb_binlog_index_table__write_rows(thd, rows);
                /* Restore kill flag */
                thd->killed= killed;
                (void) mysql_mutex_unlock(&LOCK_thread_count);
              }
            }
          }
          ndb_latest_applied_binlog_epoch= gci;
          break;
        }
        ndb_latest_handled_binlog_epoch= gci;

#ifdef RUN_NDB_BINLOG_TIMER
        gci_timer.stop();
        sql_print_information("gci %ld event_count %d write time "
                              "%ld(%d e/s), total time %ld(%d e/s)",
                              (ulong)gci, event_count,
                              write_timer.elapsed_ms(),
                              (1000*event_count) / write_timer.elapsed_ms(),
                              gci_timer.elapsed_ms(),
                              (1000*event_count) / gci_timer.elapsed_ms());
#endif
      }
      if(!i_ndb->isConsistent(gci))
      {
        char errmsg[64];
        uint end= sprintf(&errmsg[0],
                          "Detected missing data in GCI %llu, "
                          "inserting GAP event", gci);
        errmsg[end]= '\0';
        DBUG_PRINT("info",
                   ("Detected missing data in GCI %llu, "
                    "inserting GAP event", gci));
        LEX_STRING const msg= { C_STRING_WITH_LEN(errmsg) };
        inj->record_incident(thd, INCIDENT_LOST_EVENTS, msg);
      }
    }

    ndb_binlog_thread_handle_schema_event_post_epoch(thd,
                                                     &post_epoch_log_list,
                                                     &post_epoch_unlock_list);
    free_root(&mem_root, MYF(0));
    *root_ptr= old_root;
    ndb_latest_handled_binlog_epoch= ndb_latest_received_binlog_epoch;
  }
 err:
  if (do_ndbcluster_binlog_close_connection != BCCC_restart)
  {
    sql_print_information("Stopping Cluster Binlog");
    DBUG_PRINT("info",("Shutting down cluster binlog thread"));
    thd->proc_info= "Shutting down";
  }
  else
  { 
    sql_print_information("Restarting Cluster Binlog");
    DBUG_PRINT("info",("Restarting cluster binlog thread"));
    thd->proc_info= "Restarting";
  }
  if (!have_injector_mutex_lock)
    pthread_mutex_lock(&injector_mutex);
  /* don't mess with the injector_ndb anymore from other threads */
  injector_thd= 0;
  injector_ndb= 0;
  schema_ndb= 0;
  pthread_mutex_unlock(&injector_mutex);
  thd->db= 0; // as not to try to free memory

  /*
    This will cause the util thread to start to try to initialize again
    via ndbcluster_setup_binlog_table_shares.  But since injector_ndb is
    set to NULL it will not succeed until injector_ndb is reinitialized.
  */
  ndb_binlog_tables_inited= FALSE;

  if (ndb_apply_status_share)
  {
    /* ndb_share reference binlog extra free */
    DBUG_PRINT("NDB_SHARE", ("%s binlog extra free  use_count: %u",
                             ndb_apply_status_share->key,
                             ndb_apply_status_share->use_count));
    free_share(&ndb_apply_status_share);
    ndb_apply_status_share= 0;
  }
  if (ndb_schema_share)
  {
    /* begin protect ndb_schema_share */
    pthread_mutex_lock(&ndb_schema_share_mutex);
    /* ndb_share reference binlog extra free */
    DBUG_PRINT("NDB_SHARE", ("%s binlog extra free  use_count: %u",
                             ndb_schema_share->key,
                             ndb_schema_share->use_count));
    free_share(&ndb_schema_share);
    ndb_schema_share= 0;
    pthread_mutex_unlock(&ndb_schema_share_mutex);
    /* end protect ndb_schema_share */
  }

  /* remove all event operations */
  if (s_ndb)
  {
    remove_event_operations(s_ndb);
    delete s_ndb;
    s_ndb= 0;
  }
  if (i_ndb)
  {
    remove_event_operations(i_ndb);
    delete i_ndb;
    i_ndb= 0;
  }

  my_hash_free(&ndb_schema_objects);

  if (thd_ndb)
  {
    Thd_ndb::release(thd_ndb);
    thd_set_thd_ndb(thd, NULL);
    thd_ndb= NULL;
  }

  /**
   * release all extra references from tables
   */
  {
    if (opt_ndb_extra_logging > 9)
      sql_print_information("NDB Binlog: Release extra share references");

    pthread_mutex_lock(&ndbcluster_mutex);
    for (uint i= 0; i < ndbcluster_open_tables.records;)
    {
      NDB_SHARE * share = (NDB_SHARE*)my_hash_element(&ndbcluster_open_tables,
                                                      i);
      if (share->state != NSS_DROPPED)
      {
        /*
          The share kept by the server has not been freed, free it
        */
        share->state= NSS_DROPPED;
        /* ndb_share reference create free */
        DBUG_PRINT("NDB_SHARE", ("%s create free  use_count: %u",
                                 share->key, share->use_count));
        free_share(&share, TRUE);

        /**
         * This might have altered hash table...not sure if it's stable..
         *   so we'll restart instead
         */
        i = 0;
      }
      else
      {
        i++;
      }
    }
    pthread_mutex_unlock(&ndbcluster_mutex);
  }

  close_cached_tables((THD*) 0, (TABLE_LIST*) 0, FALSE, FALSE, FALSE);
  if (opt_ndb_extra_logging > 15)
  {
    sql_print_information("NDB Binlog: remaining open tables: ");
    for (uint i= 0; i < ndbcluster_open_tables.records; i++)
    {
      NDB_SHARE* share = (NDB_SHARE*)my_hash_element(&ndbcluster_open_tables,i);
      sql_print_information("  %s.%s state: %u use_count: %u",
                            share->db,
                            share->table_name,
                            (uint)share->state,
                            share->use_count);
    }
  }

  if (do_ndbcluster_binlog_close_connection == BCCC_restart)
  {
    pthread_mutex_lock(&injector_mutex);
    goto restart_cluster_failure;
  }

  net_end(&thd->net);
  thd->cleanup();
  delete thd;

  ndb_binlog_thread_running= -1;
  ndb_binlog_running= FALSE;
  (void) pthread_cond_signal(&injector_cond);

  DBUG_PRINT("exit", ("ndb_binlog_thread"));

  DBUG_LEAVE;                               // Must match DBUG_ENTER()
  my_thread_end();
  pthread_exit(0);
  return NULL;                              // Avoid compiler warnings
}

bool
ndbcluster_show_status_binlog(THD* thd, stat_print_fn *stat_print,
                              enum ha_stat_type stat_type)
{
  char buf[IO_SIZE];
  uint buflen;
  ulonglong ndb_latest_epoch= 0;
  DBUG_ENTER("ndbcluster_show_status_binlog");
  
  pthread_mutex_lock(&injector_mutex);
  if (injector_ndb)
  {
    char buff1[22],buff2[22],buff3[22],buff4[22],buff5[22];
    ndb_latest_epoch= injector_ndb->getLatestGCI();
    pthread_mutex_unlock(&injector_mutex);

    buflen=
      my_snprintf(buf, sizeof(buf),
                  "latest_epoch=%s, "
                  "latest_trans_epoch=%s, "
                  "latest_received_binlog_epoch=%s, "
                  "latest_handled_binlog_epoch=%s, "
                  "latest_applied_binlog_epoch=%s",
                  llstr(ndb_latest_epoch, buff1),
                  llstr(ndb_get_latest_trans_gci(), buff2),
                  llstr(ndb_latest_received_binlog_epoch, buff3),
                  llstr(ndb_latest_handled_binlog_epoch, buff4),
                  llstr(ndb_latest_applied_binlog_epoch, buff5));
    if (stat_print(thd, ndbcluster_hton_name, ndbcluster_hton_name_length,
                   "binlog", strlen("binlog"),
                   buf, buflen))
      DBUG_RETURN(TRUE);
  }
  else
    pthread_mutex_unlock(&injector_mutex);
  DBUG_RETURN(FALSE);
}

/*
   AnyValue carries ServerId or Reserved codes
   Bits from opt_server_id_bits to 30 may carry other data
   so we ignore them when reading/setting AnyValue.
 
   332        21        10        0
   10987654321098765432109876543210
   roooooooooooooooooooooooosssssss
 
   r = Reserved bit indicates whether
   bits 0-7+ have ServerId (0) or
   some special reserved code (1).
   o = Optional bits, depending on value
       of server-id-bits will be 
       serverid bits or user-specific 
       data
   s = Serverid bits or reserved codes
       At least 7 bits will be available
       for serverid or reserved codes
  
*/

#define NDB_ANYVALUE_RESERVED_BIT   0x80000000
#define NDB_ANYVALUE_RESERVED_MASK  0x8000007f

#define NDB_ANYVALUE_NOLOGGING_CODE 0x8000007f

#ifndef DBUG_OFF
void dbug_ndbcluster_anyvalue_set_userbits(Uint32& anyValue)
{
  /* 
     Set userData part of AnyValue (if there is one) to
     all 1s to test that it is ignored
  */
  const Uint32 userDataMask = ~(opt_server_id_mask | 
                                NDB_ANYVALUE_RESERVED_BIT);

  anyValue |= userDataMask;
}
#endif

bool ndbcluster_anyvalue_is_reserved(Uint32 anyValue)
{
  return ((anyValue & NDB_ANYVALUE_RESERVED_BIT) != 0);
}

bool ndbcluster_anyvalue_is_nologging(Uint32 anyValue)
{
  return ((anyValue & NDB_ANYVALUE_RESERVED_MASK) ==
          NDB_ANYVALUE_NOLOGGING_CODE);
}

void ndbcluster_anyvalue_set_nologging(Uint32& anyValue)
{
  anyValue |= NDB_ANYVALUE_NOLOGGING_CODE;
}

void ndbcluster_anyvalue_set_normal(Uint32& anyValue)
{
  /* Clear reserved bit and serverid bits */
  anyValue &= ~(NDB_ANYVALUE_RESERVED_BIT);
  anyValue &= ~(opt_server_id_mask);
}

bool ndbcluster_anyvalue_is_serverid_in_range(Uint32 serverId)
{
  return ((serverId & ~opt_server_id_mask) == 0);
}

Uint32 ndbcluster_anyvalue_get_serverid(Uint32 anyValue)
{
  assert(! (anyValue & NDB_ANYVALUE_RESERVED_BIT) );

  return (anyValue & opt_server_id_mask);
}

void ndbcluster_anyvalue_set_serverid(Uint32& anyValue, Uint32 serverId)
{
  assert(! (anyValue & NDB_ANYVALUE_RESERVED_BIT) );
  anyValue &= ~(opt_server_id_mask);
  anyValue |= (serverId & opt_server_id_mask); 
}

#ifdef NDB_WITHOUT_SERVER_ID_BITS

/* No --server-id-bits=<bits> -> implement constant opt_server_id_mask */
ulong opt_server_id_mask = ~0;

#endif

#endif
