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

/*
 * sql_admission_control.h/cc
 *
 * This module handles multi-tenancy resource allocation on the server side.
 * Functions will call into multi-tenancy plugin interfaces (if installed) to
 * make a decision of whether or not a resource can be allocated. Connections
 * and query limits are among the resources multi-tenancy plugin allocates.
 * Note that admission control is now part of the multi-tenancy module to
 * hanlde query throttling.
 *
 * The isolation level is defined by an entity. The entity could be a database,
 * a user, or any thing that multi-tenancy plugin defines to isolate the
 * resource allocation.
 *
 * See sql_admission_control.cc for implementation.
 */

#ifndef _sql_admission_control_h
#define _sql_admission_control_h

#include "mysql/components/services/bits/mysql_cond_bits.h"
#include "mysql/components/services/bits/mysql_rwlock_bits.h"
#include "mysql/components/services/bits/psi_cond_bits.h"
#include "mysql/components/services/bits/psi_rwlock_bits.h"
#include "mysql/components/services/bits/psi_stage_bits.h"
#include "mysql/psi/mysql_mutex.h"

#include <array>
#include <atomic>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

extern bool opt_admission_control_by_trx;
extern ulonglong admission_control_filter;
extern char *admission_control_weights;
extern ulonglong admission_control_wait_events;
extern ulonglong admission_control_yield_freq;

class AC;
class THD;
class Table_ref;
class Item;

extern AC *db_ac;

#ifdef HAVE_PSI_INTERFACE
extern PSI_stage_info stage_admission_control_enter;
extern PSI_stage_info stage_admission_control_exit;
extern PSI_stage_info stage_waiting_for_admission;
extern PSI_stage_info stage_waiting_for_readmission;
#endif

// TODO: remove this block eventually when this becomes part of audit plugin?
// TBD later
// Resource isolation types that a multi-tenancy plugin will handle.
// Currently connection and query limits are two resource types. More will be
// supported in the future.
enum class enum_multi_tenancy_resource_type : int32_t {
  MULTI_TENANCY_RESOURCE_CONNECTION,
  MULTI_TENANCY_RESOURCE_QUERY,

  MULTI_TENANCY_NUM_RESOURCE_TYPES
};

// TODO: Remove this enum when code becomes part of plugin
// Callback function return types.
// - ACCEPT: resource can be granted
// - WAIT: may need to wait for resource to be freed up
// - REJECT: resource cannot be granted
// - FALLBACK: plugin is disabled
enum class enum_multi_tenancy_return_type : int32_t {
  MULTI_TENANCY_RET_ACCEPT = 0,
  MULTI_TENANCY_RET_WAIT,
  MULTI_TENANCY_RET_REJECT,

  MULTI_TENANCY_RET_FALLBACK,
  MULTI_TENANCY_NUM_RETURN_TYPES
};

typedef enum enum_multi_tenancy_resource_type MT_RESOURCE_TYPE;
typedef enum enum_multi_tenancy_return_type MT_RETURN_TYPE;
typedef std::unordered_map<std::string, std::string> ATTRS_MAP_T;
typedef std::vector<std::pair<std::string, std::string>> ATTRS_VEC_T;

struct multi_tenancy_resource_attributes {
  const ATTRS_MAP_T *connection_attrs_map;
  const ATTRS_VEC_T *query_attrs_vec;
  const char *database;
};

typedef struct multi_tenancy_resource_attributes MT_RESOURCE_ATTRS;

// The contents here must match entries in admission_control_filter_names array
enum enum_admission_control_filter {
  ADMISSION_CONTROL_ALTER,
  ADMISSION_CONTROL_BEGIN,
  ADMISSION_CONTROL_COMMIT,
  ADMISSION_CONTROL_CREATE,
  ADMISSION_CONTROL_DELETE,
  ADMISSION_CONTROL_DROP,
  ADMISSION_CONTROL_INSERT,
  ADMISSION_CONTROL_LOAD,
  ADMISSION_CONTROL_SELECT,
  ADMISSION_CONTROL_SET,
  ADMISSION_CONTROL_REPLACE,
  ADMISSION_CONTROL_ROLLBACK,
  ADMISSION_CONTROL_TRUNCATE,
  ADMISSION_CONTROL_UPDATE,
  ADMISSION_CONTROL_SHOW,
  ADMISSION_CONTROL_USE,
  ADMISSION_CONTROL_END = 64
};

enum enum_admission_control_wait_events {
  ADMISSION_CONTROL_THD_WAIT_SLEEP = (1U << 0),
  ADMISSION_CONTROL_THD_WAIT_ROW_LOCK = (1U << 1),
  ADMISSION_CONTROL_THD_WAIT_META_DATA_LOCK = (1U << 2),
  ADMISSION_CONTROL_THD_WAIT_NET_IO = (1U << 3),
  ADMISSION_CONTROL_THD_WAIT_YIELD = (1U << 4),
};

enum enum_admission_control_request_mode {
  AC_REQUEST_NONE,
  AC_REQUEST_QUERY,
  AC_REQUEST_QUERY_READMIT_LOPRI,
  AC_REQUEST_QUERY_READMIT_HIPRI,
};

extern int multi_tenancy_admit_query(
    THD *, enum_admission_control_request_mode mode = AC_REQUEST_QUERY);
extern int multi_tenancy_exit_query(THD *);
int fill_ac_queue(THD *thd, Table_ref *tables, Item *cond);

/**
  Per-thread information used in admission control.
*/

struct st_ac_node;
struct st_ac_node {
  st_ac_node(const st_ac_node &) = delete;
  st_ac_node &operator=(const st_ac_node &) = delete;
  mysql_mutex_t lock;
  mysql_cond_t cond;
  // Note that running/queued cannot be simultaneously true.
  bool running;  // whether we need to decrement from running_queries
  bool queued;   // whether current node is queued. pos is valid iff queued.
  std::list<std::shared_ptr<st_ac_node>>::iterator pos;
  // The queue that this node belongs to.
  long queue;
  // The THD owning this node.
  THD *thd;
  st_ac_node(THD *thd_arg);
  ~st_ac_node();
};

using st_ac_node_ptr = std::shared_ptr<st_ac_node>;

const ulong MAX_AC_QUEUES = 10;
/**
  Represents a queue, and its associated stats.
*/
struct Ac_queue {
  // The list representing the queue.
  std::list<st_ac_node_ptr> queue;
  inline size_t waiting_queries() const { return queue.size(); }
  // Track number of running queries.
  unsigned long running_queries = 0;
  // Track number of rejected queries.
  unsigned long aborted_queries = 0;
  // Track number of timed out queries.
  unsigned long timeout_queries = 0;
};

/**
  Class used in admission control.

  Every entity (database or table or user name) will have this
  object created and stored in the global map AC::ac_map.

*/
class Ac_info {
  friend class AC;
  friend int fill_ac_queue(THD *thd, Table_ref *tables, Item *cond);
  // Queues
  std::array<Ac_queue, MAX_AC_QUEUES> queues{};
  // Count for waiting_queries in queues for this Ac_info.
  unsigned long waiting_queries;
  // Count for running queries in queues for this Ac_info.
  unsigned long running_queries;
  // Protects the queues.
  mysql_mutex_t lock;

 public:
  Ac_info();
  ~Ac_info();
  // Disable copy constructor.
  Ac_info(const Ac_info &) = delete;
  Ac_info &operator=(const Ac_info &) = delete;
};

using Ac_info_ptr = std::shared_ptr<Ac_info>;

enum class Ac_result {
  AC_ADMITTED,  // Admitted
  AC_ABORTED,   // Rejected because queue size too large
  AC_TIMEOUT,   // Rejected because waiting on queue for too long
  AC_KILLED     // Killed while waiting for admission
};

/**
  Global class used to enforce per admission control limits.
*/
class AC {
  friend int fill_ac_queue(THD *thd, Table_ref *tables, Item *cond);

  // This map is protected by the rwlock LOCK_ac.
  using Ac_info_ptr_container = std::unordered_map<std::string, Ac_info_ptr>;
  Ac_info_ptr_container ac_map;
  // Variables to track global limits
  ulong max_running_queries, max_waiting_queries;

  std::array<unsigned long, MAX_AC_QUEUES> weights{};
  /**
    Protects the above variables.

    Locking order followed is LOCK_ac, Ac_info::lock, st_ac_node::lock.
  */
  mutable mysql_rwlock_t LOCK_ac;

  std::atomic_ullong total_aborted_queries;
  std::atomic_ullong total_timeout_queries;

 public:
  AC();
  ~AC();

  // Disable copy constructor.
  AC(const AC &) = delete;
  AC &operator=(const AC &) = delete;

  /*
   * Removes a dropped entity info from the global map.
   */
  void remove(const char *entity);

  void insert(const std::string &entity);

  void update_max_running_queries(ulong val);

  void update_max_waiting_queries(ulong val);

  ulong get_max_running_queries() const;

  ulong get_max_waiting_queries() const;

  Ac_result admission_control_enter(THD *, const MT_RESOURCE_ATTRS *,
                                    enum_admission_control_request_mode);
  void admission_control_exit(THD *, const MT_RESOURCE_ATTRS *);
  bool wait_for_signal(THD *, st_ac_node_ptr &, const Ac_info_ptr &ac_info,
                       enum_admission_control_request_mode);
  static void enqueue(THD *thd, Ac_info_ptr ac_info,
                      enum_admission_control_request_mode);
  static void dequeue(THD *thd, Ac_info_ptr ac_info);
  static void dequeue_and_run(THD *thd, Ac_info_ptr ac_info);

  int update_queue_weights(char *str);

  ulonglong get_total_aborted_queries() const { return total_aborted_queries; }
  ulonglong get_total_timeout_queries() const { return total_timeout_queries; }

  ulong get_total_running_queries() const;

  ulong get_total_waiting_queries() const;
};

#endif /* _sql_admission_control_h */
