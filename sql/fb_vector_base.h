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

#ifdef WITH_FB_VECTORDB
constexpr bool FB_VECTORDB_ENABLED = true;
#else
constexpr bool FB_VECTORDB_ENABLED = false;
#endif

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

bool parse_fb_vector(Json_wrapper &wrapper, std::vector<float> &data);

bool ensure_fb_vector(const Json_dom *dom, FB_vector_dimension dimension);
