/* Copyright (c) 2013, 2017, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */
#ifndef MTS_SUBMODE_H
#define MTS_SUBMODE_H

#include <stddef.h>
#include <sys/types.h>
#include <atomic>
#include <deque>
#include <memory>
#include <stack>
#include <utility>

#include "binlog_event.h"  // SEQ_UNINIT
#include "log_event_wrapper.h"
#include "my_inttypes.h"
#include "my_thread_local.h"   // my_thread_id
#include "prealloced_array.h"  // Prealloced_array

class Log_event;
class Query_log_event;
class Relay_log_info;
class Slave_worker;
class THD;
struct TABLE;

typedef Prealloced_array<Slave_worker *, 4> Slave_worker_array;

enum enum_mts_parallel_type {
  /* Parallel slave based on Database name */
  MTS_PARALLEL_TYPE_DB_NAME = 0,
  /* Parallel slave based on group information from Binlog group commit */
  MTS_PARALLEL_TYPE_LOGICAL_CLOCK = 1,
  /* Parallel slave based on dependencies between RBR events */
  MTS_PARALLEL_TYPE_DEPENDENCY = 2,
};

// Extend the following class as per requirement for each sub mode
class Mts_submode {
 private:
 protected:
  /* Parallel type */
  enum_mts_parallel_type type;

 public:
  Mts_submode() {}
  inline enum_mts_parallel_type get_type() { return type; }
  // pure virtual methods. Should be extended in the derieved class

  /* Logic to schedule the next event. called at the B event for each
     transaction */
  virtual int schedule_next_event(Relay_log_info *rli, Log_event *ev) = 0;

  /* logic to attach temp tables Should be extended in the derieved class */
  virtual void attach_temp_tables(THD *thd, const Relay_log_info *rli,
                                  Query_log_event *ev) = 0;

  /* logic to detach temp tables. Should be extended in the derieved class  */
  virtual void detach_temp_tables(THD *thd, const Relay_log_info *rli,
                                  Query_log_event *ev) = 0;

  /* returns the least occupied worker. Should be extended in the derieved class
   */
  virtual Slave_worker *get_least_occupied_worker(Relay_log_info *rli,
                                                  Slave_worker_array *ws,
                                                  Log_event *ev) = 0;
  /* wait for slave workers to finish */
  virtual int wait_for_workers_to_finish(Relay_log_info *rli,
                                         Slave_worker *ignore = NULL) = 0;

  virtual ~Mts_submode() {}
};

/**
  DB partitioned submode
  For significance of each method check definition of Mts_submode
*/
class Mts_submode_database : public Mts_submode {
 public:
  Mts_submode_database() { type = MTS_PARALLEL_TYPE_DB_NAME; }
  int schedule_next_event(Relay_log_info *rli, Log_event *ev);
  void attach_temp_tables(THD *thd, const Relay_log_info *rli,
                          Query_log_event *ev);
  void detach_temp_tables(THD *thd, const Relay_log_info *rli,
                          Query_log_event *ev);
  Slave_worker *get_least_occupied_worker(Relay_log_info *,
                                          Slave_worker_array *ws, Log_event *);
  ~Mts_submode_database(){};
  int wait_for_workers_to_finish(Relay_log_info *rli,
                                 Slave_worker *ignore = NULL);
};

/**
  Parallelization using Master parallelization information
  For significance of each method check definition of Mts_submode
 */
class Mts_submode_logical_clock : public Mts_submode {
 private:
  bool first_event, force_new_group;
  bool is_new_group;
  uint delegated_jobs;
  /* "instant" value of committed transactions low-water-mark */
  std::atomic<longlong> last_lwm_timestamp;
  /* GAQ index corresponding to the min commit point */
  ulong last_lwm_index;
  longlong last_committed;
  longlong sequence_number;

 public:
  uint jobs_done;
  bool is_error;
  /*
    the logical timestamp of the olderst transaction that is being waited by
    before to resume scheduling.
  */
  std::atomic<longlong> min_waited_timestamp;
  /*
    Committed transactions and those that are waiting for their commit parents
    comprise sequences whose items are identified as GAQ index.
    An empty sequence is described by the following magic value which can't
    be in the GAQ legitimate range.
    todo: an alternative could be to pass a magic value to the constructor.
    E.g GAQ.size as a good candidate being outside of the valid range.
    That requires further wl6314 refactoring in activation/deactivation
    of the scheduler.
  */
  static const ulong INDEX_UNDEF = (ulong)-1;

 protected:
  std::pair<uint, my_thread_id> get_server_and_thread_id(TABLE *table);
  Slave_worker *get_free_worker(Relay_log_info *rli);

 public:
  Mts_submode_logical_clock();
  int schedule_next_event(Relay_log_info *rli, Log_event *ev);
  void attach_temp_tables(THD *thd, const Relay_log_info *rli,
                          Query_log_event *ev);
  void detach_temp_tables(THD *thd, const Relay_log_info *rli,
                          Query_log_event *);
  Slave_worker *get_least_occupied_worker(Relay_log_info *rli,
                                          Slave_worker_array *ws,
                                          Log_event *ev);
  /* Sets the force new group variable */
  inline void start_new_group() {
    force_new_group = true;
    first_event = true;
  }
  /**
    Withdraw the delegated_job increased by the group.
  */
  void withdraw_delegated_job() { delegated_jobs--; }
  int wait_for_workers_to_finish(Relay_log_info *rli,
                                 Slave_worker *ignore = NULL);
  bool wait_for_last_committed_trx(Relay_log_info *rli,
                                   longlong last_committed_arg);
  /*
    LEQ comparison of two logical timestamps follows regular rules for
    integers. SEQ_UNINIT is regarded as the least value in the clock domain.

    @param a  the lhs logical timestamp value
    @param b  the rhs logical timestamp value

    @return   true  when a "<=" b,
              false otherwise
  */
  static bool clock_leq(longlong a, longlong b) {
    if (a == SEQ_UNINIT)
      return true;
    else if (b == SEQ_UNINIT)
      return false;
    else
      return a <= b;
  }

  longlong get_lwm_timestamp(Relay_log_info *rli, bool need_lock);
  longlong estimate_lwm_timestamp() { return last_lwm_timestamp.load(); };
  ~Mts_submode_logical_clock() {}
};

class Mts_submode_dependency : public Mts_submode {
 private:
  std::deque<std::shared_ptr<Log_event_wrapper>> dep_queue;
  mysql_mutex_t dep_lock;

  /* Mapping from key to penultimate (for multi event trx)/end event of the
     last trx that updated that table */
  std::unordered_map<Dependency_key, std::shared_ptr<Log_event_wrapper>>
      dep_key_lookup;
  mysql_mutex_t dep_key_lookup_mutex;

  /* Set of keys accessed by the group */
  std::unordered_set<Dependency_key> keys_accessed_by_group;

  /* Set of all DBs accessed by the current group */
  std::unordered_set<std::string> dbs_accessed_by_group;

  // Mutex-condition pair to notify when queue is/is not full
  mysql_cond_t dep_full_cond;
  bool dep_full = false;

  // Mutex-condition pair to notify when queue is/is not empty
  mysql_cond_t dep_empty_cond;

  // Mutex-condition pair to notify when all scheduled transactions are done
  mysql_cond_t dep_trx_all_done_cond;
  ulonglong num_in_flight_trx = 0;

  bool trx_queued = false;

  void handle_terminal_event(Relay_log_info *rli,
                             std::shared_ptr<Log_event_wrapper> &ev);

  void wait_for_workers_to_finish_nolock() {
    mysql_mutex_assert_owner(&dep_lock);
    ulonglong num = current_begin_event ? 1 : 0;
    while (num_in_flight_trx > num) {
      mysql_cond_wait(&dep_trx_all_done_cond, &dep_lock);
    }
  }

  std::pair<uint, my_thread_id> get_server_and_thread_id(TABLE *table);

 public:
  // Used to signal when a dependency worker dies
  std::atomic<bool> dependency_worker_error{false};

  bool dep_sync_group = false;

  std::shared_ptr<Log_event_wrapper> prev_event;
  std::shared_ptr<Log_event_wrapper> current_begin_event;
  std::unordered_map<ulonglong, Table_map_log_event *> table_map_events;

  // Statistics
  ulonglong begin_event_waits = 0;
  ulonglong next_event_waits = 0;
  ulonglong num_workers_waiting = 0;

  Mts_submode_dependency() {
    type = MTS_PARALLEL_TYPE_DEPENDENCY;
    mysql_mutex_init(0, &dep_lock, MY_MUTEX_INIT_FAST);
    mysql_mutex_init(0, &dep_key_lookup_mutex, MY_MUTEX_INIT_FAST);
    mysql_cond_init(0, &dep_full_cond);
    mysql_cond_init(0, &dep_empty_cond);
    mysql_cond_init(0, &dep_trx_all_done_cond);
  }

  bool enqueue(const std::shared_ptr<Log_event_wrapper> &begin_event);
  std::shared_ptr<Log_event_wrapper> dequeue(Slave_worker *worker);
  void signal_trx_done(std::shared_ptr<Log_event_wrapper> begin_event);

  void add_row_event(std::shared_ptr<Log_event_wrapper> ev) {
    DBUG_ASSERT(prev_event != NULL);
    const auto &keys =
        static_cast<Rows_log_event *>(ev->raw_event())->m_keylist;
    keys_accessed_by_group.insert(keys.begin(), keys.end());

    mysql_mutex_lock(&dep_key_lookup_mutex);
    /* Handle dependencies. */
    for (const auto &k : keys) {
      auto last_key_event = dep_key_lookup.find(k);
      if (last_key_event != dep_key_lookup.end()) {
        last_key_event->second->add_dependent(ev);
      }
    }
    mysql_mutex_unlock(&dep_key_lookup_mutex);
  }

  void register_keys(std::shared_ptr<Log_event_wrapper> ev);

  void unregister_keys(std::shared_ptr<Log_event_wrapper> ev);

  void cleanup_group(std::shared_ptr<Log_event_wrapper> begin_event) {
    // Delete all events manually in bottom-up manner to avoid stack overflow
    // from cascading shared_ptr deletions
    std::stack<std::weak_ptr<Log_event_wrapper>> events;
    auto &event = begin_event;
    while (event) {
      events.push(event);
      event = event->next_ev;
    }

    while (!events.empty()) {
      auto sptr = events.top().lock();
      if (sptr) sptr->next_ev.reset();
      events.pop();
    }
  }

  void clear(bool need_dep_lock = true) {
    if (need_dep_lock) mysql_mutex_lock(&dep_lock);

    DBUG_ASSERT(num_in_flight_trx >= dep_queue.size());
    num_in_flight_trx -= dep_queue.size();
    for (const auto &begin_event : dep_queue) cleanup_group(begin_event);
    dep_queue.clear();

    prev_event.reset();
    current_begin_event.reset();
    table_map_events.clear();

    keys_accessed_by_group.clear();
    dbs_accessed_by_group.clear();

    mysql_cond_broadcast(&dep_empty_cond);
    mysql_cond_broadcast(&dep_full_cond);
    mysql_cond_broadcast(&dep_trx_all_done_cond);

    dep_full = false;

    mysql_mutex_lock(&dep_key_lookup_mutex);
    dep_key_lookup.clear();
    mysql_mutex_unlock(&dep_key_lookup_mutex);

    trx_queued = false;

    if (need_dep_lock) mysql_mutex_unlock(&dep_lock);
  }

  /* Logic to schedule the next event. called at the B event for each
     transaction */
  int schedule_next_event(Relay_log_info *rli, Log_event *ev);

  void attach_temp_tables(THD *thd, const Relay_log_info *rli,
                          Query_log_event *ev);
  void detach_temp_tables(THD *thd, const Relay_log_info *rli,
                          Query_log_event *ev);

  /* returns the least occupied worker. Should be extended in the derieved class
   */
  Slave_worker *get_least_occupied_worker(
      Relay_log_info *rli MY_ATTRIBUTE((unused)),
      Slave_worker_array *ws MY_ATTRIBUTE((unused)),
      Log_event *ev MY_ATTRIBUTE((unused))) {
    DBUG_ASSERT(false);
    return nullptr;
  }

  /* wait for slave workers to finish */
  int wait_for_workers_to_finish(Relay_log_info *rli MY_ATTRIBUTE((unused)),
                                 Slave_worker *ignore = NULL) {
    DBUG_ASSERT(ignore == NULL);
    mysql_mutex_lock(&dep_lock);
    wait_for_workers_to_finish_nolock();
    mysql_mutex_unlock(&dep_lock);
    return 0;
  }

  virtual ~Mts_submode_dependency() { clear(); }
};

#endif /*MTS_SUBMODE_H*/
