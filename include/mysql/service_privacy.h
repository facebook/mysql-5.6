#ifndef MYSQL_SERVICE_PRIVACY_INCLUDED
#define MYSQL_SERVICE_PRIVACY_INCLUDED
/* Copyright (c) 2010, 2018, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2.0,
  as published by the Free Software Foundation.

  This program is also distributed with certain software (including
  but not limited to OpenSSL) that is licensed under separate terms,
  as designated in a particular file or component or in included license
  documentation.  The authors of MySQL hereby grant you an additional
  permission to link the program and your derivative works with the
  separately licensed software that they have included with MySQL.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License, version 2.0, for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
  */

/**
  @file mysql/service_privacy.h
  Service API for Privacy Plugin
*/

#pragma once
#include <memory>
#include <string>

class Item;

struct Column_ref_info {
  std::string m_db_name;
  std::string m_table_name;
  std::string m_column_name;
};

/**
 * @brief Get column reference info from an Item object
 *
 * @param[in] item The Item object to inspect column reference info from
 * @param[out] column_ref_info  The output object to receive the column
 * reference info
 *
 * @retval TRUE if the item is an column reference. FALSE otherwise
 */
bool get_column_ref_info(Item *item, Column_ref_info &column_ref_info);

/**
   @ingroup group_ext_plugin_services

   Provide API for xdb privacy plugin to have access to internal data like
   LEX* and Item*
*/
extern "C" struct mysql_privacy_service_st {
  /** Get column reference information from an Item */
  bool (*get_column_ref_info)(Item *item, Column_ref_info &column_ref_info);
} * mysql_privacy_service;

#endif /* MYSQL_SERVICE_PRIVACY_INCLUDED */
