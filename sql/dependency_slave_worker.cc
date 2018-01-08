#include <my_global.h>
#include "dependency_slave_worker.h"
#include "log_event_wrapper.h"
#include "rpl_slave_commit_order_manager.h"


bool append_item_to_jobs(slave_job_item *job_item,
                         Slave_worker *w,
                         Relay_log_info *rli);

bool
Dependency_slave_worker::extract_group(std::vector<Log_event_wrapper*>& group)
{
  if (!find_group(group))
    return wait_for_group(group);
  return true;
}

bool Dependency_slave_worker::find_group(std::vector<Log_event_wrapper*>& group)
{
  bool found= false;
  Log_event_wrapper *event= NULL;
  Commit_order_manager *commit_order_mngr= get_commit_order_manager();

  c_rli->dag_wrlock();
  if (c_rli->begin_event_list.size() > 0)
  {
    found= true;
    event= c_rli->begin_event_list.front();
    DBUG_ASSERT(event != NULL);
    c_rli->begin_event_list.pop_front();
    if (commit_order_mngr != NULL)
    {
      DBUG_ASSERT(opt_mts_dependency_order_commits);
      commit_order_mngr->register_trx(this);
    }
  }
  c_rli->dag_unlock();

  while (event)
  {
    event->is_assigned.store(1U);
    group.push_back(event);
    event= event->next_group_event;
  }
  return found;
}

bool
Dependency_slave_worker::wait_for_group(std::vector<Log_event_wrapper*>& group)
{
  mysql_mutex_lock(&c_rli->dag_group_ready_mutex);
  bool found= false;
  PSI_stage_info old_stage;

  info_thd->ENTER_COND(&c_rli->dag_group_ready_cond,
                       &c_rli->dag_group_ready_mutex,
                       &stage_slave_waiting_event_from_coordinator,
                       &old_stage);
  // case: wait for a change in the DAG which brings a begin event to the head
  // of the DAG, while the slave is running
  while (!info_thd->killed && running_status == RUNNING &&
         !(c_rli->dag_group_ready && (found= find_group(group))))
    mysql_cond_wait(&c_rli->dag_group_ready_cond,
                    &c_rli->dag_group_ready_mutex);
  c_rli->dag_group_ready= false;

  info_thd->EXIT_COND(&old_stage);

  return found;
}

// Gets the next event for the group from the DAG
// NOTE: this method assumes that a lock is taken on the DAG
Log_event_wrapper*
Dependency_slave_worker::get_next_event(Log_event_wrapper *event)
{
  if (event->is_end_event) return NULL;

  Log_event_wrapper *ret= NULL;

  Log_event_wrapper *begin_event= event->is_begin_event ?
    event : event->get_begin_event();

  for (auto& child : c_rli->dag.get_children(event))
  {
    if (child->get_begin_event() == begin_event)
    {
      child->is_assigned.store(1U);
      ret= child;
      break;
    }
  }

  // since the whole group must be in the DAG before execution starts, we always
  // have to find the all the events of a group in the DAG
  DBUG_ASSERT(ret != NULL);
  return ret;
}

// Pulls and executes events from the DAG which are part of the current group
// Returns true if the group executed successfully
bool Dependency_slave_worker::execute_group()
{
  int err= 0;
  size_t last_grp_event_in_dag= 0;
  std::vector<Log_event_wrapper*> events;
  Commit_order_manager *commit_order_mngr= get_commit_order_manager();

  if (!extract_group(events))
  {
    err= -1;
    goto end;
  }

  // case: place ourselves in the commit order queue
  /*
  if (commit_order_mngr != NULL)
  {
    DBUG_ASSERT(opt_mts_dependency_order_commits);
    commit_order_mngr->register_trx(this);
  }
  */

  /**
    Here's what is happening in the loop:
    - The loop executes until all the events of the group are removed from
      the DAG.
    - If a temporary error occurs during execution of an event, the grp is
      rollbacked and @current_event_index is reset to 0
    - If a fatal error occurs we break out of the loop immidiately
   */
  DBUG_ASSERT(current_event_index == 0);
  DBUG_ASSERT(events.size() >= 1);
  do
  {
    auto ev= events[current_event_index];
    if ((err= execute_event(ev)))
      break;
    // case: we are not retrying this event, i.e. this was the first time we
    // executed this event, so we should remove it from the DAG
    if (!trans_retries || current_event_index >= last_current_event_index)
    {
      ev->mark_executed();
      // remove_event(ev);
      ++last_grp_event_in_dag;
    }
  // case: when grp execution succeeds both @trans_retries and
  // @current_event_index are reset
  } while (!(trans_retries == 0 && current_event_index == 0));

  // case: error while appending to worker's internal queue
  if (err == 1)
  {
    // Signal a rollback if commit ordering is enabled, we have to do
    // this here because it's an append error, so @slave_worker_ends_group
    // is not called
    if (commit_order_mngr)
      commit_order_mngr->report_rollback(this);
  }

  // remove the rest of the group's events from the DAG (if any)
  // for (size_t i= last_grp_event_in_dag; i < events.size(); ++i)
  //  remove_event(events[i]);

  DBUG_ASSERT(events.size() >= 1);
  cleanup_group(events);

end:
  // cleanup
  for (auto& event : events) delete event;
  return err == 0 && running_status == RUNNING;
}

void Dependency_slave_worker::cleanup_map(Log_event_wrapper *event)
{
  /* Attempt to clean up entries from the penultimate event map.
   *
   * There are two cases:
   * 1) The "value" of the key-value pair is equal to this event. In this case,
   *    remove the key-value pair from the map.
   * 2) The "value" of the key-value pair is _not_ equal to this event. In this
   *    case, leave it be; the event corresponds to a later transaction.
   */
  for (auto key : event->keys)
  {
    auto it= c_rli->dag_key_last_penultimate_event.find(key);
    DBUG_ASSERT(it != c_rli->dag_key_last_penultimate_event.end());

    /* Case 1. (Case 2 is implicitly handled by doing nothing.) */
    if (it->second == event)
    {
      c_rli->dag_key_last_penultimate_event.erase(key);
    }
  }
}

void
Dependency_slave_worker::cleanup_group(std::vector<Log_event_wrapper*> &events)
{
  c_rli->dag_wrlock();

  // jmf: A lock-free hash-table would not require a lock here.
  for (auto ev : events)
  {
    cleanup_map(ev);
  }

  // admission control for DAG
  mysql_mutex_lock(&c_rli->dag_full_mutex);
  DBUG_ASSERT(c_rli->dag_num_groups > 0);
  --c_rli->dag_num_groups;
  dag_empty= (c_rli->dag_num_groups == 0);
  // case: signal if DAG has space
  if (c_rli->dag_full && c_rli->dag_num_groups <
      (opt_mts_dependency_size * opt_mts_dependency_refill_threshold / 100))
  {
    c_rli->dag_full= false;
    mysql_cond_signal(&c_rli->dag_full_cond);
  }
  mysql_mutex_unlock(&c_rli->dag_full_mutex);

    // case: signal if DAG is now empty
  if (dag_empty)
  {
    DBUG_ASSERT(c_rli->begin_event_list.size() == 0);
    mysql_mutex_lock(&c_rli->dag_empty_mutex);
    c_rli->dag_empty= true;
    mysql_cond_signal(&c_rli->dag_empty_cond);
    mysql_mutex_unlock(&c_rli->dag_empty_mutex);
  }


  c_rli->dag_unlock();
}

int Dependency_slave_worker::execute_event(Log_event_wrapper *ev)
{
  // wait for all dependencies to be satisfied
  ev->wait();
  // case: append to jobs queue only if this is not a trx retry, trx retries
  // resets @current_event_index, see @slave_worker_ends_group
  if (current_event_index == jobs.len)
  {
    // NOTE: this is done so that @pop_jobs_item() can extract this event
    // although this is redundant it makes integration with existing code much
    // easier
    Slave_job_item item= { ev->get_raw_event() };
    if (append_item_to_jobs(&item, this, c_rli)) return 1;
    ev->is_appended_to_queue= true;
  }
  DBUG_ASSERT(ev->is_appended_to_queue);
  return ev->execute(this, this->info_thd, c_rli) == 0 ? 0 : -1;
}

void Dependency_slave_worker::remove_event(Log_event_wrapper *ev)
{
  DBUG_ASSERT(ev->is_assigned.load() == 1U);

  c_rli->dag_wrlock();

  DBUG_ASSERT(c_rli->dag.exists(ev));

  bool group_ready= false;

  for (auto& child : c_rli->dag.get_children(ev))
  {
    // case: this event is going to be the top of the DAG, ready to be executed
    if (c_rli->dag.get_parents(child).size() == 1)
    {
      // assert: the only parent should be @ev
      DBUG_ASSERT(c_rli->dag.get_parents(child).find(ev) !=
                  c_rli->dag.get_parents(child).end());
      child->signal();
      group_ready |= (child->is_begin_event && child->whole_group_in_dag);
    }
  }
  c_rli->dag.remove_head_node(ev);

  /* Attempt to clean up entries from the penultimate event map.
   *
   * There are two cases:
   * 1) The "value" of the key-value pair is equal to this event. In this case,
   *    remove the key-value pair from the map.
   * 2) The "value" of the key-value pair is _not_ equal to this event. In this
   *    case, leave it be; the event corresponds to a later transaction.
   */
  for (auto key : ev->keys)
  {
    auto it= c_rli->dag_key_last_penultimate_event.find(key);
    DBUG_ASSERT(it != c_rli->dag_key_last_penultimate_event.end());

    /* Case 1. (Case 2 is implicitly handled by doing nothing.) */
    if (it->second == ev)
    {
      c_rli->dag_key_last_penultimate_event.erase(key);
    }
  }

  /* We have to clear the set of keys so that the shared_ptr corresponding to
   * the key buffer is deallocated. */
  ev->keys.clear();

  for (auto it= c_rli->dag_db_last_start_event.cbegin();
            it != c_rli->dag_db_last_start_event.cend();)
  {
    if (it->second == ev)
      c_rli->dag_db_last_start_event.erase(it++);
    else
      ++it;
  }

  // case: signal if DAG is now empty
  if (c_rli->dag.is_empty())
  {
    mysql_mutex_lock(&c_rli->dag_empty_mutex);
    c_rli->dag_empty= true;
    mysql_cond_signal(&c_rli->dag_empty_cond);
    mysql_mutex_unlock(&c_rli->dag_empty_mutex);
  }

  // admission control for DAG
  if (ev->is_end_event)
  {
    mysql_mutex_lock(&c_rli->dag_full_mutex);
    DBUG_ASSERT(c_rli->dag_num_groups > 0);
    --c_rli->dag_num_groups;
    // case: signal if DAG has space
    if (c_rli->dag_full && c_rli->dag_num_groups <
        (opt_mts_dependency_size * opt_mts_dependency_refill_threshold / 100))
    {
      c_rli->dag_full= false;
      mysql_cond_signal(&c_rli->dag_full_cond);
    }
    mysql_mutex_unlock(&c_rli->dag_full_mutex);
  }

  c_rli->dag_unlock();

  // case: removal of this event has brought a begin event to the head of the
  // DAG, signal!
  if (group_ready)
  {
    // broadcast a change in the DAG
    mysql_mutex_lock(&c_rli->dag_group_ready_mutex);
    c_rli->dag_group_ready= true;
    mysql_cond_broadcast(&c_rli->dag_group_ready_cond);
    mysql_mutex_unlock(&c_rli->dag_group_ready_mutex);
  }
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
  DBUG_ASSERT(c_rli->dag.is_empty() &&
              dag_key_last_penultimate_event.empty() &&
              keys_accessed_by_group.empty() &&
              dag_db_last_start_event.empty() &&
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

