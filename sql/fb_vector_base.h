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
  // return false on success
  bool set_dimension(size_t n) {
    if (n > get_dimension()) {
      if (!m_own_data) {
        own_data();
      }
      m_data.resize(n, 0.0);
      return false;
    }
    // do not allow shrinking the data
    return n < get_dimension();
  }

  size_t get_dimension() {
    if (m_own_data) return m_data.size();
    return m_data_view_len;
  }

  const float *get_data_view() const {
    if (m_own_data) return m_data.data();
    return m_data_view;
  }

  /// get mutable data pointer
  float *get_data() {
    if (!m_own_data) own_data();
    return m_data.data();
  }

  std::vector<float> &get_data_ref() {
    if (!m_own_data) own_data();
    return m_data;
  }

  void set_data_view(const uchar *data, const uint32 len) {
    assert(len % sizeof(float) == 0);
    assert(data != nullptr);
    size_t num_floats = len / sizeof(float);
    m_data_view = reinterpret_cast<const float *>(data);
    m_data_view_len = num_floats;
    m_own_data = false;
  }

 private:
  std::vector<float> m_data;
  const float *m_data_view = nullptr;
  size_t m_data_view_len = 0;
  bool m_own_data = false;
  void own_data() {
    assert(m_data.empty());
    assert(!m_own_data);
    if (m_data_view_len > 0) {
      m_data.resize(m_data_view_len);
      memcpy(m_data.data(), m_data_view, m_data_view_len * sizeof(float));
    }
    m_own_data = true;
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
