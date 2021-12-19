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
#include <mem_root_deque.h>
#include <my_alloc.h>
#include <memory>
#include <string>
#include <utility>

class Item;
class Query_block;
class Query_expression;
class THD;
class Table_ref;

struct Column_ref_info {
  std::string m_db_name;
  std::string m_table_name;
  std::string m_column_name;
};

/**
 * @brief Abstract lineage node for resolving column lineage.
 *
 * @note For fast memory allocation, Column_lineage_info is dynamically
 * allocated in heap with MEM_ROOT which gaurantees O(1) memory allocation and
 * deallocation. The caveat is that no destructors are run, which poses
 * potential memory leak if there's any memory allocated not by MEM_ROOT, e.g.
 * std::vector and std::string. To prevent that:
 *
 * 1. Use containers like mem_root_deque instead of STL container.
 *    mem_root_deque allocates memory from MEM_ROOT.
 * 2. Use const char * instead of std::string. If copy is required, use
 *    safe_strdup_root which allocates memory from MEM_ROOT.
 */
class Column_lineage_info {
 protected:
  Column_lineage_info(uint32_t id) : m_id(id) {}

 public:
  enum class Type {
    INVALID = 0,
    QUERY_BLOCK = 1,
    UNION = 2,
    TABLE = 3,
  };

  virtual Type type() const { return Type::INVALID; }
  virtual ~Column_lineage_info() {}

  uint32_t m_id;  // unique identifier
};

/**
 * @brief Lineage node represent Item level lineage
 */
struct Item_lineage_info {
  uint32_t m_index;
  Column_lineage_info *m_cli;
};

/**
 * @brief Lineage node represent Item level lineage
 */
struct Field_lineage_info {
  const char *m_field_name;
  mem_root_deque<Item_lineage_info> m_item_lineage_info;
};

/**
 * @brief Lineage node represents Query_expression union unit
 */
class Union_column_lineage_info : public Column_lineage_info {
 public:
  Union_column_lineage_info(uint32_t id, Query_expression *unit,
                            MEM_ROOT *mem_root)
      : Column_lineage_info(id), m_unit(unit), m_parents(mem_root) {}

  Query_expression *m_unit;
  mem_root_deque<Column_lineage_info *> m_parents;
  enum Type type() const override { return Type::UNION; }
};

/**
 * @brief Lineage node represents Query_block query block
 */
class Query_block_column_lineage_info : public Column_lineage_info {
 public:
  Query_block_column_lineage_info(uint32_t id, Query_block *query_block,
                                  MEM_ROOT *mem_root)
      : Column_lineage_info(id),
        m_query_block(query_block),
        m_parents(mem_root),
        m_selected_field(mem_root),
        m_where_condition(mem_root),
        m_group_list(mem_root),
        m_having_condition(mem_root),
        m_order_list(mem_root) {}

  Query_block *m_query_block;
  mem_root_deque<Column_lineage_info *> m_parents;
  mem_root_deque<Field_lineage_info> m_selected_field;
  mem_root_deque<Item_lineage_info> m_where_condition;
  mem_root_deque<Item_lineage_info> m_group_list;
  mem_root_deque<Item_lineage_info> m_having_condition;
  mem_root_deque<Item_lineage_info> m_order_list;
  enum Type type() const override { return Type::QUERY_BLOCK; }
};

/**
 * @brief Lineage node represents Table_ref table reference
 */
class Table_column_lineage_info : public Column_lineage_info {
 public:
  Table_column_lineage_info(uint32_t id, Table_ref *table_ref,
                            MEM_ROOT *mem_root)
      : Column_lineage_info(id),
        m_table_ref(table_ref),
        m_column_refs(mem_root) {}

  Table_ref *m_table_ref = nullptr;
  const char *m_db_name;
  const char *m_table_name;
  const char *m_table_alias;
  mem_root_deque<const char *> m_column_refs;
  enum Type type() const override { return Type::TABLE; }
};

/**
 * @brief Build column lineage info for the query associated with the thread
 *
 * @param[in] thd The MySQL internal thread pointer
 *
 * @retval Pointer to the root of Column_lineage_info tree
 */
Column_lineage_info *build_column_lineage_info(THD *thd);

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
  /** Build column lineage info from THD */
  Column_lineage_info *(*build_column_lineage_info)(THD *thd);
} * mysql_privacy_service;

#endif /* MYSQL_SERVICE_PRIVACY_INCLUDED */
