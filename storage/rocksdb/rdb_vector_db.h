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
#include <rocksdb/slice.h>
#include <rocksdb/write_batch.h>
#include <memory>
#include "./rdb_cmd_srv_helper.h"
#include "./rdb_global.h"
#include "rdb_utils.h"
#include "sql/item_fb_vector_func.h"
#include "sql/item_json_func.h"
#include "sql/sql_class.h"

namespace myrocks {

/** for infomation schema */
class Rdb_vector_index_info {
 public:
  /**
    total number of vectors, this is populated when
    scanning the index, not garanteed to be accurate.
   */
  int64_t m_ntotal{0};
  /**
   number of time the index is used for knn search
  */
  uint m_hit{0};

  /**
    stats for ivf lists
  */
  uint m_min_list_size{0};
  uint m_max_list_size{0};
  uint m_avg_list_size{0};
  uint m_median_list_size{0};
};

class Rdb_vector_search_params {
 public:
  FB_VECTOR_INDEX_METRIC m_metric = FB_VECTOR_INDEX_METRIC::NONE;
  uint m_k = 0;
  uint m_batch_size = 0;
  uint m_nprobe = 0;
};

/**
  vector index base class
*/
class Rdb_vector_index {
 public:
  Rdb_vector_index() = default;
  virtual ~Rdb_vector_index() = default;
  /**
    add a vector and its associated pk to the index.
  */
  virtual uint add_vector(rocksdb::WriteBatchBase *write_batch,
                          const rocksdb::Slice &pk, std::vector<float> &value,
                          const rocksdb::Slice &old_pk,
                          std::vector<float> &old_value) = 0;

  /**
    add a vector and its associated pk to the index.
  */
  virtual uint delete_vector(rocksdb::WriteBatchBase *write_batch,
                             const rocksdb::Slice &pk,
                             std::vector<float> &old_value) = 0;

  virtual uint knn_search(
      THD *thd, std::vector<float> &query_vector,
      Rdb_vector_search_params &params,
      std::vector<std::pair<std::string, float>> &result) = 0;

  /**
    scans all vectors in index and populate counters
  */
  virtual uint analyze(THD *thd, uint64_t max_num_rows_scanned,
                       std::atomic<THD::killed_state> *killed) = 0;

  virtual Rdb_vector_index_info dump_info() = 0;

  virtual FB_vector_dimension dimension() const = 0;

  virtual uint setup(const std::string &db_name [[maybe_unused]],
                     Rdb_cmd_srv_helper &cmd_srv_helper [[maybe_unused]]) {
    return HA_EXIT_SUCCESS;
  }
};

uint create_vector_index(Rdb_cmd_srv_helper &cmd_srv_helper,
                         const std::string &db_name,
                         const FB_vector_index_config index_def,
                         std::shared_ptr<rocksdb::ColumnFamilyHandle> cf_handle,
                         const Index_id index_id,
                         std::unique_ptr<Rdb_vector_index> &index);

/**
  one instance per handler, hold the vector buffers and knn results for the
  handler.
 */
class Rdb_vector_db_handler {
 public:
  Rdb_vector_db_handler();
  uint decode_value(Field *field, FB_vector_dimension dimension) {
    return decode_value_to_buffer(field, dimension, m_buffer);
  }
  uint decode_value2(Field *field, FB_vector_dimension dimension) {
    return decode_value_to_buffer(field, dimension, m_buffer2);
  }
  /**
   get the buffer to store the vector value.
  */
  std::vector<float> &get_buffer() { return m_buffer; }
  std::vector<float> &get_buffer2() { return m_buffer2; }

  bool has_more_results() {
    return !m_search_result.empty() &&
           m_vector_db_result_iter != m_search_result.cend();
  }

  void next_result() {
    if (has_more_results()) {
      ++m_vector_db_result_iter;
    }
  }

  std::string current_pk(const Index_id pk_index_id) const;

  uint knn_search(THD *thd, Rdb_vector_index *index);

  int vector_index_orderby_init(Item *sort_func, int limit, uint batch_size,
                                uint nprobe, uint limit_multiplier) {
    m_limit = limit;
    m_limit_multiplier = limit_multiplier;
    m_batch_size = batch_size;
    m_nprobe = nprobe;

    Fb_vector input_vector;
    Item_func *item_func = (Item_func *)sort_func;
    Item **args = ((Item_func *)item_func)->arguments();

    auto functype = item_func->functype();
    if (functype == Item_func::FB_VECTOR_L2) {
      m_metric = FB_VECTOR_INDEX_METRIC::L2;
    } else if (functype == Item_func::FB_VECTOR_IP) {
      m_metric = FB_VECTOR_INDEX_METRIC::IP;
    } else {
      // should never happen
      assert(false);
      return HA_ERR_UNSUPPORTED;
    }

    // input vector is expected as the second argument
    uint arg_idx = 1;
    String tmp_str;

    assert((args[0]->type() == Item::FIELD_ITEM) &&
           (args[0]->data_type() == MYSQL_TYPE_JSON));

    assert(((args[1]->type() == Item::STRING_ITEM) &&
           (args[1]->data_type() == MYSQL_TYPE_VARCHAR)) ||
          ((args[1]->type() == Item::CACHE_ITEM) &&
           (args[1]->data_type() == MYSQL_TYPE_JSON)));

    if (parse_fb_vector_from_item(args, arg_idx, tmp_str, __FUNCTION__,
                                  input_vector))
      return HA_EXIT_FAILURE;

    m_buffer = std::move(input_vector.data);
    return HA_EXIT_SUCCESS;
  }

  void vector_index_orderby_end() {
    m_metric = FB_VECTOR_INDEX_METRIC::NONE;
    // reset ORDER BY related
    m_limit = 0;
    m_limit_multiplier = 0;
    m_nprobe = 0;
    m_buffer.clear();
  }

 private:
  // input vector from the USER query,
  // new vector for index write
  std::vector<float> m_buffer;
  // old vector for index write
  std::vector<float> m_buffer2;
  std::vector<std::pair<std::string, float>> m_search_result;
  decltype(m_search_result.cbegin()) m_vector_db_result_iter;
  FB_VECTOR_INDEX_METRIC m_metric = FB_VECTOR_INDEX_METRIC::NONE;
  // LIMIT associated with the ORDER BY clause
  uint m_limit;
  uint m_batch_size;
  uint m_nprobe;
  uint m_limit_multiplier;

  uint decode_value_to_buffer(Field *field, FB_vector_dimension dimension,
                              std::vector<float> &buffer);
};

}  // namespace myrocks
