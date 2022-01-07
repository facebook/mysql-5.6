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
  match_op
    Overloads match_op to match ORDER::enum_order to operators that feature
    in column usage stats.
  Input:
    direction     in: ORDER::enum_order
  Output:
    operator_type
*/
operator_type match_op(ORDER::enum_order direction);

/*
  fetch_table_info
    Helper to fetch information of the base table to which the field belongs.
    This includes the name and the table instance in a query.
  Input:
    field_arg     in:  Item_field
                       The field argument to parse the column usage info
                       struct from.
    cui           out: ColumnUsageInfo
                       Base table information is populated in this structure.
*/
void fetch_table_info(Item_field *field_arg, ColumnUsageInfo &cui);

/*
  fill_column_usage_struct
    Helper function to fill out the columnn usage information derived from
    Item_field into the ColumnsUsageInfo struct.
  Input:
    sql_op        in: sql_operation
                      The SQL operation FILTER, TABLE_JOIN etc.
                      in which the field was used.
    op_type       in: operator_type
                      For eg. EQUAL, LESS_THAN etc.
    field_arg     in:  Item_field
                       The field argument to parse the column usage info
                       struct from.
    cui           out: ColumnUsageInfo
                       Stucture representing column usage.
*/
bool fill_column_usage_struct(const sql_operation &sql_op,
                              const operator_type &op_type,
                              Item_field *field_arg, ColumnUsageInfo &cui);

/*
  populate_field_info
    Helper to parse column usage information corresponding to a single
    function item.
  Input:
    op            in: sql_operation
                      The SQL operation FILTER, TABLE_JOIN etc.
                      in which the field was used.
    op_type       in: operator_type
                      For eg. EQUAL, LESS_THAN etc.
    field_arg     in: Item_field
                      The field argument to parse the column usage info
                      struct from.
    out_cus       out: std::set<ColumnUsageInfo>
                       Column usage information parsed from field_arg.
*/
void populate_field_info(
    const sql_operation& op, const operator_type& op_type,
    Item_field *field_arg, std::set<ColumnUsageInfo>& out_cus);

/*
  populate_fields_info
    Helper to parse column usage information corresponding to all
    function items in an atomic predicate (Item_func).
  Input:
    sql_op        in: sql_operation
                      The SQL operation FILTER, TABLE_JOIN etc.
                      in which the field was used.
    op_type       in: operator_type
                      For eg. EQUAL, LESS_THAN etc.
    fields_arg    in: std::vector<Item_field *>
                      The field arguments to parse the column usage info
                      structs from.
    out_cus       out: std::set<ColumnUsageInfo>
                       Column usage information parsed from field_arg.
*/
void populate_fields_info(const sql_operation &sql_op,
                          const operator_type &op_type,
                          std::vector<Item_field *> &field_args,
                          std::set<ColumnUsageInfo> &out_cus);

/*
  parse_column_from_func_item
    Helper to parse column usage information corresponding to a single
    function item.
  Input:
    op            in: sql_operation
                      The SQL operation FILTER, TABLE_JOIN etc.
                      corresponding to the functional item.
    fitem         in: Item_func
                      The functional item to be parsed.
    out_cus       out: std::set<ColumnUsageInfo>
                       Column usage information parsed from fitem.
*/
int parse_column_from_func_item(
    sql_operation op, Item_func *fitem, std::set<ColumnUsageInfo>& out_cus);

/*
  parse_column_from_cond_item
    Helper to parse column usage information corresponding to a single
    conditional item.
  Input:
    op                 in: sql_operation
                           The SQL operation FILTER, TABLE_JOIN etc.
                           corresponding to the conditional item.
    citem              in: Item_cond
                           The conditional item to be parsed.
    out_cus            out: std::set<ColumnUsageInfo>
                            Column usage information parsed from fitem.
    recursion_depth    in: int
                           Book-keeping variable to prevent infinite recursion.
                           To be removed later.
*/
int parse_column_from_cond_item(
    sql_operation op, Item_cond *citem, std::set<ColumnUsageInfo>& out_cus,
    int recursion_depth);

/*
  parse_column_from_item
    Helper to parse column usage information corresponding to a single item.
  Input:
    op                 in: sql_operation
                           The SQL operation FILTER, TABLE_JOIN etc.
                           corresponding to the generic item being parsed.
    item               in: Item
                           The item to be parsed.
    out_cus            out: std::set<ColumnUsageInfo>
                            Column usage information parsed from fitem.
    recursion_depth    in: int
                           Book-keeping variable to prevent infinite recursion.
                           To be removed later.
*/
int parse_column_from_item(
    sql_operation op, Item *item, std::set<ColumnUsageInfo>& out_cus,
    int recursion_depth);

/*
  parse_columns_from_order_list
    Helper to parse column usage information corresponding to an ordered list
    of columns. This is used for ORDER BY and GROUP BY clauses.
  Input:
    op                 in: sql_operation
                           The operation GROUP BY or ORDER_BY which corresponds
                           to the list being processed.
    first_col          in: ORDER*
                           Pointer to the first column being parsed.
    out_cus            out: std::set<ColumnUsageInfo>
                            Column usage information parsed from fitem.
*/
int parse_columns_from_order_list(
    sql_operation op, ORDER* first_col, std::set<ColumnUsageInfo>& out_cus);

/*
  parse_column_usage_info
    Parses column usage information from the parse tree before execution of the
    query.
  Input:
    thd        in: THD
    out_cus    out: std::set<ColumnUsageInfo>
                    Column usage info derived from the parse tree.
*/
extern int parse_column_usage_info(THD *thd);

/*
  exists_column_usage_info
    Returns TRUE if we already collected column usage statistics for the SQL
    statement
  Input:
    thd        in: THD
*/
extern bool exists_column_usage_info(THD *thd);

/*
  populate_column_usage_info
    Populates column usage information into the temporary table data structures.
    This information was derived in `parse_column_usage_info`. Also, clears the
    THD data structure after it has been used to populate the global column
    usage statistics.
  Input:
    thd        in: THD
    cus        in: std::set<ColumnUsageInfo>
                   A set of column usage info structs to populate into the
                   temporary table (COLUMN_STATISTICS) data structure.
                   NOTE: This parameter is acquired by the callee and cannot
                   be used any further by the caller.
*/
extern void populate_column_usage_info(THD *thd);

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
