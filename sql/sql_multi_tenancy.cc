/* Copyright (c) 2016, Facebook. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */


#include "sql_acl.h"
#include "sql_priv.h"
#include "sql_multi_tenancy.h"
#include "global_threads.h"


#ifndef EMBEDDED_LIBRARY


/*
  @param sql_command command the thread is currently executing

  @return true Skips the current query in admission control
          false Admission control checks are applied for this query
*/
static bool filter_command(enum_sql_command sql_command)
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
      return IS_BIT_SET(admission_control_filter, ADMISSION_CONTROL_ALTER);

    case SQLCOM_BEGIN:
      return IS_BIT_SET(admission_control_filter, ADMISSION_CONTROL_BEGIN);

    case SQLCOM_COMMIT:
      return IS_BIT_SET(admission_control_filter, ADMISSION_CONTROL_COMMIT);

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
      return IS_BIT_SET(admission_control_filter, ADMISSION_CONTROL_CREATE);

    case SQLCOM_DELETE:
    case SQLCOM_DELETE_MULTI:
      return IS_BIT_SET(admission_control_filter, ADMISSION_CONTROL_DELETE);

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
      return IS_BIT_SET(admission_control_filter, ADMISSION_CONTROL_DROP);

    case SQLCOM_INSERT:
    case SQLCOM_INSERT_SELECT:
      return IS_BIT_SET(admission_control_filter, ADMISSION_CONTROL_INSERT);

    case SQLCOM_LOAD:
      return IS_BIT_SET(admission_control_filter, ADMISSION_CONTROL_LOAD);

    case SQLCOM_SELECT:
      return IS_BIT_SET(admission_control_filter, ADMISSION_CONTROL_SELECT);

    case SQLCOM_SET_OPTION:
      return IS_BIT_SET(admission_control_filter, ADMISSION_CONTROL_SET);

    case SQLCOM_REPLACE:
    case SQLCOM_REPLACE_SELECT:
      return IS_BIT_SET(admission_control_filter, ADMISSION_CONTROL_REPLACE);

    case SQLCOM_ROLLBACK:
      return IS_BIT_SET(admission_control_filter, ADMISSION_CONTROL_ROLLBACK);

    case SQLCOM_TRUNCATE:
      return IS_BIT_SET(admission_control_filter,  ADMISSION_CONTROL_TRUNCATE);

    case SQLCOM_UPDATE:
    case SQLCOM_UPDATE_MULTI:
      return IS_BIT_SET(admission_control_filter, ADMISSION_CONTROL_UPDATE);

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
    case SQLCOM_SHOW_MASTER_STAT:
    case SQLCOM_SHOW_SLAVE_STAT:
    case SQLCOM_SHOW_GRANTS:
    case SQLCOM_SHOW_CREATE:
    case SQLCOM_SHOW_CHARSETS:
    case SQLCOM_SHOW_COLLATIONS:
    case SQLCOM_SHOW_CREATE_DB:
    case SQLCOM_SHOW_TABLE_STATUS:
    case SQLCOM_SHOW_TRIGGERS:
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
    case SQLCOM_SHOW_CONNECTION_ATTRIBUTES:
      return IS_BIT_SET(admission_control_filter, ADMISSION_CONTROL_SHOW);

    default:
      return false;
  }
}


/*
 * Add a connection in multi-tenancy plugin
 *
 * @param thd THD structure
 * @param attr_map connection attribute map
 *
 * @return 0 if the connection is added, 1 if rejected
 */
int multi_tenancy_add_connection(THD *thd, ATTRS_MAP_T &attr_map)
{
  int ret = 0;
  st_mysql_multi_tenancy *data= NULL;

  if (thd->variables.multi_tenancy_plugin)
  {
    mysql_mutex_lock(&thd->LOCK_thd_data);
    data = plugin_data(thd->variables.multi_tenancy_plugin,
                       struct st_mysql_multi_tenancy *);
    ret = (int) data->
      request_resource(
        thd,
        MT_RESOURCE_TYPE::MULTI_TENANCY_RESOURCE_CONNECTION,
        &attr_map);
    mysql_mutex_unlock(&thd->LOCK_thd_data);

    // plugin is turned off
    if (ret == (int) MT_RETURN_TYPE::MULTI_TENANCY_RET_FALLBACK)
      ret = 0;
  }

  return ret;
}


/*
 * Release a connection in multi-tenancy plugin
 *
 * @param thd THD structure
 * @param attr_map connection attribute map
 *
 * @return 0 if successful, 1 if otherwise
 */
int multi_tenancy_close_connection(THD *thd, ATTRS_MAP_T &attr_map)
{
  st_mysql_multi_tenancy *data= NULL;
  if (thd->variables.multi_tenancy_plugin)
  {
    mysql_mutex_lock(&thd->LOCK_thd_data);
    data = plugin_data(thd->variables.multi_tenancy_plugin,
                       struct st_mysql_multi_tenancy *);
    data->release_resource(
        thd,
        MT_RESOURCE_TYPE::MULTI_TENANCY_RESOURCE_CONNECTION,
        &attr_map);
    mysql_mutex_unlock(&thd->LOCK_thd_data);
  }

  return 0;
}


/*
 * Admit a query based on database entity
 *
 * @param thd THD structure
 * @param attr_map Query attribute map for the query
 *
 * @return 0 if the query is admitted, 1 if otherwise
 */
int multi_tenancy_admit_query(THD *thd, ATTRS_MAP_T &attr_map)
{
  bool admission_check= false;

  auto entity_iter = attr_map.find(multi_tenancy_entity_db);
  if (entity_iter == attr_map.end())
    return 0; // no limiting

  /**
    Admission control checks are skipped in the following
    scenarios.
    1. The query is run by super user
    2. The THD is a replication thread
    3. If this is a multi query packet, or is part of a transaction (controlled
       by global var opt_admission_control_by_trx), and the THD already entered
       admission control
    4. No database is set for this session
    5. Admission control checks are turned off by setting
       max_running_queries=0
    6. If the command is filtered from admission checks
  */
  if (!(thd->security_ctx->master_access & SUPER_ACL) && /* 1 */
      !thd->rli_slave && /* 2 */
      ((!opt_admission_control_by_trx || thd->is_real_trans) &&
       !thd->is_in_ac) && /* 3 */
      !entity_iter->second.empty() && /* 4 */
      db_ac->get_max_running_queries() && /* 5 */
      !filter_command(thd->lex->sql_command) /* 6 */
     ) {
    admission_check= true;
  }

  if (admission_check && db_ac->admission_control_enter(thd, attr_map))
    return 1;

  if (admission_check)
    thd->is_in_ac = true;

  return 0;
}


/*
 * Exit a query based on database entity
 *
 * @param thd THD structure
 * @param attr_map Query attribute map for the query
 *
 * @return 0 if successful, 1 if otherwise
 */
int multi_tenancy_exit_query(THD *thd, ATTRS_MAP_T &attr_map)
{
  if (attr_map.find(multi_tenancy_entity_db) == attr_map.end())
    return 0; // no limiting

  db_ac->admission_control_exit(thd, attr_map);
  return 0;
}


int initialize_multi_tenancy_plugin(st_plugin_int *plugin_int)
{
  if (plugin_int->plugin->init && plugin_int->plugin->init(plugin_int))
  {
    sql_print_error("Plugin '%s' init function returned error.",
                    plugin_int->name.str);
    return 1;
  }

  /* Make the interface info more easily accessible */
  plugin_int->data= plugin_int->plugin->info;

  /* release the ref count of the previous multi-tenancy plugin */
  if (global_system_variables.multi_tenancy_plugin)
    plugin_unlock(NULL, global_system_variables.multi_tenancy_plugin);

  /* Add the global var referencing the plugin */
  plugin_ref plugin_r= plugin_int_to_ref(plugin_int);
  global_system_variables.multi_tenancy_plugin=
    my_plugin_lock(NULL, &plugin_r);

  return 0;
}


int finalize_multi_tenancy_plugin(st_plugin_int *plugin_int)
{
  /* release the ref count of the previous multi-tenancy plugin */
  if (global_system_variables.multi_tenancy_plugin)
  {
    plugin_unlock(NULL, global_system_variables.multi_tenancy_plugin);
    global_system_variables.multi_tenancy_plugin= NULL;
  }

  if (plugin_int->plugin->deinit && plugin_int->plugin->deinit(NULL))
  {
    DBUG_PRINT("warning", ("Plugin '%s' deinit function returned error.",
                            plugin_int->name.str));
    DBUG_EXECUTE("finalize_audit_plugin", return 1; );
  }

  plugin_int->data= NULL;
  return 0;
}


#else /* EMBEDDED_LIBRARY */


int multi_tenancy_add_connection(THD *thd, ATTRS_MAP_T &attr_map)
{
  return 1;
}

int multi_tenancy_close_connection(THD *thd, ATTRS_MAP_T &attr_map)
{
  return 1;
}

int multi_tenancy_admit_query(THD *thd, ATTRS_MAP_T &attr_map)
{
  return 1;
}

int multi_tenancy_exit_query(THD *thd, ATTRS_MAP_T &attr_map)
{
  return 1;
}


int initialize_multi_tenancy_plugin(st_plugin_int *plugin)
{
  return 1;
}


int finalize_multi_tenancy_plugin(st_plugin_int *plugin)
{
  return 1;
}


#endif /* EMBEDDED_LIBRARY */


AC *db_ac; // admission control object

/**
   @param thd THD structure.
   @param entity current session's entity.

   Applies admission control checks for the entity. Outline of
   the steps in this function:
   1. Error out if we crossed the max waiting limit.
   2. Put the thd in a queue.
   3. If we crossed the max running limit then wait for signal from threads
      that completed their query execution.

   @return False Run this query.
           True  maximum waiting queries limit reached. Error out this query.
*/
bool AC::admission_control_enter(THD* thd, ATTRS_MAP_T &attr_map) {
  bool error = false;
  DBUG_ASSERT(attr_map.find(multi_tenancy_entity_db) != attr_map.end());
  const std::string &entity = attr_map.at(multi_tenancy_entity_db);
  const char* prev_proc_info = thd->proc_info;
  THD_STAGE_INFO(thd, stage_admission_control_enter);
  bool release_lock_ac = true;
  // Unlock this before waiting.
  mysql_rwlock_rdlock(&LOCK_ac);
  if (max_running_queries) {
    auto it = ac_map.find(entity);
    if (it == ac_map.end()) {
      // New DB.
      mysql_rwlock_unlock(&LOCK_ac);
      insert(entity);
      mysql_rwlock_rdlock(&LOCK_ac);
      it = ac_map.find(entity);
    }

    if (!thd->ac_node) {
      // Both THD and the admission control queue will share the object
      // created here.
      thd->ac_node = std::make_shared<st_ac_node>();
    }
    auto ac_info = it->second;
    MT_RETURN_TYPE ret = MT_RETURN_TYPE::MULTI_TENANCY_RET_FALLBACK;
    st_mysql_multi_tenancy *data= NULL;
    mysql_mutex_lock(&ac_info->lock);
    // If a multi_tenancy plugin exists, check plugin for per-entity limit
    if (thd->variables.multi_tenancy_plugin)
    {
      data = plugin_data(thd->variables.multi_tenancy_plugin,
                         struct st_mysql_multi_tenancy *);
      ret = data->request_resource(
          thd,
          MT_RESOURCE_TYPE::MULTI_TENANCY_RESOURCE_QUERY,
          &attr_map);
    }
    // if plugin is disabled, fallback to check global query limit
    if (ret == MT_RETURN_TYPE::MULTI_TENANCY_RET_FALLBACK)
    {
      if (ac_info->queue.size() < max_running_queries)
        ret = MT_RETURN_TYPE::MULTI_TENANCY_RET_ACCEPT;
      else
      {
        if (max_waiting_queries &&
            ac_info->queue.size() >= max_running_queries + max_waiting_queries)
          ret = MT_RETURN_TYPE::MULTI_TENANCY_RET_REJECT;
        else
          ret = MT_RETURN_TYPE::MULTI_TENANCY_RET_WAIT;
      }
    }

    switch (ret)
    {
      case MT_RETURN_TYPE::MULTI_TENANCY_RET_REJECT:
        ++total_aborted_queries;
        // We reached max waiting limit. Error out
        mysql_mutex_unlock(&ac_info->lock);
        error = true;
        break;

      case MT_RETURN_TYPE::MULTI_TENANCY_RET_WAIT:
        ac_info->queue.push_back(thd->ac_node);
        ++ac_info->waiting_queries;
        /**
          Inserting or deleting in std::map will not invalidate existing
          iterators except of course if the current iterator is erased. If the
          db corresponding to this iterator is getting dropped, these waiting
          queries are given signal to abort before the iterator
          is erased. See AC::remove().
          So, we don't need LOCK_ac here. The motivation to unlock the read lock
          is that waiting queries here shouldn't block other operations
          modifying ac_map or max_running_queries/max_waiting_queries.
        */
        mysql_rwlock_unlock(&LOCK_ac);
        release_lock_ac = false;
        wait_for_signal(thd, thd->ac_node, ac_info);
        --ac_info->waiting_queries;
        break;

      case MT_RETURN_TYPE::MULTI_TENANCY_RET_ACCEPT:
        // We are below the max running limit.
        ac_info->queue.push_back(thd->ac_node);
        mysql_mutex_unlock(&ac_info->lock);
        break;

      default:
        // unreachable branch
        DBUG_ASSERT(0);
    }
  }
  if (release_lock_ac) {
    mysql_rwlock_unlock(&LOCK_ac);
  }
  thd->proc_info = prev_proc_info;
  return error;
}

void AC::wait_for_signal(THD* thd, std::shared_ptr<st_ac_node>& ac_node,
                         std::shared_ptr<Ac_info> ac_info) {
  PSI_stage_info old_stage;
  mysql_mutex_lock(&ac_node->lock);
  /**
    The locking order followed during admission_control_enter() is
    lock ac_info
    lock ac_node
    unlock ac_info
    unlock ac_node
    The locks are interleaved to avoid possible races which makes
    this waiting thread miss the signal from admission_control_exit().
  */
  mysql_mutex_unlock(&ac_info->lock);
  thd->ENTER_COND(&ac_node->cond, &ac_node->lock,
                  &stage_waiting_for_admission,
                  &old_stage);
  // Spurious wake-ups are rare and fine in this design.
  mysql_cond_wait(&ac_node->cond, &ac_node->lock);
  thd->EXIT_COND(&old_stage);
}

/**
  @param thd THD structure
  @param entity current session's entity

  Signals one waiting thread. Pops out the first THD in the queue.
*/
void AC::admission_control_exit(THD* thd, ATTRS_MAP_T &attr_map) {
  DBUG_ASSERT(attr_map.find(multi_tenancy_entity_db) != attr_map.end());
  const std::string &entity = attr_map.at(multi_tenancy_entity_db);
  const char* prev_proc_info = thd->proc_info;
  THD_STAGE_INFO(thd, stage_admission_control_exit);
  mysql_rwlock_rdlock(&LOCK_ac);
  auto it = ac_map.find(entity);
  if (it != ac_map.end()) {
    auto ac_info = it->second.get();
    st_mysql_multi_tenancy *data= NULL;
    mysql_mutex_lock(&ac_info->lock);
    // If a multi_tenancy plugin exists, check plugin for per-entity limit
    if (thd->variables.multi_tenancy_plugin)
    {
      data = plugin_data(thd->variables.multi_tenancy_plugin,
                         struct st_mysql_multi_tenancy *);
      data->release_resource(
          thd,
          MT_RESOURCE_TYPE::MULTI_TENANCY_RESOURCE_QUERY,
          &attr_map);
    }
    if (max_running_queries &&
        ac_info->queue.size() > max_running_queries) {
      signal(ac_info->queue[max_running_queries]);
    }
    // The queue is empty if max_running_queries is toggled to 0
    // when this THD is inside admission_control_enter().
    if (ac_info->queue.size())
    {
      /**
        The popped value here doesn't necessarily give the ac_node of the
        current THD. It is better if the popped value is not accessed at all.
      */
      ac_info->queue.pop_front();
    }
    mysql_mutex_unlock(&ac_info->lock);
  }
  mysql_rwlock_unlock(&LOCK_ac);
  thd->proc_info = prev_proc_info;
}
