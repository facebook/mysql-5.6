/* Copyright (c) 2016, Facebook. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */


#include <stdio.h>
#include "mysqld.h"
#include "my_atomic.h"
#include <mysql/plugin.h>
#include <mysql/plugin_multi_tenancy.h>


/*
 * Dummy multi-tenancy plugin
 */


// Number of open connections
static int number_of_conns;
// Number of running queries
static int number_of_queries;
// Number of conns with db in conn attrs
static int number_of_db_conns;

extern LEX_STRING INFORMATION_SCHEMA_NAME;
extern LEX_STRING PERFORMANCE_SCHEMA_DB_NAME;
extern LEX_STRING MYSQL_SCHEMA_NAME;

static MYSQL_PLUGIN plugin_info_ptr;
static char plugin_on;

static void plugin_on_set(
    THD *thd MY_ATTRIBUTE((unused)),
    struct st_mysql_sys_var *var MY_ATTRIBUTE((unused)),
    void *var_ptr MY_ATTRIBUTE((unused)),
    const void *save)
{
  if (*(my_bool*) save)
  {
    plugin_on = 1;
    my_plugin_log_message(&plugin_info_ptr, MY_INFORMATION_LEVEL,
                          "mt_simple plugin on");
  }
  else
  {
    plugin_on = 0;
    my_plugin_log_message(&plugin_info_ptr, MY_INFORMATION_LEVEL,
                          "mt_simple plugin off");
  }
}

static MYSQL_SYSVAR_BOOL(on,
                         plugin_on,
                         PLUGIN_VAR_NOCMDARG,
                         "Turns the plugin on and off.",
                         nullptr,
                         plugin_on_set,
                         1);

/*
 * Initialize the plugin at server start or plugin installation.
 */
static int mt_simple_plugin_init(MYSQL_PLUGIN plugin_ref)
{
  plugin_info_ptr= plugin_ref;
  number_of_conns = 0;
  number_of_queries = 0;
  number_of_db_conns = 0;
  my_plugin_log_message(&plugin_info_ptr, MY_INFORMATION_LEVEL,
                        "mt_simple plugin initiated");
  return(0);
}


/*
 * Terminate the plugin at server shutdown or plugin deinstallation.
 */
static int mt_simple_plugin_deinit(void *arg MY_ATTRIBUTE((unused)))
{
  my_plugin_log_message(&plugin_info_ptr, MY_INFORMATION_LEVEL,
                        "mt_simple plugin deinitiated");
  return(0);
}


/*
 * Increment stats
 */
static MT_RETURN_TYPE mt_simple_request_resource(
    MYSQL_THD thd,
    MT_RESOURCE_TYPE type,
    const MT_RESOURCE_ATTRS *resource_attrs)
{
  if (!plugin_on)
    return MT_RETURN_TYPE::MULTI_TENANCY_RET_FALLBACK;

  switch(type)
  {
    case MT_RESOURCE_TYPE::MULTI_TENANCY_RESOURCE_CONNECTION:
      {
        if (!resource_attrs)
          break;

        const char *db = resource_attrs->database;
        // ignore system schemas
        if (db &&
            strcmp(db, INFORMATION_SCHEMA_NAME.str) &&
            strcmp(db, PERFORMANCE_SCHEMA_DB_NAME.str) &&
            strcmp(db, MYSQL_SCHEMA_NAME.str))
        {
            my_atomic_add32(&number_of_db_conns, 1);
        }

        // add to the total conns
        my_atomic_add32(&number_of_conns, 1);

        break;
      }

    case MT_RESOURCE_TYPE::MULTI_TENANCY_RESOURCE_QUERY:
      {
        my_atomic_add32(&number_of_queries, 1);
        break;
      }

    default:
      return MT_RETURN_TYPE::MULTI_TENANCY_RET_ACCEPT;
  }

  return MT_RETURN_TYPE::MULTI_TENANCY_RET_ACCEPT;
}


/*
 * Decrement stats
 */
static MT_RETURN_TYPE mt_simple_release_resource(
    MYSQL_THD thd,
    MT_RESOURCE_TYPE type,
    const MT_RESOURCE_ATTRS *resource_attrs)
{
  if (!plugin_on)
    return MT_RETURN_TYPE::MULTI_TENANCY_RET_FALLBACK;

  switch(type)
  {
    case MT_RESOURCE_TYPE::MULTI_TENANCY_RESOURCE_CONNECTION:
      {
        if (!resource_attrs)
          break;

        const char *db = resource_attrs->database;
        int val = my_atomic_load32(&number_of_db_conns);
        // ignore system schemas
        if (!db && val > 0)
        {
          if (strcmp(db, INFORMATION_SCHEMA_NAME.str) &&
              strcmp(db, PERFORMANCE_SCHEMA_DB_NAME.str) &&
              strcmp(db, MYSQL_SCHEMA_NAME.str))
            // decrement number_of_db_conns
            my_atomic_add32(&number_of_db_conns, -1);
        }

        // load total conns
        val = my_atomic_load32(&number_of_conns);
        if (val > 0) {
          //decrement total conns
          my_atomic_add32(&number_of_conns, -1);
        }

        break;
      }

    case MT_RESOURCE_TYPE::MULTI_TENANCY_RESOURCE_QUERY:
      {
        int val = my_atomic_load32(&number_of_queries);
        if (val > 0)
          my_atomic_add32(&number_of_queries, -1);
        break;
      }

    default:
      return MT_RETURN_TYPE::MULTI_TENANCY_RET_ACCEPT;
  }

  return MT_RETURN_TYPE::MULTI_TENANCY_RET_ACCEPT;
}

/*
 * Plugin type-specific descriptor
 */
static struct st_mysql_multi_tenancy mt_simple_descriptor=
{
  MYSQL_MULTI_TENANCY_INTERFACE_VERSION, /* interface version    */
  mt_simple_request_resource,
  mt_simple_release_resource
};


static struct st_mysql_sys_var* simple_vars[]=
{
  MYSQL_SYSVAR(on),
  NULL
};

/*
 * Plugin status variables for SHOW STATUS
 */
static struct st_mysql_show_var simple_status[]=
{
  { "mt_simple_open_conns",
    (char *) &number_of_conns,
    SHOW_INT },
  { "mt_simple_open_db_conns",
    (char *) &number_of_db_conns,
    SHOW_INT },
  { "mt_simple_running_queries",
    (char *) &number_of_queries,
    SHOW_INT },
  { 0, 0, SHOW_UNDEF }
};


/*
 * Plugin library descriptor
 */
mysql_declare_plugin(mt_simple)
{
  MYSQL_MULTI_TENANCY_PLUGIN, /* type                            */
  &mt_simple_descriptor,      /* descriptor                      */
  "MT_SIMPLE",                /* name                            */
  "Tian Xia",                 /* author                          */
  "Simple multi_tenancy",     /* description                     */
  PLUGIN_LICENSE_GPL,
  mt_simple_plugin_init,      /* init function (when loaded)     */
  mt_simple_plugin_deinit,    /* deinit function (when unloaded) */
  0x0100,                     /* version                         */
  simple_status,              /* status variables                */
  simple_vars,                /* system variables                */
  NULL,
  0,
}
mysql_declare_plugin_end;
