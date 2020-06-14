// Copyright 2004-present Facebook. All Rights Reserved.

#pragma once

#include <future>
#include <map>
#include <vector>

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
  UNSET_READ_ONLY = 2,
  TRIM_LOGGED_GTIDS = 3,
  ROTATE_BINLOG = 4,
  ROTATE_RELAYLOG = 5,
  RAFT_LISTENER_THREADS_EXIT = 6,
  RLI_RELAY_LOG_RESET = 7,
  RESET_SLAVE = 8,
  BINLOG_CHANGE_TO_APPLY = 9,
  BINLOG_CHANGE_TO_BINLOG = 10,
  STOP_SQL_THREAD = 11,
  START_SQL_THREAD = 12,
  STOP_IO_THREAD = 13,
  CHANGE_MASTER = 14,
  GET_COMMITTED_GTIDS = 15,
  GET_EXECUTED_GTIDS = 16,
  SET_BINLOG_DURABILITY = 17,
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
