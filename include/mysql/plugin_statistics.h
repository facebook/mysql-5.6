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


#ifndef _mysql_statistics_h
#define _mysql_statistics_h

/*************************************************************************
 *   API for Statistics plugin. (MYSQL_QUERY_PERF_STATS_PLUGIN)
 */


#include "plugin.h"
#include <stdint.h>

#define MYSQL_STATISTICS_INTERFACE_VERSION 0x0100

struct stats_measurements
{
  int64_t  thd_cpu_time;
};
struct mysql_stmt_stats
{
  const char *tag;
  const struct stats_measurements  *stats;
};

typedef struct mysql_stmt_stats mysql_stmt_stats_t;

// Plugin descriptor struct
struct st_mysql_statistics
{
  int interface_version;
  void (*publish_stmt_stats)(MYSQL_THD, mysql_stmt_stats_t* );
};

#endif
