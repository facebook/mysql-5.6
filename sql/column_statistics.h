#pragma once

#include <set>
#include <string>
#include <unordered_map>

#include "column_statistics_dt.h"
#include "item.h"

/*
  sql_operation_string
    Stringifies sql_operation enum.
  Input:
    sql_op                in: sql_operation
  Output:
    sql_operation_name    std::string
*/
std::string sql_operation_string(const sql_operation& sql_op);

/*
  operator_type_string
    Stringifies operator_type enum.
  Input:
    op_type         in: operator_type
  Output:
    op_type_name    std::string
*/
std::string operator_type_string(const operator_type& op_type);

/*
  match_op
    Matches an item function type to operators that feature in column usage
    stats.
  Input:
    fitem_type     in: Item_func::Functype
  Output:
    operator_type
*/
operator_type match_op(Item_func::Functype fitem_type);

/*
  parse_column_from_func_item
    Helper to parse column usage information corresponding to a single
    function item.
  Input:
    db_name       in: std::string
    table_name    in: std::string
    fitem         in: Item_func
                      The functional item to be parsed.
    out_cus       out: std::set<ColumnUsageInfo>
                       Column usage information parsed from fitem.
*/
int parse_column_from_func_item(
    const std::string& db_name, const std::string& table_name,
    Item_func *fitem, std::set<ColumnUsageInfo>& out_cus);

/*
  parse_column_from_cond_item
    Helper to parse column usage information corresponding to a single
    conditional item.
  Input:
    db_name            in: std::string
    table_name         in: std::string
    citem              in: Item_cond
                           The conditional item to be parsed.
    out_cus            out: std::set<ColumnUsageInfo>
                            Column usage information parsed from fitem.
    recursion_depth    in: int
                           Book-keeping variable to prevent infinite recursion.
                           To be removed later.
*/
int parse_column_from_cond_item(
    const std::string& db_name, const std::string& table_name, Item_cond *citem,
    std::set<ColumnUsageInfo>& out_cus, int recursion_depth);

/*
  parse_column_from_item
    Helper to parse column usage information corresponding to a single item.
  Input:
    db_name            in: std::string
    table_name         in: std::string
    item               in: Item
                           The item to be parsed.
    out_cus            out: std::set<ColumnUsageInfo>
                            Column usage information parsed from fitem.
    recursion_depth    in: int
                           Book-keeping variable to prevent infinite recursion.
                           To be removed later.
*/
int parse_column_from_item(
    const std::string& db_name, const std::string& table_name, Item *item,
    std::set<ColumnUsageInfo>& out_cus, int recursion_depth);

/*
  parse_column_usage_info
    Parses column usage information from the parse tree before execution of the
    query.
  Input:
    thd        in: THD
    out_cus    out: std::set<ColumnUsageInfo>
                    Column usage info derived from the parse tree.
*/
extern int parse_column_usage_info(
    THD *thd, std::set<ColumnUsageInfo>& out_cus);

/*
  populate_column_usage_info
    Populates column usage information into the temporary table data structures.
    This information was derived in `parse_column_usage_info`.
  Input:
    thd        in: THD
    cus        in: std::set<ColumnUsageInfo>
                   A set of column usage info structs to populate into the
                   temporary table (COLUMN_STATISTICS) data structure.
                   NOTE: This parameter is acquired by the callee and cannot
                   be used any further by the caller.
*/
extern void populate_column_usage_info(
    THD *thd, std::set<ColumnUsageInfo>& cus);

/*
  fill_column_statistics
    Populates the temporary table by reading from the column usage map.
  Input:
    thd     in: THD
    cond    in: Item
    table   out: TABLE_LIST
*/
extern int fill_column_statistics(THD *thd, TABLE_LIST *tables, Item *cond);

/*
  free_column_stats
    Evicts column stats from col_statistics_map.
*/
extern void free_column_stats();

// Read-write lock to make the unordered map storing column usage, threadsafe.
extern mysql_rwlock_t LOCK_column_statistics;

// Mapping from SQL_ID to all the column usage information.
extern std::unordered_map<md5_key, std::set<ColumnUsageInfo> >
col_statistics_map;
