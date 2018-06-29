#ifndef LOG_EVENT_WRAPPER_H
#define LOG_EVENT_WRAPPER_H

#include "log_event.h"

int slave_worker_exec_job(Slave_worker *worker, Relay_log_info *rli);

/**
  @class Log_event_wrapper
  */
class Log_event_wrapper
{
  Log_event *raw_ev;
  std::weak_ptr<Log_event_wrapper> begin_ev;

  // events that depend on us
  std::vector<std::shared_ptr<Log_event_wrapper>> dependents;
  // number of events that we depend on
  ulonglong dependencies= 0;

  // mutex for everything in this event
  mysql_mutex_t mutex;
  // cond var for dependency tracking
  mysql_cond_t cond;
  // cond var for next event in the group
  mysql_cond_t next_event_cond;

  // has all dependendents of this event been notified after execution?
  bool is_finalized= false;

public:
  std::shared_ptr<Log_event_wrapper> next_ev;

  // keys touched by this event, it should not be empty for rows event
  std::unordered_set<Dependency_key> keys;

  // has this event been assigned to a worker queue?
  std::atomic_bool is_appended_to_queue{false};
  // is this the first event of a group?
  std::atomic_bool is_begin_event{false};
  // is this the last event of a group?
  std::atomic_bool is_end_event{false};
  // entire group of this event scheduled?
  std::atomic_bool whole_group_scheduled{false};

  Log_event_wrapper(Log_event *raw_ev,
                    std::shared_ptr<Log_event_wrapper> &begin_ev) :
    raw_ev(raw_ev), begin_ev(begin_ev)
  {
    mysql_mutex_init(0, &mutex, MY_MUTEX_INIT_FAST);
    mysql_cond_init(0, &cond, NULL);
    mysql_cond_init(0, &next_event_cond, NULL);
  }

  ~Log_event_wrapper()
  {
    // case: event was not appended to a worker's queue, so we need to delete it
    if (unlikely(!is_appended_to_queue)) { delete raw_ev; }
    raw_ev= nullptr;

#ifndef DBUG_OFF
    mysql_mutex_lock(&mutex);
    DBUG_ASSERT(dependencies == 0);
    mysql_mutex_unlock(&mutex);
#endif

    mysql_mutex_destroy(&mutex);
    mysql_cond_destroy(&cond);
    mysql_cond_destroy(&next_event_cond);
  }

  Log_event* raw_event() const { return raw_ev; }

  std::shared_ptr<Log_event_wrapper>
  begin_event() const { return begin_ev.lock(); }

  void add_dependent(std::shared_ptr<Log_event_wrapper> &ev)
  {
    mysql_mutex_lock(&mutex);
    DBUG_ASSERT(!is_finalized && ev.get() != this && ev->raw_ev && raw_ev);
    dependents.push_back(ev);
    ev->incr_dependency();
    mysql_mutex_unlock(&mutex);
  }

  void incr_dependency()
  {
    mysql_mutex_lock(&mutex);
    ++dependencies;
    mysql_mutex_unlock(&mutex);
  }

  void decr_dependency()
  {
    mysql_mutex_lock(&mutex);
    if ((--dependencies) == 0)
      mysql_cond_signal(&cond);
    mysql_mutex_unlock(&mutex);
  }

  void wait()
  {
    mysql_mutex_lock(&mutex);
    while (dependencies)
      mysql_cond_wait(&cond, &mutex);
    mysql_mutex_unlock(&mutex);
  }

  void finalize()
  {
    mysql_mutex_lock(&mutex);
    if (likely(!is_finalized))
    {
      for (auto& dep : dependents)
        dep->decr_dependency();
      is_finalized= true;
    }
    mysql_mutex_unlock(&mutex);
  }

  bool finalized()
  {
    mysql_mutex_lock(&mutex);
    auto ret= is_finalized;
    mysql_mutex_unlock(&mutex);
    return ret;
  }

  int execute(Slave_worker *w, THD *thd, Relay_log_info *rli)
  {
    // the raw event was already added to the worker's jobs queue, so it's safe
    // to call @slave_worker_exec_job function directly
    return slave_worker_exec_job(w, rli);
  }

  void put_next(std::shared_ptr<Log_event_wrapper> &ev);
  std::shared_ptr<Log_event_wrapper> next();
  bool path_exists(const std::shared_ptr<Log_event_wrapper> &ev) const;
};

#endif // LOG_EVENT_WRAPPER_H
