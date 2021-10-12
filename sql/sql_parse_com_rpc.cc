#include "sql_parse.h"
#include "sql_parse_com_rpc.h"

#include "sql_acl.h"
#include "srv_session.h"

#ifndef EMBEDDED_LIBRARY

/*
 * @retval
 *  false  success to find attribute
 * @retval
 *  true  failed to find attribute
 * */
static bool check_for_attribute(THD *thd, const char *attr,
                                std::string& value) {
  auto entity = thd->query_attrs_map.find(attr);
  if (entity != thd->query_attrs_map.end())
  {
    value = entity->second;
    return false;
  }

  return true;
}

static bool update_default_session_object(
    std::shared_ptr<Srv_session>& srv_session,
    THD* conn_thd,
    bool state_changed)
{
  // if we changed state and the default object was reused for this session,
  // reset it
  bool used_default_srv_session =
      srv_session == conn_thd->get_default_srv_session();

  if (state_changed && used_default_srv_session) {
    conn_thd->set_default_srv_session(nullptr);
    used_default_srv_session = false;
  }

  return used_default_srv_session;
}

void reset_conn_thd_after_query_execution(THD* thd) {
  thd->update_server_status();

  thd->reset_query();
  thd->reset_query_attrs();
  thd->set_command(COM_SLEEP);
  thd->proc_info= 0;
  thd->m_digest= NULL;

  if (thd->m_statement_psi)
  {
    MYSQL_END_STATEMENT(thd->m_statement_psi, thd->get_stmt_da());
    thd->m_statement_psi= NULL;
  }

  thd->get_stmt_da()->reset_message();
  thd->packet.shrink(thd->variables.net_buffer_length);	// Reclaim some memory
  free_root(thd->mem_root,MYF(MY_KEEP_PREALLOC));
}

/*
  @retval
    pair(0, nullptr)  success with no rpc_role or rpc_id specificed
  @retval
    pair(0, session)  success - the session is the one specified by the
                                rpc_role or rpc_id
  @retval
    pair(1, nullptr)  failure
*/
std::pair<bool, std::shared_ptr<Srv_session>>  handle_com_rpc(THD *conn_thd)
{
  DBUG_ENTER(__func__);
  std::string rpc_role, rpc_db, rpc_id;
  bool used_default_srv_session = false;
  Security_context* conn_security_ctx = NULL;
  THD* srv_session_thd = NULL;
  std::shared_ptr<Srv_session> srv_session;

  check_for_attribute(conn_thd, QATTR_RPC_ROLE, rpc_role);
  check_for_attribute(conn_thd, QATTR_RPC_DB, rpc_db);
  check_for_attribute(conn_thd, QATTR_RPC_ID, rpc_id);

  if (!rpc_role.size() && !rpc_id.size())
  {
    // not an rpc command;
    DBUG_PRINT("info", ("Not an RPC query"));
    DBUG_RETURN(std::make_pair(false, std::move(srv_session)));
  }

  DBUG_PRINT("info", ("rpc_role='%s', rpc_db='%s', rpc_id='%s'",
                      rpc_role.c_str(), rpc_db.c_str(), rpc_id.c_str()));

  // TODO send a specific error code/msg for all exit on error

  if (rpc_id.size())
  {
    auto session_id = Srv_session::parse_session_key(rpc_id);
    if (session_id == (my_thread_id) -1) {
      my_error(ER_RPC_MALFORMED_ID, MYF(0), rpc_id.c_str());
      goto error;
    }

    srv_session = Srv_session::access_session(session_id);
    if (!srv_session)
    {
      bool timed_out;
      std::chrono::milliseconds idle_timeout;
      std::tie(timed_out, idle_timeout) =
          Srv_session::session_timed_out(session_id);
      if (timed_out) {
        my_error(ER_RPC_IDLE_TIMEOUT, MYF(0), rpc_id.c_str(),
            (uint32_t) idle_timeout.count());
      } else {
        my_error(ER_RPC_INVALID_ID, MYF(0), rpc_id.c_str());
      }

      goto error;
    }

    // Check to make sure the current user is coming from the same machine as
    // the srv_session was originally created from
    std::string curr_host_or_ip = conn_thd->main_security_ctx.host_or_ip;

    DBUG_EXECUTE_IF("rpc_id_different_ip",
        {
          curr_host_or_ip = "other_host";
        });

    if (srv_session->get_host_or_ip() != curr_host_or_ip)
    {
      my_error(ER_RPC_HOST_MISMATCH, MYF(0), rpc_id.c_str());
      goto error;
    }

    DBUG_PRINT("info", ("Found session in map, rpc_id=%s", rpc_id.c_str()));
  }
  else
  {
    // Use default session from conn thd. We put it back at the end if execution
    // was successful and there was no session state change.
    srv_session = conn_thd->get_default_srv_session();
    if (!srv_session)
    {
      // default one not present, allocate a new session
      srv_session = std::shared_ptr<Srv_session>(new Srv_session);

      if (srv_session->open(conn_thd))
      {
        my_error(ER_RPC_SESSION_OPEN, MYF(0));
        goto error;
      }

      // enable state change tracking
      srv_session->get_thd()->session_tracker.get_tracker(
                SESSION_STATE_CHANGE_TRACKER)->force_enable();
      srv_session->get_thd()->session_tracker.get_tracker(
                SESSION_RESP_ATTR_TRACKER)->force_enable();
      conn_thd->set_default_srv_session(srv_session);
      // newly created sessions need the net_ptr set to null
      srv_session->get_thd()->clear_net();
    }
    else
    {
      // set to know if to reset if after query execution state changes
      used_default_srv_session = true;
    }

    srv_session_thd = srv_session->get_thd();
    srv_session->set_host_or_ip(conn_thd->main_security_ctx.host_or_ip);

    // update user to the one in rpc attributes
    conn_security_ctx = conn_thd->security_context();

    // Make sure the current user has permission to proxy for the requested
    // user
    if (strcmp(conn_security_ctx->user, rpc_role.c_str()) == 0) {
      // Attempting to proxy as ourselves - just copy the user_connect
      srv_session_thd->copy_user_connect(conn_thd);
    } else {
      // For the attached session the net_ptr should be null, but we need it
      // set to the connection's net_ptr for the acl_validate_proxy_user()
      // call, because inside it there is a call got get_or_create_user_conn()
      // which will attempt to increment a counter based on whether the
      // the connection is an SSL connection or not and the net_ptr has that
      // information.  Reset it after we call this as we may fail to use this
      // session and we don't want to leave it set until we know we will
      // succeed.  It will be set again outside this function by the caller.
      DBUG_ASSERT(srv_session_thd->get_net_nullable() == nullptr);
      srv_session_thd->set_net(conn_thd->get_net());

      auto res = acl_validate_proxy_user(
            srv_session_thd,
            conn_security_ctx->user, conn_security_ctx->get_host()->c_ptr(),
            conn_security_ctx->get_ip()->c_ptr(),
            rpc_role.c_str());
      // Clear the net_ptr again - see above comment
      srv_session_thd->clear_net();

      if (!res)
      {
        my_error(ER_RPC_NO_PERMISSION, MYF(0), conn_security_ctx->user,
            rpc_role.c_str());
        goto error;
      }
    }

    if (srv_session->switch_to_user(rpc_role.c_str(),
          conn_security_ctx->get_host()->c_ptr(),
          conn_security_ctx->get_ip()->c_ptr(), rpc_db.c_str()))
    {
      my_error(ER_RPC_FAILED_TO_SWITCH_USER, MYF(0), rpc_role.c_str());
      goto error;
    }

    // update db
    if (rpc_db.size() > 0 &&
        srv_session_thd->set_db(rpc_db.c_str(), rpc_db.size()))
    {
      my_error(ER_RPC_FAILED_TO_SWITCH_DB, MYF(0), rpc_db.c_str());
      goto error;
    }
    per_user_session_variables.set_thd(srv_session_thd);
  }

  if (srv_session->attach())
  {
    my_error(ER_RPC_FAILED_TO_ATTACH, MYF(0));
    goto error;
  }

  if (rpc_id.empty()) { // if is new session
    // Session needs to be stored in session map for "show srv_sessions"
    if (Srv_session::store_session(srv_session)) {
      my_error(ER_RPC_FAILED_TO_STORE_DETACHED_SESSION, MYF(0));
      goto error;
    }
  }

  // Not all code paths have this set yet
  srv_session_thd = srv_session->get_thd();

  DBUG_PRINT("info", ("rpc_thread_id=%d  to attach conn_thread_id=%d",
                      srv_session_thd->thread_id(), conn_thd->thread_id()));

  srv_session->set_conn_thd_id(conn_thd->thread_id());

  // we need srv_session to use connection THD for network operations
  srv_session_thd->protocol = conn_thd->protocol;
  conn_thd->protocol->setSessionTHD(srv_session_thd);

  srv_session_thd->set_stmt_da(conn_thd->get_stmt_da());
  srv_session_thd->set_query_attrs(conn_thd->query_attrs_map);

  // set srv_session THD, used by "show processlist"
  conn_thd->set_attached_srv_session(srv_session);

  DBUG_PRINT("info", ("handle_com_rpc thread_thd=%p session_thd=%p",
                      conn_thd, srv_session_thd));

  DBUG_RETURN(std::make_pair(false, std::move(srv_session)));

error:
  // If we have a srv_session and it is not attached as the default session
  // for a THD, enable idle timeouts on it.
  if (srv_session != nullptr && !used_default_srv_session) {
    srv_session->enableWaitTimeout();
  }

  srv_session = nullptr;

  DBUG_RETURN(std::make_pair(true, std::move(srv_session)));
}

// Do all the cleanup necessary for a query with RPC_* attributes
void cleanup_com_rpc(
    THD* conn_thd,
    std::shared_ptr<Srv_session> srv_session,
    bool state_changed) {
  DBUG_ENTER(__func__);

  // Check to see if we remove it from the default session because of
  // some state change
  bool used_default_srv_session = update_default_session_object(
      srv_session, conn_thd, state_changed);

  // reset the srv session thd
  conn_thd->set_attached_srv_session(nullptr);

  // detach
  srv_session->detach();
  // from this point on other threads can access the session

  // Install back connection THD object as current_thd
  conn_thd->store_globals();
  conn_thd->protocol->resetSessionTHD();

  // If we have a srv_session and it is not expiring at the end of this
  // function, enable idle timeouts on it.
  if (srv_session != nullptr && !used_default_srv_session) {
    srv_session->enableWaitTimeout();
  }

  reset_conn_thd_after_query_execution(conn_thd);
  DBUG_VOID_RETURN;
}

// Mark the statement as ended.
void srv_session_end_statement(Srv_session& session) {
  session.end_statement();
}

#else

void cleanup_com_rpc(
    THD* conn_thd,
    std::shared_ptr<Srv_session> srv_session,
    bool state_changed) {}
void srv_session_end_statement(Srv_session& session) {}

std::pair<bool, std::shared_ptr<Srv_session>> handle_com_rpc(THD *conn_thd) {
  return std::make_pair(false, nullptr);
}

#endif // #ifndef EMBEDDED_LIBRARY
