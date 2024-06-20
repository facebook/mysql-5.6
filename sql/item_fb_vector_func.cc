/*
   Copyright (c) 2023, Facebook, Inc.

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

#include "sql/item_fb_vector_func.h"
#include <cassert>
#include "sql/fb_vector_base.h"
#ifdef WITH_FB_VECTORDB
#include <faiss/utils/distances.h>
#endif
#include "sql-common/json_dom.h"
#include "sql/item_json_func.h"
#include "sql/sql_exception_handler.h"

#define FB_VECTORDB_DISABLED_ERR                                            \
  do {                                                                      \
    my_error(ER_FEATURE_DISABLED, MYF(0), "vector db", "WITH_FB_VECTORDB"); \
  } while (0)

bool parse_fb_vector_from_item(Item **args, uint arg_idx, String &str,
                               const char *func_name, Fb_vector &vector) {
  if (args[arg_idx]->data_type() == MYSQL_TYPE_VARCHAR ||
      args[arg_idx]->data_type() == MYSQL_TYPE_JSON) {
    Json_wrapper wrapper;
    if (get_json_wrapper(args, arg_idx, &str, func_name, &wrapper)) {
      return true;
    }

    if (parse_fb_vector_from_json(wrapper, vector.get_data_ref())) {
      my_error(ER_INCORRECT_TYPE, MYF(0), std::to_string(arg_idx).c_str(),
               func_name);
      return true;
    }
    return false;
  }

  if (args[arg_idx]->data_type() == MYSQL_TYPE_BLOB &&
      args[arg_idx]->type() == Item::FIELD_ITEM) {
    const Item_field *fi = down_cast<const Item_field *>(args[arg_idx]);
    if (parse_fb_vector_from_blob(fi->field, vector)) {
      my_error(ER_INCORRECT_TYPE, MYF(0), std::to_string(arg_idx).c_str(),
               func_name);
      return true;
    }
    return false;
  }

  return true;
}

Item_func_fb_vector_distance::Item_func_fb_vector_distance(THD * /* thd */,
                                                           const POS &pos,
                                                           PT_item_list *a)
    : Item_real_func(pos, a) {}

bool Item_func_fb_vector_distance::resolve_type(THD *thd) {
  if (args[0]->type() == Item::FUNC_ITEM &&
      ((Item_func *)args[0])->functype() == Item_func::FB_VECTOR_BLOB_TO_JSON) {
    char buffer[100];
    snprintf(buffer, sizeof(buffer),
             "%s: please use blob item directly, no need to use "
             "FB_VECTOR_BLOB_TO_JSON",
             func_name());
    my_error(ER_WRONG_ARGUMENTS, MYF(0), buffer);
    return true;
  }
  if (param_type_is_default(thd, 0, 2, MYSQL_TYPE_JSON)) return true;
  set_nullable(true);

  return false;
}

Item_func::enum_const_item_cache
Item_func_fb_vector_distance::can_cache_json_arg(Item *arg) {
  // first 2 args are json, cache them
  return arg == args[0] || arg == args[1] ? CACHE_JSON_VALUE : CACHE_NONE;
}

bool Item_func_fb_vector_distance::get_input_vector(
    std::vector<float> &result) {
  if (fix_input_vector()) {
    return true;
  }
  result.clear();
  result.reserve(m_input_vector.get_dimension());
  result.insert(
      result.begin(), m_input_vector.get_data_view(),
      m_input_vector.get_data_view() + m_input_vector.get_dimension());
  return false;
}

bool Item_func_fb_vector_distance::fix_input_vector() {
  // the second arg is the input vector, it is a constant,
  // only need to parse it once.
  if (m_input_vector.get_dimension() == 0) {
    if (parse_fb_vector_from_item(args, 1, m_value, func_name(),
                                  m_input_vector)) {
      return true;
    }
  }
  return false;
}

double Item_func_fb_vector_distance::val_real() {
  if (args[0]->null_value || args[1]->null_value) {
    return error_real();
  }

  try {
    if (fix_input_vector()) {
      return error_real();
    }

    Fb_vector vector1;
    if (parse_fb_vector_from_item(args, 0, m_value, func_name(), vector1)) {
      return error_real();
    }
    const size_t dimension =
        std::max(vector1.get_dimension(), m_input_vector.get_dimension());
    if (vector1.set_dimension(dimension) ||
        m_input_vector.set_dimension(dimension)) {
      assert(false);
      // should never happen
      my_error(ER_INVALID_CAST, MYF(0), "a smaller dimension");
      return error_real();
    }
    return compute_distance(vector1.get_data_view(),
                            m_input_vector.get_data_view(), dimension);
  } catch (...) {
    handle_std_exception(func_name());
    return error_real();
  }

  return 0.0;
}

Item_func_fb_vector_l2::Item_func_fb_vector_l2(THD *thd, const POS &pos,
                                               PT_item_list *a)
    : Item_func_fb_vector_distance(thd, pos, a) {}

const char *Item_func_fb_vector_l2::func_name() const { return "fb_vector_l2"; }

enum Item_func::Functype Item_func_fb_vector_l2::functype() const {
  return FB_VECTOR_L2;
}

Item_func_fb_vector_ip::Item_func_fb_vector_ip(THD *thd, const POS &pos,
                                               PT_item_list *a)
    : Item_func_fb_vector_distance(thd, pos, a) {}

const char *Item_func_fb_vector_ip::func_name() const { return "fb_vector_ip"; }

enum Item_func::Functype Item_func_fb_vector_ip::functype() const {
  return FB_VECTOR_IP;
}

Item_func_fb_vector_normalize_l2::Item_func_fb_vector_normalize_l2(
    THD *thd, const POS &pos, PT_item_list *a)
    : Item_json_func(thd, pos, a) {}

bool Item_func_fb_vector_normalize_l2::resolve_type(THD *thd) {
  if (args[0]->type() == Item::FUNC_ITEM &&
      ((Item_func *)args[0])->functype() == Item_func::FB_VECTOR_BLOB_TO_JSON) {
    char buffer[100];
    snprintf(buffer, sizeof(buffer),
             "%s: please use blob item directly, no need to use "
             "FB_VECTOR_BLOB_TO_JSON",
             func_name());
    my_error(ER_WRONG_ARGUMENTS, MYF(0), buffer);
    return true;
  }
  if (param_type_is_default(thd, 0, 1, MYSQL_TYPE_JSON)) return true;
  set_nullable(true);

  return false;
}

Item_func::enum_const_item_cache
Item_func_fb_vector_normalize_l2::can_cache_json_arg(Item *arg) {
  return arg == args[0] ? CACHE_JSON_VALUE : CACHE_NONE;
}

const char *Item_func_fb_vector_normalize_l2::func_name() const {
  return "fb_vector_normalize_l2";
}

enum Item_func::Functype Item_func_fb_vector_normalize_l2::functype() const {
  return FB_VECTOR_NORMALIZE_L2;
}

Item_func_fb_vector_blob_to_json::Item_func_fb_vector_blob_to_json(
    THD *thd, const POS &pos, PT_item_list *a)
    : Item_json_func(thd, pos, a) {}

bool Item_func_fb_vector_blob_to_json::resolve_type(THD *thd) {
  if (args[0]->data_type() != MYSQL_TYPE_BLOB) {
    my_error(ER_WRONG_ARGUMENTS, MYF(0), func_name());
    return true;
  }
  if (param_type_is_default(thd, 0, 1, MYSQL_TYPE_BLOB)) return true;
  set_nullable(true);

  return false;
}

Item_func::enum_const_item_cache
Item_func_fb_vector_blob_to_json::can_cache_json_arg(Item *arg) {
  return arg == args[0] ? CACHE_JSON_VALUE : CACHE_NONE;
}

const char *Item_func_fb_vector_blob_to_json::func_name() const {
  return "fb_vector_blob_to_json";
}

enum Item_func::Functype Item_func_fb_vector_blob_to_json::functype() const {
  return FB_VECTOR_BLOB_TO_JSON;
}

Item_func_fb_vector_json_to_blob::Item_func_fb_vector_json_to_blob(
    const POS &pos, PT_item_list *a)
    : Item_str_func(pos, a) {}

bool Item_func_fb_vector_json_to_blob::resolve_type(THD *thd) {
  if (args[0]->data_type() != MYSQL_TYPE_JSON &&
      args[0]->data_type() != MYSQL_TYPE_VARCHAR) {
    my_error(ER_WRONG_ARGUMENTS, MYF(0), func_name());
    return true;
  }
  if (param_type_is_default(thd, 0, 1, MYSQL_TYPE_JSON)) return true;
  set_data_type_string(MAX_BLOB_WIDTH, &my_charset_bin);
  set_nullable(true);
  return false;
}

const char *Item_func_fb_vector_json_to_blob::func_name() const {
  return "fb_vector_json_to_blob";
}

enum Item_func::Functype Item_func_fb_vector_json_to_blob::functype() const {
  return FB_VECTOR_JSON_TO_BLOB;
}

String *Item_func_fb_vector_json_to_blob::val_str(String *) {
  assert(fixed);
  if (args[0]->null_value) {
    return error_str();
  }
  Fb_vector vector;
  if (parse_fb_vector_from_item(args, 0, m_value, func_name(), vector)) {
    return error_str();
  }
  m_value.mem_free();
  const size_t size = vector.get_dimension() * sizeof(float);
  m_value.copy((const char *)vector.get_data_view(), size, &my_charset_bin);
  return &m_value;
}

bool Item_func_fb_vector_blob_to_json::val_json(Json_wrapper *wr) {
  if (args[0]->null_value) {
    return error_json();
  }
  try {
    Fb_vector vector1;
    // Input is blob, so must has data_view and data_view_len set
    if (parse_fb_vector_from_item(args, 0, m_value, func_name(), vector1) ||
        !vector1.get_data_view() || !vector1.get_dimension()) {
      return error_json();
    }
    Json_array_ptr array = create_dom_ptr<Json_array>();
    const float *data = vector1.get_data_view();
    for (size_t i = 0; i < vector1.get_dimension(); ++i) {
      Json_double d(data[i]);
      if (array->append_clone(&d)) {
        return error_json();
      }
    }
    *wr = Json_wrapper(std::move(array));
  } catch (...) {
    handle_std_exception(func_name());
    return error_json();
  }
  return false;
}

#ifdef WITH_FB_VECTORDB
float Item_func_fb_vector_l2::compute_distance(const float *v1, const float *v2,
                                               size_t dimension) {
  return faiss::fvec_L2sqr(v1, v2, dimension);
}

float Item_func_fb_vector_ip::compute_distance(const float *v1, const float *v2,
                                               size_t dimension) {
  return faiss::fvec_inner_product(v1, v2, dimension);
}

bool Item_func_fb_vector_normalize_l2::val_json(Json_wrapper *wr) {
  if (args[0]->null_value) {
    return error_json();
  }
  try {
    Fb_vector vector1;
    if (parse_fb_vector_from_item(args, 0, m_value, func_name(), vector1)) {
      return error_json();
    }
    float *data = vector1.get_data();
    faiss::fvec_renorm_L2(vector1.get_dimension(), 1, data);
    Json_array_ptr array(new (std::nothrow) Json_array());
    for (size_t i = 0; i < vector1.get_dimension(); ++i) {
      Json_double d(data[i]);
      if (array->append_clone(&d)) {
        return error_json();
      }
    }
    Json_wrapper docw(array.release());
    *wr = std::move(docw);
  } catch (...) {
    handle_std_exception(func_name());
    return error_json();
  }
  return false;
}

#else

// dummy implementation when not compiled with fb_vector

float Item_func_fb_vector_l2::compute_distance(const float *v1 [[maybe_unused]],
                                               const float *v2 [[maybe_unused]],
                                               size_t dimension
                                               [[maybe_unused]]) {
  FB_VECTORDB_DISABLED_ERR;
  return error_real();
}

float Item_func_fb_vector_ip::compute_distance(const float *v1 [[maybe_unused]],
                                               const float *v2 [[maybe_unused]],
                                               size_t dimension
                                               [[maybe_unused]]) {
  FB_VECTORDB_DISABLED_ERR;
  return error_real();
}

bool Item_func_fb_vector_normalize_l2::val_json(Json_wrapper *wr
                                                [[maybe_unused]]) {
  FB_VECTORDB_DISABLED_ERR;
  return error_bool();
}

#endif
