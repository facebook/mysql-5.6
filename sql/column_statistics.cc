#include "column_statistics.h"

#include "sql_class.h"
#include "sql_show.h" // schema_table_store_record

/*
  COLUMN_STATISTICS
  Associates a SQL Id with a set of columns that were used in the query.
  Captures column identification information such as database, table name etc.
  and information about how the column was used, for eg. operation, operator
  etc.
*/
ST_FIELD_INFO column_statistics_fields_info[]=
{
  {"SQL_ID", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"TABLE_SCHEMA", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"TABLE_NAME", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"TABLE_INSTANCE", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"COLUMN_NAME", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"SQL_OPERATION", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"OPERATOR_TYPE", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};

// Mapping from SQL_ID to column usage information.
std::unordered_map<md5_key, std::set<ColumnUsageInfo> > col_statistics_map;

// Operator definition for strict weak ordering. Should be a function of all
// constituents of the struct. The ordering is done for ColumnUsageInfo structs
// in lexicographic fashion on the following elements.
// TABLE_SCHEMA, TABLE_NAME, TABLE_INSTANCE, COLUMN_NAME, SQL_OPERATION,
// OPERATOR_TYPE
// For eg. (xdb_mzait, tab1, instance1, col1, FILTER, GREATER_THAN) comes
// before (xdb_ritwik, tab1, instance2, col1, FILTER, GREATER_THAN)
// because the table schema `xdb_mzait` is lexicographically ordered before
// `xdb_ritwik`.
bool ColumnUsageInfo::operator<(const ColumnUsageInfo& other) const {
  if (table_schema.compare(other.table_schema) < 0) {
    return true;
  } else if (table_schema.compare(other.table_schema) == 0) {
    if (table_name.compare(other.table_name) < 0) {
      return true;
    } else if (table_name.compare(other.table_name) == 0) {
      if (table_instance.compare(other.table_instance) < 0) {
        return true;
      } else if (table_instance.compare(other.table_instance) == 0) {
        if (column_name.compare(other.column_name) < 0) {
          return true;
        } else if (column_name.compare(other.column_name) == 0) {
          if (sql_op < other.sql_op) {
            return true;
          } else if (sql_op == other.sql_op) {
            return op_type < other.op_type;
          }
        }
      }
    }
  }
  return false;
}

std::string sql_operation_string(const sql_operation& sql_op) {
  switch (sql_op) {
    case sql_operation::FILTER:
      return "FILTER";
    case sql_operation::TABLE_JOIN:
      return "TABLE_JOIN";
    case sql_operation::ORDER_BY:
      return "ORDER_BY";
    case sql_operation::GROUP_BY:
      return "GROUP_BY";
    default:
      // Asserting in debug mode since this code path implies that we've missed
      // some SQL operation.
      DBUG_ASSERT(true);
      return "";
  }
}

std::string operator_type_string(const operator_type& op_type) {
  switch (op_type) {
    case operator_type::BETWEEN:
      return "BETWEEN";
    case operator_type::EQUAL:
      return "EQUAL";
    case operator_type::NULLSAFE_EQUAL:
      return "NULLSAFE_EQUAL";
    case operator_type::LESS_THAN:
      return "LESS_THAN";
    case operator_type::LESS_THAN_EQUAL:
      return "LESS_THAN_EQUAL";
    case operator_type::NULL_CHECK:
      return "NULL_CHECK";
    case operator_type::GREATER_THAN:
      return "GREATER_THAN";
    case operator_type::GREATER_THAN_EQUAL:
      return "GREATER_THAN_EQUAL";
    case operator_type::NOT_EQUAL:
      return "NOT_EQUAL";
    case operator_type::SET_MEMBERSHIP:
      return "SET_MEMBERSHIP";
    case operator_type::PATTERN_MATCH:
      return "PATTERN_MATCH";
    case operator_type::SORT_ASCENDING:
      return "SORT_ASCENDING";
    case operator_type::SORT_DESCENDING:
      return "SORT_DESCENDING";
    case operator_type::UNKNOWN_OPERATOR:
    default:
      // Asserting in debug mode since this code path implies that we've missed
      // some operator.
      DBUG_ASSERT(true);
      return "UNKNOWN_OPERATOR";
  }
}

operator_type match_op(Item_func::Functype fitem_type) {
  switch (fitem_type) {
    case Item_func::BETWEEN:
      return operator_type::BETWEEN;
    case Item_func::EQ_FUNC:
      return operator_type::EQUAL;
    case Item_func::EQUAL_FUNC:
      return operator_type::NULLSAFE_EQUAL;
    case Item_func::LIKE_FUNC:
      return operator_type::PATTERN_MATCH;
    case Item_func::LT_FUNC:
      return operator_type::LESS_THAN;
    case Item_func::LE_FUNC:
      return operator_type::LESS_THAN_EQUAL;
    case Item_func::ISNULL_FUNC:
    case Item_func::ISNOTNULL_FUNC:
      return operator_type::NULL_CHECK;
    case Item_func::GT_FUNC:
      return operator_type::GREATER_THAN;
    case Item_func::GE_FUNC:
      return operator_type::GREATER_THAN_EQUAL;
    case Item_func::NE_FUNC:
      return operator_type::NOT_EQUAL;
    default:
      // Asserting in debug mode since this code path implies that we've missed
      // some operator.
      DBUG_ASSERT(true);
      break;
  }
  return operator_type::UNKNOWN_OPERATOR;
}

operator_type match_op(ORDER::enum_order direction) {
  switch(direction) {
    case ORDER::ORDER_ASC:
      return operator_type::SORT_ASCENDING;
    case ORDER::ORDER_DESC:
      return operator_type::SORT_DESCENDING;
    case ORDER::ORDER_NOT_RELEVANT:
    default:
      return operator_type::UNKNOWN_OPERATOR;
  }
}

void fetch_table_info(Item_field *field_arg, ColumnUsageInfo &cui) {
  DBUG_ENTER("fetch_table_info");
  DBUG_ASSERT(field_arg);

  // Setting default values in case of derived / temp tables.
  cui.table_name = "";
  cui.table_instance = "";

  // `orig_table` and associated parameters maybe absent for
  // derived / temp tables.
  if (field_arg->field && field_arg->field->orig_table &&
      field_arg->field->orig_table->pos_in_table_list) {
    TABLE_LIST *tl = field_arg->field->orig_table->pos_in_table_list;
    const char* tn = tl->get_table_name();
    if (tn) {
      cui.table_name = tn;
    }

    // Table instance in column statistics is just a string representation
    // of the TABLE_LIST object corresponding to the table instance. We could
    // have used an integer to represent it but the order is immaterial.
    // The only property that matters is distinctness of the following tuple.
    // sql_id, table_schema, table_name, table_instance
    std::stringstream tl_addr;
    tl_addr << (void const *)tl;
    cui.table_instance = tl_addr.str();
  }

  DBUG_VOID_RETURN;
}

bool fill_column_usage_struct(const sql_operation &op,
                              const operator_type &op_type,
                              Item_field *field_arg, ColumnUsageInfo &cui) {
  // Helper function to fill out the column usage information for one
  // Item_field object. Returns true if successfully resolved, false otherwise.
  DBUG_ENTER("fill_column_usage_struct");
  DBUG_ASSERT(field_arg);

  // We do not populate column usage information if we weren't able to resolve
  // the table_schema (database). This is particularly true for derived tables
  // where indexing cannot help.
  std::string db_name = (field_arg->db_name) ? field_arg->db_name : "";

  // It is essential that we have the table_instance populated because we
  // rely on in when distinguishing between predicates on the same table
  // instance versus different instances.
  fetch_table_info(field_arg, cui);

  if (db_name == "") {
    DBUG_RETURN(false);
  }

  cui.sql_op = op;
  cui.op_type = op_type;
  cui.table_schema = db_name;
  cui.column_name = (field_arg->field_name) ? field_arg->field_name : "";

  // This condition should never trigger because for non-base tables, db_name
  // would be empty as well. However, adding this check to be absolutely
  // certain.
  if (cui.table_name == "") {
    DBUG_ASSERT(false);
    DBUG_RETURN(false);
  }

  DBUG_RETURN(true);
}

void populate_field_info(const sql_operation &op, const operator_type &op_type,
                         Item_field *field_arg,
                         std::set<ColumnUsageInfo> &out_cus) {
  // This is a helper to parse column usage information for one Item_field.
  // It relies on the prepare stage to resolve column names to their respective
  // tables and database. Derived and temporary tables do NOT have db_name.

  DBUG_ENTER("populate_field_info");
  DBUG_ASSERT(field_arg);

  ColumnUsageInfo cui;
  if (fill_column_usage_struct(op, op_type, field_arg, cui)) {
    out_cus.insert(cui);
  }

  DBUG_VOID_RETURN;
}

void populate_fields_info(const sql_operation &op, const operator_type &op_type,
                          std::vector<Item_field *> &field_args,
                          std::set<ColumnUsageInfo> &out_cus) {
  // This is a helper to parse column usage information for Item_field objects
  // from the same functional item. This comes in handy when we want to detect
  // joins and predicates where indexing would NOT help in actually filtering
  // out a subset of the rows.
  // For eg. SELECT col1 FROM tbl1 WHERE col1 = col2;
  // It relies on the prepare stage to resolve column names to their respective
  // tables and database. Derived and temporary tables do NOT have db_name.

  DBUG_ENTER("populate_fields_info");

  if (field_args.empty()) {
    DBUG_VOID_RETURN;
  } else if (field_args.size() == 1) {
    populate_field_info(op, op_type, field_args[0], out_cus);
    DBUG_VOID_RETURN;
  }

  std::vector<ColumnUsageInfo> cuis;
  std::set<std::string> table_instances;
  for (uint32_t idx = 0; idx < field_args.size(); idx++) {
    ColumnUsageInfo cui;
    if (fill_column_usage_struct(op, op_type, field_args[idx], cui)) {
      cuis.push_back(cui);
    }

    // We populate table instance regardless of whether it's empty or not.
    // This is done because joins can happen between base tables and derived /
    // temp tables for which there would be no valid ColumnUsageInfo but
    // table_instance can be used to determine if a predicate corresponds to a
    // join or just an expression containing multiple columns of the same table
    // instance.
    table_instances.insert(cui.table_instance);
  }

  // Override SQL operation when multiple table instances are detected.
  // This is true in the case of multi-table joins and self joins.
  // JOIN: SELECT ... FROM tbl1, tbl2 WHERE tbl1.col1 = tbl2.col2 ...
  // Self Join: SELECT ... FROM tbl1 t1, tbl1 t2 WHERE t1.col1 = t2.col2 AND...
  if (table_instances.size() > 1) {
    for (ColumnUsageInfo &cui : cuis) {
      cui.sql_op = sql_operation::TABLE_JOIN;
    }
  }
  // TODO(ritwikyadav): Handle cases where the predicate is on two columns of
  // the same table instance when adding support for covering indexes with
  // projections.
  // For eg. SELECT col1 FROM tbl1 WHERE col1 = col2
  // We return in this case because adding an index on either col1 or col2
  // not help with the filtration. It might be beneficial to add a covering
  // index on (col1, col2) though. Therefore, it would be covered when adding
  // support for projection list.
  if (table_instances.size() == 1) {
    DBUG_VOID_RETURN;
  }

  out_cus.insert(cuis.begin(), cuis.end());

  DBUG_VOID_RETURN;
}

int parse_column_from_func_item(
    sql_operation op, Item_func *fitem, std::set<ColumnUsageInfo>& out_cus) {
  DBUG_ENTER("parse_column_from_func_item");
  DBUG_ASSERT(fitem);

  const auto type = fitem->functype();

  switch (type) {
    case Item_func::IN_FUNC:
    {
      const auto args = fitem->arguments();

      // If a field item is present in the item function, it has to be the
      // first arg in the IN operator.
      if (args[0]->type() == Item::FIELD_ITEM)
      {
        Item_field *field_arg = static_cast<Item_field *>(args[0]);

        // Populating ColumnUsageInfo struct
        populate_field_info(
            op, operator_type::SET_MEMBERSHIP, field_arg, out_cus);
      }
      break;
    }
    case Item_func::BETWEEN:
    case Item_func::EQ_FUNC:
    case Item_func::EQUAL_FUNC:
    case Item_func::ISNULL_FUNC:
    case Item_func::ISNOTNULL_FUNC:
    case Item_func::LT_FUNC:
    case Item_func::LE_FUNC:
    case Item_func::GE_FUNC:
    case Item_func::GT_FUNC:
    case Item_func::NE_FUNC:
    {
      const auto args = fitem->arguments();

      // Populating ColumnUsageInfo structs
      std::vector<Item_field *> field_args;
      for (uint i = 0; i < fitem->argument_count(); i++) {
        if (args[i]->type() == Item::FIELD_ITEM)
        {
          Item_field *field_arg = static_cast<Item_field *>(args[i]);
          field_args.push_back(field_arg);
        }
      }
      populate_fields_info(op, match_op(type), field_args, out_cus);

      break;
    }
    case Item_func::LIKE_FUNC:
    {
      DBUG_ASSERT(fitem->argument_count() == 2);
      const auto args = fitem->arguments();

      // Populating ColumnUsageInfo structs
      // Clause col1 LIKE 'pr%' uses an index but 'prn' LIKE col1 doesn't.
      if (fitem->argument_count() == 2 && args[0]->type() == Item::FIELD_ITEM &&
          args[1]->type() == Item::STRING_ITEM) {
        Item_field *field_arg = static_cast<Item_field *>(args[0]);
        Item_string *string_arg = static_cast<Item_string *>(args[1]);

        // The passed in argument isn't really used but following the
        // convention seen elsewhere. The lifetime semantics of String objects
        // seem dangerous for anything non-trivial.
        // More details in sql_string.h
        String tmp1, *tmp2;
        tmp2 = string_arg->val_str(&tmp1);
        if (!tmp2->is_empty() && tmp2->ptr()[0] != '%' &&
            tmp2->ptr()[0] != '_') {
          populate_field_info(op, match_op(type), field_arg, out_cus);
        }
      }

      break;
    }
    default:
    {
      // Asserting in debug mode in case we reach this codepath which signifies
      // that we've missed some function type.
      DBUG_ASSERT(true);
      break;
    }
  }
  DBUG_RETURN(0);
}

int parse_column_from_cond_item(
    sql_operation op, Item_cond *citem, std::set<ColumnUsageInfo>& out_cus,
    int recursion_depth) {
  DBUG_ENTER("parse_column_from_cond_item");
  DBUG_ASSERT(citem);

  switch (citem->functype())
  {
    case Item_func::COND_AND_FUNC:
    case Item_func::COND_OR_FUNC:
    {
      List<Item> *arg_list= citem->argument_list();
      Item *item;
      List_iterator_fast<Item> li(*arg_list);
      while ((item = li++)) {
        parse_column_from_item(
            op, item, out_cus, recursion_depth + 1);
      }
      break;
    }
    default:
    {
      // Asserting in debug mode in case we reach this codepath which signifies
      // that we've missed some conditional type.
      DBUG_ASSERT(true);
      break;
    }
  }
  DBUG_RETURN(0);
}

int parse_column_from_item(
    sql_operation op, Item *item, std::set<ColumnUsageInfo>& out_cus,
    int recursion_depth) {
  DBUG_ENTER("parse_column_from_item");
  DBUG_ASSERT(item);

  // This is just a sanity check to detect infinite recursion. We will remove it
  // once the code matures. There is no scientific reason behind choosing 15
  // as a limit; it should be high enough for programmatically generated
  // queries.
  if (recursion_depth >= 15) {
    // Killing the server only when debugger is attached. In production, we will
    // just return.
    DBUG_EXECUTE_IF(
        "crash_excessive_recursion_column_stats", DBUG_SUICIDE();
    );
    DBUG_RETURN(-1);
  }

  switch (item->type())
  {
    case Item::COND_ITEM:
    {
      Item_cond *citem = static_cast<Item_cond *>(item);
      parse_column_from_cond_item(op, citem, out_cus, recursion_depth);
      break;
    }
    case Item::FUNC_ITEM:
    {
      Item_func* fitem= static_cast<Item_func *>(item);
      parse_column_from_func_item(op, fitem, out_cus);
      break;
    }
    case Item::SUBSELECT_ITEM:
    {
      /**
       * The case of SUBSELECT_ITEM is special. Simple usage of the
       * IN operator results in a FUNC_ITEM which is an instatiation of
       * IN_FUNC. Example of simple usage is as follows.
       *   SELECT * FROM tbl1 WHERE col1 IN (2, 5, 7)
       * However, for cases where a subselect is involved like,
       *   SELECT * FROM tbl1 WHERE col1 IN (SELECT col2 FROM tbl2)
       * the parser treats it slightly differently and it results in an item
       * of type SUBSELECT_ITEM. Since we are only interested in parsing
       * columns from the IN operator, we only consider SUBSELECT_ITEM of
       * subtype IN_SUBS. Only the left expression for it is parsed, provided
       * it's a field item.
       * MySQL treats this differently for non-select statements
       * (like UPDATE, DELETE etc.) wherein the prepare stage replaces
       * Item_subselect by an UNKNOWN_FUNC item. More details:
       * 1. bug#17766653
       * 2. https://fburl.com/8qnjbage
       */
      Item_subselect* ssitem= static_cast<Item_subselect*>(item);
      if (ssitem->substype() == Item_subselect::IN_SUBS) {
        // Only parse subselects of type IN_SUBS which denotes usage of IN
        // operator.
        Item_in_subselect* in_predicate=
            static_cast<Item_in_subselect *>(ssitem);
        // The left expression if only parse if it is a single field and not a
        // complex expression.
        if (in_predicate->left_expr &&
            in_predicate->left_expr->type() == Item::FIELD_ITEM) {
          Item_field *field_arg= static_cast<Item_field *>(
              in_predicate->left_expr);
          populate_field_info(
              op, operator_type::SET_MEMBERSHIP, field_arg, out_cus);
        }
      }
      break;
    }
    default:
      break;
  }
  DBUG_RETURN(0);
}

int parse_columns_from_order_list(
    sql_operation op, ORDER* first_col, std::set<ColumnUsageInfo>& out_cus) {
  DBUG_ENTER("parse_columns_from_order_list");

  // Return early if the column is null.
  if (!first_col) {
    DBUG_RETURN(0);
  }

  // Iterate over the linked list of columns.
  for (ORDER* order_obj = first_col; order_obj && order_obj->item;
      order_obj = order_obj->next) {

    // Skip if there is no item corresponding to the column or if the
    // item type is not FIELD_ITEM (representing a FIELD).
    if (*(order_obj->item) == nullptr ||
        (*(order_obj->item))->type() != Item::FIELD_ITEM) {
      continue;
    }

    Item_field *field_arg = static_cast<Item_field *>(*(order_obj->item));

    // Populating ColumnUsageInfo struct
    populate_field_info(
        op, match_op(order_obj->direction), field_arg, out_cus);
  }
  DBUG_RETURN(0);
}

int parse_column_usage_info(THD *thd) {
  DBUG_ENTER("parse_column_usage_info");
  DBUG_ASSERT(thd);

  // Return early without doing anything if
  // 1. COLUMN_STATS switch is not ON
  // 2. The SQL_ID was already processed
  if (column_stats_control != SQL_INFO_CONTROL_ON ||
      exists_column_usage_info(thd))
  {
    DBUG_RETURN(0);
  }

  thd->column_usage_info.clear();

  LEX *lex= thd->lex;

  // List of supported commands for the collection of COLUMN_STATISTICS.
  static const std::set<enum_sql_command> supported_commands = {
    SQLCOM_SELECT,
    SQLCOM_UPDATE,
    SQLCOM_DELETE,
    SQLCOM_INSERT_SELECT,
    SQLCOM_REPLACE_SELECT
  };

  // Check statement type - only commands featuring SIMPLE SELECTs.
  if (supported_commands.find(lex->sql_command) == supported_commands.end()) {
    DBUG_RETURN(0);
  }

  // There is a SELECT_LEX for each SELECT statement in a query. We iterate
  // over all of them and process them sequentially, extracting column usage
  // information from each such select / subselect.
  for (SELECT_LEX *select_lex= lex->all_selects_list;
      select_lex;
      select_lex= select_lex->next_select_in_list())
  {
    if (select_lex->where != nullptr) {
      // Parsing column statistics from the WHERE clause.
      parse_column_from_item(
          sql_operation::FILTER, select_lex->where, thd->column_usage_info,
          0 /* recursion_depth */);
    }

    // Parsing column statistics from the ON clause in joins.
    for (TABLE_LIST *table= select_lex->leaf_tables;
        table;
        table= table->next_leaf)
    {
      TABLE_LIST *embedded; /* The table at the current level of nesting. */
      TABLE_LIST *embedding= table; /* The parent nested table reference. */
      do
      {
        embedded= embedding;
        if (embedded->join_cond())
        {
          parse_column_from_item(
              sql_operation::TABLE_JOIN, embedded->join_cond(),
              thd->column_usage_info, 0 /* recursion_depth */);
        }
        embedding= embedded->embedding;
      }
      while (embedding &&
            embedding->nested_join->join_list.head() == embedded);
    }

    // Parsing column statistics from the GROUP BY clause.
    parse_columns_from_order_list(
        sql_operation::GROUP_BY, select_lex->group_list.first,
        thd->column_usage_info);

    // Parsing column statistics from the ORDER BY clause.
    parse_columns_from_order_list(
        sql_operation::ORDER_BY, select_lex->order_list.first,
        thd->column_usage_info);
  }
  DBUG_RETURN(0);
}

void populate_column_usage_info(THD *thd) {
  DBUG_ENTER("populate_column_usage_info");
  DBUG_ASSERT(thd);

  // If the transaction wasn't successful, return.
  // Column usage statistics are not updated in this case.
  if (thd->is_error() || (thd->variables.option_bits & OPTION_MASTER_SQL_ERROR))
  {
    thd->column_usage_info.clear();
    DBUG_VOID_RETURN;
  }

  /* Return early if any of the following true
    - the column usage information is empty
    - SQL_ID is not set
  */
  if (thd->column_usage_info.empty() || !thd->mt_key_is_set(THD::SQL_ID))
  {
    thd->column_usage_info.clear();
    DBUG_VOID_RETURN;
  }

  mysql_rwlock_wrlock(&LOCK_column_statistics);
  auto iter= col_statistics_map.find(thd->mt_key_value(THD::SQL_ID));
  if (iter == col_statistics_map.end())
  {
    col_statistics_map.insert(
        std::make_pair(thd->mt_key_value(THD::SQL_ID), thd->column_usage_info));
  }
  mysql_rwlock_unlock(&LOCK_column_statistics);
  thd->column_usage_info.clear();
  DBUG_VOID_RETURN;
}

bool exists_column_usage_info(THD *thd) {
  DBUG_ENTER("exists_column_usage_info");
  DBUG_ASSERT(thd);

  // return now if the SQL ID was not set
  if (!thd->mt_key_is_set(THD::SQL_ID)) {
    DBUG_RETURN(true);
  }

  mysql_rwlock_rdlock(&LOCK_column_statistics);
  bool exists =
      (col_statistics_map.find(thd->mt_key_value(THD::SQL_ID)) ==
          col_statistics_map.end()) ? false : true;
  mysql_rwlock_unlock(&LOCK_column_statistics);

  DBUG_RETURN(exists);
}

int fill_column_statistics(THD *thd, TABLE_LIST *tables, Item *cond)
{
  DBUG_ENTER("fill_column_statistics");
  TABLE* table= tables->table;

  mysql_rwlock_rdlock(&LOCK_column_statistics);
  for (auto iter= col_statistics_map.cbegin();
      iter != col_statistics_map.cend(); ++iter)
  {
    for (const ColumnUsageInfo& cui : iter->second)
    {
      int f= 0;

      // SQL ID
      char sql_id_hex_string[MD5_BUFF_LENGTH];
      array_to_hex(sql_id_hex_string, iter->first.data(), iter->first.size());

      table->field[f++]->store(sql_id_hex_string, MD5_BUFF_LENGTH,
                               system_charset_info);
     // TABLE SCHEMA
     table->field[f++]->store(
         cui.table_schema.c_str(), cui.table_schema.size(),
         system_charset_info);

      // TABLE_NAME
      table->field[f++]->store(
          cui.table_name.c_str(), cui.table_name.size(), system_charset_info);

      // TABLE_INSTANCE
      table->field[f++]->store(
          cui.table_instance.c_str(), cui.table_instance.size(),
          system_charset_info);

      // COLUMN_NAME
      table->field[f++]->store(
          cui.column_name.c_str(), cui.column_name.size(), system_charset_info);

      // SQL_OPERATION
      std::string sql_op= sql_operation_string(cui.sql_op);
      table->field[f++]->store(
          sql_op.c_str(), sql_op.size(), system_charset_info);

      // OPERATOR_TYPE
      std::string op_type= operator_type_string(cui.op_type);
      table->field[f++]->store(
          op_type.c_str(), op_type.size(), system_charset_info);

      schema_table_store_record(thd, table);
    }
  }
  mysql_rwlock_unlock(&LOCK_column_statistics);

  DBUG_RETURN(0);
}

void free_column_stats()
{
  mysql_rwlock_wrlock(&LOCK_column_statistics);
  col_statistics_map.clear();
  mysql_rwlock_unlock(&LOCK_column_statistics);
}
