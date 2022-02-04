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
#include <memory>
#include <string>
#include <utility>
#include <vector>

/* C standard header files */
#include <ctype.h>

/* MySQL header files */
#include "./sql/item.h"
#include "./sql/item_func.h"
#include "./sql/query_result.h"
#include "./sql/rpc_plugin.h"
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
#include "./rdb_iterator.h"

static const size_t DEFAULT_FIELD_LIST_SIZE = 16;
static const size_t MAX_NOSQL_COND_COUNT = 16;

// A soft max for key writers used for initial vector allocation and
// for stack allocation
static const size_t KEY_WRITER_DEFAULT_SIZE = 16;

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

typedef enum {
  NONE = 0,
  BOOL = 1,
  UNSIGNED_INT = 2,
  DOUBLE = 3,
  STRING = 4
} nosql_cond_value_type;

struct nosql_cond_value {
  nosql_cond_value_type type;
  union {
    bool boolVal;
    uint64_t i64Val;
    double doubleVal;
    String *stringVal;
  };
};

/*
  base class covering both MySQL protocol and RPC protocol
 */
class base_protocol {
 public:
  virtual ~base_protocol() = default;
  // Do something only when we send data through buffer other than record[0].
  // We need to call this prior to start_row() because the buffer might be
  // used during the evaluation.
  virtual bool fill_buffer() const = 0;
  virtual bool send_result_metadata() const = 0;
  virtual bool send_row() const = 0;
};

class sql_protocol : public base_protocol {
 public:
  explicit sql_protocol(THD *thd, const mem_root_deque<Item *> &item_list)
      : m_thd(thd), m_protocol(thd->get_protocol()), m_item_list(item_list) {}
  bool fill_buffer() const override { return false; }
  bool send_row() const override {
    m_protocol->start_row();
    // This works because we read everything into record and all the items
    // are pointing into the record[0]
    // This is the slow path as we need to unpack everything into record[0]
    if (m_thd->send_result_set_row(m_item_list) || m_protocol->end_row()) {
      return true;
    }
    return false;
  }
  bool send_result_metadata() const override {
    return m_thd->send_result_metadata(
        m_item_list, Protocol::SEND_NUM_ROWS | Protocol::SEND_EOF);
  }

 private:
  THD *m_thd;
  Protocol *m_protocol;
  const mem_root_deque<Item *> &m_item_list;
};

class rpc_protocol : public base_protocol {
 public:
  explicit rpc_protocol(const std::vector<Field *> *field_list,
                        const myrocks_select_from_rpc *_param,
                        myrocks_columns *_columns)
      : m_field_list(field_list),
        m_columns(_columns),
        m_rpc_buffer(_param->rpc_buffer),
        m_send_row_fn(_param->send_row),
        m_num_columns(_param->columns.size()) {}
  bool fill_buffer() const override {
    int idx = 0;
    for (const auto field : *m_field_list) {
      auto rpcbuf = &m_columns->at(idx++);
      switch (field->real_type()) {
        case MYSQL_TYPE_LONGLONG: {
          pack_integer<8>(rpcbuf, field->field_ptr(),
                          field->is_flag_set(UNSIGNED_FLAG));
          break;
        }
        case MYSQL_TYPE_LONG: {
          pack_integer<4>(rpcbuf, field->field_ptr(),
                          field->is_flag_set(UNSIGNED_FLAG));
          break;
        }
        case MYSQL_TYPE_INT24: {
          pack_integer<3>(rpcbuf, field->field_ptr(),
                          field->is_flag_set(UNSIGNED_FLAG));
          break;
        }
        case MYSQL_TYPE_SHORT: {
          pack_integer<2>(rpcbuf, field->field_ptr(),
                          field->is_flag_set(UNSIGNED_FLAG));
          break;
        }
        case MYSQL_TYPE_TINY: {
          pack_integer<1>(rpcbuf, field->field_ptr(),
                          field->is_flag_set(UNSIGNED_FLAG));
          break;
        }
        case MYSQL_TYPE_VARCHAR: {
          const auto field_var = static_cast<const Field_varstring *>(field);
          const auto ptr = field_var->field_ptr();
          uint length_bytes = field_var->get_length_bytes();
          uint data_len;
          /* field_var->get_length_bytes() is 1 or 2 */
          if (length_bytes == 1) {
            data_len = ptr[0];
          } else {
            assert(length_bytes == 2);
            data_len = uint2korr(ptr);
          }
          rpcbuf->stringVal = const_cast<uchar *>(ptr) + length_bytes;
          rpcbuf->length = data_len;
          rpcbuf->type = myrocks_value_type::STRING;
          break;
        }
        case MYSQL_TYPE_BLOB: {
          const auto field_blob = static_cast<const Field_blob *>(field);
          uint packlength = field_blob->pack_length_no_ptr();
          uint length = field_blob->get_length_bytes();
          uchar *ucharptr;
          memcpy(&ucharptr, field_blob->field_ptr() + packlength,
                 sizeof(uchar *));
          rpcbuf->stringVal = ucharptr;
          rpcbuf->length = length;
          rpcbuf->type = myrocks_value_type::STRING;
          break;
        }

        default:
          // todo: support more type
          return true;
      }
    }
    return false;
  }
  bool send_row() const override {
    m_send_row_fn(m_rpc_buffer, m_columns, m_num_columns);
    return false;
  }
  bool send_result_metadata() const override {
    // do nothing
    return false;
  }

 private:
  const std::vector<Field *> *m_field_list;
  myrocks_columns *m_columns;
  // below are copied from myrocks_select_from_rpc
  void *m_rpc_buffer;
  myrocks_bypass_rpc_send_row_fn m_send_row_fn;
  int m_num_columns;

  template <int length>
  void pack_integer(myrocks_column_value *rpcbuf, const uchar *ptr,
                    bool is_unsigned) const {
    static_assert(length <= 8, "Length is too large");
    uint64_t val;
    uchar *dst = (uchar *)&val;
    for (int i = 0; i < length; i++) {
      dst[i] = ptr[i];
    }
    bool is_positive = (ptr[length - 1] & 128) ^ 128;
    uint fill_val = (!is_unsigned && !is_positive) ? 255 : 0;
    for (int i = length; i < 8; i++) {
      dst[i] = fill_val;
    }

    if (is_unsigned) {
      rpcbuf->i64Val = val;
      rpcbuf->type = myrocks_value_type::UNSIGNED_INT;
    } else {
      rpcbuf->signed_i64Val = val;
      rpcbuf->type = myrocks_value_type::SIGNED_INT;
    }
  }
};

/*
  Represents single conditional expression in SELECT WHERE
  Example: id1 > 100
 */
class base_cond {
 public:
  base_cond(Item_func::Functype _op_type, Field *_field)
      : op_type(_op_type), field(_field) {}
  base_cond() = default;
  virtual ~base_cond() = default;
  // dump the condition in string
  virtual std::string dump() const = 0;
  // read the condition value
  virtual nosql_cond_value get_value(String *str) const = 0;
  // read the k-th condition value (when IN operator is used)
  virtual nosql_cond_value get_value(String *str, uint k) const = 0;
  // read number of condition values (IN operator is used)
  virtual int get_size() const = 0;
  // fix fields before using items
  virtual bool fix_fields(THD *thd) const = 0;
  // evaluate the condition against read values
  // return true if the cond is met, false otherwise
  virtual bool evaluate() const = 0;

  Item_func::Functype op_type;
  Field *field;
};

class sql_cond : public base_cond {
 public:
  sql_cond(Item_func::Functype _op_type, Field *_field, Item *_cond_item,
           Item *_val_item)
      : base_cond(_op_type, _field),
        cond_item(_cond_item),
        val_item(_val_item) {}
  sql_cond() = default;

  Item *cond_item;
  Item *val_item;

  std::string dump() const override {
    std::string str;
    str += "(";
    str += field->field_name;
    str += " ";
    switch (op_type) {
      case Item_func::EQ_FUNC: {
        str += "==";
        break;
      }
      case Item_func::LT_FUNC: {
        str += "<";
        break;
      }
      default:
        str += "?";
    }
    str += " ";
    if (op_type == Item_func::IN_FUNC) {
      str += "(";
      auto in_func = static_cast<Item_func_in *>(cond_item);
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
    }

    return str;
  }

  nosql_cond_value get_value(String *str) const override {
    return __get_value(str, val_item);
  }

  nosql_cond_value get_value(String *str, uint idx) const override {
    assert(op_type == Item_func::IN_FUNC);
    auto in_func = static_cast<Item_func_in *>(cond_item);
    auto item = in_func->arguments()[idx + 1];
    return __get_value(str, item);
  }

  int get_size() const override {
    if (op_type != Item_func::IN_FUNC) return 1;
    auto in_func = static_cast<Item_func_in *>(cond_item);
    return in_func->argument_count() - 1;  // idx starts from 1
  }

  bool fix_fields(THD *thd) const override {
    if (!cond_item->fixed) {
      if (cond_item->fix_fields(thd, const_cast<Item **>(&cond_item))) {
        return true;
      }
    }
    return false;
  }
  bool evaluate() const override { return (cond_item->val_int() != 0); }

 private:
  nosql_cond_value __get_value(String *str, Item *item) const {
    nosql_cond_value value;

    switch (field->type()) {
      case MYSQL_TYPE_LONGLONG:
      case MYSQL_TYPE_LONG:
      case MYSQL_TYPE_SHORT:
      case MYSQL_TYPE_TINY: {
        value.type = nosql_cond_value_type::UNSIGNED_INT;
        value.i64Val = item->val_int();
        break;
      }
      case MYSQL_TYPE_STRING: {
        value.type = nosql_cond_value_type::STRING;
        value.stringVal = item->val_str(str);
        break;
      }
      default: {
        // TODO: support more types
        value.type = nosql_cond_value_type::NONE;
      }
    }
    return value;
  }
};

class rpc_cond : public base_cond {
 public:
  rpc_cond(Item_func::Functype _op_type, Field *_field,
           const myrocks_column_cond_value *_cond_val, uint _cond_val_count,
           const myrocks_column_value *_val)
      : base_cond(_op_type, _field),
        cond_val(_cond_val),
        cond_val_count(_cond_val_count),
        val(_val) {}
  rpc_cond() = default;

  const myrocks_column_cond_value *cond_val;
  uint cond_val_count;
  const myrocks_column_value *val;

  std::string dump() const override { return {}; }

  nosql_cond_value get_value(String *str, uint idx) const override {
    nosql_cond_value value;

    switch (cond_val[idx].type) {
      case myrocks_value_type::UNSIGNED_INT:
      case myrocks_value_type::SIGNED_INT: {
        value.type = nosql_cond_value_type::UNSIGNED_INT;
        value.i64Val = cond_val[idx].i64Val;
        break;
      }
      case myrocks_value_type::STRING: {
        value.type = nosql_cond_value_type::STRING;
        String tmp(reinterpret_cast<const char *>(cond_val[idx].stringVal),
                   cond_val[idx].length, field->charset());
        str->copy(tmp);
        value.stringVal = str;
        break;
      }
      default: {
        value.type = nosql_cond_value_type::NONE;
      }
    }
    return value;
  }

  nosql_cond_value get_value(String *str) const override { return get_value(str, 0); }
  int get_size() const override { return cond_val_count; }
  bool fix_fields(THD *) const override { return false; }
  bool evaluate() const override {
    if (op_type == Item_func::IN_FUNC) {
      bool found = false;
      for (uint i = 0; i < cond_val_count; ++i) {
        if (__evaluate(Item_func::EQ_FUNC, cond_val[i], val)) {
          found = true;
          break;
        }
      }
      return found;
    } else {
      return __evaluate(op_type, cond_val[0], val);
    }
  }

 private:
  bool __evaluate(Item_func::Functype op,
                  const myrocks_column_cond_value &cond_val,
                  const myrocks_column_value *val) const {
    switch (cond_val.type) {
      case myrocks_value_type::BOOL: {
        assert(val->type == myrocks_value_type::BOOL);
        return compare(op, val->boolVal, cond_val.boolVal);
      }
      case myrocks_value_type::UNSIGNED_INT:
      case myrocks_value_type::SIGNED_INT: {
        assert(val->type == myrocks_value_type::UNSIGNED_INT ||
               val->type == myrocks_value_type::SIGNED_INT);
        return compare(op, val->i64Val, cond_val.i64Val);
      }
      case myrocks_value_type::DOUBLE: {
        assert(val->type == myrocks_value_type::DOUBLE);
        return compare(op, val->doubleVal, cond_val.doubleVal);
      }
      case myrocks_value_type::STRING: {
        assert(val->type == myrocks_value_type::STRING);
        if (cond_val.length != val->length) {
          return false;
        }
        return compare(op, reinterpret_cast<char *>(val->stringVal),
                       cond_val.stringVal, val->length);
      }
      default:
        return false;
    }
    return false;
  }

  template <typename P, typename Q>
  bool compare(Item_func::Functype op, P a, Q b) const {
    switch (op) {
      case Item_func::EQ_FUNC:
        return (a == b) ? true : false;
      case Item_func::LT_FUNC:
        return (a < b) ? true : false;
      case Item_func::GT_FUNC:
        return (a > b) ? true : false;
      case Item_func::LE_FUNC:
        return (a <= b) ? true : false;
      case Item_func::GE_FUNC:
        return (a >= b) ? true : false;
      default:
        return false;
    }
    return false;
  }

  bool compare(Item_func::Functype op, char *a, const char *b,
               uint length) const {
    switch (op) {
      case Item_func::EQ_FUNC:
        // TODO: test with different collation and charset
        return (!strncmp(a, b, length)) ? true : false;
      default:
        // not supported
        return false;
    }
    return false;
  }
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
  virtual const std::vector<base_cond *> &get_cond_list() const = 0;
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
  uint m_cond_count = 0;
  uint64_t m_select_limit;
  uint64_t m_offset_limit;
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
  }

  const std::vector<base_cond *> &get_cond_list() const override {
    return m_cond_list_ptr;
  }

  bool INLINE_ATTR parse() {
    // No locking
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
    if (parse_index() || parse_items() || parse_order_by() || parse_where() ||
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

    for (uint i = 0; i < m_field_list.size(); ++i) {
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
      str += cond.dump();
    }
    str += " }";

    return str;
  }

  const char *get_error_msg() const { return m_error_msg; }

 private:
  const char *m_error_msg;

  // Buffer to store snprintf-ed error messages
  char m_error_msg_buf[FN_REFLEN];

  sql_cond m_cond_list[MAX_NOSQL_COND_COUNT];
  std::vector<base_cond *> m_cond_list_ptr;

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
        std::string rewritten_index;
        bool is_rewritten = lookup_optimizer_force_index_rewrite(
            to_string(hint->key_name), &rewritten_index);
        LEX_CSTRING index_to_use =
            is_rewritten
                ? LEX_CSTRING{rewritten_index.c_str(), rewritten_index.size()}
                : hint->key_name;
        uint pos = find_type(&m_table->s->keynames, index_to_use.str,
                             index_to_use.length, true);
        if (!pos) {
          // Unrecognized index
          snprintf(m_error_msg_buf, sizeof(m_error_msg_buf) / sizeof(char),
                   "Unrecognized index: '%s'", index_to_use.str);
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

  // Parse all items in SELECT
  bool parse_items() {
    m_field_list.reserve(DEFAULT_FIELD_LIST_SIZE);

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
          field->charset() != &my_charset_bin &&
          field->charset() != &my_charset_utf8mb3_bin &&
          field->charset() != &my_charset_latin1_bin) {
        m_error_msg =
            "only binary, utf8_bin, latin1_bin is supported for varchar field";
        return true;
      }

      // This is needed so that we can use protocol->send_items and
      // item->send
      field_item->set_field(field);

      m_field_list.push_back(field);
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

    if (found->is_nullable()) {
      m_error_msg = "NULL fields not supported";
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

    m_cond_list_ptr.reserve(m_cond_count);
    for (uint i = 0; i < m_cond_count; i++) {
      m_cond_list_ptr.push_back(&m_cond_list[i]);
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

/*
  Executes the SELECT query directly without going through MySQL
 */
class select_exec {
 public:
  explicit select_exec(const base_select_parser &parser,
                       const base_protocol &protocol)
      : m_parser(parser),
        m_protocol(protocol),
        m_cond_list(parser.get_cond_list()) {
    m_table = parser.get_table();
    m_table_share = m_table->s;
    m_index = parser.get_index();
    m_index_info = &m_table->key_info[m_index];
    m_pk_info = &m_table->key_info[m_table_share->primary_key];
    m_is_point_query = true;
    m_start_full_key = m_end_full_key = true;
    m_ddl_manager = rdb_get_ddl_manager();
    m_index_is_pk = (m_index == m_table_share->primary_key);
    m_handler = static_cast<ha_rocksdb *>(m_table->file);
    m_thd = parser.get_thd();
    m_examined_rows = 0;
    m_rows_sent = 0;
    m_row_count = 0;
    m_offset_limit = m_parser.get_offset_limit();
    m_select_limit = m_parser.get_select_limit() + m_offset_limit;
    m_debug_row_delay = get_select_bypass_debug_row_delay();

    m_field_index_to_where.resize(m_table_share->fields,
                                  std::make_pair(-1, -1));
    m_unsupported = false;
    m_error_msg = "UNKNOWN";
    m_start_inclusive = m_end_inclusive = true;
  }

  ~select_exec() { bitmap_free(&m_lookup_bitmap); }

  bool run();

  bool is_unsupported() { return m_unsupported; }
  const char *get_error_msg() { return m_error_msg; }

 private:
  /*
    Wraps Rdb_transaction related operations
    It tries to use the most optimal way for transaction/snapshots and hides
    the details
   */
  class txn_wrapper {
   public:
    explicit txn_wrapper(THD *thd) : m_thd(thd), m_tx(get_tx_from_thd(m_thd)) {
      assert(m_tx && rdb_tx_started(m_tx));
    }

    bool start() {
      rdb_tx_acquire_snapshot(m_tx);

      return false;
    }

    rocksdb::Iterator *get_iterator(rocksdb::ColumnFamilyHandle *cf,
                                    bool use_bloom,
                                    const rocksdb::Slice &lower_bound,
                                    const rocksdb::Slice &upper_bound) {
      return rdb_tx_get_iterator(m_thd, cf, !use_bloom, lower_bound,
                                 upper_bound, nullptr);
    }

    rocksdb::Status get(rocksdb::ColumnFamilyHandle *cf,
                        const rocksdb::Slice &key_slice,
                        rocksdb::PinnableSlice *value_slice) {
      rocksdb::Status s;
      return rdb_tx_get(m_tx, cf, key_slice, value_slice);
    }

    void multi_get(rocksdb::ColumnFamilyHandle *cf, size_t size,
                   bool sorted_input, const rocksdb::Slice *key_slices,
                   rocksdb::PinnableSlice *value_slices,
                   rocksdb::Status *statuses) {
      rdb_tx_multi_get(m_tx, cf, size, key_slices, value_slices, statuses,
                       sorted_input);
    }

    void report_error(rocksdb::Status s) {
      if (s.IsIOError() || s.IsCorruption()) {
        rdb_handle_io_error(s, RDB_IO_ERROR_GENERAL);
      }
      // No need to return the error as we already reported with my_error
      // in this function, which is all we need
      __attribute((unused)) int err = ha_rocksdb::rdb_error_to_mysql(s);
    }

   private:
    THD *m_thd;
    Rdb_transaction *m_tx;
  };

  struct key_index_tuple_writer {
    Rdb_string_writer start;  // Start KeyIndexTuple
    uint eq_len;              // Length of prefix key
    Rdb_string_writer end;    // End KeyIndexTuple, only used for ranges

    // Get the slice for packed key - point query only
    rocksdb::Slice get_key_slice() {
      assert(end.is_empty());
      return start.to_slice();
    }

    // Get the start range for packed key - range query only
    rocksdb::Slice get_start_key_slice() { return start.to_slice(); }

    // Get the end range for packed key - range query only
    rocksdb::Slice get_end_key_slice() { return end.to_slice(); }

    // Return slice for prefix range query
    rocksdb::Slice get_eq_slice() {
      assert(eq_len <= start.get_current_pos());
      return rocksdb::Slice(reinterpret_cast<char *>(start.ptr()), eq_len);
    }
  };

  bool scan_where();
  void scan_value();
  bool run_query();
  bool run_range_query(txn_wrapper *txn);
  bool unpack_for_sk(txn_wrapper *txn, const rocksdb::Slice &rkey,
                     const rocksdb::Slice &rvalue);
  bool unpack_for_pk(const rocksdb::Slice &rkey, const rocksdb::Slice &rvalue);
  bool eval_cond();
  int eval_and_send();
  bool run_pk_point_query(txn_wrapper *txn);
  bool run_sk_point_query(txn_wrapper *txn);
  bool pack_index_tuple(uint key_part_no, Rdb_string_writer *writer,
                        const Field *field, const nosql_cond_value &value);
  bool pack_cond(uint key_part_no, const base_cond *cond, bool is_start = true);
  bool send_row();

  bool setup_iterator(THD *thd);

  bool handle_killed() {
    if (m_thd->killed) {
      m_thd->send_kill_message();
      return true;
    }

    return false;
  }

 private:
  const base_select_parser &m_parser;
  const base_protocol &m_protocol;
  std::vector<base_cond *> m_cond_list;
  TABLE *m_table;
  TABLE_SHARE *m_table_share;
  Rdb_ddl_manager *m_ddl_manager;
  Rdb_tbl_def *m_tbl_def;
  std::shared_ptr<Rdb_key_def> m_key_def;
  std::shared_ptr<Rdb_key_def> m_pk_def;
  ha_rocksdb *m_handler;
  THD *m_thd;
  std::unique_ptr<Rdb_converter> m_converter;
  uint m_index;
  KEY *m_index_info;
  KEY *m_pk_info;

  // Current query is not supported
  bool m_unsupported;
  const char *m_error_msg;

  // All filters (such as A=1)
  uint m_filter_list[MAX_NOSQL_COND_COUNT];
  uint m_filter_count = 0;

  // All key index tuples we packed during scanning the WHERE clause
  std::vector<key_index_tuple_writer> m_key_index_tuples;

  // map from field_index to where_list
  std::vector<std::pair<int, int>> m_field_index_to_where;

  // The iterator used in secondary index query or range query
  std::unique_ptr<Rdb_iterator> m_iterator;

  // The entire index (including extended keyparts) is used in query in equality
  // predicates - meaning it is a point query
  bool m_is_point_query;

  // The start key uses full key (including extended keyparts)
  bool m_start_full_key;

  // The end key uses full key (including extended keyparts)
  bool m_end_full_key;

  // We need to unpack the given index (primary or secondary) as indicated
  // in WHERE clause
  bool m_unpack_index;

  // We need to unpack the primary index (when given index is secondary
  // index) as indicated in WHERE clause
  bool m_unpack_pk;

  // We need to unpack the value
  bool m_unpack_value;

  // We may need to only read keys
  bool m_keyread_only;

  // If the given index is primary index
  bool m_index_is_pk;

  std::vector<uchar> m_pk_tuple_buf;
  std::vector<uchar> m_index_tuple_buf;
  Rdb_string_writer m_row_buf;

  // Temporary buffer for storing value
  rocksdb::PinnableSlice m_pk_value;

  // Artificial delays for kill testing
  uint32_t m_debug_row_delay;

  // Number of rows we've examined (before filtering)
  uint64_t m_examined_rows;

  // Number of rows sent across the wire / returned to client
  uint64_t m_rows_sent;

  // Current number of rows found - subject to LIMIT OFFSET
  uint64_t m_row_count;

  // LIMIT upper bound
  // For LIMIT offset, row_count, this is offset + row_count
  uint64_t m_select_limit;

  // LIMIT lower bound
  // For LIMIT offset, row_count, this is offset
  uint64_t m_offset_limit;

  // Whether the range query (begin, end) is inclusive in begin/end
  bool m_start_inclusive;
  bool m_end_inclusive;

  // Lookup bitmap for covering check
  MY_BITMAP m_lookup_bitmap;
};

bool select_exec::pack_index_tuple(uint key_part_no, Rdb_string_writer *writer,
                                   const Field *field,
                                   const nosql_cond_value &value) {
  // Don't support keys that may be NULL
  assert(!field->is_nullable());

  switch (field->type()) {
    case MYSQL_TYPE_LONGLONG: {
      assert(value.type == nosql_cond_value_type::UNSIGNED_INT);
      if (field->is_flag_set(UNSIGNED_FLAG)) {
        writer->write_uint64(value.i64Val);
      } else {
        writer->write_uint64(value.i64Val ^ 0x8000000000000000);
      }
      break;
    }
    case MYSQL_TYPE_LONG: {
      assert(value.type == nosql_cond_value_type::UNSIGNED_INT);
      if (field->is_flag_set(UNSIGNED_FLAG)) {
        writer->write_uint32(value.i64Val);
      } else {
        writer->write_uint32(value.i64Val ^ 0x80000000);
      }
      break;
    }
    case MYSQL_TYPE_SHORT: {
      assert(value.type == nosql_cond_value_type::UNSIGNED_INT);
      if (field->is_flag_set(UNSIGNED_FLAG)) {
        writer->write_uint16(value.i64Val);
      } else {
        writer->write_uint16(value.i64Val ^ 0x8000);
      }
      break;
    }
    case MYSQL_TYPE_TINY: {
      assert(value.type == nosql_cond_value_type::UNSIGNED_INT);
      if (field->is_flag_set(UNSIGNED_FLAG)) {
        writer->write_uint8(value.i64Val);
      } else {
        writer->write_uint8(value.i64Val ^ 0x80);
      }
      break;
    }
    case MYSQL_TYPE_STRING: {
      assert(value.type == nosql_cond_value_type::STRING);
      uchar *buf = m_index_tuple_buf.data();
      const CHARSET_INFO *const charset = field->charset();
      const auto field_var = static_cast<const Field_string *>(field);

      uint len = charset->coll->strnxfrm(
          charset, buf, m_key_def->get_pack_info(key_part_no)->m_max_image_len,
          field_var->char_length(),
          reinterpret_cast<const uchar *>(value.stringVal->ptr()),
          value.stringVal->length(),
          /*MY_STRXFRM_PAD_WITH_SPACE |*/ MY_STRXFRM_PAD_TO_MAXLEN);
      writer->write(m_index_tuple_buf.data(), len);
      break;
    }
    case MYSQL_TYPE_VARCHAR: {
      uchar *buf = m_index_tuple_buf.data();
      m_key_def->pack_with_varlength_encoding(
          m_key_def->get_pack_info(key_part_no), const_cast<Field *>(field),
          m_index_tuple_buf.data(), &buf, nullptr);
      writer->write(m_index_tuple_buf.data(), buf - m_index_tuple_buf.data());
      break;
    }
    default:
      return true;
  }

  return false;
}

bool inline select_exec::pack_cond(uint key_part_no, const base_cond *cond,
                                   bool is_start) {
  String str;
  if (is_start) {
    // Start key in range query or key in point query
    auto cond_value = cond->get_value(&str);
    for (auto &entry : m_key_index_tuples) {
      if (pack_index_tuple(key_part_no, &entry.start, cond->field,
                           cond_value)) {
        return true;
      }
    }
  } else {
    // End key in range query
    auto cond_value = cond->get_value(&str);
    for (auto &entry : m_key_index_tuples) {
      assert(!entry.end.is_empty());
      if (pack_index_tuple(key_part_no, &entry.end, cond->field, cond_value)) {
        return true;
      }
    }
  }

  return false;
}

/*
  Scan the entire WHERE clause using the given index, and create a
  query plan. The goal is to break the WHERE clause into prefix and
  filter in index order, and build the key as we go
 */
bool INLINE_ATTR select_exec::scan_where() {
  m_index_tuple_buf.resize(m_key_def->max_storage_fmt_length());

  // Approximation of the required buff
  m_row_buf.reserve(m_table_share->reclength);

  auto where_list_count = m_cond_list.size();

  std::array<bool, MAX_NOSQL_COND_COUNT> where_list_processed = {};

  // Build a lookup map from field_index to where_list
  // Will be used later to reverse lookup from index -> expression
  // inside WHERE clause
  for (uint i = 0; i < where_list_count; ++i) {
    int field_index = m_cond_list[i]->field->field_index();
    std::pair<int, int> &index_pair = m_field_index_to_where[field_index];
    if (index_pair.first == -1) {
      index_pair.first = i;
    } else {
      // We have multiple sql_cond for the same field
      if (index_pair.second != -1) {
        // We've already seen 2 of conditions already - fail
        m_unsupported = true;
        m_error_msg = "Unsupported range query pattern";
        return true;
      }

      index_pair.second = i;
    }
  }

  // We start with just one KeyIndexTuple to pack
  m_key_index_tuples.emplace_back();
  m_key_index_tuples[0].start.reserve(m_key_def->max_storage_fmt_length());
  m_key_index_tuples[0].start.write_uint32(m_key_def->get_index_number());

  // Scan the index sequentially, and pack the prefix key as we go
  // We stop building the the key if we see a gap, rest become filters.
  // If we see either >, >=, <, <=, we keep building the key but remember
  // the filter starts at the first >, >=, <, <=
  // The above is best illustrated with the following examples:
  // Suppose we have index (A, B, C, D), then:
  //
  // WHERE A=1 AND B=2 And C=3:
  //   KeyIndexTuples = {(1, 2, 3)}, prefix_key_count=3
  // WHERE A=1 AND B=2 And C IN (1, 2, 3):
  //   KeyIndexTuples = {(1, 2, 1), (1, 2, 2), (1, 2, 3)}, prefix_key_count = 2
  // WHERE A=1 AND B=2 And C IN (1) AND D in (1, 2):
  //   KeyIndexTuples = {(1, 2, 1, 1), (1, 2, 1, 2)}, prefix_key_count = 4
  // WHERE A=1 AND B=2 AND D in (1, 2):
  //   KeyIndexTuples = {(1, 2)}, Filters = {D in (1, 2)}, prefix_key_count = 2
  // WHERE A=1 AND C=3:
  //   KeyIndexTuples = {(1)}, Filters = (C=3), prefix_key_count=1
  // WHERE A=1 AND B=2 AND C>3:
  //   KeyIndexTuples = {(1, 2, 3)}, Filters = {C>3}, prefix_key_count = 2
  bool eq_only = true;
  uint prefix_key_count = 0;
  uint start_key_count = 0;
  uint end_key_count = 0;
  uint key_part_no = 0;
  for (; key_part_no < m_index_info->actual_key_parts; ++key_part_no) {
    auto &key_part = m_index_info->key_part[key_part_no];
    std::pair<int, int> index_pair =
        m_field_index_to_where[key_part.field->field_index()];
    if (index_pair.first >= 0) {
      // The current key_part in the index has a corresponding match in
      // WHERE clause. Pack the current value in WHERE into the key
      const base_cond *cond = m_cond_list[index_pair.first];
      if (cond->op_type == Item_func::EQ_FUNC) {
        // This is = operator - just keep packing the key
        if (pack_cond(key_part_no, cond)) {
          return true;
        }

        if (eq_only) {
          // Remember how many key part in where eq(prefix)
          prefix_key_count++;

          // Remember we've processed this condition already
          // Anything we haven't processed here become filters
          where_list_processed[index_pair.first] = true;
          continue;
        }
      } else if (cond->op_type == Item_func::IN_FUNC) {
        if (!eq_only) {
          // We can't build key anymore with next key_part being a IN func
          // The remaining WHERE clauses will be filters
          break;
        }

        // Remember number of prefix keys
        prefix_key_count++;

        // In the IN case if we are building a prefix key we need to
        // multiply the key slices, so that A=1, B IN (2, 3) becomes
        // (1, 2) and (1, 3)
        size_t prev_size = m_key_index_tuples.size();
        auto in_elem_count = cond->get_size();

        std::vector<key_index_tuple_writer> new_writers;
        new_writers.reserve(KEY_WRITER_DEFAULT_SIZE);
        for (uint i = 0; i < prev_size; ++i) {
          assert(m_key_index_tuples[i].end.is_empty());
          for (int j = 0; j < in_elem_count; ++j) {
            new_writers.emplace_back(m_key_index_tuples[i]);
            String str;
            if (pack_index_tuple(key_part_no,
                                 &new_writers[new_writers.size() - 1].start,
                                 cond->field, cond->get_value(&str, j))) {
              return true;
            }
          }
        }

        // new_writers become the new m_key_index_tuples
        m_key_index_tuples.swap(new_writers);

        // Remember we've processed this condition already
        // Anything we haven't processed here become filters
        where_list_processed[index_pair.first] = true;
        continue;
      }

      // Process >, >=, <, <=
      if (eq_only) {
        // We now start to build range query initial position
        // Remeber eq_len at this point - this is the prefix
        // However we do need to keep going
        for (uint i = 0; i < m_key_index_tuples.size(); ++i) {
          m_key_index_tuples[i].eq_len =
              m_key_index_tuples[i].start.get_current_pos();
        }
        eq_only = false;
      }

      // Figure out (start, end) range for range query
      int start_id = -1, end_id = -1;
      bool start_inclusive = false, end_inclusive = false;
      if (cond->op_type == Item_func::GT_FUNC ||
          cond->op_type == Item_func::GE_FUNC) {
        start_id = index_pair.first;
        if (cond->op_type == Item_func::GE_FUNC) {
          start_inclusive = true;
        }
      } else if (cond->op_type == Item_func::LE_FUNC ||
                 cond->op_type == Item_func::LT_FUNC) {
        end_id = index_pair.first;
        if (cond->op_type == Item_func::LE_FUNC) {
          end_inclusive = true;
        }
      } else {
        // Unsupported operator - go to filters
        break;
      }

      if (index_pair.second >= 0) {
        auto second_cond = m_cond_list[index_pair.second];
        if (second_cond->op_type == Item_func::GT_FUNC ||
            second_cond->op_type == Item_func::GE_FUNC) {
          if (start_id != -1) {
            m_unsupported = true;
            m_error_msg = "Unsupported range query pattern";
            return true;
          } else {
            start_id = index_pair.second;
            if (second_cond->op_type == Item_func::GE_FUNC) {
              start_inclusive = true;
            }
          }
        } else if (second_cond->op_type == Item_func::LE_FUNC ||
                   second_cond->op_type == Item_func::LT_FUNC) {
          if (end_id != -1) {
            m_unsupported = true;
            m_error_msg = "Unsupported range query pattern";
            return true;
          } else {
            end_id = index_pair.second;
            if (second_cond->op_type == Item_func::LE_FUNC) {
              end_inclusive = true;
            }
          }
        } else {
          // Unsupported operator - go to filters
          break;
        }
      }

      // Process the range
      assert(start_id >= 0 || end_id >= 0);
      // This ensures we always go from start -> end regardless of ordering
      if (m_parser.is_order_desc()) {
        std::swap(start_id, end_id);
        std::swap(start_inclusive, end_inclusive);
      }

      start_key_count = end_key_count = prefix_key_count;

      if (end_id >= 0) {
        // end key should start from prefix of start key, which is
        // the current value of start key before appending the condition
        for (auto &entry : m_key_index_tuples) {
          if (entry.end.is_empty()) {
            entry.end = entry.start;
          }
        }
      }
      if (start_id >= 0) {
        // Mark the where condition as processed so that they don't go into
        // filters - there is no point evaluating them since we already
        // accounted for them in (start, end) range
        where_list_processed[start_id] = true;
        m_start_inclusive = start_inclusive;
        if (pack_cond(key_part_no, m_cond_list[start_id],
                      true /* is_start */)) {
          return true;
        }
        start_key_count++;
      }
      if (end_id >= 0) {
        // Mark the where condition as processed so that they don't go into
        // filters
        where_list_processed[end_id] = true;
        m_end_inclusive = end_inclusive;
        if (pack_cond(key_part_no, m_cond_list[end_id], false /* is_end */)) {
          return true;
        }
        end_key_count++;
      }

      // We already processed a range and we don't support cases like A >= 1 and
      // B >= 2
      key_part_no++;
      break;
    } else {
      // We stop building the KeyIndexTuple - all the remaining are going to be
      // filters
      break;
    }
  }

  // Sort the key in index order to ensure the query output is also in
  // correct index order
  if (!m_parser.is_order_desc()) {
    std::sort(
        m_key_index_tuples.begin(), m_key_index_tuples.end(),
        [](const key_index_tuple_writer &l, const key_index_tuple_writer &r) {
          return l.start < r.start;
        });
  } else {
    std::sort(
        m_key_index_tuples.begin(), m_key_index_tuples.end(),
        [](const key_index_tuple_writer &l, const key_index_tuple_writer &r) {
          return l.start > r.start;
        });
  }

  // Remove duplicates
  m_key_index_tuples.erase(
      std::unique(
          m_key_index_tuples.begin(), m_key_index_tuples.end(),
          [](const key_index_tuple_writer &l, const key_index_tuple_writer &r) {
            return l.start == r.start && l.end == r.end;
          }),
      m_key_index_tuples.end());

  if (eq_only) {
    for (uint i = 0; i < m_key_index_tuples.size(); ++i) {
      m_key_index_tuples[i].eq_len =
          m_key_index_tuples[i].start.get_current_pos();
    }
    start_key_count = prefix_key_count;
    end_key_count = prefix_key_count;
  }

  // This is TRICKY:
  // actual_key_parts can be SK+PK in SQL layer if SK is non-unique,
  // otherwise it is only SK in SQL layer if SK is unique.
  // key_def->m_key_parts always has SK+PK
  // For m_is_point_query check we need to see it has both SK+PK for
  // SK point lookup and bloom filter check, since bloom filter
  // has a special optimization that allows bloom filter use even if
  // eq_cond length is less than prefix length
  m_is_point_query = (prefix_key_count == m_key_def->get_key_parts());
  assert_IMP(m_is_point_query, eq_only);
  m_start_full_key = (start_key_count == m_key_def->get_key_parts());
  m_end_full_key = (end_key_count == m_key_def->get_key_parts());

  // Build list of all filters
  // Any condition we haven't processed in prefix key are filters
  for (uint i = 0; i < where_list_count; ++i) {
    if (!where_list_processed[i]) {
      if (m_cond_list[i]->fix_fields(m_thd)) {
        return true;
      }
      m_filter_list[m_filter_count++] = i;
    }
  }

  if (!should_allow_filters_select_bypass() && m_filter_count > 0 &&
      key_part_no < m_index_info->user_defined_key_parts) {
    // Only support filter usage in well supported cases such as point query
    // or simple range query where key is fully specified with well defined
    // (start, end) with only the last column is a range
    m_unsupported = true;
    m_error_msg = "Non-optimal queries with filters are not allowed";
    return true;
  }

  for (uint i = 0; i < m_filter_count; ++i) {
    Field *field = m_cond_list[m_filter_list[i]]->field;

    // Since we bypassed prepare function, read_set might not be set.
    // Let's set it before calling eval_cond().
    bitmap_set_bit(m_table->read_set, field->field_index());
  }
  return false;
}

/*
  Scan all the fields that going to be unpacked in SELECT, and figure out
  do we need to just unpack the index or need to unpack the value as well
 */
void INLINE_ATTR select_exec::scan_value() {
  m_unpack_index = false;
  m_unpack_pk = false;
  m_unpack_value = false;

  const auto &field_list = m_parser.get_field_list();
  for (uint i = 0; i < field_list.size(); i++) {
    // bitmap is not set yet because we skips prepare function
    bitmap_set_bit(m_table->read_set, field_list[i]->field_index());
  }

  std::vector<bool> index_cover_bitmap(m_table_share->fields, false);
  for (uint i = 0; i < m_index_info->actual_key_parts; ++i) {
    if (m_key_def->get_pack_info(i)->m_covered == Rdb_key_def::KEY_COVERED) {
      index_cover_bitmap[m_index_info->key_part[i].field->field_index()] = true;
    }
  }

  m_keyread_only = true;
  for (uint i = 0; i < m_table_share->fields; i++) {
    if (bitmap_is_set(m_table->read_set, i) && !index_cover_bitmap[i]) {
      m_keyread_only = false;
      break;
    }
  }

  if (!m_keyread_only && !m_index_is_pk) {
    m_key_def->get_lookup_bitmap(m_table, &m_lookup_bitmap);
  }

  m_converter->setup_field_decoders(m_table->read_set, m_index,
                                    false /* keyread_only */);
}

bool INLINE_ATTR select_exec::run() {
  if (!m_thd->lex->is_query_tables_locked()) {
    /*
      If tables are not locked at this point, it means that we have delayed
      this step until after prepare stage (i.e. this moment). This allows to
      do better partition pruning and avoid locking unused partitions.
      As a consequence, in such a case, prepare stage can rely only on
      metadata about tables used and not data from them.
      We need to lock tables now in order to proceed with the remaning
      stages of query optimization and execution.
    */
    if (lock_tables(m_thd, m_thd->lex->query_tables, m_thd->lex->table_count,
                    0)) {
      assert(m_thd->is_error());
      return true;
    }
  }

  // Look for the table metadata
  std::string db_table;
  db_table.append(m_table_share->db.str);
  db_table.append(".");
  db_table.append(m_table_share->table_name.str);
  m_tbl_def = m_ddl_manager->find(db_table);
  if (m_tbl_def == nullptr) {
    m_handler->print_error(HA_ERR_ROCKSDB_INVALID_TABLE, 0);
    return true;
  }

  m_key_def = m_tbl_def->m_key_descr_arr[m_index];
  m_pk_def = m_tbl_def->m_key_descr_arr[m_table_share->primary_key];
  m_converter.reset(new Rdb_converter(m_thd, m_tbl_def, m_table));

  // Scans WHERE and build the key and filter list
  if (scan_where()) {
    return true;
  }

  // Scan the value and devise a strategy to unpack the values
  scan_value();

  // Prepare to send
  if (m_protocol.send_result_metadata()) {
    return true;
  }

  if (m_select_limit == 0) {
    return false;
  }

  return run_query();
}

/*
  Send the entire row in select_lex->item_list and record[0]
 */
bool inline select_exec::send_row() {
  if (m_protocol.send_row()) {
    return true;
  }
  m_rows_sent++;
  return false;
}

bool INLINE_ATTR select_exec::run_query() {
  bool is_pk_point_query = m_index_is_pk && m_is_point_query;

  // Initialize Rdb_transaction as needed
  txn_wrapper txn(m_thd);

  // TODO(yzha) - Refactor ReadOptions initialization into a shared wrapper
  if (txn.start()) {
    return true;
  }

  bool ret = false;

  m_row_buf.clear();

  if (setup_iterator(m_thd)) {
    return true;
  }

  if (is_pk_point_query) {
    ret = run_pk_point_query(&txn);
  } else {
    m_pk_tuple_buf.resize(m_pk_def->max_storage_fmt_length());

    if (m_is_point_query) {
      // The index is fully covered including both SK+PK
      ret = run_sk_point_query(&txn);
    } else {
      ret = run_range_query(&txn);
    }
  }

  // Update examined_row count
  m_thd->inc_sent_row_count(m_rows_sent);
  m_thd->inc_examined_row_count(m_examined_rows);

  m_handler->update_row_read(m_examined_rows);

  return ret;
}

bool INLINE_ATTR select_exec::run_pk_point_query(txn_wrapper *txn) {
  auto cf = m_key_def->get_cf();

  if (m_key_index_tuples.size() > get_select_bypass_multiget_min()) {
    size_t size = m_key_index_tuples.size();
    std::vector<rocksdb::Slice> key_slices;
    key_slices.reserve(size);
    std::vector<rocksdb::PinnableSlice> value_slices(size);
    std::vector<rocksdb::Status> statuses(size);

    for (auto &writer : m_key_index_tuples) {
      key_slices.push_back(writer.get_key_slice());
    }

    // Just let linter shut up
    assert(size == key_slices.size());

    bool sorted_input =
        (m_key_def->m_is_reverse_cf == m_parser.is_order_desc());

    txn->multi_get(m_key_def->get_cf(), size, sorted_input, key_slices.data(),
                   value_slices.data(), statuses.data());

    for (size_t i = 0; i < size; ++i) {
      if (unlikely(handle_killed())) {
        return true;
      }

      if (statuses[i].IsNotFound()) {
        continue;
      } else if (!statuses[i].ok()) {
        txn->report_error(statuses[i]);
        return true;
      }

      if (unpack_for_pk(key_slices[i], value_slices[i])) {
        return true;
      }

      int ret = eval_and_send();
      if (ret > 0) {
        return true;
      } else if (ret < 0) {
        // no more items
        return false;
      }
    }
  } else {
    rocksdb::PinnableSlice value_slice;
    for (auto &writer : m_key_index_tuples) {
      if (handle_killed()) {
        return true;
      }

      value_slice.Reset();

      rocksdb::Slice key_slice = writer.get_key_slice();
      rocksdb::Status s = txn->get(cf, key_slice, &value_slice);
      if (s.IsNotFound()) {
        continue;
      } else if (!s.ok()) {
        txn->report_error(s);
        return true;
      }

      if (unpack_for_pk(key_slice, value_slice)) {
        return true;
      }

      int ret = eval_and_send();
      if (ret > 0) {
        return true;
      } else if (ret < 0) {
        // no more items
        return false;
      }
    }
  }

  return false;
}

bool INLINE_ATTR select_exec::setup_iterator(THD *thd) {
  if (m_key_def->is_partial_index()) {
    m_iterator.reset(
        new Rdb_iterator_partial(thd, m_key_def, m_pk_def, m_tbl_def, m_table));
  } else {
    m_iterator.reset(
        new Rdb_iterator_base(thd, m_key_def, m_pk_def, m_tbl_def));
  }

  return m_iterator == nullptr;
}

bool INLINE_ATTR select_exec::run_sk_point_query(txn_wrapper *txn) {
  for (auto &writer : m_key_index_tuples) {
    if (unlikely(handle_killed())) {
      return true;
    }

    rocksdb::PinnableSlice value;
    rocksdb::Slice key_slice = writer.get_key_slice();
    int rc = m_iterator->get(&key_slice, &value, RDB_LOCK_NONE);

    if (rc == HA_ERR_KEY_NOT_FOUND) {
      continue;
    }

    if (rc) {
      m_handler->print_error(rc, 0);
      return true;
    }

    if (unpack_for_sk(txn, key_slice, value)) {
      return true;
    }

    int ret = eval_and_send();
    if (ret > 0) {
      return true;
    } else if (ret < 0) {
      // no more items
      return false;
    }
  }

  return false;
}

/*
  Evaluate the condition using item->val_int, assuming item pointing
  to record[0] and is already unpacked.
  This is the slow path as we need to unpack into record[0]
 */
bool INLINE_ATTR select_exec::eval_cond() {
  if (unlikely(m_filter_count > 0)) {
    for (uint i = 0; i < m_filter_count; ++i) {
      if (!m_cond_list[m_filter_list[i]]->evaluate()) return false;
    }
  }
  return true;
}

bool INLINE_ATTR select_exec::unpack_for_pk(const rocksdb::Slice &rkey,
                                            const rocksdb::Slice &rvalue) {
  // decode will handle key/value decoding for PK
  int rc = m_converter->decode(m_key_def, m_table->record[0], &rkey, &rvalue);
  if (rc) {
    m_handler->print_error(rc, 0);
    return true;
  }

  return false;
}

bool INLINE_ATTR select_exec::unpack_for_sk(txn_wrapper *txn,
                                            const rocksdb::Slice &rkey,
                                            const rocksdb::Slice &rvalue) {
  bool covers_lookup =
      m_keyread_only || m_key_def->covers_lookup(&rvalue, &m_lookup_bitmap);

  // SECONDARY KEY - there are a few cases to take care of:
  // 1. Secondary index covers the entire look up
  // 2. Unpack everything using PK index + value
  int rc = 0;
  if (covers_lookup) {
    // SK covers the entire lookup
    rc =
        m_key_def->unpack_record(m_table, m_table->record[0], &rkey, &rvalue,
                                 m_converter->get_verify_row_debug_checksums());
    if (rc) {
      m_handler->print_error(rc, 0);
      return true;
    }

    ha_rocksdb::inc_covered_sk_lookup();
    return false;
  }

  // Unpack PK index + value
  uint pk_tuple_size = 0;
  pk_tuple_size =
      m_key_def->get_primary_key_tuple(*m_pk_def, &rkey, m_pk_tuple_buf.data());
  if (pk_tuple_size == RDB_INVALID_KEY_LEN) {
    m_handler->print_error(HA_ERR_ROCKSDB_CORRUPT_DATA, 0);
    return true;
  }

  rocksdb::Slice pk_key(reinterpret_cast<const char *>(m_pk_tuple_buf.data()),
                        pk_tuple_size);

  m_pk_value.Reset();
  rocksdb::Status s = txn->get(m_pk_def->get_cf(), pk_key, &m_pk_value);
  if (!s.ok()) {
    txn->report_error(s);
    return true;
  }

  rc = m_converter->decode(m_pk_def, m_table->record[0], &pk_key, &m_pk_value);
  if (rc) {
    m_handler->print_error(rc, 0);
    return true;
  }

  return false;
}

int INLINE_ATTR select_exec::eval_and_send() {
  m_examined_rows++;

  if (m_protocol.fill_buffer()) {
    // failure
    return 1;
  }
  if (eval_cond()) {
    m_row_count++;
    if (m_row_count > m_offset_limit) {
      if (m_debug_row_delay > 0) {
        // Inject artificial delays for debugging/testing purposes
        my_sleep(m_debug_row_delay * 1000000);
      }
      if (send_row()) {
        // failure
        return 1;
      }
    }

    if (m_row_count >= m_select_limit) {
      // we just sent the last one
      return -1;
    }
  }

  // keep going
  return 0;
}

bool INLINE_ATTR select_exec::run_range_query(txn_wrapper *txn) {
  for (uint i = 0; i < m_key_index_tuples.size(); ++i) {
    if (unlikely(handle_killed())) {
      return true;
    }

    rocksdb::Slice start_key_slice =
        m_key_index_tuples[i].get_start_key_slice();
    rocksdb::Slice end_key_slice = m_key_index_tuples[i].get_end_key_slice();
    rocksdb::Slice eq_slice = m_key_index_tuples[i].get_eq_slice();

    // There are 6 flags to consider:
    //              direction
    // seek method  ASC                   DESC
    // prefix       HA_READ_KEY_EXACT     HA_READ_PREFIX_LAST
    // inclusive    HA_READ_KEY_OR_NEXT   HA_READ_PREFIX_LAST_OR_PREV
    // exclusive    HA_READ_AFTER_KEY     HA_READ_BEFORE_KEY
    enum ha_rkey_function find_flag;
    if (eq_slice.size() == start_key_slice.size()) {
      find_flag =
          m_parser.is_order_desc() ? HA_READ_PREFIX_LAST : HA_READ_KEY_EXACT;
    } else if (m_start_inclusive) {
      find_flag = m_parser.is_order_desc() ? HA_READ_PREFIX_LAST_OR_PREV
                                           : HA_READ_KEY_OR_NEXT;
    } else {
      find_flag =
          m_parser.is_order_desc() ? HA_READ_BEFORE_KEY : HA_READ_AFTER_KEY;
    }

    int rc = m_iterator->seek(find_flag, start_key_slice, m_start_full_key,
                              end_key_slice);
    if (rc == HA_ERR_END_OF_FILE) {
      continue;
    }
    if (rc) {
      m_handler->print_error(rc, 0);
      return true;
    }

    // Range query need to support 4 combinations
    // * forward cf, ascending order
    // * forward cf, desending order
    // * reverse cf, ascending order
    // * reverse cf, descending order
    // By look at the different combinations, one is able to infer
    // * (Start, end) vs (end, start) is determined by order
    // * End condition is determined by order
    //
    // Note we have already swapped start/end in case of descending order in
    // scan_where, so we always start at start_key_slice and end at
    // end_key_slice regardless. The more challenging / confusing part is to
    // determine the exact (start, end) based on cf, order, and prefix
    std::vector<uchar> end_pos(end_key_slice.data(),
                               end_key_slice.data() + end_key_slice.size());
    rocksdb::Slice end_pos_slice(reinterpret_cast<char *>(end_pos.data()),
                                 end_key_slice.size());

    if (m_parser.is_order_desc()) {
      if (!end_key_slice.empty()) {
        // Adjust end key if needed
        // In reverse order, both forward cf (SeekForPrev) and reverse cf
        // (Seek) have the same matrix:
        // Full     A >= n --> stop at A < n
        // Full     A >  n --> stop at A < succ(n)
        // Partial  A >= n --> stop at A < n
        // Partial  A >  n --> stop at A < succ(n)
        if (!m_end_inclusive) {
          assert(end_key_slice.size() > eq_slice.size());
          m_key_def->successor(end_pos.data(), end_pos.size());
        }
      }
    } else {
      // Adjust end key if needed
      // In forward order, both forward cf (SeekForPrev) and reverse cf
      // (Seek) have the same matrix:
      // Full     A <= n --> stop at A > n
      // Full     A <  n --> stop at A > pred(n)
      // Partial  A <= n --> stop at A > succ(n)
      // Partial  A <  n --> stop at A > n
      if (!end_key_slice.empty()) {
        if (m_end_inclusive && !m_end_full_key) {
          // Partial A <= n
          m_key_def->successor(end_pos.data(), end_pos.size());
        } else if (!m_end_inclusive && m_end_full_key) {
          // Full A < n
          m_key_def->predecessor(end_pos.data(), end_pos.size());
        }
      }
    }

    // Make sure the slice is alive as we'll point into the slice during
    // unpacking
    while (true) {
      if (unlikely(handle_killed())) {
        return true;
      }

      const rocksdb::Slice rkey = m_iterator->key();
      if (end_key_slice.empty()) {
        // No end - we stop when prefix no longer matches
        if (!rkey.starts_with(eq_slice)) {
          break;
        }
      } else {
        // Range query (start, end)
        // NOTE: To simplify the algorithm we always use non-inclusive
        // check (> or <) and let the end slice handle the boundary
        int diff = rkey.compare(end_pos_slice);
        if (m_parser.is_order_desc()) {
          if (diff < 0) {
            break;
          }
        } else {
          if (diff > 0) {
            break;
          }
        }
      }

      const rocksdb::Slice rvalue = m_iterator->value();
      if (m_index_is_pk) {
        if (unlikely(unpack_for_pk(rkey, rvalue))) {
          return true;
        }
      } else {
        if (unlikely(unpack_for_sk(txn, rkey, rvalue))) {
          return true;
        }
      }

      int ret = eval_and_send();
      if (unlikely(ret > 0)) {
        // failure
        return true;
      } else if (unlikely(ret < 0)) {
        // no more items
        return false;
      }

      rc = m_parser.is_order_desc() ? m_iterator->prev() : m_iterator->next();
      if (rc == HA_ERR_END_OF_FILE) {
        break;
      }
      if (rc) {
        m_handler->print_error(rc, 0);
        return true;
      }
    }  // while (true)
  }    // for m_key_index_tuples

  return false;
}

}  // namespace

bool inline is_bypass_on(Query_block *select_lex) {
  auto policy = get_select_bypass_policy();

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

std::deque<REJECTED_ITEM> rejected_bypass_queries;
std::mutex rejected_bypass_query_lock;
bool handle_unsupported_bypass(THD *thd, const char *error_msg) {
  rocksdb_select_bypass_rejected++;

  if (should_log_rejected_select_bypass()) {
    // Record the rejected query into the error log if rejected query history
    // size equals zero
    if (get_select_bypass_rejected_query_history_size() == 0) {
      // NO_LINT_DEBUG
      sql_print_information("[REJECTED_BYPASS_QUERY] Query='%s', Reason='%s'\n",
                            thd->query().str, error_msg);
    } else {
      // Otherwise, record the rejected query into information_schema
      const std::lock_guard<std::mutex> lock(rejected_bypass_query_lock);

      while (rejected_bypass_queries.size() >=
             get_select_bypass_rejected_query_history_size()) {
        rejected_bypass_queries.pop_back();
      }

      REJECTED_ITEM rejected_query_record;
      rejected_query_record.rejected_bypass_query_timestamp =
          thd->query_start_timeval_trunc(0);
      // Normalize rejected query
      String normalized_query_text;
      compute_digest_text(&thd->m_digest->m_digest_storage,
                          &normalized_query_text);
      rejected_query_record.rejected_bypass_query =
          normalized_query_text.c_ptr_safe();
      rejected_query_record.error_msg = error_msg;

      rejected_bypass_queries.push_front(rejected_query_record);
    }
  }

  // During parse you can just let unsupported scenario fallback to MySQL
  // implementation - but keep in mind it may regress performance
  // Default is TRUE - let unsupported SELECT scenario just fail
  if (should_fail_unsupported_select_bypass()) {
    my_printf_error(ER_NOT_SUPPORTED_YET,
                    "SELECT statement pattern not supported: %s", MYF(0),
                    error_msg);
    return true;
  } else {
    return false;
  }
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
    return handle_unsupported_bypass(thd, select_stmt.get_error_msg());
  }

  // Execute SELECT statement
  sql_protocol protocol(thd, *select_stmt.get_select_lex()->get_fields_list());
  select_exec exec(select_stmt, protocol);
  if (exec.run()) {
    if (exec.is_unsupported()) {
      return handle_unsupported_bypass(thd, exec.get_error_msg());
    }
    if (!thd->is_error()) {
      // The contract is that any booleaning return function should do its
      // best to report an error before return true, otherwise we'll report
      // a generic error. It would be better if MySQL is consistently return
      // error code in all layers but that is not the case
      my_printf_error(ER_UNKNOWN_ERROR, "Unknown error", 0);
    }
    if (should_log_failed_select_bypass()) {
      // NO_LINT_DEBUG
      sql_print_information("[FAILED_BYPASS_QUERY] Query='%s', Reason='%s'\n",
                            thd->query().str,
                            thd->get_stmt_da()->message_text());
    }
    rocksdb_select_bypass_failed++;
  } else {
    my_eof(thd);
    rocksdb_select_bypass_executed++;
  }

  return true;
}

}  // namespace myrocks
