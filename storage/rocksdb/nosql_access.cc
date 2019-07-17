/*
   Copyright (c) 2019, Facebook, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#define MYSQL_SERVER 1

/* This C++ file's header */
#include "./nosql_access.h"

/* C++ standard header files */
#include <algorithm>
#include <array>
#include <limits>
#include <string>
#include <utility>
#include <vector>

/* C standard header files */
#include <ctype.h>

/* MySQL header files */
#include "./sql/item.h"
#include "./sql/item_func.h"
#include "./sql/query_result.h"
#include "./sql/sql_base.h"
#include "./sql/sql_select.h"
#include "./sql/strfunc.h"
#include "mysql/services.h"
#include "sql/sql_lex.h"

/* MyRocks header files */
#include "./ha_rocksdb.h"
#include "./ha_rocksdb_proto.h"
#include "./rdb_buff.h"
#include "./rdb_converter.h"
#include "./rdb_datadic.h"

static const size_t MAX_NOSQL_FIELD_COUNT = 16;
static const size_t MAX_NOSQL_COND_COUNT = 16;

// A soft max for key writers used for initial vector allocation and
// for stack allocation
__attribute__((unused)) static const size_t KEY_WRITER_DEFAULT_SIZE = 16;

namespace myrocks {

/* We only support simple equal / comparison functions */
bool inline is_supported_item_func(Item_func::Functype type) {
  // TODO(yzha) - Support EQUAL_FUNC <=>
  // TODO(yzha) - Convert into a lookup table
  if (type != Item_func::EQ_FUNC && type != Item_func::IN_FUNC &&
      type != Item_func::LT_FUNC && type != Item_func::LE_FUNC &&
      type != Item_func::GE_FUNC && type != Item_func::GT_FUNC) {
    return false;
  }

  return true;
}

namespace {

/*
  Represents single conditional expression in SELECT WHERE
  Example: id1 > 100
 */
struct sql_cond {
  Item_func::Functype op_type;  // the operator, such as >
  Field *field;                 // field
  Item *cond_item;              // item
  Item *val_item;               // item containing the value

  sql_cond(Item_func::Functype _op_type, Field *_field, Item *_cond_item,
           Item *_val_item)
      : op_type(_op_type),
        field(_field),
        cond_item(_cond_item),
        val_item(_val_item) {}
  sql_cond() = default;
};

class base_select_parser {
 public:
  base_select_parser(THD *thd, Query_block *select_lex)
      : m_thd(thd), m_select_lex(select_lex) {}
  virtual ~base_select_parser() = default;

  THD *get_thd() const { return m_thd; }
  TABLE *get_table() const { return m_table; }
  Query_block *get_select_lex() const { return m_select_lex; }
  uint get_index() const { return m_index; }
  bool is_order_desc() const { return m_is_order_desc; }
  const std::vector<Field *> &get_field_list() const { return m_field_list; }
  const sql_cond *get_cond_list() const { return m_cond_list; }
  uint get_cond_count() const { return m_cond_count; }
  uint64_t get_select_limit() const { return m_select_limit; }
  uint64_t get_offset_limit() const { return m_offset_limit; }

 protected:
  THD *m_thd;
  TABLE *m_table;
  Table_ref *m_table_list;
  Query_block *m_select_lex;
  uint m_index;
  bool m_is_order_desc;
  std::vector<Field *> m_field_list;
  sql_cond m_cond_list[MAX_NOSQL_COND_COUNT];
  uint m_field_count = 0;
  uint m_cond_count = 0;
  uint64_t m_select_limit;
  uint64_t m_offset_limit;
  bool m_use_unoptimized_field_type;
};

/*
  Extract necessary information from SELECT statements
 */
class sql_select_parser : public base_select_parser {
 public:
  sql_select_parser(THD *thd, Query_block *select_lex)
      : base_select_parser(thd, select_lex) {
    // Single table only
    assert(select_lex->m_table_list.elements == 1);
    m_table_list = select_lex->m_table_list.first;
    m_table = m_table_list->table;
    assert(ha_legacy_type(m_table->s->db_type()) == DB_TYPE_ROCKSDB);
    m_error_msg = "UNKNOWN";
    m_use_unoptimized_field_type = false;
  }

  bool INLINE_ATTR parse() {
    // No HAVING clause
    if (m_table_list->lock_descriptor().type > TL_READ) {
      m_error_msg = "Only SELECT with default READ lock is supported";
      return true;
    }

    // No HAVING clause
    if (m_select_lex->having_cond() != nullptr) {
      m_error_msg = "HAVING not supported";
      return true;
    }

    // No GROUP BY clauses
    if (m_select_lex->group_list.elements > 0) {
      m_error_msg = "GROUP BY not supported";
      return true;
    }

    // No SELECT options such as SELECT_DISTINCT except for comments
    // See sql_priv.h for a list
    if (m_select_lex->active_options() & SELECT_DISTINCT) {
      m_error_msg = "SELECT options not supported (such as SELECT DISTINCT)";
      return true;
    }

    // INTO OUTFILE
    // DUMP OUTFILE
    if (m_thd->lex->result && m_thd->lex->result->needs_file_privilege()) {
      m_error_msg = "SELECT INTO/DUMP not supported";
      return true;
    }

    // @TODO - PROCEDURE
    // NOTE: These have side effects and their orders are important
    if (parse_index() || parse_fields() || parse_order_by() || parse_where() ||
        parse_limit()) {
      return true;
    }

    return false;
  }

  /*
    Dump out the parsed contents of the SELECT statement
    Typically used in debug only
   */
  std::string dump() {
    std::string str;
    str += "Index=" + std::to_string(m_index);
    str += ", ";
    str += "Order=";
    str += (m_is_order_desc ? "DESC" : "ASC");
    str += ", ";
    str += "Fields={ ";

    for (uint i = 0; i < m_field_count; ++i) {
      auto field = m_field_list[i];
      if (i) {
        str += ", ";
      }
      str += "(Name=";
      str += field->field_name;
      str += ", Index=";
      str += std::to_string(field->field_index());
      str += ")";
    }

    str += " }, Cond={ ";

    for (uint i = 0; i < m_cond_count; ++i) {
      auto &cond = m_cond_list[i];
      if (i) {
        str += ", ";
      }
      str += "(";
      str += cond.field->field_name;
      str += " ";
      switch (cond.op_type) {
        case Item_func::EQ_FUNC: {
          str += "==";
          break;
        }
        case Item_func::LT_FUNC: {
          str += "<";
          break;
        }
        case Item_func::GT_FUNC: {
          str += ">";
          break;
        }
        case Item_func::LE_FUNC: {
          str += "<=";
          break;
        }
        case Item_func::GE_FUNC: {
          str += ">=";
          break;
        }
        case Item_func::IN_FUNC: {
          str += "IN";
          break;
        }
        default:
          str += "?";
      }
      str += " ";
      if (cond.op_type == Item_func::IN_FUNC) {
        str += "(";
        auto in_func = static_cast<Item_func_in *>(cond.cond_item);
        auto args = in_func->arguments();
        for (uint j = 1; j < in_func->argument_count(); ++j) {
          if (j > 1) {
            str += ",";
          }
          String storage;
          String *ret = args[j]->val_str(&storage);
          if (ret == nullptr) {
            str += "NULL";
          } else {
            str += ret->c_ptr_safe();
          }
        }
        str += ")";
      } else {
        String storage;
        String *ret = cond.val_item->val_str(&storage);
        if (ret == nullptr) {
          str += "NULL";
        } else {
          str += ret->c_ptr_safe();
        }
      }
      str += ")";
    }
    str += " }";

    return str;
  }

  const char *get_error_msg() const { return m_error_msg; }

 private:
  const char *m_error_msg;

  // Buffer to store snprintf-ed error messages
  char m_error_msg_buf[FN_REFLEN];

 private:
  bool parse_index() {
    if (m_table_list->index_hints != nullptr) {
      if (m_table_list->index_hints->elements == 1) {
        // Must be a FORCE INDEX
        Index_hint *hint = m_table_list->index_hints->head();
        if (hint->type != INDEX_HINT_FORCE) {
          m_error_msg = "Index hint must be FORCE INDEX";
          return true;
        }
        uint pos = find_type(&m_table->s->keynames, hint->key_name.str,
                             hint->key_name.length, true);
        if (!pos) {
          // Unrecognized index
          snprintf(m_error_msg_buf, sizeof(m_error_msg_buf) / sizeof(char),
                   "Unrecognized index: '%s'", hint->key_name.str);
          m_error_msg = m_error_msg_buf;
          return true;
        }
        m_index = pos - 1;
      } else {
        // Have to be at most 1 index hints
        m_error_msg = "More than 1 index hints unsupported";
        return true;
      }

    } else {
      // Default to PRIMARY
      m_index = m_table->s->primary_key;
    }

    return false;
  }

  bool parse_fields() {
    // All item must be fields
    for (Item *item : m_select_lex->visible_fields()) {
      auto type = item->type();
      if (type != Item::FIELD_ITEM) {
        m_error_msg = "SELECT expressions can only be field";
        return true;
      }

      Item_field *field_item = static_cast<Item_field *>(item);
      auto name = field_item->field_name;

      // At this point we only know field name and need to resolve the
      // field ourselves (under normal circumstances MySQL does it for us)
      uint field_index = field_item->field_index;

      Field *field = find_field_in_table(m_table, name, false, &field_index);
      if (!field) {
        snprintf(m_error_msg_buf, sizeof(m_error_msg_buf) / sizeof(char),
                 "Unrecognized field name: '%s'", name);
        m_error_msg = m_error_msg_buf;
        return true;
      }

      auto field_type = field->real_type();
      if (field_type == MYSQL_TYPE_VARCHAR &&
          field->charset() != &my_charset_utf8mb3_bin &&
          field->charset() != &my_charset_latin1_bin) {
        m_error_msg =
            "only utf8_bin, latin1_bin is supported for varchar field";
        return true;
      }

      if (!m_use_unoptimized_field_type) {
        switch (field_type) {
          case MYSQL_TYPE_LONGLONG:
          case MYSQL_TYPE_LONG:
          case MYSQL_TYPE_INT24:
          case MYSQL_TYPE_SHORT:
          case MYSQL_TYPE_TINY:
          case MYSQL_TYPE_VARCHAR:
          case MYSQL_TYPE_BLOB:
          case MYSQL_TYPE_STRING:
            break;
          default: {
            m_use_unoptimized_field_type = true;
          }
        }
      }

      // This is needed so that we can use protocol->send_items and
      // item->send
      field_item->set_field(field);

      if (m_field_count >= MAX_NOSQL_FIELD_COUNT) {
        m_error_msg = "Too many SELECT expressions";
        return true;
      }

      m_field_list[m_field_count++] = field;
    }

    return false;
  }

  bool check_field_name_match(Field *field, const char *field_name) {
    return (field->field_name &&
            !my_strcasecmp(system_charset_info, field->field_name, field_name));
  }

  bool parse_order_by() {
    // Extract order ascending vs descending from the order by list
    if (m_select_lex->order_list.elements == 0) {
      m_is_order_desc = false;
      return false;
    }

    auto order = m_select_lex->order_list.first;
    bool is_first = true;
    KEY *key = &m_table->key_info[m_index];
    uint cur_index = key->actual_key_parts;
    for (uint i = 0; i < m_select_lex->order_list.elements; ++i) {
      Item *item = *(order->item);
      if (item->type() != Item::FIELD_ITEM) {
        m_error_msg = "ORDER BY should be only using field";
        return true;
      }
      auto field_item = static_cast<Item_field *>(item);
      if (is_first) {
        m_is_order_desc = (order->direction == ORDER_DESC);
        is_first = false;

        // Find the first index
        for (uint j = 0; j < key->actual_key_parts; ++j) {
          if (check_field_name_match(key->key_part[j].field,
                                     field_item->field_name)) {
            cur_index = j;
            break;
          }
        }

        if (cur_index >= key->actual_key_parts) {
          m_error_msg = "ORDER BY field doesn't belong to the index";
          return true;
        }
      } else {
        if (m_is_order_desc != (order->direction == ORDER_DESC)) {
          // Found a different order
          m_error_msg = "ORDER BY should be either ascending or descending";
          return true;
        }

        cur_index++;
        if (cur_index >= key->actual_key_parts) {
          m_error_msg = "ORDER BY field is not in index order";
          return true;
        }

        if (!check_field_name_match(key->key_part[cur_index].field,
                                    field_item->field_name)) {
          m_error_msg = "ORDER BY is not in index order";
          return true;
        }
      }
      order = order->next;
    }

    return false;
  }

  bool inline is_constant_bool(Item *op_arg, Item::Type type) {
    if (type == Item::FUNC_ITEM) {
      auto func_item = static_cast<Item_func *>(op_arg);
      if (func_item->basic_const_item()) {
        // Must be Item_func_true / Item_func_false
        return true;
      }
    }
    return false;
  }

  bool inline is_supported_op_arg(Item *op_arg) {
    auto type = op_arg->type();
    return (type == Item::STRING_ITEM || type == Item::INT_ITEM ||
            type == Item::REAL_ITEM || type == Item::NULL_ITEM ||
            type == Item::VARBIN_ITEM || is_constant_bool(op_arg, type));
  }

  bool inline parse_cond(Item_func *func) {
    auto type = func->functype();
    if (!is_supported_item_func(type)) {
      m_error_msg = "Unsupported WHERE - needs to be >, >=, <, <=, =, IN";
      return true;
    }

    const auto args = func->arguments();
    Item_field *field_arg = nullptr;
    Item *op_arg = nullptr;

    if (type == Item_func::IN_FUNC) {
      // arg0 is always the field
      field_arg = static_cast<Item_field *>(args[0]);
      auto in_func = static_cast<Item_func_in *>(func);
      for (uint i = 1; i < in_func->argument_count(); ++i) {
        if (!is_supported_op_arg(args[i])) {
          // Make sure the field has supported type
          // Where as for values we convert them to the correct type just
          // like MySQL
          m_error_msg =
              "Unsupported WHERE - operand should be "
              "int/string/real/varbinary";
          return true;
        }
      }
    } else {
      // Extract the field and operand, such as A > 0
      if (args[0]->type() == Item::FIELD_ITEM) {
        field_arg = static_cast<Item_field *>(args[0]);
        op_arg = args[1];
      } else if (args[1]->type() == Item::FIELD_ITEM) {
        field_arg = static_cast<Item_field *>(args[1]);
        op_arg = args[0];
      } else {
        m_error_msg = "Unsupported WHERE - should only reference field";
        return true;
      }

      if (!is_supported_op_arg(op_arg)) {
        m_error_msg =
            "Unsupported WHERE - operand should be "
            "int/string/real/varbinary";
        return true;
      }
    }

    // Locate the field
    auto field_name = field_arg->field_name;
    uint field_index = field_arg->field_index;
    Field *found =
        find_field_in_table(m_table, field_name, false, &field_index);
    if (!found) {
      snprintf(m_error_msg_buf, sizeof(m_error_msg_buf) / sizeof(char),
               "Unrecognized field name: '%s'", field_name);
      m_error_msg = m_error_msg_buf;
      return true;
    }

    // TAO-specific optimizations to remove redundant time >= 0 and time <=
    // UINT32_MAX. Once TAO removes those unnecessary WHERE we can take
    // these out
    if (found->is_flag_set(UNSIGNED_FLAG) && found->type() == MYSQL_TYPE_LONG) {
      assert(op_arg != nullptr);
      if (type == Item_func::GE_FUNC && op_arg->val_int() == 0) {
        // unsigned A >= 0 - we can skip this one
        return false;
      }

      if (type == Item_func::LE_FUNC &&
          op_arg->val_uint() == std::numeric_limits<uint32_t>::max()) {
        // unsigned A <= UINT32_MAX - we can skip this one
        return false;
      }
    }

    // Let MySQL know about the field so that we can evaluate the conditional
    // expression later
    field_arg->set_field(found);

    if (m_cond_count >= MAX_NOSQL_COND_COUNT) {
      m_error_msg = "Too many WHERE expressions";
      return true;
    }

    if (type == Item_func::IN_FUNC) {
      // In the case of IN func, we use the actual Item_func_in as it contain
      // the list of items
      m_cond_list[m_cond_count++] = {type, found, func, nullptr};
    } else {
      assert(op_arg != nullptr);
      m_cond_list[m_cond_count++] = {type, found, func, op_arg};
    }

    return false;
  }

  const char *where_err_msg =
      "Unsupported WHERE: should be expr [(AND expr)*] where expr only "
      "contains >, >=, <, <=, =, IN";

  bool parse_where() {
    auto where = m_select_lex->where_cond();
    if (where == nullptr) {
      m_error_msg = where_err_msg;
      return true;
    }

    // We only allow pure conjunctive where clauses such as A=1 AND B=2
    auto where_type = where->type();
    if (where_type == Item::COND_ITEM) {
      Item_cond *where_cond = static_cast<Item_cond *>(where);
      if (where_cond->functype() != Item_func::COND_AND_FUNC) {
        m_error_msg = where_err_msg;
        return true;
      }

      Item_cond_and *and_cond = static_cast<Item_cond_and *>(where);
      List<Item> *and_list = and_cond->argument_list();
      Item *item;
      List_iterator_fast<Item> li(*and_list);
      while ((item = li++)) {
        if (item->type() != Item::FUNC_ITEM) {
          m_error_msg = where_err_msg;
          return true;
        }
        if (parse_cond(static_cast<Item_func *>(item))) {
          return true;
        }
      }
    } else if (where_type == Item::FUNC_ITEM) {
      if (parse_cond(static_cast<Item_func *>(where))) {
        return true;
      }
    } else {
      m_error_msg = where_err_msg;
      return true;
    }

    return false;
  }

  bool parse_limit() {
    // NOTE: We can't rely on explicit_limit as execute_sqlcom_select may
    // assign one using the global parameters
    if (m_select_lex->select_limit) {
      if (m_select_lex->select_limit->type() != Item::INT_ITEM ||
          !m_select_lex->select_limit->unsigned_flag) {
        m_error_msg = "Unexpected LIMIT - must be unsigned integer";
        return true;
      }

      m_select_limit = m_select_lex->select_limit->val_uint();
    } else {
      m_select_limit = std::numeric_limits<uint64_t>::max();
    }

    if (m_select_lex->offset_limit) {
      if (m_select_lex->offset_limit->type() != Item::INT_ITEM ||
          !m_select_lex->offset_limit->unsigned_flag) {
        m_error_msg = "Unexpected LIMIT offset - must be unsigned integer";
        return true;
      }

      m_offset_limit = m_select_lex->offset_limit->val_uint();
    } else {
      m_offset_limit = 0;
    }

    return false;
  }
};

}  // namespace

bool inline is_bypass_on(Query_block *select_lex) {
  uint32_t policy = get_select_bypass_policy();

  if ((policy & SELECT_BYPASS_POLICY_DEFAULT_MASK) == 0) {
    // Force on/off ignoring statement level hint
    return (policy & SELECT_BYPASS_POLICY_ON_MASK);
  }

  if (select_lex->select_bypass_hint ==
      Query_block::SELECT_BYPASS_HINT_DEFAULT) {
    // Whether it is FORCE / DEFAULT mode, ON/OFF MASK gives the right answer
    // if no hint is given
    return (policy & SELECT_BYPASS_POLICY_ON_MASK);
  }

  return (select_lex->select_bypass_hint == Query_block::SELECT_BYPASS_HINT_ON);
}

bool rocksdb_handle_single_table_select(THD *thd, Query_block *select_lex) {
  // Checks for hint and policy
  if (!is_bypass_on(select_lex)) {
    // Fallback to MySQL
    return false;
  }

  // Parse the SELECT statement
  sql_select_parser select_stmt(thd, select_lex);
  if (select_stmt.parse()) {
    rocksdb_select_bypass_rejected++;

    if (should_log_rejected_select_bypass()) {
      // NO_LINT_DEBUG
      sql_print_information("[REJECTED_BYPASS_QUERY] Query='%s', Reason='%s'\n",
                            thd->query().str, select_stmt.get_error_msg());
    }

    // During parse you can just let unsupported scenario fallback to MySQL
    // implementation - but keep in mind it may regress performance
    // Default is TRUE - let unsupported SELECT scenario just fail
    if (should_fail_unsupported_select_bypass()) {
      const char *error_msg = select_stmt.get_error_msg();
      my_printf_error(ER_NOT_SUPPORTED_YET,
                      "SELECT statement pattern not supported: %s", MYF(0),
                      error_msg);
      return true;
    } else {
      return false;
    }
  }

  if (!thd->lex->is_query_tables_locked()) {
    /*
      If tables are not locked at this point, it means that we have delayed
      this step until after prepare stage (i.e. this moment). This allows to
      do better partition pruning and avoid locking unused partitions.
      As a consequence, in such a case, prepare stage can rely only on
      metadata about tables used and not data from them.
      We need to lock tables now in order to proceed with the remaning
      stages of query optimization and execution.
    */
    if (lock_tables(thd, thd->lex->query_tables, thd->lex->table_count, 0)) {
      assert(thd->is_error());
      return true;
    }
  }

  // Temporary code that dumps out everything before actual execution code
  // is fully ported
  // This generates a table with a single column "DUMP"
  auto protocol = thd->get_protocol();

  // Dump out the extracted string for diagnostic purpose
  mem_root_deque<Item *> field_list(thd->mem_root);
  field_list.push_back(new Item_empty_string("Dump", 65535));
  if (thd->send_result_metadata(field_list,
                                Protocol::SEND_NUM_ROWS | Protocol::SEND_EOF)) {
    return true;
  }

  protocol->start_row();
  std::string str = select_stmt.dump();
  if (protocol->store_string(str.c_str(), str.length(), system_charset_info)) {
    return true;
  }

  if (protocol->end_row()) {
    return true;
  }

  my_eof(thd);

  rocksdb_select_bypass_executed++;

  return true;
}

}  // namespace myrocks
