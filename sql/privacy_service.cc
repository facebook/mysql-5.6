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
  @file sql/privacy_service.cc
  Service API implementation for Privacy Plugin
*/

#include "mysql/service_privacy.h"
#include "sql/column_statistics.h"
#include "sql/item.h"

bool get_column_ref_info(Item *item, Column_ref_info &column_ref_info) {
  if (item->type() == Item::FIELD_ITEM) {
    Item_field *item_field = (Item_field *)item;
    ColumnUsageInfo cui;
    fetch_table_info(item_field, &cui);
    column_ref_info.m_column_name =
        (item_field->field_name) ? item_field->field_name : "";
    column_ref_info.m_table_name = cui.table_name;
    column_ref_info.m_db_name =
        (item_field->db_name) ? item_field->db_name : "";
    return true;
  }
  return false;
}
