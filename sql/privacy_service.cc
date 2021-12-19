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

#include "mem_root_deque.h"
#include "my_dbug.h"
#include "mysql/service_privacy.h"
#include "sql/column_statistics.h"
#include "sql/item.h"
#include "sql/select_lex_visitor.h"
#include "sql/sql_class.h"
#include "sql/sql_lex.h"
#include "sql/sql_optimizer.h"  // JOIN
#include "sql/table.h"

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

class Item_lineage_info_builder : public Select_lex_visitor {
 public:
  bool visit_union(Query_expression *) override { return false; }
  bool visit_query_block(Query_block *) override { return false; }
  /**
   * @brief Find Item_field and store them in m_item_fields.
   *
   * @param [in]     item An item to visit
   *
   * @retval false Succeed and continue traversal.
   * @retval true Halt traversal.
   */
  bool visit_item(Item *item) override {
    if (m_visited.find(item) != m_visited.end()) return false;
    m_visited.insert(item);

    if (item->type() == Item::FIELD_ITEM) {
      m_item_fields.push_back(dynamic_cast<Item_field *>(item));
    }
    return false;
  }
  const std::vector<Item_field *> &get_item_fields() { return m_item_fields; }

 private:
  std::vector<Item_field *> m_item_fields;
  std::unordered_set<Item *> m_visited;
};

/**
 * @brief Column_lineage_info_builder build the lineage info by implementing the
 * visit_* methods to allow itself to be accepted by Query_expression,
 * Query_block and Item.
 *
 * The traversal order is post-order traversal, defined in the accept methods in
 * sql/sql_lex.h. During the traversal, the builder stores the mapping from AST
 * node to corresponding Column_lineage_info.
 *
 * After the traversal, the top-level Column_lineage_info can be accessed by
 * looking up m_unit_map by Query_block_UNION * pointer.
 */
class Column_lineage_info_builder : public Select_lex_visitor {
 public:
  Column_lineage_info_builder(MEM_ROOT *mem_root) : m_mem_root(mem_root) {
    assert(m_mem_root);
  }

  /**
   * @brief Build Union_column_lineage_info for the given union unit
   * The built column lineage info will be stored in m_unit_map.
   *
   * @param[in] unit The Query_expression union unit
   *
   * @retval FALSE if successful. Otherwise, TRUE is returned.
   */
  bool visit_union(Query_expression *unit) override {
    DBUG_TRACE;
    DBUG_PRINT("column_lineage_info", ("unit %p", unit));
    if (m_unit_map.find(unit) != m_unit_map.end()) return false;

    Union_column_lineage_info *cli = new (m_mem_root)
        Union_column_lineage_info(m_next_id++, unit, m_mem_root);

    // visit all the inner unit and add to parents
    for (Query_block *query_block = unit->first_query_block(); query_block;
         query_block = query_block->next_query_block()) {
      if (query_block->accept(this)) return true;
      const auto &it = m_query_block_map.find(query_block);
      if (it == m_query_block_map.end()) {
        DBUG_PRINT("column_lineage_info", ("Query block lineage is not built"));
        return true;
      }
      cli->m_parents.push_back(it->second);
    }

    // store the result
    DBUG_PRINT("column_lineage_info",
               ("storing m_unit_map %p => %p\n", unit, cli));
    m_unit_map[unit] = cli;
    DBUG_PRINT("column_lineage_info", ("exiting visit_union %p\n", unit));
    return false;
  }

  /**
   * @brief Build Query_block_column_lineage_info for the given query block
   * The built column lineage info will be stored in m_query_block_map.
   *
   * @param[in] query_block The Query_block query block
   *
   * @retval FALSE if successful. Otherwise, TRUE is returned.
   */
  bool visit_query_block(Query_block *query_block) override {
    DBUG_TRACE;
    DBUG_PRINT("column_lineage_info", ("query block %p", query_block));
    if (m_query_block_map.find(query_block) != m_query_block_map.end())
      return false;

    Query_block_column_lineage_info *cli = new (m_mem_root)
        Query_block_column_lineage_info(m_next_id++, query_block, m_mem_root);

    // visit all the inner unit and add to parents
    for (Query_expression *unit = query_block->first_inner_query_expression();
         unit; unit = unit->next_query_expression()) {
      if (unit->accept(this)) return true;
      const auto &it = m_unit_map.find(unit);
      if (it == m_unit_map.end()) {
        DBUG_PRINT("column_lineage_info", ("Union lineage is not built"));
        return true;
      }
      cli->m_parents.push_back(it->second);
    }

    // Determine if the query block is the leaf query block by looking into 2
    // things:
    // 1) inner unit: If yes, the query block might have subqueries
    // 2) table_list: If there's more than one table, this query block might
    //                perform JOIN
    const auto &unit = query_block->first_inner_query_expression();
    if (!unit) {
      // Leaf query block. Iterate through all the item and their positions in
      // Table_ref. There might be multiple Table_ref
      for (Table_ref *t = query_block->m_table_list.first; t;
           t = t->next_local) {
        if (build_table_lineage_info(t)) {
          return true;
        }

        Table_column_lineage_info *table_cli = m_table_ref_map[t];
        cli->m_parents.push_back(table_cli);
      }

      // Select clause: Build Field_lineage_info for each field from
      // visible_fields() and append into cli->m_selected_field.
      for (Item *item : query_block->visible_fields()) {
        Item_lineage_info_builder item_builder;
        walk_item(item, &item_builder);
        mem_root_deque<Item_lineage_info> item_lineage_infos(m_mem_root);
        build_multiple_item_lineage_infos(item_builder.get_item_fields(),
                                          item_lineage_infos);
        Field_lineage_info field_lineage_info{item->item_name.ptr(),
                                              std::move(item_lineage_infos)};
        cli->m_selected_field.push_back({std::move(field_lineage_info)});
      }

      // Where clause: Collect all the Item_lineage_info for the clause
      // expression and store in cli->m_where_condition.
      {
        Item_lineage_info_builder item_builder;
        Item *where_condition = query_block->join != nullptr
                                    ? query_block->join->where_cond
                                    : query_block->where_cond();
        if (where_condition != nullptr &&
            walk_item(where_condition, &item_builder))
          return true;
        build_multiple_item_lineage_infos(item_builder.get_item_fields(),
                                          cli->m_where_condition);
      }

      // Group by and olap clauses: Collect all the Item_lineage_info for the
      // clause expression and store in cli->m_group_list.
      {
        Item_lineage_info_builder item_builder;
        if (accept_for_order(query_block->group_list, &item_builder))
          return true;
        build_multiple_item_lineage_infos(item_builder.get_item_fields(),
                                          cli->m_group_list);
      }

      // Having clause: Collect all the Item_lineage_info for the clause
      // expression and store in cli->m_having_condition.
      {
        Item_lineage_info_builder item_builder;
        Item *having_condition = query_block->join != nullptr
                                     ? query_block->join->having_for_explain
                                     : query_block->having_cond();
        if (walk_item(having_condition, &item_builder)) return true;
        build_multiple_item_lineage_infos(item_builder.get_item_fields(),
                                          cli->m_having_condition);
      }

      // Order clause: Collect all the Item_lineage_info for the clause
      // expression and store in cli->m_order_list.
      {
        Item_lineage_info_builder item_builder;
        if (accept_for_order(query_block->order_list, &item_builder))
          return true;
        build_multiple_item_lineage_infos(item_builder.get_item_fields(),
                                          cli->m_order_list);
      }
    } else {
      // inner union units not supported
      DBUG_PRINT("column_lineage_info",
                 ("inner union units under a Query_block not supported"));
      return true;
    }

    // store the result
    DBUG_PRINT(
        "column_lineage_info",
        ("storing m_query_block_map result %p => %p\n", query_block, cli));
    m_query_block_map[query_block] = cli;
    return false;
  }

  /**
   * @brief Override visit_item method. No implementation.
   *
   * @retval FALSE if successful. Otherwise, TRUE is returned.
   */
  bool visit_item(Item *) override { return false; }

  /**
   * @brief Build Table_column_lineage_info for the given table reference
   * The built column lineage info will be stored in m_table_ref_map
   *
   * @param[in] table_ref The Table_ref table reference
   *
   * @retval FALSE if successful. Otherwise, TRUE is returned.
   */
  bool build_table_lineage_info(Table_ref *table_ref) {
    DBUG_TRACE;
    if (m_table_ref_map.find(table_ref) != m_table_ref_map.end()) return false;

    Table_column_lineage_info *cli = new (m_mem_root)
        Table_column_lineage_info(m_next_id++, table_ref, m_mem_root);
    cli->m_db_name = table_ref->get_db_name();
    cli->m_table_name = table_ref->get_table_name();
    cli->m_table_alias = table_ref->alias ? table_ref->alias : "";

    if (!table_ref->table) {
      DBUG_PRINT("column_lineage_info", ("table_ref->table is empty"));
      return true;
    }
    for (Field **field_ptr = table_ref->table->field; *field_ptr; ++field_ptr) {
      cli->m_column_refs.push_back(
          (*field_ptr)->field_name ? (*field_ptr)->field_name : "");
    }

    DBUG_PRINT("column_lineage_info",
               ("storing m_table_ref_map result %p => %p\n", table_ref, cli));
    m_table_ref_map[table_ref] = cli;
    return false;
  }

  /**
   * @brief Build Item_lineage_info from Item_field
   *
   * @param [in]     item_field The Item_field to convert from
   * @param [out]    item_lineage_info The Item_lineage_info to convert to
   *
   * @retval false Succeed
   * @retval true Fail
   */
  bool build_item_lineage_info(Item_field *item_field,
                               Item_lineage_info &item_lineage_info) {
    Field *field = item_field->field;
    if (!field) {
      DBUG_PRINT("column_lineage_info",
                 ("Field of item_field (%s) is null", item_field->full_name()));
      return true;
    }
    Table_ref *table_ref = item_field->table_ref;
    if (!table_ref) {
      DBUG_PRINT("column_lineage_info",
                 ("table (Table_ref) of item_field (%s) is null",
                  item_field->full_name()));
      return true;
    }
    const auto &table_cli = m_table_ref_map[table_ref];

    item_lineage_info.m_index = field->field_index();
    item_lineage_info.m_cli = table_cli;
    return false;
  }

  /**
   * @brief Build Item_lineage_info from multiple Item_field and append the
   * converted Item_lineage_info into the deque of Item_lineage_info
   *
   * @param [in]     item_fields        The Item_field vector to convert from
   * @param [out]    item_lineage_infos The deque of Item_lineage_info to append
   *                                    the converted Item_lineage_info
   *
   * @retval false Succeed
   * @retval true Fail
   */
  bool build_multiple_item_lineage_infos(
      const std::vector<Item_field *> &item_fields,
      mem_root_deque<Item_lineage_info> &item_lineage_infos) {
    for (Item_field *item_field : item_fields) {
      Item_lineage_info item_lineage_info;
      if (build_item_lineage_info(item_field, item_lineage_info)) {
        return true;
      }
      item_lineage_infos.push_back(std::move(item_lineage_info));
    }
    return false;
  }

  uint32_t m_next_id = 0;
  MEM_ROOT *m_mem_root = nullptr;  // Pointer to current memroot
  std::unordered_map<Query_expression *, Union_column_lineage_info *>
      m_unit_map;
  std::unordered_map<Query_block *, Query_block_column_lineage_info *>
      m_query_block_map;
  std::unordered_map<Table_ref *, Table_column_lineage_info *> m_table_ref_map;
};

/**
 * @brief Build column lineage info for the query associated with the thread
 *
 * @param[in] thd The MySQL internal thread pointer
 *
 * @retval Pointer to the root of Column_lineage_info tree
 */
Column_lineage_info *build_column_lineage_info(THD *thd) {
  DBUG_TRACE;
  assert(thd->mem_root);
  if (!thd->mem_root) {
    DBUG_PRINT("column_lineage_info", ("thd->mem_root cannot be null"));
    return nullptr;
  }
  Column_lineage_info_builder visitor(thd->mem_root);
  thd->lex->unit->accept(&visitor);
  const auto &it = visitor.m_unit_map.find(thd->lex->unit);
  if (it != visitor.m_unit_map.end()) {
    return it->second;
  }
  DBUG_PRINT("column_lineage_info", ("Failed to build Column_lineage_info"));
  return nullptr;
}
