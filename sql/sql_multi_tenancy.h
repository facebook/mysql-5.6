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


#ifndef _sql_multi_tenancy_h
#define _sql_multi_tenancy_h

#include <my_global.h>

#include <mysql/plugin_multi_tenancy.h>
#include "sql_class.h"

#include <list>

/*
 * sql_multi_tenancy.h/cc
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
 * See sql_multi_tenancy.cc for implementation.
 */

/**
  Forward declarations.
*/
class Ac_info;

enum enum_admission_control_request_mode
{
  AC_REQUEST_NONE,
  AC_REQUEST_QUERY,
  AC_REQUEST_QUERY_READMIT_LOPRI,
  AC_REQUEST_QUERY_READMIT_HIPRI,
};

extern int multi_tenancy_add_connection(THD *, const char *);
extern int multi_tenancy_close_connection(THD *);
extern int multi_tenancy_admit_query(THD *, enum_admission_control_request_mode mode = AC_REQUEST_QUERY);
extern int multi_tenancy_exit_query(THD *);
extern int multi_tenancy_set_admission_control_queue(THD *, std::string);
extern std::string multi_tenancy_get_entity_counter(
    THD *thd, MT_RESOURCE_TYPE type, const MT_RESOURCE_ATTRS *,
    const char *entity_name, int *limit, int *count);
extern void multi_tenancy_show_resource_counters(
    THD *thd, const MT_RESOURCE_ATTRS *, const char *entity);

extern ST_FIELD_INFO admission_control_queue_fields_info[];
int fill_ac_queue(THD *thd, TABLE_LIST *tables, Item *cond);

extern ST_FIELD_INFO admission_control_entities_fields_info[];
int fill_ac_entities(THD *thd, TABLE_LIST *tables, Item *cond);

/**
  Per-thread information used in admission control.
*/
struct st_ac_node {
  mysql_mutex_t lock;
  mysql_cond_t cond;
  bool running; // whether current node is given ac slot.
  bool queued;  // whether current node is queued. pos is valid iff queued.
  std::list<std::shared_ptr<st_ac_node>>::iterator pos;
  // The queue that this node belongs to.
  long queue;
  // The THD owning this node.
  THD *thd;
  // The ac_info this node belongs to.
  std::shared_ptr<Ac_info> ac_info;
  st_ac_node(THD *thd_arg) {
    mysql_mutex_init(key_LOCK_ac_node, &lock, MY_MUTEX_INIT_FAST);
    mysql_cond_init(key_COND_ac_node, &cond, NULL);
    running = false;
    queued = false;
    queue = 0;
    thd = thd_arg;
  }

  ~st_ac_node () {
    mysql_mutex_destroy(&lock);
    mysql_cond_destroy(&cond);
  }
};

const ulong MAX_AC_QUEUES = 10;
/**
  Represents a queue, and its associated stats.
*/
struct Ac_queue {
  // The list representing the queue.
  std::list<std::shared_ptr<st_ac_node>> queue;
  inline size_t waiting_queries() const {
    return queue.size();
  }
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
  friend int fill_ac_queue(THD *thd, TABLE_LIST *tables, Item *cond);
  friend int fill_ac_entities(THD *thd, TABLE_LIST *tables, Item *cond);

  // Queues
  std::array<Ac_queue, MAX_AC_QUEUES> queues{};

  // Entity name used as key in ac_info map.
  std::string entity;

  // Count for waiting queries in queues for this Ac_info.
  unsigned long waiting_queries = 0;
  // Count for running queries in queues for this Ac_info.
  unsigned long running_queries = 0;
  // Count for rejected queries in queues for this Ac_info.
  unsigned long aborted_queries = 0;
  // Count for timed out queries in queues for this Ac_info.
  unsigned long timeout_queries = 0;

  // Count for current connections.
  unsigned long connections = 0;
  // Stats for rejected connections.
  ulonglong rejected_connections = 0;

  // Protects Ac_info.
  mysql_mutex_t lock;
public:
  Ac_info(const std::string &_entity) : entity(_entity) {
    mysql_mutex_init(key_LOCK_ac_info, &lock, MY_MUTEX_INIT_FAST);
  }
  ~Ac_info() {
    mysql_mutex_destroy(&lock);
  }
  // Disable copy constructor.
  Ac_info(const Ac_info&) = delete;
  Ac_info& operator=(const Ac_info&) = delete;

  // Accessors.
  const std::string &get_entity() const {
    return entity;
  }
};

enum class Ac_result {
  AC_ADMITTED, // Admitted
  AC_ABORTED,  // Rejected because queue size too large
  AC_TIMEOUT,  // Rejected because waiting on queue for too long
  AC_KILLED    // Killed while waiting for admission
};

/**
  Global class used to enforce per admission control limits.
*/
class AC {
  friend int fill_ac_queue(THD *thd, TABLE_LIST *tables, Item *cond);
  friend int fill_ac_entities(THD *thd, TABLE_LIST *tables, Item *cond);

  // This map is protected by the rwlock LOCK_ac.
  std::unordered_map<std::string, std::shared_ptr<Ac_info>> ac_map;
  // Variables to track global limits
  ulong max_running_queries;
  ulong max_waiting_queries;
  ulong max_connections;

  std::array<unsigned long, MAX_AC_QUEUES> weights{};
  std::unordered_set<std::string> low_pri_sql_ids;
  int lowest_weight_queue = 0;
  /**
    Protects the above variables.

    Locking order followed is LOCK_ac, Ac_info::lock, st_ac_node::lock.
  */
  mysql_rwlock_t LOCK_ac;

  std::atomic<ulonglong> total_aborted_queries;
  std::atomic<ulonglong> total_timeout_queries;
  std::atomic<ulonglong> total_rejected_connections;

public:
  AC() {
    mysql_rwlock_init(key_rwlock_LOCK_ac, &LOCK_ac);
    max_running_queries = 0;
    max_waiting_queries = 0;
    max_connections = 0;
    total_aborted_queries = 0;
    total_timeout_queries = 0;
    total_rejected_connections = 0;
  }

  ~AC() {
    mysql_rwlock_destroy(&LOCK_ac);
  }
  // Disable copy constructor.
  AC(const AC&) = delete;
  AC& operator=(const AC&) = delete;

  static inline void signal(std::shared_ptr<st_ac_node>& ac_node) {
    DBUG_ASSERT(ac_node && ac_node.get());
    mysql_mutex_lock(&ac_node->lock);
    mysql_cond_signal(&ac_node->cond);
    mysql_mutex_unlock(&ac_node->lock);
  }

  void remove(const char* entity);

  void insert(const std::string &entity) {
    mysql_rwlock_wrlock(&LOCK_ac);
    if (ac_map.find(entity) == ac_map.end()) {
      ac_map[entity] = std::make_shared<Ac_info>(entity);
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
    //
    // We don't kill any queries if the max is lowered, so it's possible for
    // the number of running queries temporarily exceed the new max.
    if (val > old_val || !val) {
      for (auto &it: ac_map) {
        auto &ac_info = it.second;
        mysql_mutex_lock(&ac_info->lock);

        size_t signaled = 0;
        // If we're not signaling all threads, we should dequeue according to
        // score for fairness, but we're not for simplicity. This might be
        // fine since update_max_running_queries should be a relatively rare
        // operation, so we're still fair in steady state.
        for (auto &q : ac_info->queues) {
          while (q.waiting_queries() > 0) {
            dequeue_and_run(q.queue.front()->thd, ac_info);
            if (val && ++signaled >= val - old_val) {
              break;
            }
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

  void update_max_connections(ulong val) {
    mysql_rwlock_wrlock(&LOCK_ac);
    max_connections = val;
    mysql_rwlock_unlock(&LOCK_ac);
  }

  inline ulong get_max_running_queries() {
    mysql_rwlock_rdlock(&LOCK_ac);
    ulong res = max_running_queries;
    mysql_rwlock_unlock(&LOCK_ac);
    return res;
  }

  inline ulong get_max_waiting_queries() {
    mysql_rwlock_rdlock(&LOCK_ac);
    ulong res = max_waiting_queries;
    mysql_rwlock_unlock(&LOCK_ac);
    return res;
  }

  Ac_result admission_control_enter(THD *, enum_admission_control_request_mode);
  void admission_control_exit(THD*);
  bool wait_for_signal(THD *, std::shared_ptr<st_ac_node> &,
                       std::shared_ptr<Ac_info> ac_info, enum_admission_control_request_mode);
  static void enqueue(THD *thd, std::shared_ptr<Ac_info> ac_info, enum_admission_control_request_mode);
  static void dequeue(THD *thd, std::shared_ptr<Ac_info> ac_info);
  static void dequeue_and_run(THD *thd, std::shared_ptr<Ac_info> ac_info);

  Ac_result add_connection(THD *, const char *);
  void close_connection(THD*);

  int update_queue_weights(char *str);
  int get_lowest_queue_weight_number_under_lock();
  int update_queue_low_pri_sql_ids(char *str);
  bool is_low_pri_sql_id(const std::string& str);

  int get_lowest_weight_queue() const {
    return lowest_weight_queue;
  }
  ulonglong get_total_aborted_queries() const {
    return total_aborted_queries;
  }
  ulonglong get_total_timeout_queries() const { return total_timeout_queries; }
  ulonglong get_total_rejected_connections() const {
    return total_rejected_connections;
  }
  ulong get_total_running_queries() {
    ulonglong res= 0;
    mysql_rwlock_rdlock(&LOCK_ac);
    for (const auto &it : ac_map)
    {
      auto &ac_info = it.second;
      mysql_mutex_lock(&ac_info->lock);
      res += ac_info->running_queries;
      mysql_mutex_unlock(&ac_info->lock);
    }
    mysql_rwlock_unlock(&LOCK_ac);
    return res;
  }
  ulong get_total_waiting_queries() {
    ulonglong res= 0;
    mysql_rwlock_rdlock(&LOCK_ac);
    for (const auto &it : ac_map)
    {
      auto &ac_info = it.second;
      mysql_mutex_lock(&ac_info->lock);
      res += ac_info->waiting_queries;
      mysql_mutex_unlock(&ac_info->lock);
    }
    mysql_rwlock_unlock(&LOCK_ac);
    return res;
  }
};

/**
  @brief Class temporarily holding existing resources during ac_info switch.
*/
class Ac_switch_guard {
  THD *thd;
  std::shared_ptr<Ac_info> ac_info;
  bool committed = false;
  bool do_switch = true;

public:
  Ac_switch_guard(THD *);
  ~Ac_switch_guard();
  void commit() {
    // If switch is not needed then commit is ignored, and ac_info will be
    // restored.
    if (do_switch)
      committed = true;
  }
  int add_connection(const char *new_db);
};

extern AC *db_ac;

#endif /* _sql_multi_tenancy_h */
