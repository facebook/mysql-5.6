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

#include "mysql/psi/mysql_mutex.h"

#include <list>
#include "sql/sql_class.h"

extern bool opt_admission_control_by_trx;
extern ulonglong admission_control_filter;
class AC;
extern AC *db_ac;

#ifdef HAVE_PSI_INTERFACE
extern PSI_stage_info stage_admission_control_enter;
extern PSI_stage_info stage_admission_control_exit;
extern PSI_stage_info stage_waiting_for_admission;
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

extern int multi_tenancy_admit_query(THD *, const MT_RESOURCE_ATTRS *);
extern int multi_tenancy_exit_query(THD *, const MT_RESOURCE_ATTRS *);

/**
  Per-thread information used in admission control.
*/

#ifdef HAVE_PSI_INTERFACE

static PSI_mutex_key key_ac_node_lock;
static PSI_cond_key key_ac_node_cond;

static PSI_mutex_info all_ac_node_mutexes[] = {
    {&key_ac_node_lock, "st_ac_node::lock", PSI_FLAG_SINGLETON, 0,
     PSI_DOCUMENT_ME}};

static PSI_cond_info all_ac_node_conds[] = {
    {&key_ac_node_cond, "st_ac_node::cond", PSI_FLAG_SINGLETON, 0,
     PSI_DOCUMENT_ME}};

static void init_st_ac_node_keys() {
  const char *const category = "sql";
  int count;

  count = static_cast<int>(array_elements(all_ac_node_mutexes));
  mysql_mutex_register(category, all_ac_node_mutexes, count);

  count = static_cast<int>(array_elements(all_ac_node_conds));
  mysql_cond_register(category, all_ac_node_conds, count);
}
#endif

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
  st_ac_node() : running(false), queued(false) {
#ifdef HAVE_PSI_INTERFACE
    init_st_ac_node_keys();
#endif
    mysql_mutex_init(key_ac_node_lock, &lock, MY_MUTEX_INIT_FAST);
    mysql_cond_init(key_ac_node_cond, &cond);
  }

  ~st_ac_node() {
    mysql_mutex_destroy(&lock);
    mysql_cond_destroy(&cond);
  }
};

using st_ac_node_ptr = std::shared_ptr<st_ac_node>;

/**
  Class used in admission control.

  Every entity (database or table or user name) will have this
  object created and stored in the global map AC::ac_map.

*/
class Ac_info {
  friend class AC;
#ifdef HAVE_PSI_INTERFACE
  PSI_mutex_key key_lock;
  PSI_mutex_info key_lock_info[1] = {
      {&key_lock, "Ac_info::lock", PSI_FLAG_SINGLETON, 0, PSI_DOCUMENT_ME}};
#endif
  // Queue to track waiting threads.
  std::list<std::shared_ptr<st_ac_node>> queue;
  unsigned long waiting_queries;
  // Count for running queries.
  unsigned long running_queries;
  // Protects the queue.
  mysql_mutex_t lock;

 public:
  Ac_info() : waiting_queries(0), running_queries(0) {
#ifdef HAVE_PSI_INTERFACE
    mysql_mutex_register("sql", key_lock_info, array_elements(key_lock_info));
#endif
    mysql_mutex_init(key_lock, &lock, MY_MUTEX_INIT_FAST);
  }

  ~Ac_info() { mysql_mutex_destroy(&lock); }
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
  // This map is protected by the rwlock LOCK_ac.
  using Ac_info_ptr_container = std::unordered_map<std::string, Ac_info_ptr>;
  Ac_info_ptr_container ac_map;
  // Variables to track global limits
  ulong max_running_queries, max_waiting_queries;
  /**
    Protects ac_map and max_running_queries/max_waiting_queries.

    Locking order followed is LOCK_ac, Ac_info::lock, st_ac_node::lock.
  */
  mutable mysql_rwlock_t LOCK_ac;
#ifdef HAVE_PSI_INTERFACE
  PSI_rwlock_key key_rwlock_LOCK_ac;
  PSI_rwlock_info key_rwlock_LOCK_ac_info[1] = {
      {&key_rwlock_LOCK_ac, "AC::rwlock", PSI_FLAG_SINGLETON, 0,
       PSI_DOCUMENT_ME}};
#endif

  std::atomic_ullong total_aborted_queries;
  std::atomic_ullong total_timeout_queries;

 public:
  AC()
      : max_running_queries(1),
        max_waiting_queries(0),
        total_aborted_queries(0),
        total_timeout_queries(0) {
#ifdef HAVE_PSI_INTERFACE
    mysql_rwlock_register("sql", key_rwlock_LOCK_ac_info,
                          array_elements(key_rwlock_LOCK_ac_info));
#endif
    mysql_rwlock_init(key_rwlock_LOCK_ac, &LOCK_ac);
  }

  ~AC() { mysql_rwlock_destroy(&LOCK_ac); }
  // Disable copy constructor.
  AC(const AC &) = delete;
  AC &operator=(const AC &) = delete;

  // TODO: check if this can be moved to st_ac_node
  inline void signal(st_ac_node_ptr &ac_node) {
    assert(ac_node && ac_node.get());
    mysql_mutex_lock(&ac_node->lock);
    mysql_cond_signal(&ac_node->cond);
    mysql_mutex_unlock(&ac_node->lock);
  }

  /*
   * Removes a dropped entity info from the global map.
   */
  void remove(const char *entity) {
    std::string str(entity);
    // First take a read lock to unblock any waiting queries.
    mysql_rwlock_rdlock(&LOCK_ac);
    auto it = ac_map.find(str);
    if (it != ac_map.end()) {
      auto ac_info = it->second;
      while (true) {
        mysql_mutex_lock(&ac_info->lock);
        if (ac_info->waiting_queries) {
          for (auto &i : ac_info->queue) {
            signal(i);
          }
        } else {
          mysql_mutex_unlock(&ac_info->lock);
          break;
        }
        mysql_mutex_unlock(&ac_info->lock);
      }
    }
    mysql_rwlock_unlock(&LOCK_ac);
    mysql_rwlock_wrlock(&LOCK_ac);
    it = ac_map.find(std::string(str));
    if (it != ac_map.end()) {
      ac_map.erase(it);
    }
    mysql_rwlock_unlock(&LOCK_ac);
  }

  void insert(const std::string &entity) {
    mysql_rwlock_wrlock(&LOCK_ac);
    if (ac_map.find(entity) == ac_map.end()) {
      ac_map.emplace(entity, std::make_shared<Ac_info>());
    }
    mysql_rwlock_unlock(&LOCK_ac);
  }

  void update_max_running_queries(ulong val) {
    // lock to protect against erasing map iterators.
    mysql_rwlock_wrlock(&LOCK_ac);
    ulong old_val = max_running_queries;
    max_running_queries = val;
    // Signal any waiting threads which are below the new limit. Note 0 is a
    // special case where every waiting thread needs to be signalled.
    if (val > old_val || !val) {
      for (auto &it : ac_map) {
        auto &ac_info = it.second;
        mysql_mutex_lock(&ac_info->lock);
        // Calculate the number of threads we need to signal in to_signal.
        size_t to_signal = val ? val - old_val : ac_info->queue.size();
        size_t signaled = 0;

        for (auto &i : ac_info->queue) {
          signal(i);
          if (++signaled >= to_signal) {
            break;
          }
        }
        mysql_mutex_unlock(&ac_info->lock);
      }
    }
    mysql_rwlock_unlock(&LOCK_ac);
  }

  void update_max_waiting_queries(ulong val) {
    mysql_rwlock_wrlock(&LOCK_ac);
    max_waiting_queries = val;
    mysql_rwlock_unlock(&LOCK_ac);
  }

  ulong get_max_running_queries() const {
    mysql_rwlock_rdlock(&LOCK_ac);
    ulong res = max_running_queries;
    mysql_rwlock_unlock(&LOCK_ac);
    return res;
  }

  ulong get_max_waiting_queries() const {
    mysql_rwlock_rdlock(&LOCK_ac);
    ulong res = max_waiting_queries;
    mysql_rwlock_unlock(&LOCK_ac);
    return res;
  }

  Ac_result admission_control_enter(THD *, const MT_RESOURCE_ATTRS *);
  void admission_control_exit(THD *, const MT_RESOURCE_ATTRS *);
  bool wait_for_signal(THD *, st_ac_node_ptr &, const Ac_info_ptr &ac_info);
  static void enqueue(THD *thd, std::shared_ptr<Ac_info> ac_info);
  static void dequeue(THD *thd, std::shared_ptr<Ac_info> ac_info);

  ulonglong get_total_aborted_queries() const { return total_aborted_queries; }
  ulonglong get_total_timeout_queries() const { return total_timeout_queries; }

  ulong get_total_running_queries() {
    ulonglong res = 0;
    mysql_rwlock_rdlock(&LOCK_ac);
    for (auto it : ac_map) {
      auto &ac_info = it.second;
      mysql_mutex_lock(&ac_info->lock);
      res += ac_info->running_queries;
      mysql_mutex_unlock(&ac_info->lock);
    }
    mysql_rwlock_unlock(&LOCK_ac);
    return res;
  }

  ulong get_total_waiting_queries() {
    ulonglong res = 0;
    mysql_rwlock_rdlock(&LOCK_ac);
    for (auto it : ac_map) {
      auto &ac_info = it.second;
      mysql_mutex_lock(&ac_info->lock);
      res += ac_info->waiting_queries;
      mysql_mutex_unlock(&ac_info->lock);
    }
    mysql_rwlock_unlock(&LOCK_ac);
    return res;
  }
};

#endif /* _sql_admission_control_h */
