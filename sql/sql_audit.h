/* Copyright (c) 2007, 2016, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef SQL_AUDIT_INCLUDED
#define SQL_AUDIT_INCLUDED

#include <my_global.h>

#include <mysql/plugin_audit.h>
#include "sql_class.h"
#include "sql_rewrite.h"

extern unsigned long mysql_global_audit_mask[];


extern void mysql_audit_initialize();
extern void mysql_audit_finalize();


extern void mysql_audit_init_thd(THD *thd);
extern void mysql_audit_free_thd(THD *thd);
extern void mysql_audit_acquire_plugins(THD *thd, uint event_class);


#ifndef EMBEDDED_LIBRARY
extern void mysql_audit_notify(THD *thd, uint event_class,
                               uint event_subtype, ...);
bool is_any_audit_plugin_active(THD *thd MY_ATTRIBUTE((unused)));

static void get_query_attributes_from_thd(THD* thd, std::vector<const char*>& out) {
  mysql_mutex_lock(&thd->LOCK_thd_data);
  for (const auto& kp : thd->query_attrs_map) {
    out.push_back(kp.first.c_str());
    out.push_back(kp.second.c_str());
  }
  mysql_mutex_unlock(&thd->LOCK_thd_data);
  DBUG_ASSERT(out.size() % 2 == 0);
}

static void get_audit_attributes_from_thd(THD* thd, std::vector<const char*>& out) {
  Session_tracker* tracker = thd->get_tracker();
  if (tracker) {
    for (const auto& kp : tracker->get_audit_attrs()) {
      out.push_back(kp.first.c_str());
      out.push_back(kp.second.c_str());
    }
  }
  DBUG_ASSERT(out.size() % 2 == 0);
}
#else
#define mysql_audit_notify(...)
#endif
extern void mysql_audit_release(THD *thd);

#define MAX_USER_HOST_SIZE 512
static inline uint make_user_name(THD *thd, char *buf)
{
  Security_context *sctx= thd->security_ctx;
  return strxnmov(buf, MAX_USER_HOST_SIZE,
                  sctx->priv_user[0] ? sctx->priv_user : "", "[",
                  sctx->user ? sctx->user : "", "] @ ",
                  sctx->get_host()->length() ? sctx->get_host()->ptr() :
                  "", " [", sctx->get_ip()->length() ? sctx->get_ip()->ptr() :
                  "", "]", NullS) - buf;
}

/**
  Call audit plugins of GENERAL audit class, MYSQL_AUDIT_GENERAL_LOG subtype.
  
  @param[in] thd
  @param[in] cmd              Command name
  @param[in] cmdlen           Command name length
  @param[in] query_str        query text. Leave empty to fetch it from THD
  @param[in] query_len        query text length. 0 to fetch it from THD
  */
 
static inline
void mysql_audit_general_log(THD *thd, const char *cmd, uint cmdlen,
                             const char *query_str, size_t query_len)
{
#ifndef EMBEDDED_LIBRARY
  if (mysql_global_audit_mask[0] & MYSQL_AUDIT_GENERAL_CLASSMASK)
  {
    MYSQL_LEX_STRING sql_command, ip, host, external_user;
    MYSQL_LEX_STRING query={ (char *)query_str, query_len };
    static MYSQL_LEX_STRING empty= { C_STRING_WITH_LEN("") };
    ha_rows resultrows= 0;
    longlong affectrows= 0;
    int error_code= 0; 
    uint userlen, databaselen;
    const char *user, *database;
    time_t time= (time_t) thd->start_time.tv_sec;
    std::vector<const char*> query_attrs;
    std::vector<const char*> audit_attrs;

    if (thd)
    {
      user= thd->security_ctx->user;
      userlen= thd->security_ctx->user ? strlen(thd->security_ctx->user) : 0;
      if (!query_len)
      {
        /* no query specified, fetch from THD */
        if (!thd->rewritten_query.length())
          mysql_rewrite_query(thd);
        if (thd->rewritten_query.length())
        {
          query.str= (char *) thd->rewritten_query.ptr();
          query.length= thd->rewritten_query.length();
        }
        else
        {
          query.str= thd->query();
          query.length= thd->query_length();
        }
      }
      ip.str= (char *) thd->security_ctx->get_ip()->ptr();
      ip.length= thd->security_ctx->get_ip()->length();
      host.str= (char *) thd->security_ctx->get_host()->ptr();
      host.length= thd->security_ctx->get_host()->length();
      external_user.str= (char *) thd->security_ctx->get_external_user()->ptr();
      external_user.length= thd->security_ctx->get_external_user()->length();
      sql_command.str= (char *) sql_statement_names[thd->lex->sql_command].str;
      sql_command.length= sql_statement_names[thd->lex->sql_command].length;
      database= thd->db;
      databaselen= thd->db_length;
      get_query_attributes_from_thd(thd, query_attrs);
      get_audit_attributes_from_thd(thd, audit_attrs);
    }
    else
    {
      user= 0;
      userlen= 0;
      ip= empty;
      host= empty;
      external_user= empty;
      sql_command= empty;
      database= 0;
      databaselen= 0;
    }
    const CHARSET_INFO *clientcs= thd ? thd->variables.character_set_client
      : global_system_variables.character_set_client;

    mysql_audit_notify(thd, MYSQL_AUDIT_GENERAL_CLASS, MYSQL_AUDIT_GENERAL_LOG,
                       error_code, time, user, userlen, cmd, cmdlen, query.str,
                       query.length, clientcs, resultrows, affectrows,
                       sql_command, host, external_user, ip, database,
                       databaselen, query_attrs.data(), query_attrs.size(),
                       mysqld_port, audit_attrs.data(), audit_attrs.size());
  }
#endif
}


/**
  Call audit plugins of GENERAL audit class.
  event_subtype should be set to one of:
    MYSQL_AUDIT_GENERAL_ERROR
    MYSQL_AUDIT_GENERAL_RESULT
    MYSQL_AUDIT_GENERAL_STATUS
    MYSQL_AUDIT_GENERAL_WARNING
    MYSQL_AUDIT_GENERAL_ERROR_INSTR
  
  @param[in] thd
  @param[in] event_subtype    Type of general audit event.
  @param[in] error_code       Error code
  @param[in] msg              Message
*/
static inline
void mysql_audit_general(THD *thd, uint event_subtype,
                         int error_code, const char *msg)
{
#ifndef EMBEDDED_LIBRARY
  if (mysql_global_audit_mask[0] & MYSQL_AUDIT_GENERAL_CLASSMASK)
  {
    time_t time= my_time(0);
    uint msglen= msg ? strlen(msg) : 0;
    uint userlen, databaselen;
    const char *user, *database;
    CSET_STRING query;
    MYSQL_LEX_STRING ip, host, external_user, sql_command;
    // Result rows will hold the number of rows sent to the client.
    ha_rows resultrows;
    // Affected rows will hold the number of modified rows for DML statements.
    // For SELECT statements, it is -1. For other statements, it is 0.  See
    // THD::get_row_count_func for details.
    longlong affectrows;
    static MYSQL_LEX_STRING empty= { C_STRING_WITH_LEN("") };
    std::vector<const char*> query_attrs;
    std::vector<const char*> audit_attrs;

    if (thd)
    {
      if (!thd->rewritten_query.length())
        mysql_rewrite_query(thd);
      if (thd->rewritten_query.length())
        query= CSET_STRING((char *) thd->rewritten_query.ptr(),
                           thd->rewritten_query.length(),
                           thd->rewritten_query.charset());
      else
        query= thd->query_string;
      user= thd->security_ctx->user;
      userlen= thd->security_ctx->user ? strlen(thd->security_ctx->user) : 0;
      affectrows= thd->get_row_count_func();
      resultrows= thd->get_sent_row_count();
      ip.str= (char *) thd->security_ctx->get_ip()->ptr();
      ip.length= thd->security_ctx->get_ip()->length();
      host.str= (char *) thd->security_ctx->get_host()->ptr();
      host.length= thd->security_ctx->get_host()->length();
      external_user.str= (char *) thd->security_ctx->get_external_user()->ptr();
      external_user.length= thd->security_ctx->get_external_user()->length();
      sql_command.str= (char *) sql_statement_names[thd->lex->sql_command].str;
      sql_command.length= sql_statement_names[thd->lex->sql_command].length;
      database= thd->db;
      databaselen= thd->db_length;
      get_query_attributes_from_thd(thd, query_attrs);
      get_audit_attributes_from_thd(thd, audit_attrs);
    }
    else
    {
      user= 0;
      userlen= 0;
      ip= empty;
      host= empty;
      external_user= empty;
      sql_command= empty;
      resultrows= 0;
      affectrows= 0;
      database= 0;
      databaselen= 0;
    }

    mysql_audit_notify(thd, MYSQL_AUDIT_GENERAL_CLASS, event_subtype,
                       error_code, time, user, userlen, msg, msglen,
                       query.str(), query.length(), query.charset(),
                       resultrows, affectrows, sql_command, host,
                       external_user, ip, database, databaselen,
                       query_attrs.data(), query_attrs.size(), mysqld_port,
                       audit_attrs.data(), audit_attrs.size());
  }
#endif
}

#define MYSQL_AUDIT_NOTIFY_CONNECTION_CONNECT(thd) mysql_audit_notify(\
  (thd), MYSQL_AUDIT_CONNECTION_CLASS, MYSQL_AUDIT_CONNECTION_CONNECT,\
  (thd)->get_stmt_da()->is_error() ? (thd)->get_stmt_da()->sql_errno() : 0,\
  (thd)->thread_id(), (thd)->security_ctx->user,\
  (thd)->security_ctx->user ? strlen((thd)->security_ctx->user) : 0,\
  (thd)->security_ctx->priv_user, strlen((thd)->security_ctx->priv_user),\
  (thd)->security_ctx->get_external_user()->ptr(),\
  (thd)->security_ctx->get_external_user()->length(),\
  (thd)->security_ctx->proxy_user, strlen((thd)->security_ctx->proxy_user),\
  (thd)->security_ctx->get_host()->ptr(),\
  (thd)->security_ctx->get_host()->length(),\
  (thd)->security_ctx->get_ip()->ptr(),\
  (thd)->security_ctx->get_ip()->length(),\
  (thd)->db, (thd)->db ? strlen((thd)->db) : 0,\
  (thd)->connection_certificate(),\
  (thd)->connection_certificate_length(), mysqld_port);


#define MYSQL_AUDIT_NOTIFY_CONNECTION_DISCONNECT(thd, errcode)\
  mysql_audit_notify(\
  (thd), MYSQL_AUDIT_CONNECTION_CLASS, MYSQL_AUDIT_CONNECTION_DISCONNECT,\
  (errcode), (thd)->thread_id(),\
  (thd)->security_ctx->user,\
  (thd)->security_ctx->user ? strlen((thd)->security_ctx->user) : 0,\
  (thd)->security_ctx->priv_user, strlen((thd)->security_ctx->priv_user),\
  (thd)->security_ctx->get_external_user()->ptr(),\
  (thd)->security_ctx->get_external_user()->length(),\
  (thd)->security_ctx->proxy_user, strlen((thd)->security_ctx->proxy_user),\
  (thd)->security_ctx->get_host()->ptr(),\
  (thd)->security_ctx->get_host()->length(),\
  (thd)->security_ctx->get_ip()->ptr(),\
  (thd)->security_ctx->get_ip()->length(),\
  (thd)->db, (thd)->db ? strlen((thd)->db) : 0,\
  (thd)->connection_certificate(),\
  (thd)->connection_certificate_length(), mysqld_port);

#define MYSQL_AUDIT_NOTIFY_CONNECTION_CHANGE_USER(thd) mysql_audit_notify(\
  (thd), MYSQL_AUDIT_CONNECTION_CLASS, MYSQL_AUDIT_CONNECTION_CHANGE_USER,\
  (thd)->get_stmt_da()->is_error() ? (thd)->get_stmt_da()->sql_errno() : 0,\
  (thd)->thread_id(), (thd)->security_ctx->user,\
  (thd)->security_ctx->user ? strlen((thd)->security_ctx->user) : 0,\
  (thd)->security_ctx->priv_user, strlen((thd)->security_ctx->priv_user),\
  (thd)->security_ctx->get_external_user()->ptr(),\
  (thd)->security_ctx->get_external_user()->length(),\
  (thd)->security_ctx->proxy_user, strlen((thd)->security_ctx->proxy_user),\
  (thd)->security_ctx->get_host()->ptr(),\
  (thd)->security_ctx->get_host()->length(),\
  (thd)->security_ctx->get_ip()->ptr(),\
  (thd)->security_ctx->get_ip()->length(),\
  (thd)->db, (thd)->db ? strlen((thd)->db) : 0,\
  (thd)->connection_certificate(),\
  (thd)->connection_certificate_length(), mysqld_port);

#endif /* SQL_AUDIT_INCLUDED */
