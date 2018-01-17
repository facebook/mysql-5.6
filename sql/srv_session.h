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
#include <mutex>
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


class Srv_session
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

  static std::shared_ptr<Srv_session> find_session(const std::string& key);
  static std::shared_ptr<Srv_session> find_session(my_thread_id session_id);
  static void remove_session(my_thread_id session_id);
  static bool store_session(std::shared_ptr<Srv_session> session);

  static std::vector<std::shared_ptr<Srv_session>> get_sorted_sessions();

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

    @return
      session  on success
      NULL     on failure
  */
  bool open();

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
    Executes a query.

    @param packet     Pointer to beginning of query in packet
    @param length     Query length
    @param client_cs  The charset for the string data input (COM_QUERY
                      for example)

    @returns
      1   error
      0   success
  */
  int execute_query(char* packet, uint packet_length,
                      const CHARSET_INFO * client_cs);

  /**
    Returns the internal THD object
  */
  inline THD* get_thd() { return &thd_; }

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

  void set_session_tracker(Session_tracker* tracker)
  {
    session_tracker_ = tracker;
  }

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

private:
  void switch_state_safe(srv_session_state state);

  void switch_state(srv_session_state state);

  srv_session_state get_state() {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
  }

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
  Diagnostics_area * default_stmt_to_restore_ = NULL;
  Session_tracker *session_tracker_ = NULL;

  // Connection THD ID.
  std::atomic_uint conn_thd_id;

  // Used to provide exclusion: a session can be attached to only one
  // connection thread at one time.
  std::mutex mutex_;
  std::condition_variable wait_to_attach_;
  srv_session_state state_;
};

#endif /* SRV_SESSION_H */
