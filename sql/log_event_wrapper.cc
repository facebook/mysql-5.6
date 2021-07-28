#include "rpl_rli.h"
#include "rpl_rli_pdb.h"
#include "log_event_wrapper.h"

void Log_event_wrapper::put_next(std::shared_ptr<Log_event_wrapper> &ev)
{
  mysql_mutex_lock(&mutex);
  DBUG_ASSERT(!next_ev &&
              (ev->begin_event() == begin_ev.lock() ||
               is_begin_event));
  next_ev= ev;
  mysql_cond_signal(&next_event_cond);
  mysql_mutex_unlock(&mutex);
}

bool Log_event_wrapper::wait(Slave_worker *worker)
{
  DBUG_ASSERT(worker);
  mysql_mutex_lock(&mutex);
  auto info_thd= worker->info_thd;
  PSI_stage_info old_stage;
  info_thd->ENTER_COND(&cond,
                       &mutex,
                       &stage_slave_waiting_for_dependencies,
                       &old_stage);
  while (!info_thd->killed &&
         worker->running_status == Slave_worker::RUNNING &&
         dependencies &&
         !worker->found_order_commit_deadlock())
  {
    const auto timeout_nsec=
      worker->c_rli->mts_dependency_cond_wait_timeout * 1000000;
    struct timespec abstime;
    set_timespec_nsec(abstime, timeout_nsec);
    mysql_cond_timedwait(&cond, &mutex, &abstime);
  }
  info_thd->EXIT_COND(&old_stage);
  return !info_thd->killed && worker->running_status == Slave_worker::RUNNING;
}

std::shared_ptr<Log_event_wrapper> Log_event_wrapper::next()
{
  if (unlikely(is_end_event))
    return nullptr;

  mysql_mutex_lock(&mutex);
  auto worker= static_cast<Slave_worker*>(raw_ev->worker);
  DBUG_ASSERT(worker);
  auto info_thd= worker->info_thd;
  PSI_stage_info old_stage;
  info_thd->ENTER_COND(&next_event_cond, &mutex,
                       &stage_slave_waiting_event_from_coordinator, &old_stage);
  while (!info_thd->killed &&
         worker->running_status == Slave_worker::RUNNING &&
         !next_ev)
  {
    ++worker->c_rli->next_event_waits;
    const auto timeout_nsec=
      worker->c_rli->mts_dependency_cond_wait_timeout * 1000000;
    struct timespec abstime;
    set_timespec_nsec(abstime, timeout_nsec);
    mysql_cond_timedwait(&next_event_cond, &mutex, &abstime);
  }
  info_thd->EXIT_COND(&old_stage);
  return next_ev;
}

bool Log_event_wrapper::path_exists(
    const std::shared_ptr<Log_event_wrapper> &ev) const
{
  auto tmp= next_ev;
  while (tmp)
  {
    if (tmp == ev) { return true; }
    tmp= tmp->next_ev;
  }
  return false;
}
