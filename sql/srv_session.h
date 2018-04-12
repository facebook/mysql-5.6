/*  Copyright (c) 2015, 2016 Oracle and/or its affiliates. All rights reserved.

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; version 2 of the
    License.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA */

#ifndef SRV_SESSION_H
#define SRV_SESSION_H

#include <condition_variable>
#include <memory>
#include <mutex>
#include "hh_wheel_timer.h"
#include "sql_class.h"
#include "violite.h"             /* enum_vio_type */

/**
  @file
  Header file for the Srv_session class that wraps THD, DA in one bundle
  for easy use of internal APIs.
  Srv_session also provides means for physical thread initialization and
  respective deinitialization.
*/

#ifdef HAVE_PSI_STATEMENT_INTERFACE
extern PSI_statement_info stmt_info_new_packet;
#endif

extern ulong thd_get_net_wait_timeout(const THD* thd);

class Srv_session : public HHWheelTimer::Callback,
                    public std::enable_shared_from_this<Srv_session>
{
public:

  ~Srv_session() { close(); }

  static constexpr auto RpcIdAttr = "rpc_id";
  /**
    Initializes the module.


    This method has to be called at server startup.

    @return
     false  success
     true   failure
  */
  static bool module_init();

  /**
    Deinitializes the module


    This method has to be called at server shutdown.

    @return
      false  success
      true   failure
  */
  static bool module_deinit();

  static my_thread_id parse_session_key(const std::string& key);
  static std::shared_ptr<Srv_session> access_session(my_thread_id session_id);
  static void remove_session(my_thread_id session_id);
  static void remove_session_if_ids_match(const Srv_session& session,
                                          HHWheelTimer::ID id);
  static bool store_session(std::shared_ptr<Srv_session> session);

  static std::vector<std::shared_ptr<Srv_session>> get_sorted_sessions();

  static std::pair<bool, std::chrono::milliseconds> session_timed_out(
      my_thread_id session_id);

  /* Non-static members follow */

  /**
    Enum for the state of the session

    Valid Session State switches:

    CREATED  -> ATTACHED : first session attach
    ATTACHED -> TO_BE_DETACHED : for state changed, before sending out OK/ERR
    TO_BE_DETACHED -> DETACHED : when session detached from conn thd
    ATTACHED -> DETACHED : for a session without state changed,
                           when session detached from conn thd
    DETACHED -> ATTACHED : attach for executing query
    ANY STATE -> CLOSED : in case of error or session finished(no state change)

    While session is in TO_BE_DETACHED state, it is possible that the next
    in session query is received on a different conn thd. If the conn thd finds
    the session in TO_BE_DETACHED, it will wait for max 100ms for the session
    to be released by the previous conn thd it was attached to and switch to
    SRV_SESSION_DETACHED.
    For waiting, wait_to_attach_ condition variable is used.
  */
  enum srv_session_state
  {
    SRV_SESSION_CREATED,
    SRV_SESSION_ATTACHED,
    SRV_SESSION_TO_BE_DETACHED,
    SRV_SESSION_DETACHED,
    SRV_SESSION_CLOSED
  };

  /**
    Constructs a server session
  */
  Srv_session();

  /**
    Opens a server session

    @param[in]  the connection THD to get defaults from

    @return
      session  on success
      NULL     on failure
  */
  bool open(const THD* conn_thd);

  /**
    Attaches the session to the current physical thread

    @returns
      false   success
      true    failure
  */
  bool attach();

  /**
    Detaches the session from current physical thread.

    @returns
      false success
      true  failure
  */
  bool detach();

  /**
    Closes the session

    @returns
      false Session successfully closed
      true  Session wasn't found or key doesn't match
  */
  bool close();

  /**
    Returns the internal THD object
  */
  inline THD* get_thd() { return &thd_; }
  inline const THD* get_thd() const { return &thd_; }

  /**
    Returns the ID of a session.

    The value returned from THD::thread_id()
  */
  my_thread_id get_session_id() const { return thd_.thread_id(); }

  /**
    Returns the client port.

    @note The client port in SHOW PROCESSLIST, INFORMATION_SCHEMA.PROCESSLIST.
    This port is NOT shown in PERFORMANCE_SCHEMA.THREADS.
  */
  uint16_t get_client_port() const { return thd_.peer_port; }

  bool switch_to_user(
    const char *username,
    const char *hostname,
    const char *address,
    const char *db);

  void set_conn_thd_id(my_thread_id id) { conn_thd_id.store(id); }

  uint32_t get_conn_thd_id() { return conn_thd_id.load(); }

  /*
   * Function called before sending out the Ok/Err.
   * It will remove session from map if there was no session state changed or
   * leave in map and change state to SRV_SESSION_TO_BE_DETACHED otherwise.
   * */
  void end_statement();

  my_bool session_state_changed() {
    return thd_.session_tracker.get_tracker(     // session state changed
                SESSION_STATE_CHANGE_TRACKER)->is_changed(&thd_) ||
          (thd_.server_status & SERVER_STATUS_IN_TRANS) || // in transaction
          (thd_.ull != NULL) ||                  // user lock set
          (thd_.locked_tables_mode != LTM_NONE); // LOCK table active
  }

  void set_host_or_ip(const char* str) { host_or_ip = str; }
  const std::string& get_host_or_ip() { return host_or_ip; }

  // Enable the wait timeout for this detached session
  void enableWaitTimeout() {
    auto timeout = std::chrono::seconds(thd_get_net_wait_timeout(get_thd()));
    callbackId_ = hhWheelTimer->scheduleTimeout(shared_from_this(),
        std::chrono::duration_cast<std::chrono::milliseconds>(timeout));
  }

  // Disable the wait timeout
  void disableWaitTimeout() {
    hhWheelTimer->cancelTimeout(shared_from_this());
    callbackId_ = 0;
  }

private:
  void switch_state_safe(srv_session_state state);

  void switch_state(srv_session_state state);

  srv_session_state get_state() {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
  }

  // Handle the timeout expired.
  void timeoutExpired(HHWheelTimer::ID id) noexcept override {
    // Remove this session from the list if the id we just got matches the id
    // stored in the session (which was set when the session was detached).
    // We want to do this under a lock for thread safety, so the check is
    // passed in as a predicate function.
    Srv_session::remove_session_if_ids_match(*this, id);
    // TODO (jkedgar) add this session's ID into a map so we can detect that
    // it timed out.
  }

  // We don't need to know that the timer was cancelled
  void timeoutCancelled(HHWheelTimer::ID /*id*/) noexcept override {}

  // ID of the last scheduled callback
  HHWheelTimer::ID callbackId_;

  /**
    Changes the state of a session to detached
  */
  void set_detached();

  bool wait_to_attach();

  // Session THD
  THD thd_;

  // Identification (hostname or ip) from the original client connection
  std::string host_or_ip;

  // Store the default vio and stmt_da fields to use when detaching the session
  Vio* default_vio_to_restore_ = NULL;

  // Connection THD ID.
  std::atomic_uint conn_thd_id;

  // Set to true once a session has been detached
  bool has_been_detached_ = false;

  // Used to provide exclusion: a session can be attached to only one
  // connection thread at one time.
  std::mutex mutex_;
  std::condition_variable wait_to_attach_;
  srv_session_state state_;
};

#endif /* SRV_SESSION_H */
