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

#include <mysql/plugin.h>
#include <mysql/plugin_statistics.h>
#include <stdio.h>
#include "my_atomic.h"
#include "sql/mysqld.h"

/*
 * Dummy statistics plugin
 */

static MYSQL_PLUGIN plugin_info_ptr;
static bool plugin_on;

static void plugin_on_set(THD *, struct SYS_VAR *, void *, const void *save) {
  if (*static_cast<const bool *>(save)) {
    plugin_on = true;
    my_plugin_log_message(&plugin_info_ptr, MY_INFORMATION_LEVEL,
                          "stats_example plugin on");
  } else {
    plugin_on = false;
    my_plugin_log_message(&plugin_info_ptr, MY_INFORMATION_LEVEL,
                          "stats_example plugin off");
  }
}

static MYSQL_SYSVAR_BOOL(on, plugin_on, PLUGIN_VAR_NOCMDARG,
                         "Turns the plugin on and off.", nullptr, plugin_on_set,
                         true);

static int stats_example_plugin_init(MYSQL_PLUGIN plugin_ref) {
  plugin_info_ptr = plugin_ref;
  my_plugin_log_message(&plugin_info_ptr, MY_INFORMATION_LEVEL,
                        "stats_examaple plugin initiated");
  return 0;
}

static int stats_example_plugin_deinit(void *) {
  my_plugin_log_message(&plugin_info_ptr, MY_INFORMATION_LEVEL,
                        "stats_example plugin deinitiated");
  return 0;
}

static void stats_exmaple_publish_stmt_stats(MYSQL_THD,
                                             mysql_stmt_stats_t *stats) {
  if (!plugin_on) return;
  char msg[1024];
  snprintf(msg, sizeof(msg), "async query tag '%s' took %llu nanoseconds",
           stats->tag,
           static_cast<unsigned long long>(stats->stats->thd_cpu_time));
  my_plugin_log_message(&plugin_info_ptr, MY_INFORMATION_LEVEL, "%s", msg);
}

static struct st_mysql_statistics stats_example_descriptor = {
    MYSQL_STATISTICS_INTERFACE_VERSION, /* interface version    */
    stats_exmaple_publish_stmt_stats};

static struct SYS_VAR *plugin_system_vars[] = {MYSQL_SYSVAR(on), nullptr};

static struct SHOW_VAR plugin_status_vars[] = {
    {nullptr, nullptr, SHOW_UNDEF, SHOW_SCOPE_UNDEF}};

/*
 * Plugin library descriptor
 */
mysql_declare_plugin(mt_simple){
    MYSQL_QUERY_PERF_STATS_PLUGIN, /* type                            */
    &stats_example_descriptor,     /* descriptor                      */
    "STATS_EXAMPLE",               /* name                            */
    "Volodymyr Verovkin",          /* author                          */
    "Stats collection example",    /* description                     */
    PLUGIN_LICENSE_GPL,
    stats_example_plugin_init, /* init function (when loaded)     */
    nullptr,
    stats_example_plugin_deinit, /* deinit function (when unloaded) */
    0x0100,                      /* version                         */
    plugin_status_vars,          /* status variables                */
    plugin_system_vars,          /* system variables                */
    nullptr,
    0,
} mysql_declare_plugin_end;
