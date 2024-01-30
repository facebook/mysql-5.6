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
#ifdef WITH_FB_VECTORDB
#include <faiss/Index.h>
#endif
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include "./rdb_global.h"
#include "rdb_utils.h"
#include "sql/item_fb_vector_func.h"
#include "sql/item_json_func.h"

namespace myrocks {

/** for infomation schema */
class Rdb_vector_index_info {
 public:
  int64_t m_ntotal{0};
};

/**
  faiss index wrapper. not production ready,
  a minimum implementation for testing purpose.
*/
class Rdb_vector_index {
 public:
  Rdb_vector_index(const FB_vector_index_config index_def);

  /**
    add a vector and its associated pk to the index.
  */
  uint add_vector(const std::string &pk, const std::vector<float> &value);

  uint knn_search(const std::vector<float> &value, const uint k,
                  std::vector<std::pair<std::string, float>> &result);

  Rdb_vector_index_info dump_info();

 private:
#ifdef WITH_FB_VECTORDB
  mutable std::shared_mutex m_index_mutex;
  std::unique_ptr<faiss::Index> m_index;
  std::unordered_map<faiss::idx_t, std::string> m_vector_id_pk_map;
#endif
};

/**
  one instance per handler, hold the vector buffers and knn results for the
  handler.
 */
class Rdb_vector_db_handler {
 public:
  Rdb_vector_db_handler();
  uint decode_value(Field *field, FB_vector_dimension dimension);
  /**
   get the buffer to store the vector value.
  */
  std::vector<float> &get_buffer() { return m_buffer; }

  bool has_more_results() {
    return !m_search_result.empty() &&
           m_vector_db_result_iter != m_search_result.cend();
  }

  void next_result() {
    if (has_more_results()) {
      ++m_vector_db_result_iter;
    }
  }

  const std::string &current_pk() { return m_vector_db_result_iter->first; }

  uint knn_search(Rdb_vector_index *index) {
    m_search_result.clear();
    m_vector_db_result_iter = m_search_result.cend();

    if (!m_input_vector.size() || !m_limit) return HA_ERR_END_OF_FILE;

    uint rtn = index->knn_search(m_input_vector, m_limit, m_search_result);
    if (rtn) {
      return rtn;
    }
    m_vector_db_result_iter = m_search_result.cbegin();

    return rtn;
  }

  int vector_index_orderby_init(Item *sort_func, int limit) {
    m_limit = limit;

    Fb_vector input_vector;
    Item_func *item_func = (Item_func *)sort_func;
    Item **args = ((Item_func *)item_func)->arguments();

    // input vector is expected as the second argument
    uint arg_idx = 1;
    String tmp_str;

    assert(args[0]->type() == Item::FIELD_ITEM);
    assert(args[1]->type() == Item::STRING_ITEM);

    if (parse_fb_vector_from_item(args, arg_idx, tmp_str, __FUNCTION__,
                                  input_vector))
      return HA_EXIT_FAILURE;

    m_input_vector = std::move(input_vector.data);
    return HA_EXIT_SUCCESS;
  }

  void vector_index_orderby_end() {
    // reset ORDER BY related
    m_limit = 0;
    m_input_vector.clear();
  }

 private:
  std::vector<float> m_buffer;
  std::vector<std::pair<std::string, float>> m_search_result;
  decltype(m_search_result.cbegin()) m_vector_db_result_iter;
  // input vector from the USER query
  std::vector<float> m_input_vector;
  // LIMIT associated with the ORDER BY clause
  int m_limit;
};

}  // namespace myrocks
