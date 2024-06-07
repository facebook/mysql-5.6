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

#include "sql/fb_vector_base.h"
#include <cassert>
#include <map>
#include <string_view>
#include "fb_vector_base.h"
#include "field.h"
#include "item.h"
#include "item_func.h"
#include "sql/error_handler.h"

static const std::map<std::string_view, FB_VECTOR_INDEX_TYPE>
    fb_vector_index_types{{"flat", FB_VECTOR_INDEX_TYPE::FLAT},
                          {"ivfflat", FB_VECTOR_INDEX_TYPE::IVFFLAT},
                          {"ivfpq", FB_VECTOR_INDEX_TYPE::IVFPQ}};

/**
    return true on error
*/
bool parse_fb_vector_index_type(LEX_CSTRING str, FB_VECTOR_INDEX_TYPE &val) {
  auto str_view = to_string_view(str);
  auto iter = fb_vector_index_types.find(str_view);
  if (iter == fb_vector_index_types.cend()) {
    return true;
  }
  val = iter->second;
  return false;
}

std::string_view fb_vector_index_type_to_string(FB_VECTOR_INDEX_TYPE val) {
  for (const auto &pair : fb_vector_index_types) {
    if (pair.second == val) {
      return pair.first;
    }
  }
  // this is impossible
  assert(false);
  return "";
}

static const std::map<std::string_view, FB_VECTOR_INDEX_METRIC>
    fb_vector_index_metrics{{"l2", FB_VECTOR_INDEX_METRIC::L2},
                            {"ip", FB_VECTOR_INDEX_METRIC::IP},
                            {"cosine", FB_VECTOR_INDEX_METRIC::COSINE}};

/**
    return true on error
*/
bool parse_fb_vector_index_metric(LEX_CSTRING str,
                                  FB_VECTOR_INDEX_METRIC &val) {
  auto str_view = to_string_view(str);
  auto iter = fb_vector_index_metrics.find(str_view);
  if (iter == fb_vector_index_metrics.cend()) {
    return true;
  }
  val = iter->second;
  return false;
}

std::string_view fb_vector_index_metric_to_string(FB_VECTOR_INDEX_METRIC val) {
  for (const auto &pair : fb_vector_index_metrics) {
    if (pair.second == val) {
      return pair.first;
    }
  }
  // this is impossible
  assert(false);
  return "";
}

bool parse_fb_vector_from_blob(Field *field, Fb_vector &data) {
  const Field_blob *field_blob = down_cast<const Field_blob *>(field);
  const uint32 blob_length = field_blob->get_length();
  const uchar *const blob_data = field_blob->get_blob_data();
  if (blob_length % sizeof(float)) {
    return true;
  }
  data.set_data_view(blob_data, blob_length);
  return false;
}

bool parse_fb_vector_from_json(Json_wrapper &wrapper,
                               std::vector<float> &data) {
  if (wrapper.type() != enum_json_type::J_ARRAY) {
    return true;
  }

  data.clear();
  // try to use binary value when possible, because
  // converting to dom is expensive
  if (!wrapper.is_dom()) {
    bool revert_to_dom = false;
    auto json_binary = wrapper.get_binary_value();
    data.reserve(json_binary.element_count());
    for (uint32_t i = 0; i < json_binary.element_count(); i++) {
      const auto ele = json_binary.element(i);
      const auto ele_type = ele.type();
      switch (ele_type) {
        case json_binary::Value::INT:
          data.push_back(ele.get_int64());
          break;
        case json_binary::Value::UINT:
          data.push_back(ele.get_uint64());
          break;
        case json_binary::Value::DOUBLE:
          data.push_back(ele.get_double());
          break;
        case json_binary::Value::LITERAL_TRUE:
          data.push_back(1.0);
          break;
        case json_binary::Value::LITERAL_FALSE:
          data.push_back(0.0);
          break;
        case json_binary::Value::OPAQUE:
          // opaque value need some parsing logic, revert to use to_dom.
          // this happens when we use json functions like
          // json_array(i*0.0001, i*0.0001, 0, 0).
          revert_to_dom = true;
          break;
        default:
          return true;
      }
      if (revert_to_dom) {
        break;
      }
    }
    if (!revert_to_dom) {
      return false;
    } else {
      data.clear();
    }
  }

  const Json_array *arr = down_cast<const Json_array *>(wrapper.to_dom());
  data.reserve(arr->size());
  for (auto it = arr->begin(); it != arr->end(); ++it) {
    const auto ele_type = (*it)->json_type();
    // also update ensure_fb_vector when the types here
    // changes
    Json_dom *ele = (*it).get();
    switch (ele_type) {
      case enum_json_type::J_DOUBLE:
        data.push_back(down_cast<Json_double *>(ele)->value());
        break;
      case enum_json_type::J_INT:
        data.push_back(down_cast<Json_int *>(ele)->value());
        break;
      case enum_json_type::J_UINT:
        data.push_back(down_cast<Json_uint *>(ele)->value());
        break;
      case enum_json_type::J_DECIMAL:
        double val_double;
        if (decimal2double(down_cast<Json_decimal *>(ele)->value(),
                           &val_double)) {
          my_error(ER_INVALID_CAST, MYF(0), "double");
          return true;
        }
        data.push_back(val_double);
        break;
      case enum_json_type::J_BOOLEAN:
        Json_boolean *val;
        val = down_cast<Json_boolean *>(ele);
        data.push_back(val->value() ? 1.0 : 0.0);
        break;
      default:
        return true;
    }
  }
  return false;
}

bool ensure_fb_vector(const Json_dom *dom, FB_vector_dimension dimension) {
  assert(dom);
  if (dom->json_type() != enum_json_type::J_ARRAY) {
    return true;
  }

  auto *arr = down_cast<const Json_array *>(dom);
  if (arr->size() != dimension) {
    return true;
  }

  for (auto it = arr->begin(); it != arr->end(); ++it) {
    const auto ele_type = (*it)->json_type();
    // also update parse_fb_vector_from_json when the types here
    // changes
    if (ele_type != enum_json_type::J_DOUBLE &&
        ele_type != enum_json_type::J_INT &&
        ele_type != enum_json_type::J_UINT &&
        ele_type != enum_json_type::J_DECIMAL &&
        ele_type != enum_json_type::J_BOOLEAN) {
      return true;
    }
  }

  return false;
}
