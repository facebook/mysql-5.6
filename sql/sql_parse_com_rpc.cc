#include "sql_parse_com_rpc.h"

#include "sql_acl.h"
#include "srv_session.h"

#ifndef EMBEDDED_LIBRARY

static const char* RpcRoleAttr = "rpc_role";
static const char* RpcDbAttr = "rpc_db";

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

void update_default_session_object(std::shared_ptr<Srv_session> srv_session,
                                  THD* conn_thd, bool used_default_srv_session)
{
  if (srv_session->session_state_changed())
  {
    // if the default object was reused for this session, reset it
    if (used_default_srv_session)
      conn_thd->set_default_srv_session(nullptr);
  }
  else
  {
    // store as default so we don't allocate for next not in session query
    if (!conn_thd->get_default_srv_session())
    {
      DBUG_PRINT("info", ("No session change, reuse object"));
      conn_thd->set_default_srv_session(srv_session);
    }
  }
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
    0  success
  @retval
    1  failure
*/
bool handle_com_rpc(THD *conn_thd, char* packet, uint packet_length,
                    bool* is_rpc_query)
{
  DBUG_ENTER(__func__);
  std::string rpc_role, rpc_db, rpc_id;
  bool used_default_srv_session = false;
  bool ret = true;
  Security_context* conn_security_ctx = NULL;
  THD* srv_session_thd = NULL;

  check_for_attribute(conn_thd, RpcRoleAttr, rpc_role);
  check_for_attribute(conn_thd, RpcDbAttr, rpc_db);
  check_for_attribute(conn_thd, Srv_session::RpcIdAttr, rpc_id);

  if (!rpc_role.size() && !rpc_id.size())
  {
    // not an rpc command;
    DBUG_PRINT("info", ("Not an RPC query"));
    *is_rpc_query = false;
    DBUG_RETURN(true);
  }

  *is_rpc_query = true;

  DBUG_PRINT("info", ("rpc_role='%s', rpc_db='%s', rpc_id='%s'",
                      rpc_role.c_str(), rpc_db.c_str(), rpc_id.c_str()));

  std::shared_ptr<Srv_session> srv_session;

  // TODO send a specific error code/msg for all exit on error

  if (rpc_id.size())
  {
    srv_session = Srv_session::find_session(rpc_id);
    if (!srv_session)
    {
      DBUG_PRINT("info", ("Didn't find srv_session, rpc_id='%s'",
                          rpc_id.c_str()));
      goto done;
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
      DBUG_PRINT("info",
          ("Found session's host/ip (%s) does not match current session (%s)",
           srv_session->get_host_or_ip().c_str(),
           conn_thd->main_security_ctx.host_or_ip
          ));
      goto done;
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

      if (srv_session->open())
      {
        DBUG_PRINT("error", ("Failed to open srv session"));
        goto done;
      }
      // enable state change tracking
      srv_session->get_thd()->session_tracker.get_tracker(
                SESSION_STATE_CHANGE_TRACKER)->force_enable();
    }
    else
    {
      // set to know if to reset if after query execution state changes
      used_default_srv_session = true;
    }

    srv_session->set_host_or_ip(conn_thd->main_security_ctx.host_or_ip);

    // update user to the one in rpc attributes
    conn_security_ctx = conn_thd->security_context();

    // Make sure the current user has permission to proxy for the requested
    // user
    if (!acl_validate_proxy_user(
            conn_security_ctx->user, conn_security_ctx->get_host()->c_ptr(),
            conn_security_ctx->get_ip()->c_ptr(),
            rpc_role.c_str()))
    {
      DBUG_PRINT("error", ("Current user does not have permissions to run "
                           "queries as '%s'", rpc_role.c_str()));
      goto done;
    }

    if (srv_session->switch_to_user(rpc_role.c_str(),
          conn_security_ctx->get_host()->c_ptr(),
          conn_security_ctx->get_ip()->c_ptr(), rpc_db.c_str()))
    {
      DBUG_PRINT("error", ("Switching db user failed"));
      goto done;
    }

    // update db
    if (srv_session->get_thd()->set_db(rpc_db.c_str(), rpc_db.size()))
    {
      DBUG_PRINT("error", ("Setting db failed"));
      goto done;
    }
  }

  if (srv_session->attach())
  {
    DBUG_PRINT("error", ("Failed to attach srv session"));
    goto done;
  }

  if (rpc_id.empty()) { // if is new session
    // Session needs to be stored in session map for "show srv_sessions"
    if (Srv_session::store_session(srv_session)) {
      goto done;
    }
  }

  srv_session_thd = srv_session->get_thd();

  DBUG_PRINT("info", ("rpc_thread_id=%d  to attach conn_thread_id=%d",
                      srv_session_thd->thread_id(), conn_thd->thread_id()));

  srv_session->set_conn_thd_id(conn_thd->thread_id());

  // we need srv_session to use connection THD for network operations
  srv_session_thd->protocol = conn_thd->protocol;

  srv_session_thd->net.vio = conn_thd->net.vio;
  srv_session_thd->set_stmt_da(conn_thd->get_stmt_da());
  srv_session->set_session_tracker(&conn_thd->session_tracker);

  // set srv_session THD, used by "show processlist"
  conn_thd->set_attached_srv_session(srv_session);

  DBUG_PRINT("info", ("handle_com_rpc thread_thd=%p session_thd=%p "
                      "query='%.*s' query_len=%d", conn_thd, srv_session_thd,
                      packet_length, packet, packet_length));

  ret = srv_session->execute_query(packet, packet_length, 0);

  if (!ret)  // if query execution success
  {
    update_default_session_object(srv_session,
                                  conn_thd, used_default_srv_session);
  }
  else
  {
    // TODO error handling
    // remove session from map if conn THD gets destroyed
  }

  // reset the srv session thd
  conn_thd->set_attached_srv_session(NULL);

  // detach
  srv_session->detach();
  // from this point on other threads can access the session

  // Install back connection THD object as current_thd
  conn_thd->store_globals();

done:
  reset_conn_thd_after_query_execution(conn_thd);
  DBUG_RETURN(ret);
}

void srv_session_end_statement(Srv_session* session) {
  session->end_statement();
}

#else // #ifdef EMBEDDED_LIBRARY

void srv_session_end_statement(Srv_session* session) {}

#endif // #ifndef EMBEDDED_LIBRARY


