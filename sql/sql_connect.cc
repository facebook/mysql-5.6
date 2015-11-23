/*
   Copyright (c) 2007, 2013, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
*/

/*
  Functions to authenticate and handle requests for a connection
*/

#include "my_global.h"
#include "sql_priv.h"
#include "sql_base.h"
#include "sql_audit.h"
#include "sql_connect.h"
#include "my_global.h"
#include "probes_mysql.h"
#include "unireg.h"                    // REQUIRED: for other includes
#include "sql_parse.h"                          // sql_command_flags,
                                                // execute_init_command,
                                                // do_command
#include "sql_db.h"                             // mysql_change_db
#include "hostname.h" // inc_host_errors, ip_to_hostname,
                      // reset_host_errors
#include "sql_acl.h"  // acl_getroot, NO_ACCESS, SUPER_ACL
#include "sql_callback.h"
#include "sql_show.h" // schema_table_store_record
#include <algorithm>

using std::min;
using std::max;

#if defined(HAVE_OPENSSL) && !defined(EMBEDDED_LIBRARY)
/*
  Without SSL the handshake consists of one packet. This packet
  has both client capabilites and scrambled password.
  With SSL the handshake might consist of two packets. If the first
  packet (client capabilities) has CLIENT_SSL flag set, we have to
  switch to SSL and read the second packet. The scrambled password
  is in the second packet and client_capabilites field will be ignored.
  Maybe it is better to accept flags other than CLIENT_SSL from the
  second packet?
*/
#define SSL_HANDSHAKE_SIZE      2
#define NORMAL_HANDSHAKE_SIZE   6
#define MIN_HANDSHAKE_SIZE      2
#else
#define MIN_HANDSHAKE_SIZE      6
#endif /* HAVE_OPENSSL && !EMBEDDED_LIBRARY */

/*
  Get structure for logging connection data for the current user
*/

#ifndef NO_EMBEDDED_ACCESS_CHECKS
static HASH hash_user_connections;

/** Undo the work done by get_or_create_user_conn and increment the failed
    connection counters.
*/
void fix_user_conn(THD *thd, bool global_max)
{
  USER_STATS *us = thd_get_user_stats(thd);
  DBUG_ASSERT(us->magic == USER_STATS_MAGIC);

  mysql_mutex_lock(&LOCK_user_conn);
  thd->decrement_user_connections_counter();
  us->connections_total.dec();
  if (thd->net.vio->type == VIO_TYPE_SSL) {
    us->connections_ssl_total.dec();
  }

  if (global_max)
    us->connections_denied_max_global.inc();
  else
    us->connections_denied_max_user.inc();

  thd->set_user_connect(NULL);
  mysql_mutex_unlock(&LOCK_user_conn);
}

int get_or_create_user_conn(THD *thd, const char *user,
                            const char *host,
                            const USER_RESOURCES *mqh)
{
  int return_val= 0;
  size_t temp_len, user_len;
  char temp_user[USER_HOST_BUFF_SIZE];
  struct  user_conn *uc;

  DBUG_ASSERT(user != 0);
  DBUG_ASSERT(host != 0);

  user_len= strlen(user);
  temp_len= (strmov(strmov(temp_user, user)+1, host) - temp_user)+1;
  mysql_mutex_lock(&LOCK_user_conn);
  if (!(uc = (struct  user_conn *) my_hash_search(&hash_user_connections,
                 (uchar*) temp_user, temp_len)))
  {
    /* First connection for user; Create a user connection object */
    if (!(uc= ((struct user_conn*)
         my_malloc(sizeof(struct user_conn) + temp_len+1,
       MYF(MY_WME)))))
    {
      /* MY_WME ensures an error is set in THD. */
      return_val= 1;
      goto end;
    }
    uc->user=(char*) (uc+1);
    memcpy(uc->user,temp_user,temp_len+1);
    uc->host= uc->user + user_len +  1;
    uc->len= temp_len;
    uc->connections= uc->questions= uc->updates= uc->conn_per_hour= 0;
    uc->user_resources= *mqh;
    uc->reset_utime= thd->thr_create_utime;
    if (my_hash_insert(&hash_user_connections, (uchar*) uc))
    {
      /* The only possible error is out of memory, MY_WME sets an error. */
      my_free(uc);
      return_val= 1;
      goto end;
    }
    init_user_stats(&(uc->user_stats));
  }
  thd->set_user_connect(uc);
  thd->increment_user_connections_counter();
  uc->user_stats.connections_total.inc();
  if (thd->net.vio->type == VIO_TYPE_SSL) {
    uc->user_stats.connections_ssl_total.inc();
  }
end:
  mysql_mutex_unlock(&LOCK_user_conn);
  return return_val;

}


/*
  check if user has already too many connections

  SYNOPSIS
  check_for_max_user_connections()
  thd     Thread handle
  uc      User connect object

  NOTES
    If check fails, we decrease user connection count, which means one
    shouldn't call decrease_user_connections() after this function.

  RETURN
    0 ok
    1 error
*/

int check_for_max_user_connections(THD *thd, USER_CONN *uc, bool *global_max)
{
  int error=0;
  Host_errors errors;
  DBUG_ENTER("check_for_max_user_connections");

  *global_max= false;

  mysql_mutex_lock(&LOCK_user_conn);
  if (global_system_variables.max_user_connections &&
      !uc->user_resources.user_conn &&
      global_system_variables.max_user_connections < (uint) uc->connections &&
      !thd->is_admin_connection())
  {
    my_error(ER_TOO_MANY_USER_CONNECTIONS, MYF(0), uc->user);
    *global_max = true;
    error=1;
    errors.m_max_user_connection= 1;
    goto end;
  }
  thd->time_out_user_resource_limits();
  if (uc->user_resources.user_conn &&
      uc->user_resources.user_conn < uc->connections)
  {
    my_error(ER_USER_LIMIT_REACHED, MYF(0), uc->user,
             "max_user_connections",
             (long) uc->user_resources.user_conn);
    error= 1;
    errors.m_max_user_connection= 1;
    goto end;
  }
  if (uc->user_resources.conn_per_hour &&
      uc->user_resources.conn_per_hour <= uc->conn_per_hour)
  {
    my_error(ER_USER_LIMIT_REACHED, MYF(0), uc->user,
             "max_connections_per_hour",
             (long) uc->user_resources.conn_per_hour);
    error=1;
    errors.m_max_user_connection_per_hour= 1;
    goto end;
  }
  thd->increment_con_per_hour_counter();

end:
  mysql_mutex_unlock(&LOCK_user_conn);
  if (error)
  {
    inc_host_errors(thd->main_security_ctx.get_ip()->ptr(), &errors);
  }
  DBUG_RETURN(error);
}


/*
  Decrease user connection count

  SYNOPSIS
    decrease_user_connections()
    uc      User connection object

  NOTES
    If there is a n user connection object for a connection
    (which only happens if 'max_user_connections' is defined or
    if someone has created a resource grant for a user), then
    the connection count is always incremented on connect.

    The user connect object is not freed if some users has
    'max connections per hour' defined as we need to be able to hold
    count over the lifetime of the connection.
*/

void decrease_user_connections(USER_CONN *uc)
{
  DBUG_ENTER("decrease_user_connections");
  mysql_mutex_lock(&LOCK_user_conn);
  DBUG_ASSERT(uc->connections);
  uc->connections--;
  /* To preserve data in uc->user_stats, delete is no longer done */
  mysql_mutex_unlock(&LOCK_user_conn);
  DBUG_VOID_RETURN;
}

/*
   Decrements user connections count from the USER_CONN held by THD
   And removes USER_CONN from the hash if no body else is using it.

   SYNOPSIS
     release_user_connection()
     THD  Thread context object.
 */
void release_user_connection(THD *thd)
{
  const USER_CONN *uc= thd->get_user_connect();
  DBUG_ENTER("release_user_connection");

  if (uc)
  {
    mysql_mutex_lock(&LOCK_user_conn);
    DBUG_ASSERT(uc->connections > 0);
    thd->decrement_user_connections_counter();
    /* To preserve data in uc->user_stats, delete is no longer done */
    mysql_mutex_unlock(&LOCK_user_conn);
    thd->set_user_connect(NULL);
  }

  DBUG_VOID_RETURN;
}



/*
  Check if maximum queries per hour limit has been reached
  returns 0 if OK.
*/

bool check_mqh(THD *thd, uint check_command)
{
  bool error= 0;
  const USER_CONN *uc=thd->get_user_connect();
  DBUG_ENTER("check_mqh");
  DBUG_ASSERT(uc != 0);

  mysql_mutex_lock(&LOCK_user_conn);

  thd->time_out_user_resource_limits();

  /* Check that we have not done too many questions / hour */
  if (uc->user_resources.questions)
  {
    thd->increment_questions_counter();
    if ((uc->questions - 1) >= uc->user_resources.questions)
    {
      my_error(ER_USER_LIMIT_REACHED, MYF(0), uc->user, "max_questions",
               (long) uc->user_resources.questions);
      error=1;
      goto end;
    }
  }
  if (check_command < (uint) SQLCOM_END)
  {
    /* Check that we have not done too many updates / hour */
    if (uc->user_resources.updates &&
        (sql_command_flags[check_command] & CF_CHANGES_DATA))
    {
      thd->increment_updates_counter();
      if ((uc->updates - 1) >= uc->user_resources.updates)
      {
        my_error(ER_USER_LIMIT_REACHED, MYF(0), uc->user, "max_updates",
                 (long) uc->user_resources.updates);
        error=1;
        goto end;
      }
    }
  }
end:
  mysql_mutex_unlock(&LOCK_user_conn);
  DBUG_RETURN(error);
}
#else

int check_for_max_user_connections(THD *thd, const USER_CONN *uc)
{
  return 0;
}

void decrease_user_connections(USER_CONN *uc)
{
  return;
}

void release_user_connection(THD *thd)
{
  const USER_CONN *uc= thd->get_user_connect();
  DBUG_ENTER("release_user_connection");

  if (uc)
  {
    thd->set_user_connect(NULL);
  }

  DBUG_VOID_RETURN;
}

#endif /* NO_EMBEDDED_ACCESS_CHECKS */

/*
  Check for maximum allowable user connections, if the mysqld server is
  started with corresponding variable that is greater then 0.
*/

extern "C" uchar *get_key_conn(user_conn *buff, size_t *length,
            my_bool not_used __attribute__((unused)))
{
  *length= buff->len;
  return (uchar*) buff->user;
}


extern "C" void free_user(struct user_conn *uc)
{
  my_free(uc);
}


void init_max_user_conn(void)
{
#ifndef NO_EMBEDDED_ACCESS_CHECKS
  (void)
    my_hash_init(&hash_user_connections,system_charset_info,max_connections,
                 0,0, (my_hash_get_key) get_key_conn,
                 (my_hash_free_key) free_user, 0);
#endif
}


void free_max_user_conn(void)
{
#ifndef NO_EMBEDDED_ACCESS_CHECKS
  my_hash_free(&hash_user_connections);
#endif /* NO_EMBEDDED_ACCESS_CHECKS */
}


void reset_mqh(LEX_USER *lu, bool get_them= 0)
{
#ifndef NO_EMBEDDED_ACCESS_CHECKS
  mysql_mutex_lock(&LOCK_user_conn);
  if (lu)  // for GRANT
  {
    USER_CONN *uc;
    uint temp_len=lu->user.length+lu->host.length+2;
    char temp_user[USER_HOST_BUFF_SIZE];

    memcpy(temp_user,lu->user.str,lu->user.length);
    memcpy(temp_user+lu->user.length+1,lu->host.str,lu->host.length);
    temp_user[lu->user.length]='\0'; temp_user[temp_len-1]=0;
    if ((uc = (struct  user_conn *) my_hash_search(&hash_user_connections,
                                                   (uchar*) temp_user,
                                                   temp_len)))
    {
      uc->questions=0;
      get_mqh(temp_user,&temp_user[lu->user.length+1],uc);
      uc->updates=0;
      uc->conn_per_hour=0;
    }
  }
  else
  {
    /* for FLUSH PRIVILEGES and FLUSH USER_RESOURCES */
    for (uint idx=0;idx < hash_user_connections.records; idx++)
    {
      USER_CONN *uc=(struct user_conn *)
        my_hash_element(&hash_user_connections, idx);
      if (get_them)
  get_mqh(uc->user,uc->host,uc);
      uc->questions=0;
      uc->updates=0;
      uc->conn_per_hour=0;
    }
  }
  mysql_mutex_unlock(&LOCK_user_conn);
#endif /* NO_EMBEDDED_ACCESS_CHECKS */
}


/**
  Set thread character set variables from the given ID

  @param  thd         thread handle
  @param  cs_number   character set and collation ID

  @retval  0  OK; character_set_client, collation_connection and
              character_set_results are set to the new value,
              or to the default global values.

  @retval  1  error, e.g. the given ID is not supported by parser.
              Corresponding SQL error is sent.
*/

bool thd_init_client_charset(THD *thd, uint cs_number)
{
  CHARSET_INFO *cs;
  /*
   Use server character set and collation if
   - opt_character_set_client_handshake is not set
   - client has not specified a character set
   - client character set is the same as the servers
   - client character set doesn't exists in server
  */
  if (!opt_character_set_client_handshake ||
      !(cs= get_charset(cs_number, MYF(0))) ||
      !my_strcasecmp(&my_charset_latin1,
                     global_system_variables.character_set_client->name,
                     cs->name))
  {
    if (!is_supported_parser_charset(
      global_system_variables.character_set_client))
    {
      /* Disallow non-supported parser character sets: UCS2, UTF16, UTF32 */
      my_error(ER_WRONG_VALUE_FOR_VAR, MYF(0), "character_set_client",
               global_system_variables.character_set_client->csname);
      return true;
    }
    thd->variables.character_set_client=
      global_system_variables.character_set_client;
    thd->variables.collation_connection=
      global_system_variables.collation_connection;
    thd->variables.character_set_results=
      global_system_variables.character_set_results;
  }
  else
  {
    if (!is_supported_parser_charset(cs))
    {
      /* Disallow non-supported parser character sets: UCS2, UTF16, UTF32 */
      my_error(ER_WRONG_VALUE_FOR_VAR, MYF(0), "character_set_client",
               cs->csname);
      return true;
    }
    thd->variables.character_set_results=
      thd->variables.collation_connection=
      thd->variables.character_set_client= cs;
  }
  return false;
}


/*
  Initialize connection threads
*/

bool init_new_connection_handler_thread()
{
  pthread_detach_this_thread();
  if (my_thread_init())
  {
    statistic_increment(connection_errors_internal, &LOCK_status);
    return 1;
  }
  return 0;
}

#ifndef EMBEDDED_LIBRARY
/*
  Perform handshake, authorize client and update thd ACL variables.

  SYNOPSIS
    check_connection()
    thd  thread handle

  RETURN
     0  success, thd is updated.
     1  error
*/

static int check_connection(THD *thd)
{
  uint connect_errors= 0;
  int auth_rc;
  NET *net= &thd->net;

  DBUG_PRINT("info",
             ("New connection received on %s", vio_description(net->vio)));
#ifdef SIGNAL_WITH_VIO_SHUTDOWN
  thd->set_active_vio(net->vio);
#endif

  if (!thd->main_security_ctx.get_host()->length())     // If TCP/IP connection
  {
    my_bool peer_rc;
    char ip[NI_MAXHOST];

    peer_rc= vio_peer_addr(net->vio, ip, &thd->peer_port, NI_MAXHOST);

    /*
    ===========================================================================
    DEBUG code only (begin)
    Simulate various output from vio_peer_addr().
    ===========================================================================
    */

    DBUG_EXECUTE_IF("vio_peer_addr_error",
                    {
                      peer_rc= 1;
                    }
                    );
    DBUG_EXECUTE_IF("vio_peer_addr_fake_ipv4",
                    {
                      struct sockaddr *sa= (sockaddr *) &net->vio->remote;
                      sa->sa_family= AF_INET;
                      struct in_addr *ip4= &((struct sockaddr_in *) sa)->sin_addr;
                      /* See RFC 5737, 192.0.2.0/24 is reserved. */
                      const char* fake= "192.0.2.4";
                      ip4->s_addr= inet_addr(fake);
                      strcpy(ip, fake);
                      peer_rc= 0;
                    }
                    );

#ifdef HAVE_IPV6
    DBUG_EXECUTE_IF("vio_peer_addr_fake_ipv6",
                    {
                      struct sockaddr_in6 *sa= (sockaddr_in6 *) &net->vio->remote;
                      sa->sin6_family= AF_INET6;
                      struct in6_addr *ip6= & sa->sin6_addr;
                      /* See RFC 3849, ipv6 2001:DB8::/32 is reserved. */
                      const char* fake= "2001:db8::6:6";
                      /* inet_pton(AF_INET6, fake, ip6); not available on Windows XP. */
                      ip6->s6_addr[ 0] = 0x20;
                      ip6->s6_addr[ 1] = 0x01;
                      ip6->s6_addr[ 2] = 0x0d;
                      ip6->s6_addr[ 3] = 0xb8;
                      ip6->s6_addr[ 4] = 0x00;
                      ip6->s6_addr[ 5] = 0x00;
                      ip6->s6_addr[ 6] = 0x00;
                      ip6->s6_addr[ 7] = 0x00;
                      ip6->s6_addr[ 8] = 0x00;
                      ip6->s6_addr[ 9] = 0x00;
                      ip6->s6_addr[10] = 0x00;
                      ip6->s6_addr[11] = 0x00;
                      ip6->s6_addr[12] = 0x00;
                      ip6->s6_addr[13] = 0x06;
                      ip6->s6_addr[14] = 0x00;
                      ip6->s6_addr[15] = 0x06;
                      strcpy(ip, fake);
                      peer_rc= 0;
                    }
                    );
#endif /* HAVE_IPV6 */

    /*
    ===========================================================================
    DEBUG code only (end)
    ===========================================================================
    */

    if (peer_rc)
    {
      /*
        Since we can not even get the peer IP address,
        there is nothing to show in the host_cache,
        so increment the global status variable for peer address errors.
      */
      statistic_increment(connection_errors_peer_addr, &LOCK_status);
      my_error(ER_BAD_HOST_ERROR, MYF(0));
      return 1;
    }
    thd->main_security_ctx.set_ip(my_strdup(ip, MYF(MY_WME)));
    if (!(thd->main_security_ctx.get_ip()->length()))
    {
      /*
        No error accounting per IP in host_cache,
        this is treated as a global server OOM error.
        TODO: remove the need for my_strdup.
      */
      statistic_increment(connection_errors_internal, &LOCK_status);
      return 1; /* The error is set by my_strdup(). */
    }
    thd->main_security_ctx.host_or_ip= thd->main_security_ctx.get_ip()->ptr();
    if (!(specialflag & SPECIAL_NO_RESOLVE))
    {
      int rc;
      char *host= (char *) thd->main_security_ctx.get_host()->ptr();

      rc= ip_to_hostname(&net->vio->remote,
                         thd->main_security_ctx.get_ip()->ptr(),
                         &host, &connect_errors);

      thd->main_security_ctx.set_host(host);
      /* Cut very long hostnames to avoid possible overflows */
      if (thd->main_security_ctx.get_host()->length())
      {
        if (thd->main_security_ctx.get_host()->ptr() != my_localhost)
          thd->main_security_ctx.set_host(thd->main_security_ctx.get_host()->ptr(),
                               min<size_t>(thd->main_security_ctx.get_host()->length(),
                               HOSTNAME_LENGTH));
        thd->main_security_ctx.host_or_ip=
                        thd->main_security_ctx.get_host()->ptr();
      }

      if (rc == RC_BLOCKED_HOST)
      {
        /* HOST_CACHE stats updated by ip_to_hostname(). */
        my_error(ER_HOST_IS_BLOCKED, MYF(0), thd->main_security_ctx.host_or_ip);
        return 1;
      }
    }
    DBUG_PRINT("info",("Host: %s  ip: %s",
           (thd->main_security_ctx.get_host()->length() ?
                 thd->main_security_ctx.get_host()->ptr() : "unknown host"),
           (thd->main_security_ctx.get_ip()->length() ?
                 thd->main_security_ctx.get_ip()->ptr() : "unknown ip")));
    if (acl_check_host(thd->main_security_ctx.get_host()->ptr(),
                       thd->main_security_ctx.get_ip()->ptr()))
    {
      /* HOST_CACHE stats updated by acl_check_host(). */
      my_error(ER_HOST_NOT_PRIVILEGED, MYF(0),
               thd->main_security_ctx.host_or_ip);
      return 1;
    }
  }
  else /* Hostname given means that the connection was on a socket */
  {
    DBUG_PRINT("info",("Host: %s", thd->main_security_ctx.get_host()->ptr()));
    thd->main_security_ctx.host_or_ip= thd->main_security_ctx.get_host()->ptr();
    thd->main_security_ctx.set_ip("");
    /* Reset sin_addr */
    memset(&net->vio->remote, 0, sizeof(net->vio->remote));
  }
  vio_keepalive(net->vio, TRUE);

  if (thd->packet.alloc(thd->variables.net_buffer_length))
  {
    /*
      Important note:
      net_buffer_length is a SESSION variable,
      so it may be tempting to account OOM conditions per IP in the HOST_CACHE,
      in case some clients are more demanding than others ...
      However, this session variable is *not* initialized with a per client
      value during the initial connection, it is initialized from the
      GLOBAL net_buffer_length variable from the server.
      Hence, there is no reason to account on OOM conditions per client IP,
      we count failures in the global server status instead.
    */
    statistic_increment(connection_errors_internal, &LOCK_status);
    return 1; /* The error is set by alloc(). */
  }

  auth_rc= acl_authenticate(thd, 0);
  if (auth_rc == 0 && connect_errors != 0)
  {
    /*
      A client connection from this IP was successful,
      after some previous failures.
      Reset the connection error counter.
    */
    reset_host_connect_errors(thd->main_security_ctx.get_ip()->ptr());
  }

  return auth_rc;
}


/*
  Setup thread to be used with the current thread

  SYNOPSIS
    bool setup_connection_thread_globals()
    thd    Thread/connection handler

  RETURN
    0   ok
    1   Error (out of memory)
        In this case we will close the connection and increment status
*/

bool setup_connection_thread_globals(THD *thd)
{
  if (thd->store_globals())
  {
    close_connection(thd, ER_OUT_OF_RESOURCES);
    statistic_increment(aborted_connects,&LOCK_status);
    MYSQL_CALLBACK(thread_scheduler, end_thread, (thd, 0));
    return 1;                                   // Error
  }
  return 0;
}


/*
  Autenticate user, with error reporting

  SYNOPSIS
   login_connection()
   thd        Thread handler

  NOTES
    Connection is not closed in case of errors

  RETURN
    0    ok
    1    error
*/


bool login_connection(THD *thd)
{
  NET *net= &thd->net;
  int error;
  DBUG_ENTER("login_connection");
  DBUG_PRINT("info", ("login_connection called by thread %lu",
                      thd->thread_id));

  /* Use "connect_timeout" value during connection phase */
  my_net_set_read_timeout(net, timeout_from_seconds(connect_timeout));
  my_net_set_write_timeout(net, timeout_from_seconds(connect_timeout));

  error= check_connection(thd);
  thd->protocol->end_statement();

  if (error)
  {           // Wrong permissions
#ifdef _WIN32
    if (vio_type(net->vio) == VIO_TYPE_NAMEDPIPE)
      my_sleep(1000);       /* must wait after eof() */
#endif
    statistic_increment(aborted_connects,&LOCK_status);
    DBUG_RETURN(1);
  }
  /* Connect completed, set read/write timeouts back to default */
  my_net_set_read_timeout(
    net, timeout_from_seconds(thd->variables.net_read_timeout_seconds));
  my_net_set_write_timeout(
    net, timeout_from_seconds(thd->variables.net_write_timeout_seconds));
  DBUG_RETURN(0);
}


/*
  Close an established connection

  NOTES
    This mainly updates status variables
*/

void end_connection(THD *thd)
{
  NET *net= &thd->net;
  plugin_thdvar_cleanup(thd);

  bool end_on_error= thd->killed || (net->error && net->vio != 0);
  USER_CONN *uc = const_cast<USER_CONN*>(thd->get_user_connect());
  if (uc)
  {
    DBUG_ASSERT(uc->user_stats.magic == USER_STATS_MAGIC);

    if (end_on_error)
    {
      uc->user_stats.connections_lost.inc();
    }
    /*
      The thread may returned back to the pool and assigned to a user
      that doesn't have a limit. Ensure the user is not using resources
      of someone else.
    */
    release_user_connection(thd);
  }

  if (end_on_error)
  {
    statistic_increment(aborted_threads,&LOCK_status);
  }

  if (net->error && net->vio != 0)
  {
    if (!thd->killed && log_warnings > 1)
    {
      Security_context *sctx= thd->security_ctx;

      sql_print_warning(ER(ER_NEW_ABORTING_CONNECTION),
                        thd->thread_id,(thd->db ? thd->db : "unconnected"),
                        sctx->user ? sctx->user : "unauthenticated",
                        sctx->host_or_ip,
                        (thd->get_stmt_da()->is_error() ?
                         thd->get_stmt_da()->message() :
                         ER(ER_UNKNOWN_ERROR)));
    }
  }
}


/*
  Initialize THD to handle queries
*/

void prepare_new_connection_state(THD* thd)
{
  Security_context *sctx= thd->security_ctx;

  if (thd->client_capabilities & CLIENT_COMPRESS)
    thd->net.compress=1;        // Use compression

  /*
    Much of this is duplicated in create_embedded_thd() for the
    embedded server library.
    TODO: refactor this to avoid code duplication there
  */
  thd->proc_info= 0;
  thd->set_command(COM_SLEEP);
  thd->set_time();
  thd->init_for_queries();

  if (opt_init_connect.length && !(sctx->master_access & SUPER_ACL))
  {
    execute_init_command(thd, &opt_init_connect, &LOCK_sys_init_connect);
    if (thd->is_error())
    {
      Host_errors errors;
      ulong packet_length;
      NET *net= &thd->net;

      sql_print_warning(ER(ER_NEW_ABORTING_CONNECTION),
                        thd->thread_id,
                        thd->db ? thd->db : "unconnected",
                        sctx->user ? sctx->user : "unauthenticated",
                        sctx->host_or_ip, "init_connect command failed");
      sql_print_warning("%s", thd->get_stmt_da()->message());

      thd->lex->current_select= 0;
      my_net_set_read_timeout(
        net, timeout_from_seconds(thd->variables.net_wait_timeout_seconds));
      thd->clear_error();
      net_new_transaction(net);
      packet_length= my_net_read(net);
      /*
        If my_net_read() failed, my_error() has been already called,
        and the main Diagnostics Area contains an error condition.
      */
      if (packet_length != packet_error)
        my_error(ER_NEW_ABORTING_CONNECTION, MYF(0),
                 thd->thread_id,
                 thd->db ? thd->db : "unconnected",
                 sctx->user ? sctx->user : "unauthenticated",
                 sctx->host_or_ip, "init_connect command failed");

      thd->server_status&= ~SERVER_STATUS_CLEAR_SET;
      thd->protocol->end_statement();
      thd->killed = THD::KILL_CONNECTION;
      errors.m_init_connect= 1;
      inc_host_errors(thd->main_security_ctx.get_ip()->ptr(), &errors);
      return;
    }

    thd->proc_info=0;
    thd->set_time();
    thd->init_for_queries();
  }
}


/*
  Thread handler for a connection

  SYNOPSIS
    handle_one_connection()
    arg   Connection object (THD)

  IMPLEMENTATION
    This function (normally) does the following:
    - Initialize thread
    - Initialize THD to be used with this thread
    - Authenticate user
    - Execute all queries sent on the connection
    - Take connection down
    - End thread  / Handle next connection using thread from thread cache
*/

pthread_handler_t handle_one_connection(void *arg)
{
  THD *thd= (THD*) arg;

  mysql_thread_set_psi_id(thd->thread_id);

  do_handle_one_connection(thd);
  return 0;
}

void thd_update_net_stats(THD *thd)
{
  NET *net= &thd->net;

  if (net->last_errno == 0) {
    return;
  }

  USER_STATS *us= thd_get_user_stats(thd);
  us->errors_net_total.inc();

  switch (net->last_errno) {
    case ER_NET_ERROR_ON_WRITE:
      statistic_increment(connection_errors_net_ER_NET_ERROR_ON_WRITE,
                          &LOCK_status);
      us->errors_net_ER_NET_ERROR_ON_WRITE.inc();
      break;
    case ER_NET_PACKETS_OUT_OF_ORDER:
      statistic_increment(connection_errors_net_ER_NET_PACKETS_OUT_OF_ORDER,
                          &LOCK_status);
      us->errors_net_ER_NET_PACKETS_OUT_OF_ORDER.inc();
      break;
    case ER_NET_PACKET_TOO_LARGE:
      statistic_increment(connection_errors_net_ER_NET_PACKET_TOO_LARGE,
                          &LOCK_status);
      us->errors_net_ER_NET_PACKET_TOO_LARGE.inc();
      break;
    case ER_NET_READ_ERROR:
      statistic_increment(connection_errors_net_ER_NET_READ_ERROR,
                          &LOCK_status);
      us->errors_net_ER_NET_READ_ERROR.inc();
      break;
    case ER_NET_READ_INTERRUPTED:
      statistic_increment(connection_errors_net_ER_NET_READ_INTERRUPTED,
                          &LOCK_status);
      us->errors_net_ER_NET_READ_INTERRUPTED.inc();
      break;
    case ER_NET_UNCOMPRESS_ERROR:
      statistic_increment(connection_errors_net_ER_NET_UNCOMPRESS_ERROR,
                          &LOCK_status);
      us->errors_net_ER_NET_UNCOMPRESS_ERROR.inc();
      break;
    case ER_NET_WRITE_INTERRUPTED:
      statistic_increment(connection_errors_net_ER_NET_WRITE_INTERRUPTED,
                          &LOCK_status);
      us->errors_net_ER_NET_WRITE_INTERRUPTED.inc();
      break;
  }
}

bool thd_prepare_connection(THD *thd)
{
  bool rc;
  lex_start(thd);
  rc= login_connection(thd);
  MYSQL_AUDIT_NOTIFY_CONNECTION_CONNECT(thd);
  if (rc)
    return rc;

  MYSQL_CONNECTION_START(thd->thread_id, &thd->security_ctx->priv_user[0],
                         (char *) thd->security_ctx->host_or_ip);

  prepare_new_connection_state(thd);
  return FALSE;
}

bool thd_is_connection_alive(THD *thd)
{
  NET *net= &thd->net;
  if (!net->error &&
      net->vio != 0 &&
      !(thd->killed == THD::KILL_CONNECTION))
    return TRUE;
  return FALSE;
}

void do_handle_one_connection(THD *thd_arg)
{
  ulonglong start_time, connection_create_time;
  ulong launch_time= 0;
  THD *thd= thd_arg;
  USER_STATS *us= thd_get_user_stats(thd);

  thd->thr_create_utime= my_micro_time();

  if (MYSQL_CALLBACK_ELSE(thread_scheduler, init_new_connection_thread, (), 0))
  {
    close_connection(thd, ER_OUT_OF_RESOURCES);
    statistic_increment(aborted_connects,&LOCK_status);
    MYSQL_CALLBACK(thread_scheduler, end_thread, (thd, 0));
    return;
  }

  /*
    If a thread was created to handle this connection:
    increment slow_launch_threads counter if it took more than
    slow_launch_time seconds to create the thread.
  */
  if (thd->prior_thr_create_utime)
  {
    launch_time= (ulong) (thd->thr_create_utime -
                                thd->prior_thr_create_utime);
    if (launch_time >= slow_launch_time*1000000L)
      statistic_increment(slow_launch_threads, &LOCK_status);
    thd->prior_thr_create_utime= 0;
  }

  /*
    handle_one_connection() is normally the only way a thread would
    start and would always be on the very high end of the stack ,
    therefore, the thread stack always starts at the address of the
    first local variable of handle_one_connection, which is thd. We
    need to know the start of the stack so that we could check for
    stack overruns.
  */
  thd->thread_stack= (char*) &thd;
  if (setup_connection_thread_globals(thd))
    return;

  for (;;)
  {
	bool rc;

    NET *net= &thd->net;
    mysql_socket_set_thread_owner(net->vio->mysql_socket);

    start_time = my_timer_now();
    rc= thd_prepare_connection(thd);
    connection_create_time = my_timer_since(start_time) +
                             microseconds_to_my_timer(launch_time);
    latency_histogram_increment(&us->histogram_connection_create,
                                connection_create_time, 1);
    if (rc)
      goto end_thread;

    while (thd_is_connection_alive(thd))
    {
      mysql_audit_release(thd);
      if (do_command(thd))
  break;
    }
    thd_update_net_stats(thd);
    end_connection(thd);

end_thread:
    static char t_name_connection[T_NAME_LEN] = {0};
    if (t_name_connection[0] == '\0')
    {
      my_pthread_strip_name(
          t_name_connection,
          sizeof(t_name_connection),
          MYSQLD_T_NAME_PREFIX, "handle_one_connection");
    }
    pthread_setname_np(thd->real_id, t_name_connection);
    close_connection(thd);
    if (MYSQL_CALLBACK_ELSE(thread_scheduler, end_thread, (thd, 1), 0))
      return;                                 // Probably no-threads

    /*
      If end_thread() returns, we are either running with
      thread-handler=no-threads or this thread has been schedule to
      handle the next connection.
    */
    thd= current_thd;
    thd->thread_stack= (char*) &thd;
  }
}

/* This is a BSD license and covers the changes to the end of the file */
/* Copyright (C) 2009 Google, Inc.
   Copyright (C) 2010 Facebook, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of Google nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY Google ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL Google BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

/** Resets user statistics.

    Returns 0 on success;
*/

void reset_global_user_stats()
{
  DBUG_ENTER("reset_global_user_stats");

#ifndef NO_EMBEDDED_ACCESS_CHECKS
  mysql_mutex_lock(&LOCK_user_conn);

  for (uint i = 0; i < hash_user_connections.records; ++i)
  {
    USER_CONN *uc = (USER_CONN*)my_hash_element(&hash_user_connections, i);

    init_user_stats(&(uc->user_stats));
  }
  init_user_stats(&slave_user_stats);
  init_user_stats(&other_user_stats);

  mysql_mutex_unlock(&LOCK_user_conn);
#endif
  DBUG_VOID_RETURN;
}

void init_user_stats(USER_STATS *user_stats)
{
  DBUG_ENTER("init_user_stats");

  my_io_perf_atomic_init(&(user_stats->io_perf_read));
  my_io_perf_atomic_init(&(user_stats->io_perf_read_blob));
  my_io_perf_atomic_init(&(user_stats->io_perf_read_primary));
  my_io_perf_atomic_init(&(user_stats->io_perf_read_secondary));

  user_stats->binlog_bytes_written.clear();
  user_stats->binlog_disk_reads.clear();
  user_stats->bytes_received.clear();
  user_stats->bytes_sent.clear();
  user_stats->commands_ddl.clear();
  user_stats->commands_delete.clear();
  user_stats->commands_handler.clear();
  user_stats->commands_insert.clear();
  user_stats->commands_other.clear();
  user_stats->commands_select.clear();
  user_stats->commands_transaction.clear();
  user_stats->commands_update.clear();
  user_stats->connections_denied_max_global.clear();
  user_stats->connections_denied_max_user.clear();
  user_stats->connections_lost.clear();
  user_stats->connections_total.clear();
  user_stats->connections_ssl_total.clear();
  user_stats->errors_access_denied.clear();
  user_stats->errors_net_total.clear();
  user_stats->errors_net_ER_NET_ERROR_ON_WRITE.clear();
  user_stats->errors_net_ER_NET_PACKETS_OUT_OF_ORDER.clear();
  user_stats->errors_net_ER_NET_PACKET_TOO_LARGE.clear();
  user_stats->errors_net_ER_NET_READ_ERROR.clear();
  user_stats->errors_net_ER_NET_READ_INTERRUPTED.clear();
  user_stats->errors_net_ER_NET_UNCOMPRESS_ERROR.clear();
  user_stats->errors_net_ER_NET_WRITE_INTERRUPTED.clear();
  user_stats->errors_total.clear();
  user_stats->microseconds_wall.clear();
  user_stats->microseconds_ddl.clear();
  user_stats->microseconds_delete.clear();
  user_stats->microseconds_handler.clear();
  user_stats->microseconds_insert.clear();
  user_stats->microseconds_other.clear();
  user_stats->microseconds_select.clear();
  user_stats->microseconds_transaction.clear();
  user_stats->microseconds_update.clear();
  user_stats->queries_empty.clear();
  user_stats->query_comment_bytes.clear();
  user_stats->relay_log_bytes_written.clear();
  user_stats->rows_deleted.clear();
  user_stats->rows_fetched.clear();
  user_stats->rows_inserted.clear();
  user_stats->rows_read.clear();
  user_stats->rows_updated.clear();
  user_stats->rows_index_first.clear();
  user_stats->rows_index_next.clear();
  user_stats->transactions_commit.clear();
  user_stats->transactions_rollback.clear();
  user_stats->n_gtid_unsafe_create_select.clear();
  user_stats->n_gtid_unsafe_create_drop_temporary_table_in_transaction.clear();
  user_stats->n_gtid_unsafe_non_transactional_table.clear();

  latency_histogram_init(&(user_stats->histogram_connection_create),
                         histogram_step_size_connection_create);
  latency_histogram_init(&(user_stats->histogram_update_command),
                         histogram_step_size_update_command);
  latency_histogram_init(&(user_stats->histogram_delete_command),
                         histogram_step_size_delete_command);
  latency_histogram_init(&(user_stats->histogram_insert_command),
                         histogram_step_size_insert_command);
  latency_histogram_init(&(user_stats->histogram_select_command),
                         histogram_step_size_select_command);
  latency_histogram_init(&(user_stats->histogram_ddl_command),
                         histogram_step_size_ddl_command);
  latency_histogram_init(&(user_stats->histogram_transaction_command),
                         histogram_step_size_transaction_command);
  latency_histogram_init(&(user_stats->histogram_handler_command),
                         histogram_step_size_handler_command);
  latency_histogram_init(&(user_stats->histogram_other_command),
                         histogram_step_size_other_command);

#ifndef DBUG_OFF
  user_stats->magic = USER_STATS_MAGIC;
#endif // !DBUG_OFF

  DBUG_VOID_RETURN;
}

void
update_user_stats_after_statement(USER_STATS *us,
                                  THD *thd,
                                  ulonglong wall_time,
                                  bool is_other_command,
                                  bool is_xid_event,
                                  my_io_perf_t *start_perf_read,
                                  my_io_perf_t *start_perf_read_blob,
                                  my_io_perf_t *start_perf_read_primary,
                                  my_io_perf_t *start_perf_read_secondary)
{
  my_io_perf_t diff_io_perf, diff_io_perf_blob;
  my_io_perf_t diff_io_perf_primary, diff_io_perf_secondary;
  ulonglong wall_microsecs= my_timer_to_microseconds(wall_time);

  us->microseconds_wall.inc(wall_microsecs);

  /* COM_QUERY is counted in mysql_execute_command */
  if (is_other_command)
  {
    us->commands_other.inc();
    us->microseconds_other.inc(wall_microsecs);
  }

  if (!is_xid_event)
  {
    us->query_comment_bytes.inc(thd->count_comment_bytes);

    us->rows_updated.inc(thd->rows_updated);
    us->rows_deleted.inc(thd->rows_deleted);
    us->rows_inserted.inc(thd->rows_inserted);
    us->rows_read.inc(thd->rows_read);

    us->rows_index_first.inc(thd->rows_index_first);
    us->rows_index_next.inc(thd->rows_index_next);

    my_io_perf_diff(&diff_io_perf, &thd->io_perf_read, start_perf_read);
    my_io_perf_diff(&diff_io_perf_blob, &thd->io_perf_read_blob,
                    start_perf_read_blob);
    my_io_perf_diff(&diff_io_perf_primary, &thd->io_perf_read_primary,
                    start_perf_read_primary);
    my_io_perf_diff(&diff_io_perf_secondary, &thd->io_perf_read_secondary,
                    start_perf_read_secondary);

    my_io_perf_sum_atomic_helper(&(us->io_perf_read), &diff_io_perf);
    my_io_perf_sum_atomic_helper(&(us->io_perf_read_blob), &diff_io_perf_blob);
    my_io_perf_sum_atomic_helper(&(us->io_perf_read_primary),
                                 &diff_io_perf_primary);
    my_io_perf_sum_atomic_helper(&(us->io_perf_read_secondary),
                                 &diff_io_perf_secondary);
  }
  else
  {
    us->commands_transaction.inc();
    us->microseconds_transaction.inc(wall_microsecs);
  }
}

static void
fill_user_latency_histograms(TABLE *table, const char* username,
                             const char* statement_type,
                             latency_histogram* histogram,
                             const char* histogram_step_size)
{
  int i, f= 0;

  table->field[f++]->store(username, strlen(username), system_charset_info);
  table->field[f++]->store(statement_type, strlen(statement_type),
                           system_charset_info);
  table->field[f++]->store(histogram_step_size, strlen(histogram_step_size),
                           system_charset_info);

  for (i = 0; i < NUMBER_OF_HISTOGRAM_BINS; ++i)
  {
    table->field[f++]->store(latency_histogram_get_count(histogram, i), TRUE);
  }
}

int fill_user_histograms(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_user_histograms");
  TABLE* table= tables->table;

#ifndef NO_EMBEDDED_ACCESS_CHECKS
  mysql_mutex_lock(&LOCK_user_conn);

  for (uint idx=0;idx < hash_user_connections.records; idx++)
  {
    USER_CONN *user_conn= (struct user_conn *)
      my_hash_element(&hash_user_connections, idx);
    USER_STATS *us = &(user_conn->user_stats);

    fill_user_latency_histograms(table, user_conn->user,"UPDATE",
                                 &us->histogram_update_command,
                                 histogram_step_size_update_command);
    if (schema_table_store_record(thd, table))
    {
      mysql_mutex_unlock(&LOCK_user_conn);
      DBUG_RETURN(-1);
    }

    fill_user_latency_histograms(table, user_conn->user,"DELETE",
                                 &us->histogram_delete_command,
                                 histogram_step_size_delete_command);
    if (schema_table_store_record(thd, table))
    {
      mysql_mutex_unlock(&LOCK_user_conn);
      DBUG_RETURN(-1);
    }

    fill_user_latency_histograms(table, user_conn->user,"INSERT",
                                 &us->histogram_insert_command,
                                 histogram_step_size_insert_command);
    if (schema_table_store_record(thd, table))
    {
      mysql_mutex_unlock(&LOCK_user_conn);
      DBUG_RETURN(-1);
    }

    fill_user_latency_histograms(table, user_conn->user,"SELECT",
                                 &us->histogram_select_command,
                                 histogram_step_size_select_command);
    if (schema_table_store_record(thd, table))
    {
      mysql_mutex_unlock(&LOCK_user_conn);
      DBUG_RETURN(-1);
    }

    fill_user_latency_histograms(table, user_conn->user,"DDL",
                                 &us->histogram_ddl_command,
                                 histogram_step_size_ddl_command);
    if (schema_table_store_record(thd, table))
    {
      mysql_mutex_unlock(&LOCK_user_conn);
      DBUG_RETURN(-1);
    }

    fill_user_latency_histograms(table, user_conn->user,"TRANSACTION",
                                 &us->histogram_transaction_command,
                                 histogram_step_size_transaction_command);
    if (schema_table_store_record(thd, table))
    {
      mysql_mutex_unlock(&LOCK_user_conn);
      DBUG_RETURN(-1);
    }

    fill_user_latency_histograms(table, user_conn->user,"HANDLER",
                                 &us->histogram_handler_command,
                                 histogram_step_size_handler_command);
    if (schema_table_store_record(thd, table))
    {
      mysql_mutex_unlock(&LOCK_user_conn);
      DBUG_RETURN(-1);
    }

    fill_user_latency_histograms(table, user_conn->user,"OTHER",
                                 &us->histogram_other_command,
                                 histogram_step_size_other_command);
    if (schema_table_store_record(thd, table))
    {
      mysql_mutex_unlock(&LOCK_user_conn);
      DBUG_RETURN(-1);
    }

    fill_user_latency_histograms(table, user_conn->user,"CONNECTION_CREATE",
                                 &us->histogram_connection_create,
                                 histogram_step_size_connection_create);
    if (schema_table_store_record(thd, table))
    {
      mysql_mutex_unlock(&LOCK_user_conn);
      DBUG_RETURN(-1);
    }
  }

  mysql_mutex_unlock(&LOCK_user_conn);
#endif /* NO_EMBEDDED_ACCESS_CHECKS */

  DBUG_RETURN(0);
}


static void
fill_one_user_stats(TABLE *table, USER_CONN *uc, USER_STATS* us,
                    const char* username, uint connections)
{
  DBUG_ENTER("fill_one_user_stats");
  int f= 0; /* field offset */

  restore_record(table, s->default_values);

  table->field[f++]->store(username, strlen(username), system_charset_info);

  table->field[f++]->store(us->binlog_bytes_written.load(), TRUE);
  table->field[f++]->store(us->binlog_disk_reads.load(), TRUE);
  table->field[f++]->store(us->bytes_received.load(), TRUE);
  table->field[f++]->store(us->bytes_sent.load(), TRUE);
  table->field[f++]->store(us->commands_ddl.load(), TRUE);
  table->field[f++]->store(us->commands_delete.load(), TRUE);
  table->field[f++]->store(us->commands_handler.load(), TRUE);
  table->field[f++]->store(us->commands_insert.load(), TRUE);
  table->field[f++]->store(us->commands_other.load(), TRUE);
  table->field[f++]->store(us->commands_select.load(), TRUE);
  table->field[f++]->store(us->commands_transaction.load(), TRUE);
  table->field[f++]->store(us->commands_update.load(), TRUE);
  /* concurrent connections for this user */
  table->field[f++]->store(connections, TRUE);
  table->field[f++]->store(us->connections_denied_max_global.load(), TRUE);
  table->field[f++]->store(us->connections_denied_max_user.load(), TRUE);
  table->field[f++]->store(us->connections_lost.load(), TRUE);
  table->field[f++]->store(us->connections_total.load(), TRUE);
  table->field[f++]->store(us->io_perf_read.bytes.load(), TRUE);
  table->field[f++]->store(us->io_perf_read.requests.load(), TRUE);
  table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             us->io_perf_read.svc_time.load()),
                           TRUE);
  table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             us->io_perf_read.wait_time.load()),
                           TRUE);
  table->field[f++]->store(us->io_perf_read_blob.bytes.load(), TRUE);
  table->field[f++]->store(us->io_perf_read_blob.requests.load(), TRUE);
  table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             us->io_perf_read_blob.svc_time.load()),
                           TRUE);
  table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             us->io_perf_read_blob.wait_time.load()),
                           TRUE);
  table->field[f++]->store(us->io_perf_read_primary.bytes.load(), TRUE);
  table->field[f++]->store(us->io_perf_read_primary.requests.load(), TRUE);
  table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             us->io_perf_read_primary.svc_time.load()), TRUE);
  table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             us->io_perf_read_primary.wait_time.load()),
                           TRUE);
  table->field[f++]->store(us->io_perf_read_secondary.bytes.load(), TRUE);
  table->field[f++]->store(us->io_perf_read_secondary.requests.load(), TRUE);
  table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             us->io_perf_read_secondary.svc_time.load()),
                           TRUE);
  table->field[f++]->store((ulonglong)my_timer_to_microseconds(
                             us->io_perf_read_secondary.wait_time.load()),
                           TRUE);
  table->field[f++]->store(us->errors_access_denied.load(), TRUE);
  table->field[f++]->store(us->errors_net_total.load(), TRUE);
  table->field[f++]->store(us->errors_net_ER_NET_ERROR_ON_WRITE.load(), TRUE);
  table->field[f++]->store(us->errors_net_ER_NET_PACKETS_OUT_OF_ORDER.load(),
                           TRUE);
  table->field[f++]->store(us->errors_net_ER_NET_PACKET_TOO_LARGE.load(), TRUE);
  table->field[f++]->store(us->errors_net_ER_NET_READ_ERROR.load(), TRUE);
  table->field[f++]->store(us->errors_net_ER_NET_READ_INTERRUPTED.load(), TRUE);
  table->field[f++]->store(us->errors_net_ER_NET_UNCOMPRESS_ERROR.load(), TRUE);
  table->field[f++]->store(us->errors_net_ER_NET_WRITE_INTERRUPTED.load(),
                           TRUE);
  table->field[f++]->store(us->errors_total.load(), TRUE);
  table->field[f++]->store(us->microseconds_wall.load(), TRUE);
  table->field[f++]->store(us->microseconds_ddl.load(), TRUE);
  table->field[f++]->store(us->microseconds_delete.load(), TRUE);
  table->field[f++]->store(us->microseconds_handler.load(), TRUE);
  table->field[f++]->store(us->microseconds_insert.load(), TRUE);
  table->field[f++]->store(us->microseconds_other.load(), TRUE);
  table->field[f++]->store(us->microseconds_select.load(), TRUE);
  table->field[f++]->store(us->microseconds_transaction.load(), TRUE);
  table->field[f++]->store(us->microseconds_update.load(), TRUE);
  table->field[f++]->store(us->queries_empty.load(), TRUE);
  table->field[f++]->store(us->query_comment_bytes.load(), TRUE);
  table->field[f++]->store(us->relay_log_bytes_written.load(), TRUE);
  table->field[f++]->store(us->rows_deleted.load(), TRUE);
  table->field[f++]->store(us->rows_fetched.load(), TRUE);
  table->field[f++]->store(us->rows_inserted.load(), TRUE);
  table->field[f++]->store(us->rows_read.load(), TRUE);
  table->field[f++]->store(us->rows_updated.load(), TRUE);
  table->field[f++]->store(us->rows_index_first.load(), TRUE);
  table->field[f++]->store(us->rows_index_next.load(), TRUE);
  table->field[f++]->store(us->transactions_commit.load(), TRUE);
  table->field[f++]->store(us->transactions_rollback.load(), TRUE);
  table->field[f++]->store(us->n_gtid_unsafe_create_select.load(), TRUE);
  table->field[f++]->store(
    us->n_gtid_unsafe_create_drop_temporary_table_in_transaction.load(), TRUE);
  table->field[f++]->store(us->n_gtid_unsafe_non_transactional_table.load(),
                           TRUE);
  table->field[f++]->store(us->connections_ssl_total.load(), TRUE);
  DBUG_VOID_RETURN;
}

int fill_user_stats(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_user_stats");
  TABLE* table= tables->table;

#ifndef NO_EMBEDDED_ACCESS_CHECKS
  mysql_mutex_lock(&LOCK_user_conn);

  for (uint idx=0;idx < hash_user_connections.records; idx++)
  {
    USER_CONN *user_conn= (struct user_conn *)
      my_hash_element(&hash_user_connections, idx);
    USER_STATS *us = &(user_conn->user_stats);

    fill_one_user_stats(table, user_conn, us, user_conn->user,
                        user_conn->connections);

    if (schema_table_store_record(thd, table))
    {
      mysql_mutex_unlock(&LOCK_user_conn);
      DBUG_RETURN(-1);
    }
  }

  fill_one_user_stats(table, NULL, &slave_user_stats, "sys:slave", 0);
  if (schema_table_store_record(thd, table))
  {
    mysql_mutex_unlock(&LOCK_user_conn);
    DBUG_RETURN(-1);
  }

  fill_one_user_stats(table, NULL, &other_user_stats, "sys:other", 0);
  if (schema_table_store_record(thd, table))
  {
    mysql_mutex_unlock(&LOCK_user_conn);
    DBUG_RETURN(-1);
  }

  mysql_mutex_unlock(&LOCK_user_conn);
#endif /* NO_EMBEDDED_ACCESS_CHECKS */

  DBUG_RETURN(0);
}

#endif /* EMBEDDED_LIBRARY */
