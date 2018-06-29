#include <my_global.h>
#include "debug_sync.h"
#include "dependency_slave_worker.h"
#include "log_event_wrapper.h"
#include "rpl_slave_commit_order_manager.h"


bool append_item_to_jobs(slave_job_item *job_item,
                         Slave_worker *w,
                         Relay_log_info *rli);
void clear_current_group_events(Slave_worker *worker,
                                Relay_log_info *rli,
                                bool overfill);

std::shared_ptr<Log_event_wrapper>
Dependency_slave_worker::get_begin_event(Commit_order_manager *co_mngr)
{
  std::shared_ptr<Log_event_wrapper> ret;

  mysql_mutex_lock(&c_rli->dep_lock);

  PSI_stage_info old_stage;
  info_thd->ENTER_COND(&c_rli->dep_empty_cond, &c_rli->dep_lock,
                       &stage_slave_waiting_event_from_coordinator,
                       &old_stage);

  while (!info_thd->killed &&
         running_status == RUNNING &&
         c_rli->dep_queue.empty())
  {
    ++c_rli->begin_event_waits;
    ++c_rli->num_workers_waiting;
    mysql_cond_wait(&c_rli->dep_empty_cond, &c_rli->dep_lock);
    --c_rli->num_workers_waiting;
  }

  ret= c_rli->dequeue_dep();

  // case: place ourselves in the commit order queue
  if (ret && co_mngr != NULL)
  {
    DBUG_ASSERT(opt_mts_dependency_order_commits);
    co_mngr->register_trx(this);
  }

  // case: signal if queue is now empty
  if (c_rli->dep_queue.empty())
    mysql_cond_signal(&c_rli->dep_empty_cond);

  // admission control
  if (unlikely(c_rli->dep_full))
  {
    DBUG_ASSERT(c_rli->dep_queue.size() > 0);
    // case: signal if dep has space
    if (c_rli->dep_queue.size() <
        (opt_mts_dependency_size * opt_mts_dependency_refill_threshold / 100))
    {
      c_rli->dep_full= false;
      mysql_cond_signal(&c_rli->dep_full_cond);
    }
  }

  info_thd->EXIT_COND(&old_stage);
  return ret;
}

// Pulls and executes events single group
// Returns true if the group executed successfully
bool Dependency_slave_worker::execute_group()
{
  int err= 0;
  Commit_order_manager *commit_order_mngr= get_commit_order_manager();

  DBUG_ASSERT(current_event_index == 0);
  auto begin_event= get_begin_event(commit_order_mngr);
  auto ev= begin_event;

  while (ev)
  {
    if (unlikely(err= execute_event(ev)))
    {
      c_rli->dependency_worker_error= true;
      break;
    }
    // case: restart trx if temporary error, see @slave_worker_ends_group
    if (unlikely(trans_retries && current_event_index == 0))
    {
      ev= begin_event;
      continue;
    }
    finalize_event(ev);
    ev= ev->next();
  }

  // case: error while appending to worker's internal queue
  if (unlikely(err == 1))
  {
    // Signal a rollback if commit ordering is enabled, we have to do
    // this here because it's not an exec error, so @slave_worker_ends_group
    // is not called
    if (commit_order_mngr)
      commit_order_mngr->report_rollback(this);
  }

  mysql_mutex_lock(&c_rli->dep_lock);
  if (c_rli->num_in_flight_trx)
    --c_rli->num_in_flight_trx;
  if (c_rli->num_in_flight_trx <= 1)
    mysql_cond_signal(&c_rli->dep_trx_all_done_cond);
  mysql_mutex_unlock(&c_rli->dep_lock);

  cleanup_group(begin_event);

  return err == 0 && running_status == RUNNING;
}

void Dependency_slave_worker::cleanup_group(
    std::shared_ptr<Log_event_wrapper> begin_event)
{
  // Delete all events manually in bottom-up manner to avoid stack overflow from
  // cascading shared_ptr deletions
  std::stack<std::weak_ptr<Log_event_wrapper>> events;
  auto& event= begin_event;
  while (event)
  {
    events.push(event);
    event= event->next_ev;
  }

  while (!events.empty())
  {
    auto sptr= events.top().lock();
    if (likely(sptr))
      sptr->next_ev.reset();
    events.pop();
  }
}

int
Dependency_slave_worker::execute_event(std::shared_ptr<Log_event_wrapper> &ev)
{
  // wait for all dependencies to be satisfied
  ev->wait();

  DBUG_EXECUTE_IF("dbug.dep_wait_before_update_execution",
    {
       if (ev->raw_event()->get_type_code() == UPDATE_ROWS_EVENT)
       {
         const char act[]= "now signal signal.reached wait_for signal.done";
         DBUG_ASSERT(opt_debug_sync_timeout > 0);
         DBUG_ASSERT(!debug_sync_set_action(info_thd, STRING_WITH_LEN(act)));
       }
     };);

  // case: there was an error in one of the workers, so let's skip execution of
  // events immediately
  if (unlikely(c_rli->dependency_worker_error))
    return 1;

  // case: the worker job queue is full, let's flush the queue to make progress
  if (unlikely(current_event_index >= jobs.size))
  {
    // Resets current_event_index to 0 and disables trx retires because we've
    // flushed the events
    mysql_mutex_lock(&jobs_lock);
    clear_current_group_events(this, c_rli, true);
    mysql_mutex_unlock(&jobs_lock);
  }

  // case: append to jobs queue only if this is not a trx retry, trx retries
  // resets @current_event_index, see @slave_worker_ends_group
  if (likely(current_event_index == jobs.len))
  {
    // NOTE: this is done so that @pop_jobs_item() can extract this event
    // although this is redundant it makes integration with existing code much
    // easier
    Slave_job_item item= { ev->raw_event() };
    if (append_item_to_jobs(&item, this, c_rli)) return 1;
    ev->is_appended_to_queue= true;
  }
  DBUG_ASSERT(ev->is_appended_to_queue);
  return ev->execute(this, this->info_thd, c_rli) == 0 ? 0 : -1;
}

void
Dependency_slave_worker::finalize_event(std::shared_ptr<Log_event_wrapper> &ev)
{
  /* Attempt to clean up entries from the key lookup
   *
   * There are two cases:
   * 1) The "value" of the key-value pair is equal to this event. In this case,
   *    remove the key-value pair from the map.
   * 2) The "value" of the key-value pair is _not_ equal to this event. In this
   *    case, leave it be; the event corresponds to a later transaction.
   */
  mysql_mutex_lock(&c_rli->dep_key_lookup_mutex);
  for (const auto& key : ev->keys)
  {
    auto it= c_rli->dep_key_lookup.find(key);
    DBUG_ASSERT(it != c_rli->dep_key_lookup.end());

    /* Case 1. (Case 2 is implicitly handled by doing nothing.) */
    if (it->second == ev)
    {
      c_rli->dep_key_lookup.erase(key);
    }
  }
  ev->finalize();
  mysql_mutex_unlock(&c_rli->dep_key_lookup_mutex);
}

Dependency_slave_worker::Dependency_slave_worker(Relay_log_info *rli
#ifdef HAVE_PSI_INTERFACE
                                   ,PSI_mutex_key *param_key_info_run_lock,
                                   PSI_mutex_key *param_key_info_data_lock,
                                   PSI_mutex_key *param_key_info_sleep_lock,
                                   PSI_mutex_key *param_key_info_thd_lock,
                                   PSI_mutex_key *param_key_info_data_cond,
                                   PSI_mutex_key *param_key_info_start_cond,
                                   PSI_mutex_key *param_key_info_stop_cond,
                                   PSI_mutex_key *param_key_info_sleep_cond
#endif
                                   ,uint param_id
    )
: Slave_worker(rli
#ifdef HAVE_PSI_INTERFACE
               ,param_key_info_run_lock, param_key_info_data_lock,
               param_key_info_sleep_lock, param_key_info_thd_lock,
               param_key_info_data_cond, param_key_info_start_cond,
               param_key_info_stop_cond, param_key_info_sleep_cond
#endif
               ,param_id
    )
{
}

void Dependency_slave_worker::start()
{
  DBUG_ASSERT(c_rli->dep_queue.empty() &&
              dep_key_lookup.empty() &&
              keys_accessed_by_group.empty() &&
              dbs_accessed_by_group.empty());

  while (execute_group());

  // case: cleanup if stopped abruptly
  if (running_status != STOP_ACCEPTED)
  {
    // tagging as exiting so Coordinator won't be able synchronize with it
    mysql_mutex_lock(&jobs_lock);
    running_status= ERROR_LEAVING;
    mysql_mutex_unlock(&jobs_lock);

    // Killing Coordinator to indicate eventual consistency error
    mysql_mutex_lock(&c_rli->info_thd->LOCK_thd_data);
    c_rli->info_thd->awake(THD::KILL_QUERY);
    mysql_mutex_unlock(&c_rli->info_thd->LOCK_thd_data);
  }
}

