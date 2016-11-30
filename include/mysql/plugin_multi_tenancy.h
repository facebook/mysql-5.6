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


#ifndef _mysql_multi_tenancy_h
#define _mysql_multi_tenancy_h

/*************************************************************************
 *   API for Multi-Tenancy plugin. (MYSQL_MULTI_TENANCY_PLUGIN)
 */


#include "plugin.h"

#define MYSQL_MULTI_TENANCY_INTERFACE_VERSION 0x0100

// Resource isolation types that a multi-tenancy plugin will handle.
// Currently connection and query limits are two resource types. More will be
// supported in the future.
enum class enum_multi_tenancy_resource_type
{
  MULTI_TENANCY_RESOURCE_CONNECTION,
  MULTI_TENANCY_RESOURCE_QUERY,

  MULTI_TENANCY_NUM_RESOURCE_TYPES
};

// Callback function return types.
// - ACCEPT: resource can be granted
// - WAIT: may need to wait for resource to be freed up
// - REJECT: resource cannot be granted
// - FALLBACK: plugin is disabled
enum class enum_multi_tenancy_return_type
{
  MULTI_TENANCY_RET_ACCEPT = 0,
  MULTI_TENANCY_RET_WAIT,
  MULTI_TENANCY_RET_REJECT,

  MULTI_TENANCY_RET_FALLBACK,
  MULTI_TENANCY_NUM_RETURN_TYPES
};

typedef enum enum_multi_tenancy_resource_type MT_RESOURCE_TYPE;
typedef enum enum_multi_tenancy_return_type MT_RETURN_TYPE;
typedef std::unordered_map<std::string, std::string> ATTRS_MAP_T;

// Plugin descriptor struct
struct st_mysql_multi_tenancy
{
  int interface_version;
  MT_RETURN_TYPE (*request_resource)
    (MYSQL_THD, MT_RESOURCE_TYPE type, const ATTRS_MAP_T *);
  MT_RETURN_TYPE (*release_resource)
    (MYSQL_THD, MT_RESOURCE_TYPE type, const ATTRS_MAP_T *);
};

#endif
