/* Copyright (c) 2009, 2016, Oracle and/or its affiliates. All rights reserved.

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

#include "my_global.h"

#include <stdio.h>
#include <mysql/plugin.h>
#include <mysql/plugin_audit.h>

#include "my_attribute.h"
#include "my_sys.h"

static volatile int number_of_calls; /* for SHOW STATUS, see below */
/* Count MYSQL_AUDIT_GENERAL_CLASS event instances */
static volatile int number_of_calls_general_log;
static volatile int number_of_calls_general_error;
static volatile int number_of_calls_general_result;
static volatile int number_of_calls_general_status;
static volatile int number_of_calls_general_warning;
static volatile int number_of_calls_general_error_instrumented;
/* Count MYSQL_AUDIT_CONNECTION_CLASS event instances */
static volatile int number_of_calls_connection_connect;
static volatile int number_of_calls_connection_disconnect;
static volatile int number_of_calls_connection_change_user;

static volatile char *query_attributes;
static volatile char *response_attributes;

static const int attr_buf_size = 1000;

static void serializeAttrsIntoBuffer(
    const char **attrs,
    unsigned int length,
    char *buf,
    unsigned int buf_length
) {
  DBUG_ASSERT(length % 2 == 0);
  if (attrs && length) {
    for (unsigned int i = 0; (i + 1) < length; i += 2) {
      int written = snprintf(buf, buf_length, "%s=%s,", attrs[i], attrs[i+1]);
      buf_length -= written;
      buf += written;
      DBUG_ASSERT(buf_length);
    }
  } else {
    snprintf(buf, buf_length, "No attributes");
  }
}

/*
 * Print an audit plugin event to the error log
 *
 * SYNOPSIS
 *  log_event()
 *
 * DESCRIPTION
 *  Prints some details of the event to the error log for testing
 *
 */
static void log_event(const struct mysql_event_general* event)
{
  serializeAttrsIntoBuffer(
      event->query_attributes,
      event->query_attributes_length,
      (char *)query_attributes,
      attr_buf_size
  );
  serializeAttrsIntoBuffer(
      event->response_attributes,
      event->response_attributes_length,
      (char *)response_attributes,
      attr_buf_size
  );
}
/*
  Initialize the plugin at server start or plugin installation.

  SYNOPSIS
    audit_null_plugin_init()

  DESCRIPTION
    Does nothing.

  RETURN VALUE
    0                    success
    1                    failure (cannot happen)
*/

static int audit_null_plugin_init(void *arg MY_ATTRIBUTE((unused)))
{
  number_of_calls= 0;
  number_of_calls_general_log= 0;
  number_of_calls_general_error= 0;
  number_of_calls_general_result= 0;
  number_of_calls_general_status= 0;
  number_of_calls_general_warning= 0;
  number_of_calls_general_error_instrumented = 0;
  number_of_calls_connection_connect= 0;
  number_of_calls_connection_disconnect= 0;
  number_of_calls_connection_change_user= 0;
  query_attributes = my_malloc(attr_buf_size, MY_ZEROFILL);
  response_attributes = my_malloc(attr_buf_size, MY_ZEROFILL);
  return(0);
}


/*
  Terminate the plugin at server shutdown or plugin deinstallation.

  SYNOPSIS
    audit_null_plugin_deinit()
    Does nothing.

  RETURN VALUE
    0                    success
    1                    failure (cannot happen)

*/

static int audit_null_plugin_deinit(void *arg MY_ATTRIBUTE((unused)))
{
  my_free((void*)query_attributes);
  query_attributes = NULL;
  my_free((void*)response_attributes);
  response_attributes = NULL;
  return(0);
}


/*
  Foo

  SYNOPSIS
    audit_null_notify()
      thd                connection context

  DESCRIPTION
*/

static void audit_null_notify(MYSQL_THD thd MY_ATTRIBUTE((unused)),
                              unsigned int event_class,
                              const void *event)
{
  /* prone to races, oh well */
  number_of_calls++;
  if (event_class == MYSQL_AUDIT_GENERAL_CLASS)
  {
    const struct mysql_event_general *event_general=
      (const struct mysql_event_general *) event;
    switch (event_general->event_subclass)
    {
    case MYSQL_AUDIT_GENERAL_LOG:
      number_of_calls_general_log++;
      break;
    case MYSQL_AUDIT_GENERAL_ERROR:
      number_of_calls_general_error++;
      break;
    case MYSQL_AUDIT_GENERAL_RESULT:
      number_of_calls_general_result++;
      break;
    case MYSQL_AUDIT_GENERAL_STATUS:
      log_event(event);
      number_of_calls_general_status++;
      break;
    case MYSQL_AUDIT_GENERAL_WARNING:
      number_of_calls_general_warning++;
      break;
    case MYSQL_AUDIT_GENERAL_ERROR_INSTR:
      number_of_calls_general_error_instrumented++;
      break;
    default:
      break;
    }
  }
  else if (event_class == MYSQL_AUDIT_CONNECTION_CLASS)
  {
    const struct mysql_event_connection *event_connection=
      (const struct mysql_event_connection *) event;
    switch (event_connection->event_subclass)
    {
    case MYSQL_AUDIT_CONNECTION_CONNECT:
      number_of_calls_connection_connect++;
      break;
    case MYSQL_AUDIT_CONNECTION_DISCONNECT:
      number_of_calls_connection_disconnect++;
      break;
    case MYSQL_AUDIT_CONNECTION_CHANGE_USER:
      number_of_calls_connection_change_user++;
      break;
    default:
      break;
    }
  }
}


/*
  Plugin type-specific descriptor
*/

static struct st_mysql_audit audit_null_descriptor=
{
  MYSQL_AUDIT_INTERFACE_VERSION,                    /* interface version    */
  NULL,                                             /* release_thd function */
  audit_null_notify,                                /* notify function      */
  { (unsigned long) MYSQL_AUDIT_GENERAL_CLASSMASK |
                    MYSQL_AUDIT_CONNECTION_CLASSMASK } /* class mask           */
};

/*
  Plugin status variables for SHOW STATUS
*/

static struct st_mysql_show_var simple_status[]=
{
  { "Audit_null_called",
    (char *) &number_of_calls,
    SHOW_INT },
  { "Audit_null_general_log",
    (char *) &number_of_calls_general_log,
    SHOW_INT },
  { "Audit_null_general_error",
    (char *) &number_of_calls_general_error,
    SHOW_INT },
  { "Audit_null_general_result",
    (char *) &number_of_calls_general_result,
    SHOW_INT },
  { "Audit_null_general_status",
    (char *) &number_of_calls_general_status,
    SHOW_INT },
  { "Audit_null_general_warning",
    (char *) &number_of_calls_general_warning,
    SHOW_INT },
  { "Audit_null_general_error_instrumented",
    (char *) &number_of_calls_general_error_instrumented,
    SHOW_INT },
  { "Audit_null_connection_connect",
    (char *) &number_of_calls_connection_connect,
    SHOW_INT },
  { "Audit_null_connection_disconnect",
    (char *) &number_of_calls_connection_disconnect,
    SHOW_INT },
  { "Audit_null_connection_change_user",
    (char *) &number_of_calls_connection_change_user,
    SHOW_INT },
  { "Audit_null_query_attributes",
    (char *) &query_attributes,
    SHOW_CHAR_PTR },
  { "Audit_null_response_attributes",
    (char *) &response_attributes,
    SHOW_CHAR_PTR },
  { 0, 0, 0}
};


/*
  Plugin library descriptor
*/

mysql_declare_plugin(audit_null)
{
  MYSQL_AUDIT_PLUGIN,         /* type                            */
  &audit_null_descriptor,     /* descriptor                      */
  "NULL_AUDIT",               /* name                            */
  "Oracle Corp",              /* author                          */
  "Simple NULL Audit",        /* description                     */
  PLUGIN_LICENSE_GPL,
  audit_null_plugin_init,     /* init function (when loaded)     */
  audit_null_plugin_deinit,   /* deinit function (when unloaded) */
  0x0003,                     /* version                         */
  simple_status,              /* status variables                */
  NULL,                       /* system variables                */
  NULL,
  0,
}
mysql_declare_plugin_end;

