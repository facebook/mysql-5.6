#include <my_global.h>
#include "dependency_slave_worker.h"
#include "log_event_wrapper.h"


bool append_item_to_jobs(slave_job_item *job_item,
                         Slave_worker *w,
                         Relay_log_info *rli);

inline Log_event_wrapper* Dependency_slave_worker::get_begin_event()
{
  auto ret= find_begin_event();
  if (!ret) return wait_for_begin_event();
  return ret;
}

Log_event_wrapper* Dependency_slave_worker::find_begin_event()
{
  c_rli->dag_rdlock();
  Log_event_wrapper *ret= NULL;

  for (auto& event : c_rli->dag.get_head())
  {
    // NOTE: We need the whole group to exist in the DAG to avoid starvation.
    // Dependencies between start events can be determined by other conflicting
    // events between transactions, so we can't be sure if a begin event is free
    // to be executed unless the entire group is in the DAG.
    if (event->is_begin_event &&
        event->whole_group_in_dag &&
        event->is_assigned.fetch_or(1U) == 0U)
    {
      ret= event;
      break;
    }
  }

  c_rli->dag_unlock();
  return ret;
}

Log_event_wrapper* Dependency_slave_worker::wait_for_begin_event()
{
  mysql_mutex_lock(&c_rli->dag_changed_mutex);
  Log_event_wrapper *ev= NULL;
  PSI_stage_info old_stage;

  info_thd->ENTER_COND(&c_rli->dag_changed_cond, &c_rli->dag_changed_mutex,
                       &stage_slave_waiting_event_from_coordinator,
                       &old_stage);
  // case: wait for a change in the DAG which brings a begin event to the head
  // of the DAG, while the slave is running
  while (!info_thd->killed && running_status == RUNNING &&
         !(c_rli->dag_changed && (ev= find_begin_event()) != NULL))
    mysql_cond_wait(&c_rli->dag_changed_cond, &c_rli->dag_changed_mutex);
  c_rli->dag_changed= false;

  info_thd->EXIT_COND(&old_stage);

  return ev;
}

Log_event_wrapper*
Dependency_slave_worker::get_next_event(Log_event_wrapper *event)
{
  if (event->is_end_event) return NULL;

  c_rli->dag_rdlock();
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

  c_rli->dag_unlock();

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
  std::vector<Log_event_wrapper*> events;

  Log_event_wrapper *ev= get_begin_event();
  Log_event_wrapper *next_ev= NULL;

  // case: we didn't execute the group
  if (!ev) err= 1;

  /**
    Here's what is happening in the loop:
    - The loop executes until all the events of the group are removed from
      the DAG.
    - If a temporary error occurs during execution of an event, the grp is
      rollbacked and @current_event_index is reset to 0. During retries, we get
      events from the @events vector if we've already extracted them from DAG.
    - If a fatal error occurs, we continue the loop until all events of the grp
      are extracted from the DAG. We don't execute any event after a fatal err,
      we just remove them from the DAG. We do this to avoid a zombie grp in DAG.
   */

  while (ev)
  {
    // case: this is the first time we're seeing this event, trx retries can
    // cause the same event to be encountered again
    if (current_event_index == events.size()) events.push_back(ev);

    if (!err && (err= execute_event(ev)))
    {
      // case: append error, so we need to clean up the event here
      if (err == 1)
      {
        delete ev->get_raw_event();
        ev->set_raw_event(NULL);
      }
    }

    // case: temporary error hence trx retries, see @slave_worker_ends_group
    // when a temporary error is detected, @current_event_index is reset to
    // retry the trx from the beginning
    if (!err && trans_retries && current_event_index < events.size())
    {
      ev= events[current_event_index];
      continue;
    }

    next_ev= get_next_event(ev);
    remove_event(ev);
    ev= next_ev;
  }

  // cleanup
  for (auto& event : events) delete event;

  return err == 0 && running_status == RUNNING;
}

inline int Dependency_slave_worker::execute_event(Log_event_wrapper *ev)
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
  }
  return ev->execute(this, this->info_thd, c_rli) == 0 ? 0 : -1;
}

void Dependency_slave_worker::remove_event(Log_event_wrapper *ev)
{
  DBUG_ASSERT(ev->is_assigned.load() == 1U);
  DBUG_ASSERT(c_rli->dag.exists(ev));
  c_rli->dag_wrlock();

  for (auto& child : c_rli->dag.get_children(ev))
  {
    // case: this event is going to be the top of the DAG, ready to be executed
    if (c_rli->dag.get_parents(child).size() == 1)
    {
      // assert: the only parent should be @ev
      DBUG_ASSERT(c_rli->dag.get_parents(child).find(ev) !=
                  c_rli->dag.get_parents(child).end());
      child->signal();
    }
  }
  c_rli->dag.remove_head_node(ev);

  // case: signal if DAG is now empty
  if (c_rli->dag.is_empty())
  {
    mysql_mutex_lock(&c_rli->dag_empty_mutex);
    c_rli->dag_empty= true;
    mysql_cond_signal(&c_rli->dag_empty_cond);
    mysql_mutex_unlock(&c_rli->dag_empty_mutex);
  }

  // clean up other DAG related structures in rli
  for (auto it= c_rli->dag_table_last_penultimate_event.cbegin();
            it != c_rli->dag_table_last_penultimate_event.cend();)
  {
    if (it->second == ev)
      c_rli->dag_table_last_penultimate_event.erase(it++);
    else
      ++it;
  }

  c_rli->dag_unlock();

  // broadcast a change in the DAG
  mysql_mutex_lock(&c_rli->dag_changed_mutex);
  c_rli->dag_changed= true;
  mysql_cond_broadcast(&c_rli->dag_changed_cond);
  mysql_mutex_unlock(&c_rli->dag_changed_mutex);
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
              dag_table_last_penultimate_event.empty() &&
              tables_accessed_by_group.empty());

  while (execute_group())
  {
    // admission control for DAG
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

