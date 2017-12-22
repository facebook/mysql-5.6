#ifndef DEPENDENCY_SLAVE_WORKER_H
#define DEPENDENCY_SLAVE_WORKER_H

#ifdef HAVE_REPLICATION

#include "rpl_rli_pdb.h"


class Dependency_slave_worker : public Slave_worker
{
  inline bool extract_group(std::vector<Log_event_wrapper*>& group);
  bool find_group(std::vector<Log_event_wrapper*>& group);
  bool wait_for_group(std::vector<Log_event_wrapper*>& group);

  Log_event_wrapper* get_next_event(Log_event_wrapper *event);

  bool execute_group();
  inline int execute_event(Log_event_wrapper *ev);
  void remove_event(Log_event_wrapper *ev);
  void cleanup_group(std::vector<Log_event_wrapper*> &events);
  void cleanup_map(Log_event_wrapper *event);

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
