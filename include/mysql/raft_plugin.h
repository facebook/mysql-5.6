// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <future>
#include <map>
#include <vector>
#include "my_sys.h"                 // IO_CACHE
#include "mysql/psi/mysql_mutex.h"  // mysql_mutex_t

#ifdef INCL_DEFINED_IN_MYSQL_SERVER
extern bool enable_raft_plugin;
#endif
class RaftListenerQueueIf;

#ifdef __cplusplus
extern "C" {
#endif
// Finer grained error code during deregister of observer
// Observer was not present in delegate (i.e. not
// previously added to delegate ), and should be safe
// to ignore.
#define MYSQL_REPLICATION_OBSERVER_NOT_FOUND 2

enum class RaftReplicateMsgOpType {
  OP_TYPE_INVALID = 0,
  OP_TYPE_TRX = 1,
  OP_TYPE_ROTATE = 2,
  OP_TYPE_NOOP = 3,
  OP_TYPE_CHANGE_CONFIG = 4,
};

/* Type of callback that raft plugin wants to invoke in the server */
enum class RaftListenerCallbackType {
  SET_READ_ONLY = 1,
  TRIM_LOGGED_GTIDS = 2,
  ROTATE_BINLOG = 3,
  ROTATE_RELAYLOG = 4,
  RAFT_LISTENER_THREADS_EXIT = 5,
  RLI_RELAY_LOG_RESET = 6,
  RESET_SLAVE = 7,
  BINLOG_CHANGE_TO_APPLY = 8,
  BINLOG_CHANGE_TO_BINLOG = 9,
  STOP_SQL_THREAD = 10,
  START_SQL_THREAD = 11,
  STOP_IO_THREAD = 12,
  CHANGE_MASTER = 13,
  GET_COMMITTED_GTIDS = 14,
  GET_EXECUTED_GTIDS = 15,
  SET_BINLOG_DURABILITY = 16,
  RAFT_CONFIG_CHANGE = 17,
};

/* Callback argument, each type would just populate the fields needed for its
 * callback */
class RaftListenerCallbackArg {
 public:
  explicit RaftListenerCallbackArg() {}

  std::vector<std::string> trim_gtids = {};
  std::pair<std::string, unsigned long long> log_file_pos = {};
  bool val_bool;
  uint32_t val_uint;
  std::pair<std::string, unsigned int> master_instance;
  std::string val_str;
  std::map<std::string, unsigned int> val_sys_var_uint;
};

/* Result of the callback execution in the server. This will be set in the
 * future's promise (in the QueueElement) and the invoker can get()/wait() for
 * the result. Add more fields as needed */
class RaftListenerCallbackResult {
 public:
  explicit RaftListenerCallbackResult() {}

  // Indicates if the callback was able to execute successfully
  int error = 0;
  std::vector<std::string> gtids;
  std::string val_str;
};

class RaftListenerQueueIf {
 public:
  static const int RAFT_FLAGS_POSTAPPEND = 1;
  static const int RAFT_FLAGS_NOOP = 2;

  virtual ~RaftListenerQueueIf() {}

  /* Defines the element of the queue. It consists of the callback type to be
   * invoked and the argument (optional) for the callback */
  struct QueueElement {
    // Type of the callback to invoke in the server
    RaftListenerCallbackType type;

    // Argument to the callback
    RaftListenerCallbackArg arg;

    /* result of the callback will be fulfilled through this promise. If this
     * is set, then the invoker should ensure tht he eventually calls
     * get()/wait() to retrieve the result. Example:
     *
     * std::promise<RaftListenerCallbackResult> promise;
     * std::future<RaftListenerCallbackResult> fut = promise.get_future();
     *
     * QueueElement e;
     * e.type = RaftListenerCallbackType::SET_READ_ONLY;
     * e.result = &promise;
     * listener_queue.add(std::move(e));
     * ....
     * ....
     * ....
     * // Get the result when we want it. This wll block until the promise is
     * // fullfilled by the raft listener thread after executing the callback
     * RaftListenerCallbackResult result = fut.get();
     */
    std::promise<RaftListenerCallbackResult> *result = nullptr;
  };

  /* Add an element to the queue. This will signal any listening threads
   * after adding the element to the queue
   *
   * @param element QueueElement to add to queue
   *
   * @return 0 on success, 1 on error
   */
  virtual int add(QueueElement element) = 0;

  /* Get an element from the queue. This will block if there are no elements
   * in the queue to be processed
   *
   * @return QueueElement to be processed next
   */
  virtual QueueElement get() = 0;

  virtual void deinit() = 0;
};

/**
  Raft replication observer parameter
*/
typedef struct Raft_replication_param {
  uint32 server_id = 0;
  const char *host_or_ip = nullptr;
  int64_t term = -1;
  int64_t index = -1;
  uchar *gtid_buffer = nullptr;
  uchar *metadata_buffer = nullptr;
} Raft_replication_param;

/**
   Observe special events for Raft replication to work
*/
typedef struct Raft_replication_observer {
  uint32 len;

  /**
     This callback is called before transaction commit
     and after binlog sync.

     For both non-transactional tables and transactional
     tables this is called after binlog sync.

     @param param The parameter for transaction observers

     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*before_commit)(Raft_replication_param *param);

  /**
     This callback is called before events of a txn are written to binlog file

     @param param Observer common parameter
     @param cache IO_CACHE containing binlog events for the txn
     @param op_type The type of operation for which before_flush is called

     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*before_flush)(Raft_replication_param *param, IO_CACHE *cache,
                      RaftReplicateMsgOpType op_type);

  /**
     This callback is called once upfront to setup the appropriate
     binlog file, io_cache and its mutexes

     @param is_relay_log whether the file being registered is relay or binlog
     @param log_file_cache  the IO_CACHE pointer
     @param log_prefix the prefix of logs e.g. /binlogs/binary-logs-3306
     @param log_name the pointer to current log name
     @param lock_log the mutex that protects the current log
     @param lock_index the mutex that protects the index file
     @param update_cond the condvar that is fired after writing to log
     @param cur_log_ext a pointer the number of the file.
     @param context context of the call (0 for 1st run, 1 for next time)

     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*setup_flush)(bool is_relay_log, IO_CACHE *log_file_cache,
                     const char *log_prefix, const char *log_name,
                     mysql_mutex_t *lock_log, mysql_mutex_t *lock_index,
                     mysql_cond_t *update_cond, ulong *cur_log_ext,
                     int context);

  /**
   * This callback is invoked by the server to gracefully shutdown the
   * Raft threads
   */
  int (*before_shutdown)();

  /**
   * @param raft_listener_queue - the listener queue in which to add requests
   * @param s_uuid - the uuid of the server to be used as the INSTANCE UUID
   *                 in Raft
   * @param wal_dir_parent - the parent directory under which raft will create
   * config metadata
   * @param log_dir_parent - the parent directory under which raft will create
   * metric logs
   * @param raft_log_path_prefix - the prefix with the dirname path which tells
   * @param s_hostname - the proper hostname of server which can be used in
   *                     SMC and logging
   * @param port - the port of the server
   * raft where to find raft binlogs.
   */
  int (*register_paths)(RaftListenerQueueIf *raft_listener_queue,
                        const std::string &s_uuid,
                        const std::string &wal_dir_parent,
                        const std::string &log_dir_parent,
                        const std::string &raft_log_path_prefix,
                        const std::string &s_hostname, uint64_t port);

  /**
     This callback is called after transaction commit to engine

     @param param The parameter for the observers

     @retval 0 Sucess
     @retval 1 Failure
  */
  int (*after_commit)(Raft_replication_param *param);
} Raft_replication_observer;

/**
   Register a Raft replication observer

   @param observer The raft replication observer to register
   @param p pointer to the internal plugin structure

   @retval 0 Sucess
   @retval 1 Observer already exists
*/
int register_raft_replication_observer(Raft_replication_observer *observer,
                                       void *p);

/**
   Unregister a raft replication observer

   @param observer The raft observer to unregister
   @param p pointer to the internal plugin structure

   @retval 0 Sucess
   @retval 1 Observer not exists
*/
int unregister_raft_replication_observer(Raft_replication_observer *observer,
                                         void *p);

/**
 * Mark this GTID as logged in the rli and sets the master_log_file and
 * master_log_pos in mi. Also, flushes the master.info file.
 * @retval 0 Success
 * @retval 1 Some failure
 */
int update_rli_and_mi(
    const std::string &gtid_s,
    const std::pair<const std::string, unsigned long long> &master_log_pos);

/*
 * An enum to control what kind of registrations the
 * plugin needs from server.
 * Currently only 2 exist.
 * RAFT_REGISTER_LOCKS - BinlogWrapper related
 * RAFT_REGISTER_PATHS - paths and ports for initial raft setup
 */
enum Raft_Registration_Item {
  RAFT_REGISTER_LOCKS = 0,
  RAFT_REGISTER_PATHS = 1
};

/**
    Ask the mysqld server to immediately register the binlog and relay
    log files.

    Eventually instead of setup_flush in both these observers we will have
    a Raft specific delegate and observer
    @param item whether to register locks and io_caches for binlog wrapper
           or register paths for initial setup
*/
int ask_server_to_register_with_raft(Raft_Registration_Item item);
#ifdef __cplusplus
}
#endif
