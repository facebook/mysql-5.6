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

#include "./rdb_vector_db.h"
#include <sys/types.h>
#include <algorithm>
#include <cstddef>
#include <memory>
#include <string_view>
#include "ha_rocksdb.h"
#include "rdb_buff.h"
#include "rdb_cmd_srv_helper.h"
#include "rdb_global.h"
#include "rdb_utils.h"
#include "sql-common/json_dom.h"
#include "sql/field.h"
#ifdef WITH_FB_VECTORDB
#include <faiss/IndexFlat.h>
#include <faiss/IndexIVFFlat.h>
#include <faiss/utils/Heap.h>
#include <faiss/utils/distances.h>
#endif
#include <rocksdb/db.h>

namespace myrocks {

#ifdef WITH_FB_VECTORDB
namespace {
// list number for flat index
constexpr size_t LIST_NUMBER_FLAT = 0;
// vector ids are generated in read time.
// use this dummy value for apis require passing vector ids.
constexpr faiss::idx_t DUMMY_VECTOR_ID = 42;

static void write_inverted_list_key(Rdb_string_writer &writer,
                                    const Index_id index_id,
                                    const size_t list_id) {
  writer.write_index_id(index_id);
  writer.write_uint64(list_id);
}

/**
  rocksdb key for vectors
  key format is:
  index_id + list_id + pk
 */
static void write_inverted_list_item_key(Rdb_string_writer &writer,
                                         const Index_id index_id,
                                         const size_t list_id,
                                         const rocksdb::Slice &pk) {
  write_inverted_list_key(writer, index_id, list_id);
  assert(pk.size() > INDEX_NUMBER_SIZE);
  rocksdb::Slice pk_without_index_id{pk};
  pk_without_index_id.remove_prefix(INDEX_NUMBER_SIZE);
  writer.write_slice(pk_without_index_id);
}

/**
read and verify key prefix
*/
static uint read_inverted_list_key(Rdb_string_reader &reader,
                                   const Index_id index_id, size_t list_id) {
  Index_id actual_index_id;
  if (reader.read_index_id(&actual_index_id)) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "Failed to read index id for key in index %d", index_id);
    return HA_ERR_ROCKSDB_CORRUPT_DATA;
  }
  if (actual_index_id != index_id) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "Invalid index id for key in index %d, actual value %d",
                    index_id, actual_index_id);
    assert(false);
    return HA_ERR_ROCKSDB_CORRUPT_DATA;
  }

  size_t actual_list_id;
  if (reader.read_uint64(&actual_list_id)) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "Failed to read list id for key in index %d", index_id);
    return HA_ERR_ROCKSDB_CORRUPT_DATA;
  }
  if (actual_list_id != list_id) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "Invalid list id for key in index %d, actual value %lu",
                    index_id, actual_list_id);
    return HA_ERR_ROCKSDB_CORRUPT_DATA;
  }
  return HA_EXIT_SUCCESS;
}

/**
  context passed to inverted list.
  no need to synchronize here, as we set openmp threads to 1.
*/
class Rdb_faiss_inverted_list_context {
 public:
  explicit Rdb_faiss_inverted_list_context(THD *thd) : m_thd(thd) {}
  THD *m_thd;
  uint m_error = HA_EXIT_SUCCESS;
  std::size_t m_current_list_size = 0;
  // list id to list size pairs
  std::vector<std::pair<std::size_t, std::size_t>> m_list_size_stats;

  void on_iterator_end(std::size_t list_id) {
    if (!m_error) {
      // only record list size when there is no error
      m_list_size_stats.push_back({list_id, m_current_list_size});
    }
    m_current_list_size = 0;
  }

  void on_iterator_record() { m_current_list_size++; }

  faiss::idx_t add_pk(const std::string &pk) {
    auto vector_id = m_vector_id++;
    m_vectorid_pk.emplace(vector_id, pk);
    return vector_id;
  }

  uint populate_result(std::vector<faiss::idx_t> &vector_ids,
                       std::vector<float> &distances,
                       std::vector<std::pair<std::string, float>> &result) {
    for (uint i = 0; i < vector_ids.size(); i++) {
      auto vector_id = vector_ids[i];
      if (vector_id < 0) {
        break;
      }
      auto iter = m_vectorid_pk.find(vector_id);
      if (iter == m_vectorid_pk.end()) {
        LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                        "Failed to find matching pk for %lu", vector_id);
        return HA_EXIT_FAILURE;
      }
      result.emplace_back(iter->second, distances[i]);
    }
    return HA_EXIT_SUCCESS;
  }

 private:
  std::map<faiss::idx_t, std::string> m_vectorid_pk;
  // current vector id
  faiss::idx_t m_vector_id = 1024;
};

/**
  context passed to inverted list for adding vectors.
  no need to synchronize here, as we set openmp threads to 1.
*/
class Rdb_faiss_inverted_list_write_context {
 public:
  Rdb_faiss_inverted_list_write_context(rocksdb::WriteBatchBase *wb,
                                        const rocksdb::Slice &pk)
      : m_write_batch(wb), m_pk(pk) {}
  rocksdb::WriteBatchBase *m_write_batch;
  const rocksdb::Slice &m_pk;
  rocksdb::Status m_status;
};

/**
  iterate a inverted list
*/
class Rdb_vector_iterator : public faiss::InvertedListsIterator {
 public:
  Rdb_vector_iterator(Rdb_faiss_inverted_list_context *context,
                      Index_id index_id, rocksdb::ColumnFamilyHandle *const cf,
                      FB_vector_dimension dimension, const size_t list_id)
      : m_context(context),
        m_index_id(index_id),
        m_list_id(list_id),
        m_dimension(dimension) {
    Rdb_string_writer lower_key_writer;
    write_inverted_list_key(lower_key_writer, index_id, list_id);
    m_iterator_lower_bound_key.PinSelf(lower_key_writer.to_slice());

    Rdb_string_writer upper_key_writer;
    write_inverted_list_key(upper_key_writer, index_id, list_id + 1);
    m_iterator_upper_bound_key.PinSelf(upper_key_writer.to_slice());
    m_iterator.reset(rdb_tx_get_iterator(
        context->m_thd, cf, /* skip_bloom_filter */ true,
        m_iterator_lower_bound_key, m_iterator_upper_bound_key,
        /* snapshot */ nullptr, TABLE_TYPE::USER_TABLE));
    m_iterator->SeekToFirst();
  }

  bool is_available() const override {
    bool available = !m_context->m_error && m_iterator->Valid();
    if (!available) {
      m_context->on_iterator_end(m_list_id);
    }
    return available;
  }

  void next() override { m_iterator->Next(); }

  uint get_pk_and_codes(std::string &pk, rocksdb::Slice &codes) {
    rocksdb::Slice key = m_iterator->key();
    Rdb_string_reader key_reader(&key);
    uint rtn = read_inverted_list_key(key_reader, m_index_id, m_list_id);
    if (rtn) {
      return rtn;
    }
    const auto pk_size = key_reader.remaining_bytes();
    if (pk_size == 0) {
      LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                      "Invalid pk in index %d, list id %lu", m_index_id,
                      m_list_id);
      return HA_ERR_ROCKSDB_CORRUPT_DATA;
    }
    // copy the pk bytes
    pk = std::string_view(key_reader.get_current_ptr(), pk_size);

    rocksdb::Slice value = m_iterator->value();
    if (value.size() != m_dimension * sizeof(float)) {
      LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                      "Invalid value size %lu for key in index %d, list id %lu",
                      value.size(), m_index_id, m_list_id);
      return HA_ERR_ROCKSDB_CORRUPT_DATA;
    }
    codes = value;
    m_context->on_iterator_record();
    return HA_EXIT_SUCCESS;
  }

  /**
   get the pk for the vector and fill codes with vector codes
  */
  uint get_pk_and_codes(std::string &pk, std::vector<float> &codes) {
    rocksdb::Slice value;
    uint rtn = get_pk_and_codes(pk, value);
    if (rtn) {
      return rtn;
    }
    codes.resize(codes.size() + m_dimension);
    std::copy(reinterpret_cast<const float *>(value.data()),
              reinterpret_cast<const float *>(value.data() + value.size()),
              codes.end() - m_dimension);
    return HA_EXIT_SUCCESS;
  }

  std::pair<faiss::idx_t, const uint8_t *> get_id_and_codes() override {
    std::string pk;
    rocksdb::Slice codes;
    uint rtn = get_pk_and_codes(pk, codes);
    if (rtn) {
      // set error to context so faiss can stop iterating
      m_context->m_error = rtn;
      // return some dummy data to faiss so it does not crash
      faiss::idx_t vector_id = 42;
      m_codes_buffer.resize(m_dimension * sizeof(float));
      return {vector_id, m_codes_buffer.data()};
    }

    faiss::idx_t vector_id = m_context->add_pk(pk);
    return {vector_id, reinterpret_cast<const uint8_t *>(codes.data())};
  }

 private:
  Rdb_faiss_inverted_list_context *m_context;
  Index_id m_index_id;
  size_t m_list_id;
  FB_vector_dimension m_dimension;
  std::unique_ptr<rocksdb::Iterator> m_iterator;
  rocksdb::PinnableSlice m_iterator_lower_bound_key;
  rocksdb::PinnableSlice m_iterator_upper_bound_key;
  std::vector<uint8_t> m_codes_buffer;
};

/**
  faiss inverted list implementation.
  throws exceptions for methods that are not used for our use case.
*/
class Rdb_faiss_inverted_list : public faiss::InvertedLists {
 public:
  Rdb_faiss_inverted_list(Index_id index_id,
                          rocksdb::ColumnFamilyHandle *const cf, uint nlist,
                          FB_vector_dimension dimension, uint code_size)
      : InvertedLists(nlist, code_size),
        m_index_id(index_id),
        m_dimension(dimension),
        m_cf(cf) {
    use_iterator = true;
  }
  ~Rdb_faiss_inverted_list() override = default;

  size_t list_size(size_t list_no) const override {
    throw std::runtime_error(std::string("unexpected function call ") +
                             __PRETTY_FUNCTION__);
  }

  faiss::InvertedListsIterator *get_iterator(
      size_t list_no, void *inverted_list_context) const override {
    // faiss is responsible for releasing the iterator object
    assert(inverted_list_context);
    return new Rdb_vector_iterator(
        reinterpret_cast<Rdb_faiss_inverted_list_context *>(
            inverted_list_context),
        m_index_id, m_cf, m_dimension, list_no);
  }

  const uint8_t *get_codes(size_t list_no) const override {
    throw std::runtime_error(std::string("unexpected function call ") +
                             __PRETTY_FUNCTION__);
  }

  const faiss::idx_t *get_ids(size_t list_no) const override {
    throw std::runtime_error(std::string("unexpected function call ") +
                             __PRETTY_FUNCTION__);
  }

  size_t add_entry(size_t list_no, faiss::idx_t theid, const uint8_t *code,
                   void *inverted_list_context) override {
    assert(theid == DUMMY_VECTOR_ID);
    assert(inverted_list_context);
    Rdb_faiss_inverted_list_write_context *context =
        reinterpret_cast<Rdb_faiss_inverted_list_write_context *>(
            inverted_list_context);
    Rdb_string_writer key_writer;
    write_inverted_list_item_key(key_writer, m_index_id, list_no,
                                 context->m_pk);

    rocksdb::Slice value_slice(reinterpret_cast<const char *>(code), code_size);
    rocksdb::Status status =
        context->m_write_batch->Put(m_cf, key_writer.to_slice(), value_slice);
    if (!status.ok()) {
      LogPluginErrMsg(
          INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
          "Failed to put inverted list entry, list no %lu, error %s", list_no,
          status.ToString().c_str());
      context->m_status = status;
    }

    // the return value is the offset in the list, not used for our use case.
    // always return 0 here.
    return 0;
  }

  size_t add_entries(size_t list_no, size_t n_entry, const faiss::idx_t *ids,
                     const uint8_t *code) override {
    throw std::runtime_error(std::string("unexpected function call ") +
                             __PRETTY_FUNCTION__);
  }

  void update_entries(size_t list_no, size_t offset, size_t n_entry,
                      const faiss::idx_t *ids, const uint8_t *code) override {
    throw std::runtime_error(std::string("unexpected function call ") +
                             __PRETTY_FUNCTION__);
  }

  void resize(size_t list_no, size_t new_size) override {
    throw std::runtime_error(std::string("unexpected function call ") +
                             __PRETTY_FUNCTION__);
  }

 private:
  Index_id m_index_id;
  FB_vector_dimension m_dimension;
  rocksdb::ColumnFamilyHandle *const m_cf;
};

/**
  maintain the KNN search state.
  2 arrays of size 3*k are allocated to hold the codes and ids.
  the arrays are divided to 3 parts, each with size k.
  | merge result | current result | previous result |

  after processing each batch of vectors,
  1. compute KNN for the current batch, store the result to the
     current result part.
  2. merge the current result with the previous result, store the
     result to the merge result part.
  3. copy merge result to previous result.
  after each batch, the merge result will hold the KNN result for all
  the vectors processed so far.

  The current result and previous result parts need to be continuous
  because of the way faiss::merge_knn_results is implemented.

*/
class Rdb_vector_knn_computer {
 public:
  Rdb_vector_knn_computer(const std::vector<float> &query_vector, const uint k,
                          const FB_vector_dimension dimension,
                          const FB_VECTOR_INDEX_METRIC metric,
                          const uint batch_size)
      : m_dimension{dimension}, m_k{k}, m_query_vector(query_vector) {
    assert(query_vector.size() == dimension);
    m_codes_batch_size = batch_size * dimension;
    m_vector_codes.reserve(m_codes_batch_size);
    init_heap(metric);
    m_id_pk_map.reserve(k);
    m_id_pk_map_buf.reserve(k);
    m_batch_id_pk_map.reserve(batch_size);
    // start the id with a none zero value, to make it easier to discover id
    // assigning and id updating bugs.
    m_current_id = m_id_offset = 1024;
  }

  /**
   read vector from iterator and update the knn result
  */
  uint update_knn(Rdb_vector_iterator &vector_iter) {
    auto iter_rtn = vector_iter.get_pk_and_codes(m_current_pk, m_vector_codes);
    if (iter_rtn) {
      return iter_rtn;
    }
    assert(m_vector_codes.size() % m_dimension == 0);
    m_batch_id_pk_map.emplace(m_current_id, m_current_pk);
    m_current_id++;
    return compute_knn(false);
  }

  uint finalize() {
    if (m_vector_codes.size() > 0) {
      return compute_knn(true);
    }
    return HA_EXIT_SUCCESS;
  }

  uint populate_result(std::vector<std::pair<std::string, float>> &result) {
    result.clear();

    for (uint i = 0; i < m_k; i++) {
      const auto vector_id = m_merge_result_ids[i];
      if (vector_id >= 0) {
        const auto it = m_id_pk_map.find(vector_id);
        if (it != m_id_pk_map.end()) {
          result.emplace_back(it->second, m_merge_result_vals[i]);
        } else {
          // should never happen
          LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                          "Cannot find vector id %ld in id pk map", vector_id);
          return HA_EXIT_FAILURE;
        }
      } else {
        // less than k results, no need to populate any more
        break;
      }
    }
    return HA_EXIT_SUCCESS;
  }

 private:
  FB_vector_dimension m_dimension;
  uint m_k;
  const std::vector<float> &m_query_vector;
  std::vector<float> m_vector_codes;
  decltype(m_vector_codes.size()) m_codes_batch_size;
  std::string m_current_pk;

  // heap id storage
  std::vector<faiss::idx_t> m_vector_ids;
  // heap value storage
  std::vector<float> m_vals;
  // pointers to the heap data
  faiss::idx_t *m_merge_result_ids;
  float *m_merge_result_vals;
  faiss::idx_t *m_current_result_ids;
  float *m_current_result_vals;
  faiss::idx_t *m_previous_result_ids;
  float *m_previous_result_vals;
  // if max heap is used
  bool m_is_maxheap;
  faiss::float_maxheap_array_t m_maxheap;
  faiss::float_maxheap_array_t m_minheap;

  // id to pk map for current result
  std::unordered_map<faiss::idx_t, std::string> m_id_pk_map;
  // used to compute m_id_pk_map
  std::unordered_map<faiss::idx_t, std::string> m_id_pk_map_buf;
  // id to pk map for current batch
  std::unordered_map<faiss::idx_t, std::string> m_batch_id_pk_map;
  // used to fix the ids returned from faiss
  faiss::idx_t m_id_offset;
  // current vector id
  faiss::idx_t m_current_id;

  faiss::idx_t *get_ids(size_t key) {
    if (m_is_maxheap) {
      return m_maxheap.get_ids(key);
    }
    return m_minheap.get_ids(key);
  }

  float *get_vals(size_t key) {
    if (m_is_maxheap) {
      return m_maxheap.get_val(key);
    }
    return m_minheap.get_val(key);
  }

  void init_heap(FB_VECTOR_INDEX_METRIC metric) {
    m_is_maxheap = (metric == FB_VECTOR_INDEX_METRIC::L2);
    constexpr size_t n_heaps = 3;
    m_vector_ids.resize(n_heaps * m_k);
    m_vals.resize(n_heaps * m_k);
    if (m_is_maxheap) {
      m_maxheap = {.nh = n_heaps,
                   .k = m_k,
                   .ids = m_vector_ids.data(),
                   .val = m_vals.data()};
      m_maxheap.heapify();
    } else {
      m_minheap = {.nh = n_heaps,
                   .k = m_k,
                   .ids = m_vector_ids.data(),
                   .val = m_vals.data()};
      m_minheap.heapify();
    }
    m_merge_result_ids = get_ids(0);
    m_merge_result_vals = get_vals(0);
    m_current_result_ids = get_ids(1);
    m_current_result_vals = get_vals(1);
    m_previous_result_ids = get_ids(2);
    m_previous_result_vals = get_vals(2);
  }

  // merge current_result_vals and prev_result_vals to merge_result_vals
  void merge_knn_results() {
    constexpr size_t query_vector_count = 1;
    constexpr int heaps_to_merge = 2;

    if (m_is_maxheap) {
      // the comparator is reversed w.r.t. the heap. see merge_knn_results
      // comments.
      faiss::merge_knn_results<faiss::idx_t, faiss::CMin<float, int>>(
          query_vector_count, m_k, heaps_to_merge, m_current_result_vals,
          m_current_result_ids, m_merge_result_vals, m_merge_result_ids);
    } else {
      faiss::merge_knn_results<faiss::idx_t, faiss::CMax<float, int>>(
          query_vector_count, m_k, heaps_to_merge, m_current_result_vals,
          m_current_result_ids, m_merge_result_vals, m_merge_result_ids);
    }
  }

  void compute_knn_inner() {
    constexpr size_t query_vector_count = 1;
    const size_t vector_count = m_vector_codes.size() / m_dimension;

    if (m_is_maxheap) {
      faiss::knn_L2sqr(m_query_vector.data(), m_vector_codes.data(),
                       m_dimension, query_vector_count, vector_count, m_k,
                       m_current_result_vals, m_current_result_ids);

    } else {
      faiss::knn_inner_product(m_query_vector.data(), m_vector_codes.data(),
                               m_dimension, query_vector_count, vector_count,
                               m_k, m_current_result_vals,
                               m_current_result_ids);
    }

    // update the ids of current result with offset
    for (uint i = 0; i < m_k; i++) {
      if (m_current_result_ids[i] < 0) {
        // less than k results, skip the rest
        break;
      }
      m_current_result_ids[i] += m_id_offset;
    }
  }

  uint compute_knn(const bool finalize) {
    // do not compute when we haven't reached batch size
    if (!finalize && m_vector_codes.size() < m_codes_batch_size) {
      return HA_EXIT_SUCCESS;
    }

    assert(m_current_id > 0);

    compute_knn_inner();
    merge_knn_results();

    // calculate current id to pk map. we do not want to hold id to pk
    // map for all vectors, so we only keep the ids that are still in
    // the top k results.
    // iterate ids in the current result, the id is either in previous
    // result, which is stored in m_id_pk_map, or in the current batch
    // which is stored in m_batch_id_pk_map.
    // store the result in m_id_pk_map_buf, at the end, swap it with
    // m_id_pk_map.
    for (uint i = 0; i < m_k; i++) {
      const auto vector_id = m_merge_result_ids[i];
      if (vector_id < 0) {
        // less than k results, skip the rest
        break;
      }
      // try to find the id in previous result
      const auto it = m_id_pk_map.find(vector_id);
      if (it == m_id_pk_map.end()) {
        // not in previous result, must be in the current batch
        auto batch_map_it = m_batch_id_pk_map.find(vector_id);
        if (batch_map_it != m_batch_id_pk_map.end()) {
          m_id_pk_map_buf.emplace(vector_id, batch_map_it->second);
        } else {
          // should never happen
          LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                          "Cannot find vector id %ld in batch id pk map",
                          vector_id);
          return HA_EXIT_FAILURE;
        }
      } else {
        m_id_pk_map_buf.emplace(vector_id, it->second);
      }
    }

    m_id_pk_map.swap(m_id_pk_map_buf);
    m_id_pk_map_buf.clear();
    m_batch_id_pk_map.clear();
    m_vector_codes.clear();
    m_id_offset = m_current_id;

    if (!finalize) {
      // copy the merge result to be merged next time,
      // dont' need to do that if it's the last batch
      memcpy(m_previous_result_vals, m_merge_result_vals, m_k * sizeof(float));
      memcpy(m_previous_result_ids, m_merge_result_ids,
             m_k * sizeof(faiss::idx_t));
    }

    return HA_EXIT_SUCCESS;
  }
};

class Rdb_vector_index_base : public Rdb_vector_index {
 public:
  Rdb_vector_index_base(const FB_vector_index_config index_def,
                        std::shared_ptr<rocksdb::ColumnFamilyHandle> cf_handle,
                        const Index_id index_id)
      : m_index_id{index_id}, m_index_def{index_def}, m_cf_handle{cf_handle} {}

  virtual ~Rdb_vector_index_base() override = default;

  uint add_vector(rocksdb::WriteBatchBase *write_batch,
                  const rocksdb::Slice &pk, std::vector<float> &value,
                  const rocksdb::Slice &old_pk,
                  std::vector<float> &old_value) override {
    uint rtn = HA_EXIT_SUCCESS;
    bool delete_old_value = false;
    uint64 new_list_id = get_list_id(value);
    uint64 old_list_id = 0;
    if (!old_pk.empty()) {
      bool pk_changed = old_pk.compare(pk) != 0;
      old_list_id = get_list_id(old_value);
      if (pk_changed) {
        delete_old_value = true;
      } else {
        if (old_list_id != new_list_id) {
          delete_old_value = true;
        }
      }
    }
    if (delete_old_value) {
      rtn = delete_vector_from_list(write_batch, old_list_id, old_pk);
      if (rtn) {
        return rtn;
      }
    }
    return add_vector_to_list(write_batch, new_list_id, pk, value);
  }

  uint delete_vector(rocksdb::WriteBatchBase *write_batch,
                     const rocksdb::Slice &pk,
                     std::vector<float> &old_value) override {
    const uint64 list_id = get_list_id(old_value);
    return delete_vector_from_list(write_batch, list_id, pk);
  }

  FB_vector_dimension dimension() const override {
    return m_index_def.dimension();
  }

 protected:
  Index_id m_index_id;
  FB_vector_index_config m_index_def;
  std::shared_ptr<rocksdb::ColumnFamilyHandle> m_cf_handle;
  std::atomic<uint> m_hit{0};

  // the number of vectors in the inverted list
  virtual uint64 get_list_id(const std::vector<float> &value) const = 0;
  virtual uint add_vector_to_list(rocksdb::WriteBatchBase *write_batch,
                                  faiss::idx_t list_id,
                                  const rocksdb::Slice &pk,
                                  std::vector<float> &value) = 0;

  uint delete_vector_from_list(rocksdb::WriteBatchBase *write_batch,
                               const uint64 list_id, const rocksdb::Slice &pk) {
    Rdb_string_writer key_writer;
    write_inverted_list_item_key(key_writer, m_index_id, LIST_NUMBER_FLAT, pk);
    auto status = write_batch->Delete(m_cf_handle.get(), key_writer.to_slice());
    if (!status.ok()) {
      LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                      "Failed to write codes for index %d", m_index_id);
      return ha_rocksdb::rdb_error_to_mysql(status);
    }
    return HA_EXIT_SUCCESS;
  }
};

/**
 stores vector codes in a flat list
*/
class Rdb_vector_index_flat : public Rdb_vector_index_base {
 public:
  Rdb_vector_index_flat(const FB_vector_index_config index_def,
                        std::shared_ptr<rocksdb::ColumnFamilyHandle> cf_handle,
                        const Index_id index_id)
      : Rdb_vector_index_base(index_def, cf_handle, index_id) {
    assert(index_def.type() == FB_VECTOR_INDEX_TYPE::FLAT);
  }

  virtual ~Rdb_vector_index_flat() override = default;

  uint knn_search(THD *thd, std::vector<float> &query_vector,
                  Rdb_vector_search_params &params,
                  std::vector<std::pair<std::string, float>> &result) override {
    const auto metric = params.m_metric;
    if (metric != FB_VECTOR_INDEX_METRIC::L2 &&
        metric != FB_VECTOR_INDEX_METRIC::IP) {
      LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG, "Unsupported metric %d",
                      metric);
      return HA_ERR_UNSUPPORTED;
    }

    m_hit++;

    const auto k = params.m_k;
    // there is no point of having a batch of less than k. set the
    // batch size as 2k or batch size, whichever is bigger.
    uint batch_size = std::max(2 * k, params.m_batch_size);
    const auto dimension = m_index_def.dimension();
    Rdb_faiss_inverted_list_context context(thd);
    Rdb_vector_iterator vector_iter(&context, m_index_id, m_cf_handle.get(),
                                    dimension, get_list_id(query_vector));
    uint rtn;
    Rdb_vector_knn_computer computer(query_vector, k, dimension, metric,
                                     batch_size);
    int64_t keys_scanned = 0;
    while (vector_iter.is_available()) {
      rtn = computer.update_knn(vector_iter);
      keys_scanned++;
      if (rtn) {
        return rtn;
      }
      vector_iter.next();
    }
    m_ntotal = keys_scanned;
    rtn = computer.finalize();
    if (rtn) {
      return rtn;
    }
    rtn = computer.populate_result(result);
    if (rtn) {
      return rtn;
    }
    return HA_EXIT_SUCCESS;
  }

  virtual uint analyze(THD *thd, uint64_t max_num_rows_scanned,
                       std::atomic<THD::killed_state> *killed) override {
    Rdb_faiss_inverted_list_context context(thd);
    Rdb_vector_iterator vector_iter(&context, m_index_id, m_cf_handle.get(),
                                    m_index_def.dimension(), LIST_NUMBER_FLAT);
    uint64_t ntotal = 0;
    std::string pk;
    rocksdb::Slice codes;
    while (vector_iter.is_available()) {
      uint rtn = vector_iter.get_pk_and_codes(pk, codes);
      if (rtn) {
        return rtn;
      }
      ntotal++;
      if (max_num_rows_scanned > 0 && ntotal > max_num_rows_scanned) {
        return HA_EXIT_SUCCESS;
      }
      if (killed && *killed) {
        return HA_EXIT_FAILURE;
      }
      vector_iter.next();
    }
    m_ntotal = ntotal;
    return HA_EXIT_SUCCESS;
  }

  Rdb_vector_index_info dump_info() override {
    return {.m_ntotal = m_ntotal, .m_hit = m_hit};
  }

 protected:
  // always return the same list id
  uint64 get_list_id(const std::vector<float> &value) const override {
    return LIST_NUMBER_FLAT;
  }

  uint add_vector_to_list(rocksdb::WriteBatchBase *write_batch,
                          faiss::idx_t list_id, const rocksdb::Slice &pk,
                          std::vector<float> &value) override {
    Rdb_string_writer key_writer;
    write_inverted_list_item_key(key_writer, m_index_id, list_id, pk);
    rocksdb::Slice value_slice((const char *)value.data(),
                               value.size() * sizeof(float));

    auto status =
        write_batch->Put(m_cf_handle.get(), key_writer.to_slice(), value_slice);
    if (!status.ok()) {
      LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                      "Failed to write codes for index %d", m_index_id);
      return ha_rocksdb::rdb_error_to_mysql(status);
    }
    return HA_EXIT_SUCCESS;
  }

 private:
  std::atomic<int64_t> m_ntotal{0};
};

class Rdb_vector_index_ivf : public Rdb_vector_index_base {
 public:
  Rdb_vector_index_ivf(const FB_vector_index_config index_def,
                       std::shared_ptr<rocksdb::ColumnFamilyHandle> cf_handle,
                       const Index_id index_id)
      : Rdb_vector_index_base(index_def, cf_handle, index_id) {}

  ~Rdb_vector_index_ivf() override = default;

  virtual uint knn_search(
      THD *thd, std::vector<float> &query_vector,
      Rdb_vector_search_params &params,
      std::vector<std::pair<std::string, float>> &result) override {
    m_hit++;
    faiss::IndexIVF *index = m_index_l2.get();
    if (params.m_metric == FB_VECTOR_INDEX_METRIC::IP) {
      index = m_index_ip.get();
    }
    faiss::idx_t k = params.m_k;
    std::vector<faiss::idx_t> vector_ids(k);
    std::vector<float> distances(k);
    constexpr faiss::idx_t vector_count = 1;
    faiss::IVFSearchParameters search_params;
    // TODO make it configurable
    search_params.nprobe = 16;
    Rdb_faiss_inverted_list_context context(thd);
    search_params.inverted_list_context = &context;
    index->search(vector_count, query_vector.data(), k, distances.data(),
                  vector_ids.data(), &search_params);
    if (context.m_error) {
      return context.m_error;
    }
    auto rtn = context.populate_result(vector_ids, distances, result);
    if (rtn) {
      return rtn;
    }

    // update counters
    for (auto &list_size_entry : context.m_list_size_stats) {
      m_list_size_stats[list_size_entry.first] = list_size_entry.second;
    }
    return HA_EXIT_SUCCESS;
  }

  virtual uint analyze(THD *thd, uint64_t max_num_rows_scanned,
                       std::atomic<THD::killed_state> *killed) override {
    std::string pk;
    rocksdb::Slice codes;
    uint64_t ntotal = 0;
    for (std::size_t i = 0; i < m_list_size_stats.size(); i++) {
      std::size_t list_size = 0;
      Rdb_faiss_inverted_list_context context(thd);
      Rdb_vector_iterator vector_iter(&context, m_index_id, m_cf_handle.get(),
                                      m_index_def.dimension(), i);
      while (vector_iter.is_available()) {
        uint rtn = vector_iter.get_pk_and_codes(pk, codes);
        if (rtn) {
          return rtn;
        }
        list_size++;
        ntotal++;
        if (max_num_rows_scanned > 0 && ntotal > max_num_rows_scanned) {
          return HA_EXIT_SUCCESS;
        }
        if (killed && *killed) {
          return HA_EXIT_FAILURE;
        }
        vector_iter.next();
      }
      m_list_size_stats[i] = list_size;
    }
    return HA_EXIT_SUCCESS;
  }

  virtual uint setup(const std::string &db_name,
                     Rdb_cmd_srv_helper &cmd_srv_helper) override {
    std::unique_ptr<Rdb_vector_index_data> m_index_data;
    const std::string trained_index_table =
        to_string(m_index_def.trained_index_table());
    auto status = cmd_srv_helper.load_index_data(
        db_name, trained_index_table, to_string(m_index_def.trained_index_id()),
        m_index_data);
    if (status.error()) {
      LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                      "Failed to load vector index data. %s",
                      status.message().c_str());
      return HA_EXIT_FAILURE;
    }
    m_quantizer = std::make_unique<faiss::IndexFlatL2>(m_index_def.dimension());
    const auto total_code_size =
        m_index_data->m_quantizer_codes.size() * sizeof(float);
    if (total_code_size % m_quantizer->code_size != 0) {
      LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                      "Invalid codes, total code size should be a multiply of "
                      "code size, total code size %lu, code size %lu.",
                      total_code_size, m_quantizer->code_size);
      return HA_EXIT_FAILURE;
    }
    const auto ncentroids = total_code_size / m_quantizer->code_size;
    m_quantizer->add(ncentroids, m_index_data->m_quantizer_codes.data());
    m_index_l2 = std::make_unique<faiss::IndexIVFFlat>(
        m_quantizer.get(), m_index_def.dimension(), ncentroids,
        faiss::METRIC_L2);
    m_index_l2->is_trained = true;

    m_index_ip = std::make_unique<faiss::IndexIVFFlat>(
        m_quantizer.get(), m_index_def.dimension(), ncentroids,
        faiss::METRIC_INNER_PRODUCT);
    m_index_ip->is_trained = true;

    // create inverted list
    m_inverted_list = std::make_unique<Rdb_faiss_inverted_list>(
        m_index_id, m_cf_handle.get(), ncentroids, m_index_def.dimension(),
        m_index_l2->code_size);
    m_index_l2->replace_invlists(m_inverted_list.get());
    m_index_ip->replace_invlists(m_inverted_list.get());

    // initialize the list size stats. does not allow resize here
    // because atomic is not move insertable
    m_list_size_stats = std::vector<std::atomic<long>>(ncentroids);
    for (auto &list_size : m_list_size_stats) {
      list_size.store(-1);
    }
    return HA_EXIT_SUCCESS;
  }

  Rdb_vector_index_info dump_info() override {
    uint ntotal = 0;
    std::optional<uint> min_list_size;
    std::optional<uint> max_list_size;
    std::vector<uint> list_size_stats;
    list_size_stats.reserve(m_list_size_stats.size());
    for (const auto &list_size : m_list_size_stats) {
      const auto list_size_value = list_size.load();
      if (list_size_value >= 0) {
        ntotal += list_size_value;
        list_size_stats.push_back(list_size_value);
        if (!min_list_size.has_value() ||
            list_size_value < min_list_size.value()) {
          min_list_size = list_size_value;
        }
        if (!max_list_size.has_value() ||
            list_size_value > max_list_size.value()) {
          max_list_size = list_size_value;
        }
      }
    }
    uint avg_list_size =
        list_size_stats.empty() ? 0 : ntotal / list_size_stats.size();
    // compute median value of list size
    std::sort(list_size_stats.begin(), list_size_stats.end());
    uint median_list_size = list_size_stats.empty()
                                ? 0
                                : list_size_stats[list_size_stats.size() / 2];
    return {.m_ntotal = ntotal,
            .m_hit = m_hit,
            .m_min_list_size = min_list_size.value_or(0),
            .m_max_list_size = max_list_size.value_or(0),
            .m_avg_list_size = avg_list_size,
            .m_median_list_size = median_list_size};
  }

 protected:
  uint64 get_list_id(const std::vector<float> &value) const override {
    faiss::idx_t list_id = 0;
    constexpr faiss::idx_t vector_count = 1;
    m_index_l2->quantizer->assign(vector_count, value.data(), &list_id);
    return list_id;
  }

  uint add_vector_to_list(rocksdb::WriteBatchBase *write_batch,
                          faiss::idx_t list_id, const rocksdb::Slice &pk,
                          std::vector<float> &value) override {
    constexpr faiss::idx_t vector_count = 1;
    Rdb_faiss_inverted_list_write_context context(write_batch, pk);
    // vector id is not actually used, use a dummy value here
    m_index_l2->add_core(vector_count, value.data(), &DUMMY_VECTOR_ID, &list_id,
                         &context);
    if (!context.m_status.ok()) {
      return ha_rocksdb::rdb_error_to_mysql(context.m_status);
    }
    return HA_EXIT_SUCCESS;
  }

 private:
  std::unique_ptr<faiss::IndexFlatL2> m_quantizer;
  std::unique_ptr<faiss::IndexIVFFlat> m_index_l2;
  std::unique_ptr<faiss::IndexIVFFlat> m_index_ip;
  std::unique_ptr<Rdb_faiss_inverted_list> m_inverted_list;
  std::vector<std::atomic<long>> m_list_size_stats;
};

}  // anonymous namespace

uint create_vector_index(Rdb_cmd_srv_helper &cmd_srv_helper,
                         const std::string &db_name,
                         const FB_vector_index_config index_def,
                         std::shared_ptr<rocksdb::ColumnFamilyHandle> cf_handle,
                         const Index_id index_id,
                         std::unique_ptr<Rdb_vector_index> &index) {
  if (index_def.type() == FB_VECTOR_INDEX_TYPE::FLAT) {
    index =
        std::make_unique<Rdb_vector_index_flat>(index_def, cf_handle, index_id);
  } else if (index_def.type() == FB_VECTOR_INDEX_TYPE::IVFFLAT) {
    index =
        std::make_unique<Rdb_vector_index_ivf>(index_def, cf_handle, index_id);
  } else {
    assert(false);
    return HA_ERR_UNSUPPORTED;
  }
  return index->setup(db_name, cmd_srv_helper);
}

#else

// dummy implementation for non-fbvectordb builds
uint create_vector_index(Rdb_cmd_srv_helper &cmd_srv_helper [[maybe_unused]],
                         const std::string &db_name [[maybe_unused]],
                         const FB_vector_index_config index_def
                         [[maybe_unused]],
                         std::shared_ptr<rocksdb::ColumnFamilyHandle> cf_handle
                         [[maybe_unused]],
                         const Index_id index_id [[maybe_unused]],
                         std::unique_ptr<Rdb_vector_index> &index) {
  index = nullptr;
  return HA_ERR_UNSUPPORTED;
}

#endif

Rdb_vector_db_handler::Rdb_vector_db_handler() {}

uint Rdb_vector_db_handler::decode_value_to_buffer(
    Field *field, FB_vector_dimension dimension, std::vector<float> &buffer) {
  Field_json *field_json = down_cast<Field_json *>(field);
  if (field_json == nullptr) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "unexpected field type for vector index");
    return HA_EXIT_FAILURE;
  }
  Json_wrapper wrapper;
  field_json->val_json(&wrapper);
  if (parse_fb_vector(wrapper, buffer)) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "failed to parse vector for vector index");
    return HA_EXIT_FAILURE;
  }

  if (buffer.size() > dimension) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "vector dimension is too big for vector index");
    return HA_EXIT_FAILURE;
  }

  if (buffer.size() < dimension) {
    buffer.resize(dimension, 0);
  }

  return HA_EXIT_SUCCESS;
}

uint Rdb_vector_db_handler::knn_search(THD *thd, Rdb_vector_index *index) {
  m_search_result.clear();
  m_vector_db_result_iter = m_search_result.cend();

  if (!m_buffer.size() || !m_limit) return HA_ERR_END_OF_FILE;

  if (m_buffer.size() < index->dimension()) {
    m_buffer.resize(index->dimension(), 0.0);
  } else if (m_buffer.size() > index->dimension()) {
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "query vector dimension is too big for vector index");
    return HA_EXIT_FAILURE;
  }

  Rdb_vector_search_params params{
      .m_metric = m_metric, .m_k = m_limit, .m_batch_size = m_batch_size};
  uint rtn = index->knn_search(thd, m_buffer, params, m_search_result);
  if (rtn) {
    return rtn;
  }
  m_vector_db_result_iter = m_search_result.cbegin();

  return rtn;
}

std::string Rdb_vector_db_handler::current_pk(
    const Index_id pk_index_id) const {
  Rdb_string_writer writer;
  writer.write_index_id(pk_index_id);
  writer.write_string(m_vector_db_result_iter->first);
  return writer.to_slice().ToString();
}

}  // namespace myrocks
