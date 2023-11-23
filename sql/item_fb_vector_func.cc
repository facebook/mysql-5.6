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
#ifdef WITH_FB_VECTORDB
#include <faiss/utils/distances.h>
#endif
#include "sql-common/json_dom.h"
#include "sql/item_json_func.h"
#include "sql/sql_exception_handler.h"

namespace {
#define FB_VECTORDB_DISABLED_ERR                                            \
  do {                                                                      \
    my_error(ER_FEATURE_DISABLED, MYF(0), "vector db", "WITH_FB_VECTORDB"); \
    return error_real();                                                    \
  } while (0)

/**
 helper class to parse json to float vector
*/
class Fb_vector {
 public:
  /// the parsed json obj
  Json_wrapper wrapper;
  std::vector<float> data;

  bool set_dimension(size_t n) {
    if (n > data.size()) {
      data.resize(n, 0.0);
      return false;
    }
    // do not allow shrinking the data
    return n < data.size();
  }
};

bool parse_fb_vector(Item **args, uint arg_idx, String &str,
                     const char *func_name, Fb_vector &vector) {
  if (get_json_wrapper(args, arg_idx, &str, func_name, &vector.wrapper)) {
    return true;
  }
  if (vector.wrapper.type() != enum_json_type::J_ARRAY) {
    my_error(ER_INCORRECT_TYPE, MYF(0), std::to_string(arg_idx).c_str(),
             func_name);
    return true;
  }

  Json_array *arr = down_cast<Json_array *>(vector.wrapper.to_dom());
  vector.data.reserve(arr->size());
  for (auto it = arr->begin(); it != arr->end(); ++it) {
    const auto ele_type = (*it)->json_type();
    if (ele_type == enum_json_type::J_DOUBLE) {
      Json_double &val = down_cast<Json_double &>(**it);
      vector.data.push_back(val.value());
    } else if (ele_type == enum_json_type::J_INT) {
      Json_int &val = down_cast<Json_int &>(**it);
      vector.data.push_back(val.value());
    } else if (ele_type == enum_json_type::J_UINT) {
      Json_uint &val = down_cast<Json_uint &>(**it);
      vector.data.push_back(val.value());
    } else if (ele_type == enum_json_type::J_DECIMAL) {
      Json_decimal &val = down_cast<Json_decimal &>(**it);
      double val_double;
      if (decimal2double(val.value(), &val_double)) {
        my_error(ER_INVALID_CAST, MYF(0), "double");
        return true;
      }
      vector.data.push_back(val_double);
    } else if (ele_type == enum_json_type::J_BOOLEAN) {
      Json_boolean &val = down_cast<Json_boolean &>(**it);
      vector.data.push_back(val.value() ? 1.0 : 0.0);
    } else {
      my_error(ER_INCORRECT_TYPE, MYF(0), std::to_string(arg_idx).c_str(),
               func_name);
      return true;
    }
  }

  return false;
}
}  // anonymous namespace

Item_func_fb_vector_distance::Item_func_fb_vector_distance(THD * /* thd */,
                                                           const POS &pos,
                                                           PT_item_list *a)
    : Item_real_func(pos, a) {}

bool Item_func_fb_vector_distance::resolve_type(THD *thd) {
  if (param_type_is_default(thd, 0, 2, MYSQL_TYPE_JSON)) return true;
  set_nullable(true);

  return false;
}

Item_func::enum_const_item_cache
Item_func_fb_vector_distance::can_cache_json_arg(Item *arg) {
  // first 2 args are json, cache them
  return arg == args[0] || arg == args[1] ? CACHE_JSON_VALUE : CACHE_NONE;
}

double Item_func_fb_vector_distance::val_real() {
  if (args[0]->null_value || args[1]->null_value) {
    return error_real();
  }

  try {
    Fb_vector vector1;
    Fb_vector vector2;
    if (parse_fb_vector(args, 0, m_value, func_name(), vector1) ||
        parse_fb_vector(args, 1, m_value, func_name(), vector2)) {
      return error_real();
    }
    size_t dimension = std::max(vector1.data.size(), vector2.data.size());
    if (vector1.set_dimension(dimension) || vector2.set_dimension(dimension)) {
      assert(false);
      // should never happen
      my_error(ER_INVALID_CAST, MYF(0), "a smaller dimension");
      return error_real();
    }
    return compute_distance(vector1.data.data(), vector2.data.data(),
                            vector1.data.size());
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

Item_func_fb_vector_cosine::Item_func_fb_vector_cosine(THD *thd, const POS &pos,
                                                       PT_item_list *a)
    : Item_func_fb_vector_distance(thd, pos, a) {}

const char *Item_func_fb_vector_cosine::func_name() const {
  return "fb_vector_cosine";
}

enum Item_func::Functype Item_func_fb_vector_cosine::functype() const {
  return FB_VECTOR_COSINE;
}

#ifdef WITH_FB_VECTORDB
float Item_func_fb_vector_l2::compute_distance(float *v1, float *v2,
                                               size_t dimension) {
  return faiss::fvec_L2sqr(v1, v2, dimension);
}

float Item_func_fb_vector_ip::compute_distance(float *v1, float *v2,
                                               size_t dimension) {
  return faiss::fvec_inner_product(v1, v2, dimension);
}

float Item_func_fb_vector_cosine::compute_distance(float *v1, float *v2,
                                                   size_t dimension) {
  faiss::fvec_renorm_L2(dimension, 1, v1);
  faiss::fvec_renorm_L2(dimension, 1, v2);
  return faiss::fvec_inner_product(v1, v2, dimension);
}

#else

// dummy implementation when not compiled with fb_vector

float Item_func_fb_vector_l2::compute_distance(float *v1 [[maybe_unused]],
                                               float *v2 [[maybe_unused]],
                                               size_t dimension
                                               [[maybe_unused]]) {
  FB_VECTORDB_DISABLED_ERR;
}

float Item_func_fb_vector_ip::compute_distance(float *v1 [[maybe_unused]],
                                               float *v2 [[maybe_unused]],
                                               size_t dimension
                                               [[maybe_unused]]) {
  FB_VECTORDB_DISABLED_ERR;
}

float Item_func_fb_vector_cosine::compute_distance(float *v1 [[maybe_unused]],
                                                   float *v2 [[maybe_unused]],
                                                   size_t dimension
                                                   [[maybe_unused]]) {
  FB_VECTORDB_DISABLED_ERR;
}
#endif
