#include "dependency_slave_worker.h"
#include "debug_sync.h"
#include "log_event_wrapper.h"
#include "rpl_slave_commit_order_manager.h"

bool append_item_to_jobs(slave_job_item *job_item, Slave_worker *w,
                         Relay_log_info *rli);

int slave_worker_exec_single_job(Slave_worker *worker, Relay_log_info *rli,
                                 std::shared_ptr<Log_event_wrapper> &ev_wrap,
                                 uint start_relay_number,
                                 my_off_t start_relay_pos);

std::shared_ptr<Log_event_wrapper> Dependency_slave_worker::get_begin_event(
    Commit_order_manager *co_mngr) {
  std::shared_ptr<Log_event_wrapper> ret;

  mysql_mutex_lock(&c_rli->dep_lock);

  PSI_stage_info old_stage;
  info_thd->ENTER_COND(&c_rli->dep_empty_cond, &c_rli->dep_lock,
                       &stage_slave_waiting_event_from_coordinator, &old_stage);

  while (!info_thd->killed && running_status == RUNNING &&
         c_rli->dep_queue.empty()) {
    ++c_rli->begin_event_waits;
    ++c_rli->num_workers_waiting;
    mysql_cond_wait(&c_rli->dep_empty_cond, &c_rli->dep_lock);
    --c_rli->num_workers_waiting;
  }

  ret = c_rli->dequeue_dep();

  // case: place ourselves in the commit order queue
  if (ret && co_mngr != nullptr) {
    DBUG_ASSERT(c_rli->mts_dependency_order_commits);
    set_current_db(ret->get_db());
    co_mngr->register_trx(this);
  }

  // case: signal if queue is now empty
  if (c_rli->dep_queue.empty()) mysql_cond_signal(&c_rli->dep_empty_cond);

  // admission control
  if (unlikely(c_rli->dep_full)) {
    DBUG_ASSERT(c_rli->dep_queue.size() > 0);
    // case: signal if dep has space
    if (c_rli->dep_queue.size() <
        (c_rli->mts_dependency_size * c_rli->mts_dependency_refill_threshold /
         100)) {
      c_rli->dep_full = false;
      mysql_cond_signal(&c_rli->dep_full_cond);
    }
  }

  mysql_mutex_unlock(&c_rli->dep_lock);
  info_thd->EXIT_COND(&old_stage);
  return ret;
}

// Pulls and executes events single group
// Returns true if the group executed successfully
bool Dependency_slave_worker::execute_group() {
  uint start_relay_number = 0;
  my_off_t start_relay_pos = 0;
  int err = 0;
  Commit_order_manager *commit_order_mngr = get_commit_order_manager();

  auto begin_event = get_begin_event(commit_order_mngr);
  auto ev = begin_event;

  if (begin_event) {
    start_relay_number = begin_event->get_event_relay_log_number();
    start_relay_pos = begin_event->get_event_start_pos();
  }

  while (ev) {
    if ((err = execute_event(ev, start_relay_number, start_relay_pos))) {
      c_rli->dependency_worker_error = true;
      break;
    }
    finalize_event(ev);
    ev = ev->next();
  }

  // case: in case of error rollback if commit ordering is enabled
  if (unlikely(err && commit_order_mngr)) {
    commit_order_mngr->report_rollback(this);
  }

  mysql_mutex_lock(&c_rli->dep_lock);
  if (begin_event) {
    DBUG_ASSERT(c_rli->num_in_flight_trx > 0);
    --c_rli->num_in_flight_trx;
  }
  if (c_rli->num_in_flight_trx <= 1)
    mysql_cond_broadcast(&c_rli->dep_trx_all_done_cond);
  mysql_mutex_unlock(&c_rli->dep_lock);

  c_rli->cleanup_group(begin_event);

  return err == 0 && !info_thd->killed && running_status == RUNNING;
}

int Dependency_slave_worker::execute_event(
    std::shared_ptr<Log_event_wrapper> &ev, uint start_relay_number,
    my_off_t start_relay_pos) {
  // wait for all dependencies to be satisfied
  if (unlikely(!ev->wait(this))) return 1;

  DBUG_EXECUTE_IF("dbug.dep_wait_before_update_execution", {
    if (ev->raw_event()->get_type_code() == binary_log::UPDATE_ROWS_EVENT) {
      const char act[] = "now signal signal.reached wait_for signal.done";
      DBUG_ASSERT(opt_debug_sync_timeout > 0);
      DBUG_ASSERT(!debug_sync_set_action(info_thd, STRING_WITH_LEN(act)));
    }
  };);

  // case: there was an error in one of the workers, so let's skip execution of
  // events immediately
  if (unlikely(c_rli->dependency_worker_error)) return 1;

  return slave_worker_exec_single_job(this, c_rli, ev, start_relay_number,
                                      start_relay_pos) == 0
             ? 0
             : -1;
}

void Dependency_slave_worker::finalize_event(
    std::shared_ptr<Log_event_wrapper> &ev) {
  /* Attempt to clean up entries from the key lookup
   *
   * There are two cases:
   * 1) The "value" of the key-value pair is equal to this event. In this case,
   *    remove the key-value pair from the map.
   * 2) The "value" of the key-value pair is _not_ equal to this event. In this
   *    case, leave it be; the event corresponds to a later transaction.
   */
  mysql_mutex_lock(&c_rli->dep_key_lookup_mutex);
  if (likely(!c_rli->dep_key_lookup.empty())) {
    for (const auto &key : ev->keys) {
      const auto it = c_rli->dep_key_lookup.find(key);
      DBUG_ASSERT(it != c_rli->dep_key_lookup.end());

      /* Case 1. (Case 2 is implicitly handled by doing nothing.) */
      if (it->second == ev) {
        c_rli->dep_key_lookup.erase(it);
      }
    }
  }
  ev->finalize();
  mysql_mutex_unlock(&c_rli->dep_key_lookup_mutex);
}

Dependency_slave_worker::Dependency_slave_worker(
    Relay_log_info *rli
#ifdef HAVE_PSI_INTERFACE
    ,
    PSI_mutex_key *param_key_info_run_lock,
    PSI_mutex_key *param_key_info_data_lock,
    PSI_mutex_key *param_key_info_sleep_lock,
    PSI_mutex_key *param_key_info_thd_lock,
    PSI_mutex_key *param_key_info_data_cond,
    PSI_mutex_key *param_key_info_start_cond,
    PSI_mutex_key *param_key_info_stop_cond,
    PSI_mutex_key *param_key_info_sleep_cond
#endif
    ,
    uint param_id, const char *param_channel)
    : Slave_worker(rli
#ifdef HAVE_PSI_INTERFACE
                   ,
                   param_key_info_run_lock, param_key_info_data_lock,
                   param_key_info_sleep_lock, param_key_info_thd_lock,
                   param_key_info_data_cond, param_key_info_start_cond,
                   param_key_info_stop_cond, param_key_info_sleep_cond
#endif
                   ,
                   param_id, param_channel) {
}

void Dependency_slave_worker::start() {
  DBUG_ASSERT(c_rli->dep_queue.empty() && dep_key_lookup.empty() &&
              keys_accessed_by_group.empty() && dbs_accessed_by_group.empty());

  while (execute_group())
    ;

  // case: cleanup if stopped abruptly
  if (running_status != STOP_ACCEPTED) {
    // tagging as exiting so Coordinator won't be able synchronize with it
    mysql_mutex_lock(&jobs_lock);
    running_status = ERROR_LEAVING;
    mysql_mutex_unlock(&jobs_lock);

    // Killing Coordinator to indicate eventual consistency error
    mysql_mutex_lock(&c_rli->info_thd->LOCK_thd_data);
    c_rli->info_thd->awake(THD::KILL_QUERY);
    mysql_mutex_unlock(&c_rli->info_thd->LOCK_thd_data);
  }
}
