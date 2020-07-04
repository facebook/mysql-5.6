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

/*
 * MySQL RIM Plugin.
 */

#include "my_global.h"
#include <mysql/plugin_rim.h>

static MYSQL_PLUGIN plugin_info_ptr;
static my_bool plugin_on;

static void plugin_on_set(
    THD *thd MY_ATTRIBUTE((unused)),
    struct st_mysql_sys_var *var MY_ATTRIBUTE((unused)),
    void *var_ptr MY_ATTRIBUTE((unused)),
    const void *save)
{
  if (*(my_bool*) save)
  {
    plugin_on = TRUE;
    my_plugin_log_message(&plugin_info_ptr, MY_INFORMATION_LEVEL,
                          "rim plugin on");
  }
  else
  {
    plugin_on = FALSE;
    my_plugin_log_message(&plugin_info_ptr, MY_INFORMATION_LEVEL,
                          "rim plugin off");
  }
}

static MYSQL_SYSVAR_BOOL(on,
                         plugin_on,
                         PLUGIN_VAR_NOCMDARG,
                         "Turns the plugin on and off.",
                         nullptr,
                         plugin_on_set,
                         FALSE);

/*
 * Initialize the plugin at server start or plugin installation.
 */
static int rim_plugin_init(MYSQL_PLUGIN plugin_ref)
{
  plugin_info_ptr = plugin_ref;
  my_plugin_log_message(&plugin_info_ptr, MY_INFORMATION_LEVEL,
                        "rim plugin initialized");
  return 0;
}

/*
 * Terminate the plugin at server shutdown or plugin deinstallation.
 */
static int rim_plugin_deinit(MYSQL_PLUGIN plugin_ref MY_ATTRIBUTE((unused)))
{
  my_plugin_log_message(&plugin_info_ptr, MY_INFORMATION_LEVEL,
                        "rim plugin deinitialized");
  return 0;
}

static int rim_update_sql_stats(
    MYSQL_THD thd MY_ATTRIBUTE((unused)),
    mysql_sql_stats *stats)
{
  if (!plugin_on || !stats)
    return 0;

  my_plugin_log_message(&plugin_info_ptr, MY_INFORMATION_LEVEL,
                        "rim update sql stats");

  return 1;
}

/*
 * Plugin status variables array
 */
static struct st_mysql_show_var plugin_status_vars[] =
{
  { nullptr, nullptr, SHOW_UNDEF }
};

/*
 * Plugin system variables array
 */
static struct st_mysql_sys_var *plugin_system_vars[] =
{
  MYSQL_SYSVAR(on),
  nullptr
};

/*
 * Plugin type-specific descriptor
 */
static struct mysql_rim rim_descriptor =
{
  MYSQL_RIM_INTERFACE_VERSION,  /* interface version */
  rim_update_sql_stats
};

/*
 * Plugin library descriptor
 */
mysql_declare_plugin(rim)
{
  MYSQL_RIM_PLUGIN,           /* type                            */
  &rim_descriptor,            /* descriptor                      */
  "RIM",                      /* name                            */
  "Facebook Inc.",            /* author                          */
  "MySQL RIM plugin",         /* description                     */
  PLUGIN_LICENSE_GPL,
  rim_plugin_init,            /* init function (when loaded)     */
  rim_plugin_deinit,          /* deinit function (when unloaded) */
  0x0100,                     /* version                         */
  plugin_status_vars,         /* status variables                */
  plugin_system_vars,         /* system variables                */
  nullptr,
  0,
}
mysql_declare_plugin_end;
