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

#pragma once

#include "lex_string.h"
#include "sql-common/json_dom.h"
#include "sql_const.h"

#ifdef WITH_FB_VECTORDB
constexpr bool FB_VECTORDB_ENABLED = true;
#else
constexpr bool FB_VECTORDB_ENABLED = false;
#endif

/**
 helper class to parse json/blob to float vector/ptr
*/
class Fb_vector {
 public:
  /// the parsed json obj
  Json_wrapper wrapper;
  std::vector<float> data;
  const float *data_view = nullptr;
  size_t data_view_len = 0;
  // remember to set own_data=true if parsed into data
  bool own_data = false;

  bool set_dimension(size_t n) {
    if (n > get_dimension()) {
      if (!own_data) {
        copy_data();
      }
      data.resize(n, 0.0);
      return false;
    }
    // do not allow shrinking the data
    return n < get_dimension();
  }

  size_t get_dimension() {
    if (own_data) return data.size();
    return data_view_len;
  }

  const float *get_data_view() const {
    if (own_data) return data.data();
    return data_view;
  }

  float *get_data() {
    if (!own_data) copy_data();
    return data.data();
  }

 private:
  void copy_data() {
    assert(data.empty());
    assert(!own_data);
    data.resize(data_view_len);
    memcpy(data.data(), data_view, data_view_len * sizeof(float));
    own_data = true;
  }
};

class Field;

enum class FB_VECTOR_INDEX_TYPE { NONE, FLAT, IVFFLAT, IVFPQ };

enum class FB_VECTOR_INDEX_METRIC { NONE, L2, IP, COSINE };

using FB_vector_dimension = uint;

class FB_vector_index_config {
 public:
  FB_vector_index_config() {}

  FB_vector_index_config(FB_VECTOR_INDEX_TYPE type,
                         FB_vector_dimension dimension,
                         LEX_CSTRING trained_index_table,
                         LEX_CSTRING trained_index_id)
      : m_type(type),
        m_dimension(dimension),
        m_trained_index_table(trained_index_table),
        m_trained_index_id(trained_index_id) {}
  FB_VECTOR_INDEX_TYPE type() const { return m_type; }
  FB_vector_dimension dimension() const { return m_dimension; }
  LEX_CSTRING trained_index_table() const { return m_trained_index_table; }
  LEX_CSTRING trained_index_id() const { return m_trained_index_id; }

 private:
  FB_VECTOR_INDEX_TYPE m_type = FB_VECTOR_INDEX_TYPE::NONE;
  FB_vector_dimension m_dimension;
  LEX_CSTRING m_trained_index_table;
  LEX_CSTRING m_trained_index_id;
};

/**
    return true on error
*/
bool parse_fb_vector_index_type(LEX_CSTRING str, FB_VECTOR_INDEX_TYPE &val);

std::string_view fb_vector_index_type_to_string(FB_VECTOR_INDEX_TYPE val);

/**
    return true on error
*/
bool parse_fb_vector_index_metric(LEX_CSTRING str, FB_VECTOR_INDEX_METRIC &val);

std::string_view fb_vector_index_metric_to_string(FB_VECTOR_INDEX_METRIC val);

// Parse field containing blob values into data_view in data
bool parse_fb_vector_from_blob(Field *field, Fb_vector &data);

// Parse json values into data
bool parse_fb_vector_from_json(Json_wrapper &wrapper, std::vector<float> &data);

bool ensure_fb_vector(const Json_dom *dom, FB_vector_dimension dimension);
