/* Copyright (c) 2008, 2014, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software Foundation,
   51 Franklin Street, Suite 500, Boston, MA 02110-1335 USA */

#include "sql_priv.h"
#include "unireg.h"
#include <m_string.h>

#include "rpl_mi.h"
#include "log_event.h"
#include "rpl_filter.h"
#include <my_dir.h>
#include "set_var.h"
#include "rpl_handler.h"
#include "debug_sync.h"
#include "global_threads.h"
#include <thread>

Trans_delegate *transaction_delegate;
Binlog_storage_delegate *binlog_storage_delegate;
#ifdef HAVE_REPLICATION
Binlog_transmit_delegate *binlog_transmit_delegate;
Binlog_relay_IO_delegate *binlog_relay_io_delegate;
struct MysqlPrimaryInfo;
extern int rli_relay_log_raft_reset(
    std::pair<std::string, unsigned long long> log_file_pos);
extern int raft_reset_slave(THD *thd);
extern int raft_change_master(THD *thd, const MysqlPrimaryInfo& info);
extern int raft_stop_sql_thread(THD *thd);
extern int raft_stop_io_thread(THD *thd);
extern int raft_start_sql_thread(THD *thd);
#endif /* HAVE_REPLICATION */
Raft_replication_delegate *raft_replication_delegate;

RaftListenerQueue raft_listener_queue;
extern int trim_logged_gtid(const std::vector<std::string>& trimmed_gtids);
extern int rotate_binlog_file(THD *thd);
extern int binlog_change_to_apply();

/*
  structure to save transaction log filename and position
*/
typedef struct Trans_binlog_info {
  my_off_t log_pos;
  char log_file[FN_REFLEN];
} Trans_binlog_info;

int get_user_var_int(const char *name,
                     long long int *value, int *null_value)
{
  my_bool null_val;
  user_var_entry *entry=
    (user_var_entry*) my_hash_search(&current_thd->user_vars,
                                  (uchar*) name, strlen(name));
  if (!entry)
    return 1;
  *value= entry->val_int(&null_val);
  if (null_value)
    *null_value= null_val;
  return 0;
}

int get_user_var_real(const char *name,
                      double *value, int *null_value)
{
  my_bool null_val;
  user_var_entry *entry=
    (user_var_entry*) my_hash_search(&current_thd->user_vars,
                                  (uchar*) name, strlen(name));
  if (!entry)
    return 1;
  *value= entry->val_real(&null_val);
  if (null_value)
    *null_value= null_val;
  return 0;
}

int get_user_var_str(const char *name, char *value,
                     size_t len, unsigned int precision, int *null_value)
{
  String str;
  my_bool null_val;
  user_var_entry *entry=
    (user_var_entry*) my_hash_search(&current_thd->user_vars,
                                  (uchar*) name, strlen(name));
  if (!entry)
    return 1;
  entry->val_str(&null_val, &str, precision);
  strncpy(value, str.c_ptr(), len);
  if (null_value)
    *null_value= null_val;
  return 0;
}

int delegates_init()
{
  static my_aligned_storage<sizeof(Trans_delegate), MY_ALIGNOF(long)> trans_mem;
  static my_aligned_storage<sizeof(Binlog_storage_delegate),
                            MY_ALIGNOF(long)> storage_mem;
#ifdef HAVE_REPLICATION
  static my_aligned_storage<sizeof(Binlog_transmit_delegate),
                            MY_ALIGNOF(long)> transmit_mem;
  static my_aligned_storage<sizeof(Binlog_relay_IO_delegate),
                            MY_ALIGNOF(long)> relay_io_mem;
#endif
  static my_aligned_storage<sizeof(Raft_replication_delegate),
                            MY_ALIGNOF(long)> raft_mem;

  void *place_trans_mem= trans_mem.data;
  void *place_storage_mem= storage_mem.data;

  transaction_delegate= new (place_trans_mem) Trans_delegate;

  if (!transaction_delegate->is_inited())
  {
    sql_print_error("Initialization of transaction delegates failed. "
                    "Please report a bug.");
    return 1;
  }

  binlog_storage_delegate= new (place_storage_mem) Binlog_storage_delegate;

  if (!binlog_storage_delegate->is_inited())
  {
    sql_print_error("Initialization binlog storage delegates failed. "
                    "Please report a bug.");
    return 1;
  }

#ifdef HAVE_REPLICATION
  void *place_transmit_mem= transmit_mem.data;
  void *place_relay_io_mem= relay_io_mem.data;

  binlog_transmit_delegate= new (place_transmit_mem) Binlog_transmit_delegate;

  if (!binlog_transmit_delegate->is_inited())
  {
    sql_print_error("Initialization of binlog transmit delegates failed. "
                    "Please report a bug.");
    return 1;
  }

  binlog_relay_io_delegate= new (place_relay_io_mem) Binlog_relay_IO_delegate;

  if (!binlog_relay_io_delegate->is_inited())
  {
    sql_print_error("Initialization binlog relay IO delegates failed. "
                    "Please report a bug.");
    return 1;
  }
#endif

  void *place_raft_mem = raft_mem.data;
  raft_replication_delegate= new (place_raft_mem) Raft_replication_delegate;

  if (!raft_replication_delegate->is_inited())
  {
    // NO_LINT_DEBUG
    sql_print_error("Initialization of raft delegate failed. "
                    "Please report a bug.");
    return 1;
  }

  return 0;
}

void delegates_destroy()
{
  if (transaction_delegate)
    transaction_delegate->~Trans_delegate();
  if (binlog_storage_delegate)
    binlog_storage_delegate->~Binlog_storage_delegate();
#ifdef HAVE_REPLICATION
  if (binlog_transmit_delegate)
    binlog_transmit_delegate->~Binlog_transmit_delegate();
  if (binlog_relay_io_delegate)
    binlog_relay_io_delegate->~Binlog_relay_IO_delegate();
#endif /* HAVE_REPLICATION */
  if (raft_replication_delegate)
    raft_replication_delegate->~Raft_replication_delegate();
}

/*
  This macro is used by raft Delegate methods to call into raft plugin
  The only difference is that this is a 'stricter' version which will return
  failure if the plugin hooks were not called
 */
#define FOREACH_OBSERVER_STRICT(r, f, thd, args)                        \
  if (thd) {                                                            \
    param.server_id= thd->server_id;                                    \
    param.host_or_ip= thd->security_ctx->host_or_ip;                    \
  }                                                                     \
  /*
     Use a struct to make sure that they are allocated adjacent, check
     delete_dynamic().
  */                                                                    \
  struct {                                                              \
    DYNAMIC_ARRAY plugins;                                              \
    /* preallocate 8 slots */                                           \
    plugin_ref plugins_buffer[8];                                       \
  } s;                                                                  \
  DYNAMIC_ARRAY *plugins= &s.plugins;                                   \
  plugin_ref *plugins_buffer= s.plugins_buffer;                         \
  my_init_dynamic_array2(plugins, sizeof(plugin_ref),                   \
                         plugins_buffer, 8, 8);                         \
  read_lock();                                                          \
  Observer_info_iterator iter= observer_info_iter();                    \
  Observer_info *info= iter++;                                          \
  (r)= 1; /* Assume failure by default */                               \
  for (; info; info= iter++)                                            \
  {                                                                     \
    plugin_ref plugin=                                                  \
      my_plugin_lock(0, &info->plugin);                                 \
    if (!plugin)                                                        \
    {                                                                   \
      /* plugin is not intialized or deleted, this is an error when
       * enable_raft_plugin is ON */                                    \
      enable_raft_plugin ? (r)= 1 : (r)= 0;                             \
      break;                                                            \
    }                                                                   \
    insert_dynamic(plugins, &plugin);                                   \
    if (((Observer *)info->observer)->f                                 \
        && ((Observer *)info->observer)->f args)                        \
    {                                                                   \
      (r)= 1;                                                           \
      sql_print_error("Run function '" #f "' in plugin '%s' failed",    \
                      info->plugin_int->name.str);                      \
      break;                                                            \
    }                                                                   \
    /* Plugin is successfully called, set return status to 0
     * indicating success */                                            \
    (r)= 0;                                                             \
  }                                                                     \
  unlock();                                                             \
  /*
     Unlock plugins should be done after we released the Delegate lock
     to avoid possible deadlock when this is the last user of the
     plugin, and when we unlock the plugin, it will try to
     deinitialize the plugin, which will try to lock the Delegate in
     order to remove the observers.
  */                                                                    \
  plugin_unlock_list(0, (plugin_ref*)plugins->buffer,                   \
                     plugins->elements);                                \
  delete_dynamic(plugins)

/*
  This macro is used by almost all the Delegate methods to iterate
  over all the observers running given callback function of the
  delegate .

  Add observer plugins to the thd->lex list, after each statement, all
  plugins add to thd->lex will be automatically unlocked.
 */
#define FOREACH_OBSERVER(r, f, thd, args)                               \
  if (thd) {                                                            \
    param.server_id= thd->server_id;                                    \
    param.host_or_ip= thd->security_ctx->host_or_ip;                    \
  }                                                                     \
  /*
     Use a struct to make sure that they are allocated adjacent, check
     delete_dynamic().
  */                                                                    \
  struct {                                                              \
    DYNAMIC_ARRAY plugins;                                              \
    /* preallocate 8 slots */                                           \
    plugin_ref plugins_buffer[8];                                       \
  } s;                                                                  \
  DYNAMIC_ARRAY *plugins= &s.plugins;                                   \
  plugin_ref *plugins_buffer= s.plugins_buffer;                         \
  my_init_dynamic_array2(plugins, sizeof(plugin_ref),                   \
                         plugins_buffer, 8, 8);                         \
  read_lock();                                                          \
  Observer_info_iterator iter= observer_info_iter();                    \
  Observer_info *info= iter++;                                          \
  for (; info; info= iter++)                                            \
  {                                                                     \
    plugin_ref plugin=                                                  \
      my_plugin_lock(0, &info->plugin);                                 \
    if (!plugin)                                                        \
    {                                                                   \
      /* plugin is not intialized or deleted, this is not an error */   \
      r= 0;                                                             \
      break;                                                            \
    }                                                                   \
    insert_dynamic(plugins, &plugin);                                   \
    if (((Observer *)info->observer)->f                                 \
        && ((Observer *)info->observer)->f args)                        \
    {                                                                   \
      r= 1;                                                             \
      sql_print_error("Run function '" #f "' in plugin '%s' failed",    \
                      info->plugin_int->name.str);                      \
      break;                                                            \
    }                                                                   \
  }                                                                     \
  unlock();                                                             \
  /*
     Unlock plugins should be done after we released the Delegate lock
     to avoid possible deadlock when this is the last user of the
     plugin, and when we unlock the plugin, it will try to
     deinitialize the plugin, which will try to lock the Delegate in
     order to remove the observers.
  */                                                                    \
  plugin_unlock_list(0, (plugin_ref*)plugins->buffer,                   \
                     plugins->elements);                                \
  delete_dynamic(plugins)


int Trans_delegate::before_commit(THD *thd, bool all)
{
  DBUG_ENTER("Trans_delegate::before_commit");
  Trans_param param = { 0, 0, 0, 0, 0};
  bool is_real_trans= (all || thd->transaction.all.ha_list == 0);

  if (is_real_trans)
    param.flags = true;

  thd->get_trans_fixed_pos(&param.log_file, &param.log_pos);

  DBUG_PRINT("enter", ("log_file: %s, log_pos: %llu",
                       param.log_file, param.log_pos));

  int ret= 0;
  FOREACH_OBSERVER(ret, before_commit, thd, (&param));

  DEBUG_SYNC(thd, "after_call_after_sync_observer");
  DBUG_RETURN(ret);
}

int Trans_delegate::after_commit(THD *thd, bool all)
{
  DBUG_ENTER("Trans_delegate::after_commit");
  Trans_param param = { 0, 0, 0, 0, 0};
  bool is_real_trans= (all || thd->transaction.all.ha_list == 0);

  if (is_real_trans)
    param.flags = true;

  thd->get_trans_fixed_pos(&param.log_file, &param.log_pos);

  DBUG_PRINT("enter", ("log_file: %s, log_pos: %llu", param.log_file, param.log_pos));
  DEBUG_SYNC(thd, "before_call_after_commit_observer");

  int ret= 0;
  FOREACH_OBSERVER(ret, after_commit, thd, (&param));
  DBUG_RETURN(ret);
}

int Trans_delegate::after_rollback(THD *thd, bool all)
{
  Trans_param param = { 0, 0, 0, 0, 0};
  bool is_real_trans= (all || thd->transaction.all.ha_list == 0);

  if (is_real_trans)
    param.flags|= TRANS_IS_REAL_TRANS;
  thd->get_trans_fixed_pos(&param.log_file, &param.log_pos);
  int ret= 0;
  FOREACH_OBSERVER(ret, after_rollback, thd, (&param));
  return ret;
}

int Binlog_storage_delegate::after_flush(THD *thd,
                                         const char *log_file,
                                         my_off_t log_pos)
{
  DBUG_ENTER("Binlog_storage_delegate::after_flush");
  DBUG_PRINT("enter", ("log_file: %s, log_pos: %llu",
                       log_file, (ulonglong) log_pos));
  Binlog_storage_param param;

  int ret= 0;
  FOREACH_OBSERVER(ret, after_flush, thd, (&param, log_file, log_pos));
  DBUG_RETURN(ret);
}

int Binlog_storage_delegate::before_flush(THD *thd, IO_CACHE* io_cache)
{
  DBUG_ENTER("Binlog_storage_delegate::before_flush");
  Binlog_storage_param param;

  int ret= 0;

  FOREACH_OBSERVER(ret, before_flush, thd, (&param, io_cache));

  DBUG_RETURN(ret);
}

#ifdef HAVE_REPLICATION
int Binlog_transmit_delegate::transmit_start(THD *thd, ushort flags,
                                             const char *log_file,
                                             my_off_t log_pos,
                                             bool *observe_transmission)
{
  Binlog_transmit_param param;
  param.flags= flags;

  int ret= 0;
  FOREACH_OBSERVER(ret, transmit_start, thd, (&param, log_file, log_pos));
  *observe_transmission= param.should_observe();
  return ret;
}

int Binlog_transmit_delegate::transmit_stop(THD *thd, ushort flags)
{
  Binlog_transmit_param param;
  param.flags= flags;

  DBUG_EXECUTE_IF("crash_binlog_transmit_hook", DBUG_SUICIDE(););

  int ret= 0;
  FOREACH_OBSERVER(ret, transmit_stop, thd, (&param));
  return ret;
}

int Binlog_transmit_delegate::reserve_header(THD *thd, ushort flags,
                                             String *packet)
{
  /* NOTE2ME: Maximum extra header size for each observer, I hope 32
     bytes should be enough for each Observer to reserve their extra
     header. If later found this is not enough, we can increase this
     /HEZX
  */
#define RESERVE_HEADER_SIZE 32
  unsigned char header[RESERVE_HEADER_SIZE];
  ulong hlen;
  Binlog_transmit_param param;
  param.flags= flags;
  param.server_id= thd->server_id;

  DBUG_EXECUTE_IF("crash_binlog_transmit_hook", DBUG_SUICIDE(););

  int ret= 0;
  read_lock();
  Observer_info_iterator iter= observer_info_iter();
  Observer_info *info= iter++;
  for (; info; info= iter++)
  {
    plugin_ref plugin=
      my_plugin_lock(thd, &info->plugin);
    if (!plugin)
    {
      ret= 1;
      break;
    }
    hlen= 0;
    if (((Observer *)info->observer)->reserve_header
        && ((Observer *)info->observer)->reserve_header(&param,
                                                        header,
                                                        RESERVE_HEADER_SIZE,
                                                        &hlen))
    {
      ret= 1;
      plugin_unlock(thd, plugin);
      break;
    }
    plugin_unlock(thd, plugin);
    if (hlen == 0)
      continue;
    if (hlen > RESERVE_HEADER_SIZE || packet->append((char *)header, hlen))
    {
      ret= 1;
      break;
    }
  }
  unlock();
  return ret;
}

int Binlog_transmit_delegate::before_send_event(THD *thd, ushort flags,
                                                String *packet,
                                                const char *log_file,
                                                my_off_t log_pos)
{
  Binlog_transmit_param param;
  param.flags= flags;

  DBUG_EXECUTE_IF("crash_binlog_transmit_hook", DBUG_SUICIDE(););

  int ret= 0;
  FOREACH_OBSERVER(ret, before_send_event, thd,
                   (&param, (uchar *)packet->c_ptr(),
                    packet->length(),
                    log_file+dirname_length(log_file), log_pos));
  return ret;
}

int Binlog_transmit_delegate::after_send_event(THD *thd, ushort flags,
                                               String *packet,
                                               const char *skipped_log_file,
                                               my_off_t skipped_log_pos)
{
  Binlog_transmit_param param;
  param.flags= flags;

  DBUG_EXECUTE_IF("crash_binlog_transmit_hook", DBUG_SUICIDE(););

  int ret= 0;
  FOREACH_OBSERVER(ret, after_send_event, thd,
                   (&param, packet->c_ptr(), packet->length(),
                   skipped_log_file+dirname_length(skipped_log_file),
                    skipped_log_pos));
  return ret;
}

int Binlog_transmit_delegate::after_reset_master(THD *thd, ushort flags)

{
  Binlog_transmit_param param;
  param.flags= flags;

  int ret= 0;
  FOREACH_OBSERVER(ret, after_reset_master, thd, (&param));
  return ret;
}

void Binlog_relay_IO_delegate::init_param(Binlog_relay_IO_param *param,
                                          Master_info *mi)
{
  param->mysql= mi->mysql;
  param->user= const_cast<char *>(mi->get_user());
  param->host= mi->host;
  param->port= mi->port;
  param->master_log_name= const_cast<char *>(mi->get_master_log_name());
  param->master_log_pos= mi->get_master_log_pos();
}

int Binlog_relay_IO_delegate::thread_start(THD *thd, Master_info *mi)
{
  Binlog_relay_IO_param param;
  init_param(&param, mi);

  int ret= 0;
  FOREACH_OBSERVER(ret, thread_start, thd, (&param));
  return ret;
}


int Binlog_relay_IO_delegate::thread_stop(THD *thd, Master_info *mi)
{

  Binlog_relay_IO_param param;
  init_param(&param, mi);

  int ret= 0;
  FOREACH_OBSERVER(ret, thread_stop, thd, (&param));
  return ret;
}

int Binlog_relay_IO_delegate::before_request_transmit(THD *thd,
                                                      Master_info *mi,
                                                      ushort flags)
{
  Binlog_relay_IO_param param;
  init_param(&param, mi);

  int ret= 0;
  FOREACH_OBSERVER(ret, before_request_transmit, thd, (&param, (uint32)flags));
  return ret;
}

int Binlog_relay_IO_delegate::after_read_event(THD *thd, Master_info *mi,
                                               const char *packet, ulong len,
                                               const char **event_buf,
                                               ulong *event_len)
{
  Binlog_relay_IO_param param;
  init_param(&param, mi);

  int ret= 0;
  FOREACH_OBSERVER(ret, after_read_event, thd,
                   (&param, packet, len, event_buf, event_len));
  return ret;
}

int Binlog_relay_IO_delegate::after_queue_event(THD *thd, Master_info *mi,
                                                const char *event_buf,
                                                ulong event_len,
                                                bool synced)
{
  Binlog_relay_IO_param param;
  init_param(&param, mi);

  uint32 flags=0;
  if (synced)
    flags |= BINLOG_STORAGE_IS_SYNCED;

  int ret= 0;
  FOREACH_OBSERVER(ret, after_queue_event, thd,
                   (&param, event_buf, event_len, flags));
  return ret;
}

int Binlog_relay_IO_delegate::after_reset_slave(THD *thd, Master_info *mi)

{
  Binlog_relay_IO_param param;
  init_param(&param, mi);

  int ret= 0;
  FOREACH_OBSERVER(ret, after_reset_slave, thd, (&param));
  return ret;
}

#endif /* HAVE_REPLICATION */

int Raft_replication_delegate::before_flush(THD *thd, IO_CACHE* io_cache,
    RaftReplicateMsgOpType op_type)
{
  DBUG_ENTER("Raft_replication_delegate::before_flush");
  Raft_replication_param param;

  int ret= 0;

  FOREACH_OBSERVER_STRICT(ret, before_flush, thd, (&param, io_cache, op_type));

  DBUG_PRINT("return", ("term: %ld, index: %ld", param.term, param.index));

  /* (term, index) will be used later in before_commit hook of trans
   * observer */
  thd->set_trans_marker(param.term, param.index);

  DBUG_RETURN(ret);
}

int Raft_replication_delegate::before_commit(THD *thd, bool all)
{
  DBUG_ENTER("Raft_replications_delegate::before_commit");
  Raft_replication_param param;

  thd->get_trans_marker(&param.term, &param.index);

  DBUG_PRINT("enter", ("term: %ld, index: %ld", param.term, param.index));

  int ret= 0;
  FOREACH_OBSERVER_STRICT(ret, before_commit, thd, (&param));

  DEBUG_SYNC(thd, "after_call_after_sync_observer");
  DBUG_RETURN(ret);
}

int Raft_replication_delegate::setup_flush(
    THD *thd,
    Raft_replication_observer::st_setup_flush_arg *arg)
{
  DBUG_ENTER("Raft_replication_delegate::setup_flush");
  Raft_replication_param param;

  int ret= 0;

  FOREACH_OBSERVER_STRICT(ret, setup_flush, thd, (arg));

  DBUG_RETURN(ret);
}

int Raft_replication_delegate::before_shutdown(THD *thd)
{
  DBUG_ENTER("Raft_replication_delegate::before_shutdown");
  int ret= 0;
  Raft_replication_param param;

  FOREACH_OBSERVER_STRICT(ret, before_shutdown, thd, ());

  DBUG_RETURN(ret);
}

int Raft_replication_delegate::register_paths(
    THD *thd, const std::string &s_uuid, uint32_t server_id,
    const std::string &wal_dir_parent, const std::string &log_dir_parent,
    const std::string &raft_log_path_prefix, const std::string &s_hostname,
    uint64_t port)
{
  DBUG_ENTER("Raft_replication_delegate::register_paths");
  int ret= 0;
  Raft_replication_param param;

  FOREACH_OBSERVER_STRICT(ret, register_paths, thd,
                          (&raft_listener_queue, s_uuid, server_id,
                           wal_dir_parent, log_dir_parent, raft_log_path_prefix,
                           s_hostname, port));

  DBUG_RETURN(ret);
}

int Raft_replication_delegate::after_commit(THD *thd, bool all)
{
  DBUG_ENTER("Raft_replication_delegate::after_commit");
  Raft_replication_param param;

  thd->get_trans_marker(&param.term, &param.index);

  if (thd->m_force_raft_after_commit_hook)
  {
    DBUG_ASSERT(thd->rli_slave);
    sql_print_warning("Forcing raft after_commit hook for opid: %ld:%ld",
                      param.term, param.index);
  }

  thd->m_force_raft_after_commit_hook= false;

  int ret= 0;
  FOREACH_OBSERVER_STRICT(ret, after_commit, thd, (&param));
  DBUG_RETURN(ret);
}

int Raft_replication_delegate::purge_logs(THD *thd, uint64_t file_ext)
{
  DBUG_ENTER("Raft_replication_delegate::purge_logs");
  Raft_replication_param param;
  param.purge_file_ext = file_ext;
  int ret= 0;
  FOREACH_OBSERVER_STRICT(ret, purge_logs, thd, (&param));

  // Set the safe purge file that was sent back by the plugin
  thd->set_safe_purge_file(param.purge_file);

  DBUG_RETURN(ret);
}

int Raft_replication_delegate::show_raft_status(
    THD *thd,
    std::vector<std::pair<std::string, std::string>> *var_value_pairs)
{
  DBUG_ENTER("Raft_replication_delegate::show_raft_status");
  Raft_replication_param param;
  int ret= 0;
  FOREACH_OBSERVER_STRICT(ret, show_raft_status, thd, (var_value_pairs));
  DBUG_RETURN(ret);
}

int Raft_replication_delegate::inform_applier_health(THD *thd, bool healthy)
{
  DBUG_ENTER("Raft_replication_delegate::inform_applier_health");
  Raft_replication_param param;
  int ret= 0;
  FOREACH_OBSERVER_STRICT(ret, inform_applier_health, thd, (healthy));
  DBUG_RETURN(ret);
}

int Raft_replication_delegate::inform_heartbeats_health(THD *thd, bool healthy)
{
  DBUG_ENTER("Raft_replication_delegate::inform_heartbeats_health");
  Raft_replication_param param;
  int ret= 0;
  FOREACH_OBSERVER_STRICT(ret, inform_heartbeats_health, thd, (healthy));
  DBUG_RETURN(ret);
}

static int update_sys_var(const char *var_name, uint name_len, Item& update_item)
{
  // find_sys_var will take a mutex on LOCK_plugin, and a read lock on
  // LOCK_system_variables_hash
  sys_var *sys_var_ptr=
    find_sys_var(current_thd, var_name, name_len);
  if (sys_var_ptr)
  {
    LEX_STRING tmp;
    set_var set_v(OPT_GLOBAL, sys_var_ptr, &tmp, &update_item);
    return !set_v.check(current_thd) && set_v.update(current_thd);
  }

  return 1;
}

static int handle_read_only(
    const std::map<std::string, unsigned int>& sys_var_map){
  int error= 0;
  auto read_only_it= sys_var_map.find("read_only");
  auto super_read_only_it= sys_var_map.find("super_read_only");
  if (read_only_it == sys_var_map.end() &&
      super_read_only_it == sys_var_map.end())
    return 1;

  if (super_read_only_it != sys_var_map.end() &&
      super_read_only_it->second == 1)
  {
    // Case 1: set super_read_only=1. This will implicitly set read_only.
    Item_uint super_read_only_item(super_read_only_it->second);
    error= update_sys_var(
        STRING_WITH_LEN("super_read_only"), super_read_only_item);
  }
  else if (read_only_it != sys_var_map.end() && read_only_it->second == 0)
  {
    // Case 2: set read_only=0. This will implicitly unset super_read_only.
    Item_uint read_only_item(read_only_it->second);
    error= update_sys_var(STRING_WITH_LEN("read_only"), read_only_item);
  }
  else
  {
    // Case 3: Need to set read_only=1 OR/AND set super_read_only=0
    if (read_only_it != sys_var_map.end())
    {
      Item_uint read_only_item(read_only_it->second);
      error= update_sys_var(STRING_WITH_LEN("read_only"), read_only_item);
    }

    if (!error && super_read_only_it != sys_var_map.end())
    {
      Item_uint super_read_only_item(super_read_only_it->second);
      error= update_sys_var(
          STRING_WITH_LEN("super_read_only"), super_read_only_item);
    }
  }

  return error;
}


static int set_durability(const std::map<std::string, unsigned int>& durability){
  auto sync_binlog_it= durability.find("sync_binlog");
  if (sync_binlog_it == durability.end())
  {
    return 1;
  }
  Item_uint sync_binlog_item(sync_binlog_it->second);
  int sync_binlog_update = update_sys_var(
    STRING_WITH_LEN("sync_binlog"), sync_binlog_item);

  if (sync_binlog_update) // failed
    return sync_binlog_update;

  // These two might not always update since innodb might not be enabled
  auto flush_log_it= durability.find("innodb_flush_log_at_trx_commit");
  if (flush_log_it != durability.end())
  {
    Item_uint flush_log_item(flush_log_it->second);
    update_sys_var(STRING_WITH_LEN("innodb_flush_log_at_trx_commit"),
      flush_log_item);
  }

  auto doublewrite_it= durability.find("innodb_doublewrite");
  if (doublewrite_it != durability.end())
  {
    Item_uint doublewrite_item(doublewrite_it->second);
    update_sys_var(STRING_WITH_LEN("innodb_doublewrite"), doublewrite_item);
  }

  return sync_binlog_update;
}

int register_trans_observer(Trans_observer *observer, void *p)
{
  return transaction_delegate->add_observer(observer, (st_plugin_int *)p);
}

int unregister_trans_observer(Trans_observer *observer, void *p)
{
  return transaction_delegate->remove_observer(observer, (st_plugin_int *)p);
}

int register_binlog_storage_observer(Binlog_storage_observer *observer, void *p)
{
  DBUG_ENTER("register_binlog_storage_observer");
  int result= binlog_storage_delegate->add_observer(observer, (st_plugin_int *)p);
  DBUG_RETURN(result);
}

int unregister_binlog_storage_observer(Binlog_storage_observer *observer, void *p)
{
  return binlog_storage_delegate->remove_observer(observer, (st_plugin_int *)p);
}

#ifdef HAVE_REPLICATION
int register_binlog_transmit_observer(Binlog_transmit_observer *observer, void *p)
{
  return binlog_transmit_delegate->add_observer(observer, (st_plugin_int *)p);
}

int unregister_binlog_transmit_observer(Binlog_transmit_observer *observer, void *p)
{
  return binlog_transmit_delegate->remove_observer(observer, (st_plugin_int *)p);
}

int register_binlog_relay_io_observer(Binlog_relay_IO_observer *observer, void *p)
{
  return binlog_relay_io_delegate->add_observer(observer, (st_plugin_int *)p);
}

int unregister_binlog_relay_io_observer(Binlog_relay_IO_observer *observer, void *p)
{
  return binlog_relay_io_delegate->remove_observer(observer, (st_plugin_int *)p);
}
#else
int register_binlog_transmit_observer(Binlog_transmit_observer *observer, void *p)
{
  return 0;
}

int unregister_binlog_transmit_observer(Binlog_transmit_observer *observer, void *p)
{
  return 0;
}

int register_binlog_relay_io_observer(Binlog_relay_IO_observer *observer, void *p)
{
  return 0;
}

int unregister_binlog_relay_io_observer(Binlog_relay_IO_observer *observer, void *p)
{
  return 0;
}
#endif /* HAVE_REPLICATION */

int register_raft_replication_observer(
    Raft_replication_observer *observer, void *p)
{
  DBUG_ENTER("register_raft_replication_observer");
  raft_listener_queue.init();
  int result= raft_replication_delegate->add_observer(
      observer, (st_plugin_int *)p);
  DBUG_RETURN(result);
}

int unregister_raft_replication_observer(
    Raft_replication_observer *observer, void *p)
{
  raft_listener_queue.deinit();
  return raft_replication_delegate->remove_observer(
      observer, (st_plugin_int *)p);
}

#ifndef MYSQL_CLIENT
pthread_handler_t process_raft_queue(void *arg)
{
  THD *thd;
  bool thd_added= false;

  /* Setup this thread */
  my_thread_init();
  thd= new THD;
  pthread_detach_this_thread();
  thd->thread_stack= (char *)&thd;
  thd->store_globals();
  thd->thr_create_utime= thd->start_utime= my_micro_time();
  thd->security_ctx->skip_grants();

  mutex_lock_shard(SHARDED(&LOCK_thread_count), thd);
  add_global_thread(thd);
  thd_added= true;
  mutex_unlock_shard(SHARDED(&LOCK_thread_count), thd);

  /* Start listening for new events in the queue */
  bool exit= false;
  // The exit is triggered by the raft plugin gracefully
  // enqueing an exit function for this thread.
  // if we listen to abort_loop or thd->killed
  // then we might not give the plugin the opportunity to
  // do some critical tasks before exit
  while (!exit)
  {
    thd->get_stmt_da()->reset_diagnostics_area();
    RaftListenerQueue::QueueElement element= raft_listener_queue.get();
    RaftListenerCallbackResult result;
    switch (element.type)
    {
      case RaftListenerCallbackType::SET_READ_ONLY:
      {
        result.error = handle_read_only(element.arg.val_sys_var_uint);
        break;
      }
      case RaftListenerCallbackType::ROTATE_BINLOG:
      {
        result.error= rotate_binlog_file(current_thd);
        break;
      }
      case RaftListenerCallbackType::ROTATE_RELAYLOG:
      {
#ifdef HAVE_REPLICATION
        RaftRotateInfo raft_rotate_info;
        raft_rotate_info.new_log_ident= element.arg.log_file_pos.first;
        raft_rotate_info.pos= element.arg.log_file_pos.second;
        myf flags= MYF(element.arg.val_uint);
        raft_rotate_info.noop= flags & RaftListenerQueue::RAFT_FLAGS_NOOP;
        raft_rotate_info.post_append= flags &
                                      RaftListenerQueue::RAFT_FLAGS_POSTAPPEND;
        raft_rotate_info.rotate_opid= element.arg.val_opid;
        result.error= rotate_relay_log_for_raft(&raft_rotate_info);
#endif
        break;
      }
      case RaftListenerCallbackType::RAFT_LISTENER_THREADS_EXIT:
        exit= true;
        break;
      case RaftListenerCallbackType::TRIM_LOGGED_GTIDS:
        result.error= trim_logged_gtid(element.arg.trim_gtids);
        break;
      case RaftListenerCallbackType::RLI_RELAY_LOG_RESET:
      {
#ifdef HAVE_REPLICATION
        result.error= rli_relay_log_raft_reset(element.arg.log_file_pos);
#endif
        break;
      }
      case RaftListenerCallbackType::RESET_SLAVE:
      {
#ifdef HAVE_REPLICATION
        result.error= raft_reset_slave(current_thd);
        // When resetting a slave we also want to clear the read-only message
        // since we can't make assumptions on the master instance anymore
        if (!result.error)
        {
          Item_string item("", 0, current_thd->charset());
          result.error= update_sys_var(
              STRING_WITH_LEN("read_only_error_msg_extra"), item);
        }
#endif
        break;
      }
      case RaftListenerCallbackType::CHANGE_MASTER:
      {
#ifdef HAVE_REPLICATION
        const MysqlPrimaryInfo &info = element.arg.primary_info;
        result.error= raft_change_master(current_thd, info);
        if (!result.error && !element.arg.val_str.empty())
        {
          Item_string item(element.arg.val_str.c_str(),
                           element.arg.val_str.length(),
                           current_thd->charset());
          result.error=
            update_sys_var(STRING_WITH_LEN("read_only_error_msg_extra"), item);
        }
#endif
        break;
      }
      case RaftListenerCallbackType::BINLOG_CHANGE_TO_APPLY:
      {
        result.error= binlog_change_to_apply();
        break;
      }
      case RaftListenerCallbackType::BINLOG_CHANGE_TO_BINLOG:
      {
        result.error= binlog_change_to_binlog();
        break;
      }
      case RaftListenerCallbackType::STOP_SQL_THREAD:
      {
#ifdef HAVE_REPLICATION
        result.error= raft_stop_sql_thread(current_thd);
#endif
        break;
      }
      case RaftListenerCallbackType::START_SQL_THREAD:
      {
#ifdef HAVE_REPLICATION
        result.error= raft_start_sql_thread(current_thd);
#endif
        break;
      }
      case RaftListenerCallbackType::STOP_IO_THREAD:
      {
#ifdef HAVE_REPLICATION
        result.error= raft_stop_io_thread(current_thd);
#endif
        break;
      }
      case RaftListenerCallbackType::GET_COMMITTED_GTIDS:
      {
        result.error= get_committed_gtids(element.arg.trim_gtids,
            &result.gtids);
        break;
      }
      case RaftListenerCallbackType::GET_EXECUTED_GTIDS:
      {
        result.error= get_executed_gtids(&result.val_str);
        break;
      }
      case RaftListenerCallbackType::SET_BINLOG_DURABILITY:
      {
        result.error= set_durability(element.arg.val_sys_var_uint);
        break;
      }
      case RaftListenerCallbackType::RAFT_CONFIG_CHANGE:
      {
        result.error= raft_config_change(current_thd,
                                         std::move(element.arg.val_str));
        break;
      }
      case RaftListenerCallbackType::RAFT_UPDATE_FOLLOWER_INFO:
      {
        result.error= raft_update_follower_info(element.arg.val_str_map,
                                                element.arg.val_bool,
                                                element.arg.is_shutdown);
        break;
      }
      case RaftListenerCallbackType::HANDLE_DUMP_THREADS:
      {
        result.error= handle_dump_threads(element.arg.val_bool);
        break;
      }
      default:
        break;
    }

    // Fulfill the promise (if requested)
    if (element.result)
      element.result->set_value(std::move(result));
  }

  // Cleanup and exit
  dec_thread_running();
  thd->release_resources();
  if (thd_added)
  {
    remove_global_thread(thd);
  }
  delete thd;
  my_thread_end();

  sql_print_information("Raft listener queue aborted");

  pthread_exit(0);
  return 0;
}

int start_raft_listener_thread()
{
  pthread_t th;
  int error= 0;
  if ((error= mysql_thread_create(0,
                                  &th,
                                  &connection_attrib,
                                  process_raft_queue,
                                  (void *) 0)))
  {
    // NO_LINT_DEBUG
    sql_print_error("Could not create raft_listener_thread");
    return 1;
  }

  return 0;
}

RaftListenerQueue::~RaftListenerQueue()
{
  deinit();
}

int RaftListenerQueue::add(QueueElement element)
{
  std::unique_lock<std::mutex> lock(queue_mutex_);
  if (!inited_)
  {
    // NO_LINT_DEBUG
    sql_print_error("Raft listener queue and thread is not inited");
    return 1;
  }

  queue_.emplace(std::move(element));
  lock.unlock();
  queue_cv_.notify_all();

  return 0;
}

RaftListenerQueue::QueueElement RaftListenerQueue::get()
{
  // Wait for something to be put into the event queue
  std::unique_lock<std::mutex> lock(queue_mutex_);
  while (queue_.empty())
    queue_cv_.wait(lock);

  QueueElement element= queue_.front();
  queue_.pop();

  return element;
}

int RaftListenerQueue::init()
{
  sql_print_information("Initializing Raft listener queue");
  std::unique_lock<std::mutex> lock(init_mutex_);
  if (inited_)
    return 0; // Already inited

  if (start_raft_listener_thread())
    return 1; // Fails to initialize

  inited_= true;
  return 0; // Initialization success
}

void RaftListenerQueue::deinit()
{
  std::unique_lock<std::mutex> lock(init_mutex_);
  if (!inited_)
    return;

  fprintf(stderr, "Shutting down Raft listener queue");

  // Queue an exit event in the queue. The listener thread will eventually pick
  // this up and exit
  std::promise<RaftListenerCallbackResult> prms;
  auto fut = prms.get_future();
  QueueElement element;
  element.type= RaftListenerCallbackType::RAFT_LISTENER_THREADS_EXIT;
  element.result = &prms;
  add(element);
  fut.get();

  inited_= false;
  return;
}

#endif /* !defined(MYSQL_CLIENT) */
