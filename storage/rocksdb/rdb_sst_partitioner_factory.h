/*
   Copyright (c) 2022, Facebook, Inc.

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

#include <strings.h>
#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <utility>

#include "rocksdb/sst_partitioner.h"

#include "./rdb_cf_manager.h"
#include "./rdb_datadic.h"

namespace myrocks {
/**
 * get string with data written with index bytes
 */
inline std::string get_index_key(const Index_id index_id) {
  std::string buf;
  buf.resize(Rdb_key_def::INDEX_NUMBER_SIZE);
  rdb_netbuf_store_index(reinterpret_cast<uchar *>(buf.data()), index_id);
  return buf;
}

/**
 * SstPartitioner is used in compaction to split output sst files.
 * The compaction iterates keys in order, calls SstPartitioner with
 * previous key and current key in the iteration, SstPartitioner can
 * return PartitionerResult::kRequired to indicate a split is needed
 * to create a new SST file starting with current key.
 *
 * This class splits output sst files when input keys crossing index boundaries.
 * For example, given index id 8, this class will partition data to 3 parts,
 * with index [0 - 7], [8], [9 - infinity).
 *
 * During bulk load, the compaction will create instances of this class with
 * indexes used in bulk load. This will split SST files with keys overlap with
 * bulk load data. Essentially, this creates "gaps" among SST files in LMAX to
 * place the bulk load SST files.
 */
class Rdb_index_boundary_sst_partitioner : public rocksdb::SstPartitioner {
 private:
  const rocksdb::Comparator *m_comparator;
  const bool m_is_reverse_cf;
  // min and max values of indexes
  std::vector<std::pair<std::string, std::string>> m_index_key_ranges;
  std::string m_min_index_key;
  std::string m_max_index_key;

  /**
   * if a partition is required for given index
   */
  bool should_partition(const std::string &index_key,
                        const rocksdb::Slice *previous_key,
                        const rocksdb::Slice *current_key) const {
    // for reverse cf, indexKey is upper limit of index data,
    // for normal cf, indexKey is lower limit of index data.
    // we want indexKey itself get partitioned with other keys in the index.
    if (m_is_reverse_cf) {
      return m_comparator->Compare(*previous_key, index_key) <= 0 &&
             m_comparator->Compare(*current_key, index_key) > 0;
    }
    return m_comparator->Compare(*previous_key, index_key) < 0 &&
           m_comparator->Compare(*current_key, index_key) >= 0;
  }

 public:
  Rdb_index_boundary_sst_partitioner(const std::set<Index_id> &index_ids,
                                     const rocksdb::Comparator *comparator,
                                     const bool is_reverse_cf)
      : m_comparator(comparator), m_is_reverse_cf(is_reverse_cf) {
    assert(!index_ids.empty());
    for (auto index_id : index_ids) {
      auto start_index_id = index_id;
      auto end_index_id = index_id + 1;
      if (is_reverse_cf) {
        std::swap(start_index_id, end_index_id);
      }
      m_index_key_ranges.push_back(std::make_pair(get_index_key(start_index_id),
                                                  get_index_key(end_index_id)));
    }

    // even index_ids is somehow empty, ShouldPartition will return kNotRequired
    // regardless of the value of min/max index
    Index_id min_index = 0;
    Index_id max_index = 0;
    if (!index_ids.empty()) {
      min_index = *index_ids.cbegin();
      max_index = (*index_ids.rbegin()) + 1;
      if (is_reverse_cf) {
        std::swap(min_index, max_index);
      }
    }
    m_min_index_key = get_index_key(min_index);
    m_max_index_key = get_index_key(max_index);
  }

  ~Rdb_index_boundary_sst_partitioner() override {}

  const char *Name() const override {
    return "Rdb_index_boundary_sst_partitioner";
  }

  rocksdb::PartitionerResult ShouldPartition(
      const rocksdb::PartitionerRequest &request) override {
    assert(m_comparator->Compare(*request.current_user_key,
                                 *request.prev_user_key) > 0);

    if (m_comparator->Compare(*request.prev_user_key, m_max_index_key) > 0 ||
        m_comparator->Compare(*request.current_user_key, m_min_index_key) < 0) {
      return rocksdb::PartitionerResult::kNotRequired;
    }
    for (const auto &index_key_range : m_index_key_ranges) {
      // partition sst file when the request keys cross index boundary
      if (should_partition(index_key_range.first, request.prev_user_key,
                           request.current_user_key) ||
          should_partition(index_key_range.second, request.prev_user_key,
                           request.current_user_key)) {
        return rocksdb::PartitionerResult::kRequired;
      }
    }
    return rocksdb::PartitionerResult::kNotRequired;
  };

  bool CanDoTrivialMove(const rocksdb::Slice &smallest_user_key,
                        const rocksdb::Slice &largest_user_key) override {
    return ShouldPartition(rocksdb::PartitionerRequest(smallest_user_key,
                                                       largest_user_key, 0)) ==
           rocksdb::PartitionerResult::kNotRequired;
  };
};

/**
 * creates sst partitioner used in compaction.
 * see comments in Rdb_index_boundary_sst_partitioner.
 */
class Rdb_sst_partitioner_factory : public rocksdb::SstPartitionerFactory {
  const rocksdb::Comparator *m_comparator;
  const int m_num_levels;
  const bool m_is_reverse_cf;
  mutable std::mutex m_index_ids_mutex;
  std::set<Index_id> m_index_ids;

  std::set<Index_id> get_index_ids() const {
    const std::lock_guard<std::mutex> lock(m_index_ids_mutex);
    std::set<Index_id> result(m_index_ids);
    return result;
  };

 public:
  Rdb_sst_partitioner_factory(const rocksdb::Comparator *comparator,
                              int num_levels, int is_reverse_cf)
      : m_comparator(comparator),
        m_num_levels(num_levels),
        m_is_reverse_cf(is_reverse_cf){};

  ~Rdb_sst_partitioner_factory() override {}

  static const char *kClassName() { return "Rdb_sst_partitioner_factory"; }
  const char *Name() const override { return kClassName(); }

  std::unique_ptr<rocksdb::SstPartitioner> CreatePartitioner(
      const rocksdb::SstPartitioner::Context &context) const override {
    // we need special partitioner for Lmax.
    // when rocksdb checks if SstPartitioner is used in a manual compaction,
    // it passes -1 as output_level to indicate output_level is not yet known.
    if (context.output_level == m_num_levels - 1 ||
        context.output_level == -1) {
      auto index_ids = get_index_ids();
      if (!index_ids.empty()) {
        // NO_LINT_DEBUG
        LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                        "MyRocks: Rdb_sst_partitioner_factory creating "
                        "partitioner with %lu "
                        "indexes.",
                        index_ids.size());
        return std::unique_ptr<rocksdb::SstPartitioner>(
            new Rdb_index_boundary_sst_partitioner(index_ids, m_comparator,
                                                   m_is_reverse_cf));
      }
    }
    return {};
  };

  /**
   * add index to be used in sst partitioner later.
   * returns true when success
   */
  bool add_index(Index_id index_id) {
    // NO_LINT_DEBUG
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "MyRocks: Rdb_sst_partitioner_factory adding index %d.",
                    index_id);
    const std::lock_guard<std::mutex> lock(m_index_ids_mutex);
    return m_index_ids.insert(index_id).second;
  };

  /**
   * remove index, it won't be used in sst partitioner created
   * afterwards.
   * return true when success
   */
  bool remove_index(Index_id index_id) {
    // NO_LINT_DEBUG
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "MyRocks: Rdb_sst_partitioner_factory removing index %d.",
                    index_id);
    const std::lock_guard<std::mutex> lock(m_index_ids_mutex);
    return m_index_ids.erase(index_id) == 1;
  }
};

/**
 * stores index information used in bulk load.
 * during bulk load
 * 1. register indexes used in bulk load with sst partitioner factory,
 *    rocksdb will partition the sst files such that the sst files
 *    does not overlap with index data
 * 2. ingest bulk load sst files to rocksdb, this could fail when
 *    there is sst files overlap with index data created before step 1
 * 3. if ingestion fails, trigger a compaction, this will guarantee
 *    no sst files overlaps with index data. After compaction, ingest
 *    bulk load sst files again
 * 4. remove indexes from sst partitioner factory.
 */
class Rdb_bulk_load_index_registry {
  // index -> sst partitioner factory
  std::map<Index_id, Rdb_sst_partitioner_factory *> m_partitioner_factories;
  // cf -> indexes
  std::map<rocksdb::ColumnFamilyHandle *, std::set<Index_id>> m_cf_indexes;

  void add_cf_index_map(rocksdb::ColumnFamilyHandle *cf, Index_id index_id) {
    auto find_result = m_cf_indexes.find(cf);
    if (find_result == m_cf_indexes.end()) {
      m_cf_indexes.emplace(cf, std::set<Index_id>{index_id});
    } else {
      find_result->second.insert(index_id);
    }
  }

 public:
  ~Rdb_bulk_load_index_registry() { clear(); }

  /**
   * register the index with sst partitioner if it is
   * not already registered.
   * returns true when success.
   */
  bool add_index(rocksdb::TransactionDB *rdb, rocksdb::ColumnFamilyHandle *cf,
                 Index_id index_id) {
    if (m_partitioner_factories.count(index_id) != 0) {
      // already processed this index, return
      return true;
    }

    auto sst_partitioner_factory = rdb->GetOptions(cf).sst_partitioner_factory;
    auto rdb_sst_partitioner_factory =
        dynamic_cast<Rdb_sst_partitioner_factory *>(
            sst_partitioner_factory.get());
    if (rdb_sst_partitioner_factory == nullptr) {
      // should never happen
      // NO_LINT_DEBUG
      LogPluginErrMsg(
          WARNING_LEVEL, ER_LOG_PRINTF_MSG,
          "MyRocks: Rdb_sst_partitioner_factory not registered for cf %s ",
          cf->GetName().c_str());
      return false;
    }

    bool index_added = rdb_sst_partitioner_factory->add_index(index_id);
    if (!index_added) {
      return false;
    } else {
      m_partitioner_factories.emplace(index_id, rdb_sst_partitioner_factory);
      add_cf_index_map(cf, index_id);
    }

    return true;
  }

  /**
   * remove registered indexes from sst partitioner factory and clear the
   * index stored in this object.
   * returns true when all indexes are removed.
   */
  bool clear() {
    bool success = true;
    for (auto &entry : m_partitioner_factories) {
      bool removed = entry.second->remove_index(entry.first);
      if (!removed) {
        // unlikely
        success = false;
      }
    }
    m_partitioner_factories.clear();
    m_cf_indexes.clear();
    return success;
  }

  /**
   * returns true when we have index registered in
   * sst partitioner factory
   */
  bool index_registered_in_sst_partitioner() const {
    return !m_partitioner_factories.empty();
  }

  /**
   * trigger compaction that covers all indexes registered in
   * this object
   */
  rocksdb::Status compact_index_ranges(
      rocksdb::TransactionDB *rdb,
      const rocksdb::CompactRangeOptions compact_range_options) {
    rocksdb::Status status;
    for (auto &entry : m_cf_indexes) {
      auto cf = entry.first;
      const auto is_reverse_cf =
          Rdb_cf_manager::is_cf_name_reverse(cf->GetName().c_str());
      const std::set<Index_id> index_ids = entry.second;
      assert(!index_ids.empty());
      Index_id begin_index = *index_ids.cbegin();
      // use maxIndex + 1 to include the data from maxIndex itself in
      // the compaction range, this works for both reverse and normal
      // cfs.
      Index_id end_index = (*index_ids.rbegin()) + 1;
      if (is_reverse_cf) {
        std::swap(begin_index, end_index);
      }
      // need to keep the string object alive for the rocksdb::Slice
      const std::string begin_index_key = get_index_key(begin_index);
      const std::string end_index_Key = get_index_key(end_index);
      const rocksdb::Slice compact_begin_key = begin_index_key;
      const rocksdb::Slice compact_end_key = end_index_Key;
      // NO_LINT_DEBUG
      LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                      "MyRocks: CompactRange on cf %s. key range ['%s', '%s'].",
                      cf->GetName().c_str(),
                      compact_begin_key.ToString(/*hex*/ true).c_str(),
                      compact_end_key.ToString(/*hex*/ true).c_str());

      status = rdb->CompactRange(compact_range_options, cf, &compact_begin_key,
                                 &compact_end_key);
      if (!status.ok()) {
        break;
      }
    }
    return status;
  }
};

}  // namespace myrocks
