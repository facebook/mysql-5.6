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
#include <cstdint>
#include <memory>
#include <string_view>
#include "ha_rocksdb.h"
#include "rdb_buff.h"
#include "rdb_cmd_srv_helper.h"
#include "rdb_global.h"
#include "rdb_iterator.h"
#include "rdb_utils.h"
#ifdef WITH_FB_VECTORDB
#include <faiss/IndexFlat.h>
#include <faiss/IndexIVFFlat.h>
#include <faiss/IndexIVFPQ.h>
#include <faiss/utils/Heap.h>
#include <faiss/utils/distances.h>
#endif
#include <rocksdb/db.h>

namespace myrocks {

#ifdef WITH_FB_VECTORDB
namespace {
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

  std::uint64_t actual_list_id;
  if (reader.read_uint64(&actual_list_id)) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "Failed to read list id for key in index %d", index_id);
    return HA_ERR_ROCKSDB_CORRUPT_DATA;
  }
  if (actual_list_id != list_id) {
    LogPluginErrMsg(
        ERROR_LEVEL, ER_LOG_PRINTF_MSG,
        "Invalid list id for key in index %d, actual value %" PRIu64, index_id,
        actual_list_id);
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

  faiss::idx_t add_key(const std::string &key) {
    auto vector_id = m_vector_id++;
    m_vectorid_key.emplace(vector_id, key);
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
      auto iter = m_vectorid_key.find(vector_id);
      if (iter == m_vectorid_key.end()) {
        LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                        "Failed to find matching pk for %" PRIu64, vector_id);
        return HA_EXIT_FAILURE;
      }
      result.emplace_back(iter->second, distances[i]);
    }
    return HA_EXIT_SUCCESS;
  }

 private:
  std::map<faiss::idx_t, std::string> m_vectorid_key;
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
                      Index_id index_id, rocksdb::ColumnFamilyHandle &cf,
                      const Rdb_key_def &kd, uint code_size, size_t list_id)
      : m_context(context),
        m_index_id(index_id),
        m_list_id(list_id),
        m_code_size(code_size) {
    Rdb_string_writer lower_key_writer;
    write_inverted_list_key(lower_key_writer, index_id, list_id);
    m_iterator_lower_bound_key.PinSelf(lower_key_writer.to_slice());

    Rdb_string_writer upper_key_writer;
    write_inverted_list_key(upper_key_writer, index_id, list_id + 1);
    m_iterator_upper_bound_key.PinSelf(upper_key_writer.to_slice());
    m_iterator = rdb_tx_get_iterator(
        context->m_thd, cf, kd, /* skip_bloom_filter */ true,
        m_iterator_lower_bound_key, m_iterator_upper_bound_key,
        /* snapshot */ nullptr, TABLE_TYPE::USER_TABLE);
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

  uint get_key_and_codes(std::string &key, rocksdb::Slice &codes) {
    rocksdb::Slice key_slice = m_iterator->key();
    Rdb_string_reader key_reader(&key_slice);
    // validate the key format
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
    // copy the key bytes
    key = key_slice.ToString();

    rocksdb::Slice value = m_iterator->value();
    codes = value;
    // after consolidating write logic to use Rdb_key_def::pack_record,
    // the value is prefixed with data tag.
    // for data written before the change,
    // the value is exactly m_code_size.
    if (codes.size() == m_code_size + 1) {
      codes.remove_prefix(1);
    }
    if (codes.size() != m_code_size) {
      LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                      "Invalid value size %lu for key in index %d, list id %lu",
                      codes.size(), m_index_id, m_list_id);
      return HA_ERR_ROCKSDB_CORRUPT_DATA;
    }
    m_context->on_iterator_record();
    return HA_EXIT_SUCCESS;
  }

  std::pair<faiss::idx_t, const uint8_t *> get_id_and_codes() override {
    std::string key;
    rocksdb::Slice codes;
    uint rtn = get_key_and_codes(key, codes);
    if (rtn) {
      // set error to context so faiss can stop iterating
      m_context->m_error = rtn;
      // return some dummy data to faiss so it does not crash
      faiss::idx_t vector_id = 42;
      m_codes_buffer.resize(m_code_size);
      return {vector_id, m_codes_buffer.data()};
    }

    faiss::idx_t vector_id = m_context->add_key(key);
    return {vector_id, reinterpret_cast<const uint8_t *>(codes.data())};
  }

 private:
  Rdb_faiss_inverted_list_context *m_context;
  Index_id m_index_id;
  size_t m_list_id;
  uint m_code_size;
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
  Rdb_faiss_inverted_list(Index_id index_id, rocksdb::ColumnFamilyHandle &cf,
                          const Rdb_key_def &kd, uint nlist, uint code_size)
      : InvertedLists(nlist, code_size),
        m_index_id(index_id),
        m_cf(cf),
        m_kd(kd) {
    use_iterator = true;
  }
  ~Rdb_faiss_inverted_list() override = default;

  Rdb_faiss_inverted_list(const Rdb_faiss_inverted_list &) = delete;
  Rdb_faiss_inverted_list &operator=(const Rdb_faiss_inverted_list &) = delete;
  Rdb_faiss_inverted_list(Rdb_faiss_inverted_list &&) = delete;
  Rdb_faiss_inverted_list &operator=(Rdb_faiss_inverted_list &&) = delete;

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
        m_index_id, m_cf, m_kd, code_size, list_no);
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
    Rdb_vector_index_assignment *context =
        reinterpret_cast<Rdb_vector_index_assignment *>(inverted_list_context);
    context->m_list_id = list_no;
    context->m_codes =
        std::string(reinterpret_cast<const char *>(code), code_size);
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
  rocksdb::ColumnFamilyHandle &m_cf;
  const Rdb_key_def &m_kd;
};

class Rdb_vector_index_ivf : public Rdb_vector_index {
 public:
  Rdb_vector_index_ivf(const FB_vector_index_config index_def,
                       std::shared_ptr<rocksdb::ColumnFamilyHandle> cf_handle,
                       const Rdb_key_def &kd, const Index_id index_id)
      : m_index_id{index_id},
        m_index_def{index_def},
        m_cf_handle{cf_handle},
        m_kd{kd} {}

  virtual ~Rdb_vector_index_ivf() override = default;

  void assign_vector(const std::vector<float> &value,
                     Rdb_vector_index_assignment &assignment) override {
    faiss_ivf_list_id list_id = get_list_id(value);
    constexpr faiss::idx_t vector_count = 1;
    // vector id is not actually used, use a dummy value here
    m_index_l2->add_core(vector_count, value.data(), &DUMMY_VECTOR_ID, &list_id,
                         &assignment);
  }

  FB_vector_dimension dimension() const override {
    return m_index_def.dimension();
  }

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

    search_params.nprobe = params.m_nprobe;
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
    assert(thd);
    std::string key;
    rocksdb::Slice codes;
    uint64_t ntotal = 0;
    for (std::size_t i = 0; i < m_list_size_stats.size(); i++) {
      std::size_t list_size = 0;
      Rdb_faiss_inverted_list_context context(thd);
      Rdb_vector_iterator vector_iter(&context, m_index_id, *m_cf_handle.get(),
                                      m_kd, m_index_l2->code_size, i);
      while (vector_iter.is_available()) {
        uint rtn = vector_iter.get_key_and_codes(key, codes);
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
    std::unique_ptr<Rdb_vector_index_data> index_data;
    if (m_index_def.type() == FB_VECTOR_INDEX_TYPE::FLAT) {
      // flat is ivf flat with 1 list
      index_data = std::make_unique<Rdb_vector_index_data>();
      index_data->m_nlist = 1;
      index_data->m_quantizer_codes.resize(m_index_def.dimension(), 0.0);
    } else {
      const std::string trained_index_table =
          to_string(m_index_def.trained_index_table());
      auto status = cmd_srv_helper.load_index_data(
          db_name, trained_index_table,
          to_string(m_index_def.trained_index_id()), index_data);
      if (status.error()) {
        LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                        "Failed to load vector index data. %s",
                        status.message().c_str());
        return HA_EXIT_FAILURE;
      }
    }
    if (index_data->m_nlist <= 0) {
      LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "Invalid nlist %d",
                      index_data->m_nlist);
      return HA_EXIT_FAILURE;
    }
    if (m_index_def.type() == FB_VECTOR_INDEX_TYPE::IVFPQ) {
      if (index_data->m_pq_m <= 0 || index_data->m_pq_nbits <= 0) {
        LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                        "Invalid pq m %d, pq nbits %d", index_data->m_pq_m,
                        index_data->m_pq_nbits);
        return HA_EXIT_FAILURE;
      }
      if (index_data->m_pq_codes.empty()) {
        LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                        "pq codes is required for IVFPQ");
        return HA_EXIT_FAILURE;
      }
    }
    uint rtn = setup_quantizer(index_data.get());
    if (rtn) {
      return rtn;
    }

    rtn = create_index(m_index_l2, index_data.get(), faiss::METRIC_L2);
    if (rtn) {
      return rtn;
    }
    rtn =
        create_index(m_index_ip, index_data.get(), faiss::METRIC_INNER_PRODUCT);
    if (rtn) {
      return rtn;
    }

    // create inverted list
    m_inverted_list = std::make_unique<Rdb_faiss_inverted_list>(
        m_index_id, *m_cf_handle.get(), m_kd, m_index_l2->nlist,
        m_index_l2->code_size);
    m_index_l2->replace_invlists(m_inverted_list.get());
    m_index_ip->replace_invlists(m_inverted_list.get());

    // initialize the list size stats. does not allow resize here
    // because atomic is not move insertable
    m_list_size_stats = std::vector<std::atomic<long>>(m_index_l2->nlist);
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
    uint pq_m = 0;
    uint pq_nbits = 0;
    if (m_index_def.type() == FB_VECTOR_INDEX_TYPE::IVFPQ) {
      faiss::IndexIVFPQ *index_ivfpq =
          dynamic_cast<faiss::IndexIVFPQ *>(m_index_l2.get());
      pq_m = index_ivfpq->pq.M;
      pq_nbits = index_ivfpq->pq.nbits;
    }
    return {.m_ntotal = ntotal,
            .m_hit = m_hit,
            .m_code_size = m_index_l2->code_size,
            .m_nlist = m_index_l2->nlist,
            .m_pq_m = pq_m,
            .m_pq_nbits = pq_nbits,
            .m_min_list_size = min_list_size.value_or(0),
            .m_max_list_size = max_list_size.value_or(0),
            .m_avg_list_size = avg_list_size,
            .m_median_list_size = median_list_size};
  }

 private:
  Index_id m_index_id;
  FB_vector_index_config m_index_def;
  std::shared_ptr<rocksdb::ColumnFamilyHandle> m_cf_handle;
  const Rdb_key_def &m_kd;
  std::atomic<uint> m_hit{0};
  std::unique_ptr<faiss::IndexFlatL2> m_quantizer;
  std::unique_ptr<faiss::IndexIVF> m_index_l2;
  std::unique_ptr<faiss::IndexIVF> m_index_ip;
  std::unique_ptr<Rdb_faiss_inverted_list> m_inverted_list;
  std::vector<std::atomic<long>> m_list_size_stats;

  uint delete_vector_from_list(rocksdb::WriteBatchBase *write_batch,
                               const uint64 list_id, const rocksdb::Slice &pk) {
    Rdb_string_writer key_writer;
    write_inverted_list_item_key(key_writer, m_index_id, list_id, pk);
    auto status = write_batch->Delete(m_cf_handle.get(), key_writer.to_slice());
    if (!status.ok()) {
      LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                      "Failed to write codes for index %d", m_index_id);
      return ha_rocksdb::rdb_error_to_mysql(status);
    }
    return HA_EXIT_SUCCESS;
  }

  uint64 get_list_id(const std::vector<float> &value) const {
    if (m_index_l2->nlist == 1) {
      return 0;
    }
    faiss::idx_t list_id = 0;
    constexpr faiss::idx_t vector_count = 1;
    m_index_l2->quantizer->assign(vector_count, value.data(), &list_id);
    return list_id;
  }

  uint setup_quantizer(Rdb_vector_index_data *index_data) {
    m_quantizer = std::make_unique<faiss::IndexFlatL2>(m_index_def.dimension());
    const auto total_code_size =
        index_data->m_quantizer_codes.size() * sizeof(float);
    const auto ncentroids = index_data->m_nlist;
    if (total_code_size != ncentroids * m_quantizer->code_size) {
      LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                      "Invalid codes, total code size %lu.", total_code_size);
      return HA_EXIT_FAILURE;
    }
    m_quantizer->add(ncentroids, index_data->m_quantizer_codes.data());
    return HA_EXIT_SUCCESS;
  }

  uint create_index(std::unique_ptr<faiss::IndexIVF> &index,
                    Rdb_vector_index_data *index_data,
                    faiss::MetricType metric_type) {
    const auto ncentroids = index_data->m_nlist;
    if (m_index_def.type() == FB_VECTOR_INDEX_TYPE::FLAT ||
        m_index_def.type() == FB_VECTOR_INDEX_TYPE::IVFFLAT) {
      index = std::make_unique<faiss::IndexIVFFlat>(
          m_quantizer.get(), m_index_def.dimension(), ncentroids, metric_type);
    } else {
      auto ivfpq_index = std::make_unique<faiss::IndexIVFPQ>(
          m_quantizer.get(), m_index_def.dimension(), ncentroids,
          index_data->m_pq_m, index_data->m_pq_nbits, metric_type);
      // pq centroids is already resized to the correct size
      if (ivfpq_index->pq.centroids.size() != index_data->m_pq_codes.size()) {
        LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                        "Invalid pq codes, expected code size %lu.",
                        ivfpq_index->pq.centroids.size());
        return HA_EXIT_FAILURE;
      }

      ivfpq_index->pq.centroids = index_data->m_pq_codes;
      ivfpq_index->precompute_table();

      index = std::move(ivfpq_index);
    }
    index->is_trained = true;
    return HA_EXIT_SUCCESS;
  }
};

}  // anonymous namespace

uint create_vector_index(Rdb_cmd_srv_helper &cmd_srv_helper,
                         const std::string &db_name,
                         const FB_vector_index_config index_def,
                         std::shared_ptr<rocksdb::ColumnFamilyHandle> cf_handle,
                         const Rdb_key_def &kd,
                         const Index_id index_id,
                         std::unique_ptr<Rdb_vector_index> &index) {
  if (index_def.type() == FB_VECTOR_INDEX_TYPE::FLAT ||
      index_def.type() == FB_VECTOR_INDEX_TYPE::IVFFLAT ||
      index_def.type() == FB_VECTOR_INDEX_TYPE::IVFPQ) {
    index =
        std::make_unique<Rdb_vector_index_ivf>(index_def, cf_handle, kd, index_id);
  } else {
    assert(false);
    return HA_ERR_UNSUPPORTED;
  }
  return index->setup(db_name, cmd_srv_helper);
}

#else

// dummy implementation for non-fbvectordb builds
uint create_vector_index(Rdb_cmd_srv_helper &, const std::string &,
                         const FB_vector_index_config,
                         std::shared_ptr<rocksdb::ColumnFamilyHandle>,
                         const Rdb_key_def, const Index_id,
                         std::unique_ptr<Rdb_vector_index> &index) {
  index = nullptr;
  return HA_ERR_UNSUPPORTED;
}

#endif

Rdb_vector_db_handler::Rdb_vector_db_handler() {}

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

  Rdb_vector_search_params params{.m_metric = m_metric,
                                  .m_k = m_limit * m_limit_multiplier,
                                  .m_nprobe = m_nprobe};
  uint rtn = index->knn_search(thd, m_buffer, params, m_search_result);
  if (rtn) {
    return rtn;
  }
  m_vector_db_result_iter = m_search_result.cbegin();

  return rtn;
}

std::string Rdb_vector_db_handler::current_key() const {
  return m_vector_db_result_iter->first;
}

}  // namespace myrocks
