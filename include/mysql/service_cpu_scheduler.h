/* Copyright (c) 2004-present, Meta. All rights reserved.
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

#pragma once

/**
  @file include/mysql/service_cpu_scheduler.h
*/

#include "my_inttypes.h"

class THD;

/**
  Scheduler hint type (for TpScheduler).
*/
using tp_scheduler_hint = void *;

/**
  Handle to a connection object storing MySQL related state for a task.
  It is not needed if the task does not call into MySQL.
*/
using tp_conn_handle = void *;

/**
  Handle to tenant id.
*/
using tp_tenant_id_handle = void *;

/**
  Signature for cpu scheduler task routine.
*/
using tp_routine = void *(*)(void *);

/**
  CPU usage stats.
*/
struct tp_cpu_stats {
  int64_t cpu_usage_ns;
  int64_t delay_total_ns;
};

extern "C" struct cpu_scheduler_service_st {
  bool (*enqueue_task)(tp_routine routine, void *param,
                       tp_tenant_id_handle tenant_id, tp_scheduler_hint &hint);
  tp_conn_handle (*create_connection)(THD *thd, const char *db,
                                      bool acquire_conn_slot);
  void (*destroy_connection)(tp_conn_handle conn_handle);
  void (*attach_connection)(tp_conn_handle conn_handle);
  void (*detach_connection)(tp_conn_handle conn_handle);
  tp_tenant_id_handle (*get_connection_tenant_id)(tp_conn_handle conn_handle);
  void (*destroy_tenant_id)(tp_tenant_id_handle tenant_id);
  bool (*get_current_task_cpu_stats)(tp_cpu_stats &cpu_stats);
  int (*get_current_task_wait_stats)(char* buf_stats, size_t buf_len);
  bool (*is_scheduler_enabled)();
  tp_conn_handle (*get_current_task_connection)();
  tp_tenant_id_handle (*get_tenant_id)(const char *db);
} * cpu_scheduler_service;

/**
  Service APIs.
*/
#ifdef MYSQL_DYNAMIC_PLUGIN

#define tp_enqueue_task(_ROUTINE, _PARAM, _TENANT_ID, _HINT) \
  cpu_scheduler_service->enqueue_task(_ROUTINE, _PARAM, _TENANT_ID, _HINT)
#define tp_create_connection(_THD, _DB, _ACQUIRE_SLOT) \
  cpu_scheduler_service->create_connection(_THD, _DB, _ACQUIRE_SLOT)
#define tp_destroy_connection(_CONN_HANDLE) \
  cpu_scheduler_service->destroy_connection(_CONN_HANDLE)
#define tp_attach_connection(_CONN_HANDLE) \
  cpu_scheduler_service->attach_connection(_CONN_HANDLE)
#define tp_detach_connection(_CONN_HANDLE) \
  cpu_scheduler_service->detach_connection(_CONN_HANDLE)
#define tp_get_connection_tenant_id(_CONN_HANDLE) \
  cpu_scheduler_service->get_connection_tenant_id(_CONN_HANDLE)
#define tp_destroy_tenant_id(_TENANT_ID) \
  cpu_scheduler_service->destroy_tenant_id(_TENANT_ID)
#define tp_get_current_task_cpu_stats(_CPU_STATS) \
  cpu_scheduler_service->get_current_task_cpu_stats(_CPU_STATS)
#define tp_get_current_task_wait_stats(_BUF_, _BUF_LEN) \
  cpu_scheduler_service->get_current_task_wait_stats(_BUF_, _BUF_LEN)
#define tp_is_scheduler_enabled() cpu_scheduler_service->is_scheduler_enabled()
#define tp_get_current_task_connection() \
  cpu_scheduler_service->get_current_task_connection()
#define tp_get_tenant_id(_DB) cpu_scheduler_service->get_tenant_id(_DB)

#else

/**
  Enqueue specified task (routine+param) for execution on a scheduler.

  @param routine Routine to execute.
  @param param Routine parameter.
  @param tenant_id Tenant id to associate task with. Passing nullptr will
                   enqueue the task in the system tenant.
  @param hint Scheduler hint to use, and returns new hint that can be used for
              subsequent enqueue to co-locate it together with previous.
  @return true if task was enqueued, false otherwise.
*/
bool tp_enqueue_task(tp_routine routine, void *param,
                     tp_tenant_id_handle tenant_id, tp_scheduler_hint &hint);

/**
  Create connection object and assign it to a database.

  @param thd THD to attach to the connection.
  @param db Database name to assign the connection to.
  @param acquire_conn_slot Account against thread_pool_max_db_connections.
  @return Connection handle, or nullptr on failure.
*/
tp_conn_handle tp_create_connection(THD *thd, const char *db,
                                    bool acquire_conn_slot);

/**
  Destroy connection object.

  @param conn_handle Connection to destroy.
*/
void tp_destroy_connection(tp_conn_handle conn_handle);

/**
  Attach connection to current task.

  @param conn_handle Connection to attach.
*/
void tp_attach_connection(tp_conn_handle conn_handle);

/**
  Detach connection from current task.

  @param conn_handle Connection to detach.
*/
void tp_detach_connection(tp_conn_handle conn_handle);

/**
  Get connection tenant id.

  @param conn_handle Connection to get tenant id for.
*/
tp_tenant_id_handle tp_get_connection_tenant_id(tp_conn_handle conn_handle);

/**
  Destroy connection tenant id.

  @param tenant_id Tenant id handle to destroy.
*/
void tp_destroy_tenant_id(tp_tenant_id_handle tenant_id);

/**
  Get CPU stats for current task.

  @param cpu_stats Stats struct to populate.
  @return true  if stats are populated,
          false if current thread doesn't have a CPU scheduler task.
*/
bool tp_get_current_task_cpu_stats(tp_cpu_stats &cpu_stats);

/**
  Get wait stats for current task.

  @param buf buffer to store the wait stats.
  @param len buffer len.
  @return expected buffer size.
*/
int tp_get_current_task_wait_stats(char* buf_stats, size_t buf_len);

/**
  Is CPU scheduler enabled? This helps decide whether to use scheduler APIs or
  fall back to the regular OS threads. If the scheduler is disabled in the
  middle of a sequence of API calls the remaining APIs will still succeed.

  @return true if enabled, false otherwise.
*/
bool tp_is_scheduler_enabled();

/**
  Get connection for current task. This connection can be used to obtain its
  tenant and then enqueue child tasks.

  @return Connection if current thread is running CPU scheduler task,
          nullptr if current thread is not a CPU scheduler task.
*/
tp_conn_handle tp_get_current_task_connection();

/**
  Get tenant for db name. This allows altering the sequence of enqueueing a
  task. Instead of creating THD and connection first and getting the tenant
  from the connection, this API could be used to get the tenant. After
  enqueueing the task, THD and connection can be created on the task itself.

  @param db Database name of the future connection. Note: this API does not
            acquire connection slot.
  @return Tenant id handle or nullptr if tenant for db doesn't exist.
          Note: tenant is created on demand when a first connection to db is
          created. So this API will fail until first successful
          tp_create_connection call for this db name.
*/
tp_tenant_id_handle tp_get_tenant_id(const char *db);

#endif
