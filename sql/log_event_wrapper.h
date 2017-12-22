#ifndef LOG_EVENT_WRAPPER_H
#define LOG_EVENT_WRAPPER_H

#include "log_event.h"

int slave_worker_exec_job(Slave_worker *worker, Relay_log_info *rli);

/**
  @class Log_event_wrapper
  */
class Log_event_wrapper
{
  Log_event *event;
  Log_event_wrapper *begin_event;

  // Condition and lock for when the event is ready to be executed
  mysql_cond_t cond;
  mysql_mutex_t mutex;

  bool ready_to_execute;

public:
  // Keys written by this event, should only be non-empty for Rows_log_events
  std::unordered_set<Dependency_key> keys;

  Log_event_wrapper *next_group_event;
  std::deque<Log_event_wrapper*> children;
  std::atomic<uint> num_parents;

  std::atomic<uint> is_assigned; // has this event been assigned to a worker?
  bool is_appended_to_queue; // has this event been assigned to a worker queue?
  bool is_begin_event;
  bool is_end_event;
  bool whole_group_in_dag; // entire group of this event exists in the DAG?

  Log_event_wrapper(Log_event *event, Log_event_wrapper *begin_event)
  {
    this->event= event;
    this->begin_event= begin_event;
    ready_to_execute= false;
    whole_group_in_dag= false;
    next_group_event= NULL;
    children.clear();
    keys.clear();
    num_parents.store(0U);
    is_assigned.store(0U);
    is_appended_to_queue= false;
    is_begin_event= false;
    is_end_event= false;
    mysql_mutex_init(0, &mutex, MY_MUTEX_INIT_FAST);
    mysql_cond_init(0, &cond, NULL);
  }

  ~Log_event_wrapper()
  {
    keys.clear();
    children.clear();

    // case: event was not appended to a worker's queue, so we need to delete it
    if (!is_appended_to_queue)
      delete event;
    keys.clear();
    mysql_mutex_destroy(&mutex);
    mysql_cond_destroy(&cond);
  }

  inline Log_event* get_raw_event() const
  {
    return event;
  }

  inline void set_raw_event(Log_event *ev)
  {
    event= ev;
  }

  inline Log_event_wrapper* get_begin_event() const
  {
    return begin_event;
  }

  inline void wait()
  {
    mysql_mutex_lock(&mutex);
    //while (!ready_to_execute || num_parents > 0)
    while (num_parents > 0)
      mysql_cond_wait(&cond, &mutex);
    mysql_mutex_unlock(&mutex);
  }

  inline void signal()
  {
    mysql_mutex_lock(&mutex);
    DBUG_ASSERT(num_parents == 0);
    mysql_cond_signal(&cond);
    mysql_mutex_unlock(&mutex);
  }

  inline void add_child(Log_event_wrapper *child)
  {
    mysql_mutex_lock(&mutex);
    if (is_assigned != 2U)
    {
      DBUG_ASSERT(is_assigned < 2U);
      children.push_back(child);
      child->num_parents.fetch_add(1U);
    }
    mysql_mutex_unlock(&mutex);
  }

  inline void mark_executed()
  {
    DBUG_ASSERT(is_assigned == 1U);
    mysql_mutex_lock(&mutex);
    is_assigned.fetch_add(1U);
    for (auto child : children)
    {
      if (child->num_parents.fetch_sub(1U) == 1)
      {
        child->signal();
      }
    }
    mysql_mutex_unlock(&mutex);
  }

  inline int execute(Slave_worker *w, THD *thd, Relay_log_info *rli)
  {
    // the raw event was already added to the worker's jobs queue, so it's safe
    // to call @slave_worker_exec_job function directly
    return slave_worker_exec_job(w, rli);
  }
};

#endif // LOG_EVENT_WRAPPER_H
