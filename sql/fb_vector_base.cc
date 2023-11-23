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

bool parse_fb_vector(Json_wrapper &wrapper, std::vector<float> &data) {
  if (wrapper.type() != enum_json_type::J_ARRAY) {
    return true;
  }

  Json_array *arr = down_cast<Json_array *>(wrapper.to_dom());
  data.clear();
  data.reserve(arr->size());
  for (auto it = arr->begin(); it != arr->end(); ++it) {
    const auto ele_type = (*it)->json_type();
    if (ele_type == enum_json_type::J_DOUBLE) {
      Json_double &val = down_cast<Json_double &>(**it);
      data.push_back(val.value());
    } else if (ele_type == enum_json_type::J_INT) {
      Json_int &val = down_cast<Json_int &>(**it);
      data.push_back(val.value());
    } else if (ele_type == enum_json_type::J_UINT) {
      Json_uint &val = down_cast<Json_uint &>(**it);
      data.push_back(val.value());
    } else if (ele_type == enum_json_type::J_DECIMAL) {
      Json_decimal &val = down_cast<Json_decimal &>(**it);
      double val_double;
      if (decimal2double(val.value(), &val_double)) {
        my_error(ER_INVALID_CAST, MYF(0), "double");
        return true;
      }
      data.push_back(val_double);
    } else if (ele_type == enum_json_type::J_BOOLEAN) {
      Json_boolean &val = down_cast<Json_boolean &>(**it);
      data.push_back(val.value() ? 1.0 : 0.0);
    } else {
      return true;
    }
  }

  return false;
}
