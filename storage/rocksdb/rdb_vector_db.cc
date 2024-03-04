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
#include "sql-common/json_dom.h"
#include "sql/field.h"
#ifdef WITH_FB_VECTORDB
#include <faiss/IndexFlat.h>
#include <mutex>
#endif

namespace myrocks {

#ifdef WITH_FB_VECTORDB

Rdb_vector_index::Rdb_vector_index(const FB_vector_index_config index_def) {
  assert(index_def.type() == FB_VECTOR_INDEX_TYPE::FLAT);
  assert(index_def.metric() == FB_VECTOR_INDEX_METRIC::L2);
  m_index = std::make_unique<faiss::IndexFlatL2>(index_def.dimension());
}

uint Rdb_vector_index::add_vector(const std::string &pk [[maybe_unused]],
                                  const std::vector<float> &value) {
  const std::unique_lock<std::shared_mutex> lock(m_index_mutex);
  const auto vector_id = m_index->ntotal;
  m_index->add(1, value.data());
  m_vector_id_pk_map.emplace(vector_id, pk);
  return HA_EXIT_SUCCESS;
}

uint Rdb_vector_index::knn_search(
    const std::vector<float> &value, const uint k,
    std::vector<std::pair<std::string, float>> &result) {
  std::unique_ptr<faiss::idx_t[]> value_indexes(new faiss::idx_t[k]);
  std::unique_ptr<float[]> distances(new float[k]);
  m_index->search(1, value.data(), k, distances.get(), value_indexes.get());
  for (uint i = 0; i < k; ++i) {
    const auto value_index = value_indexes[i];
    if (value_index < 0) {
      break;
    }
    const auto iter = m_vector_id_pk_map.find(value_index);
    if (iter != m_vector_id_pk_map.cend()) {
      result.emplace_back(iter->second, distances[i]);
    } else {
      LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG,
                      "Failed to find value for value index %ld", value_index);
    }
  }
  return HA_EXIT_SUCCESS;
}

Rdb_vector_index_info Rdb_vector_index::dump_info() {
  const std::shared_lock<std::shared_mutex> lock(m_index_mutex);
  return {
      .m_ntotal = m_index->ntotal,
  };
}
#else

// dummy implementation for non-fbvectordb builds

Rdb_vector_index::Rdb_vector_index(const FB_vector_index_config index_def
                                   [[maybe_unused]]) {}

uint Rdb_vector_index::add_vector(const std::string &pk [[maybe_unused]],
                                  const std::vector<float> &value
                                  [[maybe_unused]]) {
  return HA_EXIT_FAILURE;
}

uint Rdb_vector_index::knn_search(
    const std::vector<float> &value [[maybe_unused]],
    const uint k [[maybe_unused]],
    std::vector<std::pair<std::string, float>> &result [[maybe_unused]]) {
  return HA_EXIT_FAILURE;
}

Rdb_vector_index_info Rdb_vector_index::dump_info() { return {}; }

#endif

Rdb_vector_db_handler::Rdb_vector_db_handler() {}

uint Rdb_vector_db_handler::decode_value(Field *field,
                                         FB_vector_dimension dimension) {
  Field_json *field_json = down_cast<Field_json *>(field);
  if (field_json == nullptr) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "unexpected field type for vector index");
    return HA_EXIT_FAILURE;
  }
  Json_wrapper wrapper;
  field_json->val_json(&wrapper);
  if (parse_fb_vector(wrapper, m_buffer)) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "failed to parse vector for vector index");
    return HA_EXIT_FAILURE;
  }

  if (m_buffer.size() > dimension) {
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "vector dimension is too big for vector index");
    return HA_EXIT_FAILURE;
  }

  if (m_buffer.size() < dimension) {
    m_buffer.resize(dimension, 0);
  }

  return HA_EXIT_SUCCESS;
}

}  // namespace myrocks
