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


#ifndef _mysql_rim_h
#define _mysql_rim_h

/*************************************************************************
 *   API for RIM plugin. (MYSQL_RIM_PLUGIN)
 */


#include "plugin.h"
#include <string>

#define MYSQL_RIM_INTERFACE_VERSION 0x0100

struct mysql_sql_stats
{
  // Identifiers
  std::string sql_id;
  std::string plan_id;
  std::string client_id;
  std::string db_name;
  std::string user_name;

  // Metrics
  uint64_t cpu;
  uint64_t count;
  uint64_t skipped_count;
  uint64_t rows_inserted;
  uint64_t rows_updated;
  uint64_t rows_deleted;
  uint64_t rows_read;
  uint64_t rows_sent;
};

// Plugin descriptor struct
struct mysql_rim
{
  int interface_version;

  int (*update_sql_stats)(MYSQL_THD, mysql_sql_stats*);
};

#endif
