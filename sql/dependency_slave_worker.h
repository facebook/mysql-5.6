#ifndef DEPENDENCY_SLAVE_WORKER_H
#define DEPENDENCY_SLAVE_WORKER_H

#ifdef HAVE_REPLICATION

#include "rpl_rli_pdb.h"


class Dependency_slave_worker : public Slave_worker
{
  Log_event_wrapper* get_begin_event();
  Log_event_wrapper* find_begin_event();
  Log_event_wrapper* wait_for_begin_event();

  Log_event_wrapper* get_next_event(Log_event_wrapper *event);

  bool execute_group();
  int  execute_event(Log_event_wrapper *ev);
  void remove_event(Log_event_wrapper *ev);

public:
  Dependency_slave_worker(Relay_log_info *rli
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
      );

  void start();
};

#endif // HAVE_REPLICATION

#endif // DEPENDENCY_SLAVE_WORKER_H
