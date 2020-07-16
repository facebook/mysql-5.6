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
  {"COLUMN_NAME", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"SQL_OPERATION", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {"OPERATOR_TYPE", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};

// Mapping from SQL_ID to column usage information.
std::unordered_map<md5_key, std::set<ColumnUsageInfo> > col_statistics_map;

// Operator definition for strict weak ordering. Should be a function of all
// constituents of the struct.
bool ColumnUsageInfo::operator<(const ColumnUsageInfo& other) const {
  if (table_schema.compare(other.table_schema) < 0) {
    return true;
  } else if (table_schema.compare(other.table_schema) == 0) {
    if (table_name.compare(other.table_name) < 0) {
      return true;
    } else if (table_name.compare(other.table_name) == 0) {
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
  return false;
}

std::string sql_operation_string(const sql_operation& sql_op) {
  switch (sql_op) {
    case sql_operation::UPDATE:
      return "UPDATE";
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
    case operator_type::EQUAL:
      return "EQUAL";
    case operator_type::LESS_THAN:
      return "LESS_THAN";
    case operator_type::LESS_THAN_EQUAL:
      return "LESS_THAN_EQUAL";
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
    case Item_func::EQ_FUNC:
      return operator_type::EQUAL;
    case Item_func::LT_FUNC:
      return operator_type::LESS_THAN;
    case Item_func::LE_FUNC:
      return operator_type::LESS_THAN_EQUAL;
    case Item_func::GT_FUNC:
      return operator_type::GREATER_THAN;
    case Item_func::GE_FUNC:
      return operator_type::GREATER_THAN_EQUAL;
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

std::string parse_field_name(Item_field *field_arg)
{
  DBUG_ASSERT(field_arg);

  // This guard protects against predicates like
  // function(field_name) op literal
  if (field_arg->field_name == nullptr) {
    return "";
  }
  std::string fn(field_arg->field_name);
  return fn;
}

int parse_column_from_func_item(
    const std::string& db_name, const std::string& table_name, Item_func *fitem,
    std::set<ColumnUsageInfo>& out_cus) {
  DBUG_ENTER("parse_column_from_func_item");
  DBUG_ASSERT(fitem);

  const auto type = fitem->functype();

  switch (type) {
    case Item_func::IN_FUNC:
    {
      const auto args = fitem->arguments();

      // If a field item is present in the item function, it has to be the first
      // arg in the IN operator.
      if (args[0]->type() == Item::FIELD_ITEM)
      {
        Item_field *field_arg = static_cast<Item_field *>(args[0]);

        // Populating ColumnUsageInfo struct
        ColumnUsageInfo info;
        info.table_schema= db_name;
        info.table_name= table_name;
        info.sql_op= sql_operation::FILTER;
        info.op_type= operator_type::SET_MEMBERSHIP;
        info.column_name= parse_field_name(field_arg);

        out_cus.insert(info);
      }
      break;
    }
    case Item_func::EQ_FUNC:
    case Item_func::LT_FUNC:
    case Item_func::LE_FUNC:
    case Item_func::GE_FUNC:
    case Item_func::GT_FUNC:
    {
      const auto args = fitem->arguments();

      int field_index= 0;
      if (args[0]->type() == Item::FIELD_ITEM)
      {
        field_index= 0;
      } else if (args[1]->type() == Item::FIELD_ITEM)
      {
        field_index= 1;
      } else
      {
        field_index= -1;
      }

      if (field_index >= 0)
      {
        Item_field *field_arg = static_cast<Item_field *>(args[field_index]);

        // Populating ColumnUsageInfo struct
        ColumnUsageInfo info;
        info.table_schema= db_name;
        info.table_name= table_name;
        info.sql_op= sql_operation::FILTER;
        info.op_type= match_op(type);
        info.column_name= parse_field_name(field_arg);

        out_cus.insert(info);
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
    const std::string& db_name, const std::string& table_name, Item_cond *citem,
    std::set<ColumnUsageInfo>& out_cus, int recursion_depth) {
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
            db_name, table_name, item, out_cus, recursion_depth + 1);
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
    const std::string& db_name, const std::string& table_name, Item *item,
    std::set<ColumnUsageInfo>& out_cus, int recursion_depth) {
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
      parse_column_from_cond_item(
          db_name, table_name, citem, out_cus, recursion_depth);
      break;
    }
    case Item::FUNC_ITEM:
    {
      Item_func* fitem= static_cast<Item_func *>(item);
      parse_column_from_func_item(db_name, table_name, fitem, out_cus);
      break;
    }
    default:
      break;
  }
  DBUG_RETURN(0);
}

int parse_columns_from_order_list(
    const std::string& db_name, const std::string& table_name,
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
    ColumnUsageInfo info;
    info.table_schema= db_name;
    info.table_name= table_name;
    info.sql_op= op;
    info.op_type= match_op(order_obj->direction);
    info.column_name= parse_field_name(field_arg);

    out_cus.insert(info);
  }
  DBUG_RETURN(0);
}

int parse_column_usage_info(
    THD *thd, std::set<ColumnUsageInfo>& out_cus) {
  DBUG_ENTER("parse_column_usage_info");
  DBUG_ASSERT(thd);

  out_cus.clear();

  // `m_digest` is needed for computation of sql_id
  if (!thd->m_digest || thd->m_digest->m_digest_storage.is_empty())
    DBUG_RETURN(0);

  LEX *lex= thd->lex;
  SELECT_LEX *select_lex= &lex->select_lex;

  // List of supported commands for the collection of COLUMN_STATISTICS.
  static const std::set<enum_sql_command> supported_commands = {
    SQLCOM_SELECT,
    SQLCOM_UPDATE,
    SQLCOM_DELETE
  };

  // Check statement type - only commands featuring SIMPLE SELECTs.
  if (supported_commands.find(lex->sql_command) == supported_commands.end()) {
    DBUG_RETURN(0);
  }

  if (select_lex->type(thd) != st_select_lex::SLT_SIMPLE ||
      select_lex->table_list.elements != 1)
  {
    DBUG_RETURN(0);
  }

  TABLE_LIST *table_list= select_lex->table_list.first;

  // Since for simple select, we only have a single table, table resolution
  // is easier. For more complex queries, we might have to wait until the
  // field_name(s) are actually resolved.
  if (!table_list ||
      (table_list->db == nullptr) || (table_list->table_name == nullptr))
  {
    DBUG_RETURN(-1);
  }

  std::string db(table_list->db);
  std::string tn(table_list->table_name);

  if (select_lex->where == nullptr) {
    DBUG_RETURN(0);
  }

  // Parsing column statistics from the WHERE clause.
  parse_column_from_item(
      db, tn, select_lex->where, out_cus, 0 /* recursion_depth */);

  // Parsing column statistics from the GROUP BY clause.
  parse_columns_from_order_list(
      db, tn, sql_operation::GROUP_BY, select_lex->group_list.first, out_cus);

  // Parsing column statistics from the ORDER BY clause.
  parse_columns_from_order_list(
      db, tn, sql_operation::ORDER_BY, select_lex->order_list.first, out_cus);

  DBUG_RETURN(0);
}

void populate_column_usage_info(THD *thd, std::set<ColumnUsageInfo>& cus) {
  DBUG_ENTER("populate_column_usage_info");
  DBUG_ASSERT(thd);

  // If the transaction wasn't successful, return.
  // Column usage statistics are not updated in this case.
  if (thd->is_error() || (thd->variables.option_bits & OPTION_MASTER_SQL_ERROR))
  {
    DBUG_VOID_RETURN;
  }

  /* Return early if any of the following true
    - the column usage information is empty
    - SQL_ID is not set
  */
  if (cus.empty() || !thd->mt_key_is_set(THD::SQL_ID))
  {
    DBUG_VOID_RETURN;
  }

  mysql_rwlock_wrlock(&LOCK_column_statistics);
  auto iter= col_statistics_map.find(thd->mt_key_value(THD::SQL_ID));
  if (iter == col_statistics_map.end())
  {
    col_statistics_map.insert(
        std::make_pair(thd->mt_key_value(THD::SQL_ID), std::move(cus)));
  }
  mysql_rwlock_unlock(&LOCK_column_statistics);
  DBUG_VOID_RETURN;
}

bool exists_column_usage_info(THD *thd) {
  DBUG_ENTER("exists_column_usage_info");
  DBUG_ASSERT(thd);

  // return now if the SQL ID was not set
  if (!thd->mt_key_is_set(THD::SQL_ID))
    DBUG_RETURN(true);

  mysql_rwlock_rdlock(&LOCK_column_statistics);
  bool exists = (col_statistics_map.find(thd->mt_key_value(THD::SQL_ID))
                 == col_statistics_map.end() ? false : true);
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
