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

extern "C" struct cpu_scheduler_service_st {
  bool (*enqueue_task)(tp_routine routine, void *param,
                       tp_tenant_id_handle tenant_id, tp_scheduler_hint &hint);
  tp_conn_handle (*create_connection)(THD *thd, const char *db);
  void (*destroy_connection)(tp_conn_handle conn_handle);
  void (*attach_connection)(tp_conn_handle conn_handle);
  void (*detach_connection)(tp_conn_handle conn_handle);
  tp_tenant_id_handle (*get_connection_tenant_id)(tp_conn_handle conn_handle);
  void (*destroy_tenant_id)(tp_tenant_id_handle tenant_id);
} * cpu_scheduler_service;

/**
  Service APIs.
*/
#ifdef MYSQL_DYNAMIC_PLUGIN

#define tp_enqueue_task(_ROUTINE, _PARAM, _TENANT_ID, _HINT) \
  cpu_scheduler_service->enqueue_task(_ROUTINE, _PARAM, _TENANT_ID, _HINT)
#define tp_create_connection(_THD, _DB) \
  cpu_scheduler_service->create_connection(_THD, _DB)
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

#else

/**
  Enqueue specified task (routine+param) for execution on a scheduler.

  @param routine Routine to execute.
  @param param Routine parameter.
  @param tenant_id Tenant id to associate task with.
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
  @return Connection handle, or nullptr on failure.
*/
tp_conn_handle tp_create_connection(THD *thd, const char *db);

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

#endif
