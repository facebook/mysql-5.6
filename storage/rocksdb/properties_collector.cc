/*
   Copyright (c) 2015, Facebook, Inc.

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

/* This C++ file's header file */
#include "./properties_collector.h"

/* Standard C++ header files */
#include <algorithm>
#include <map>

/* MySQL header files */
#include "./log.h"
#include "./my_stacktrace.h"
#include "./sql_array.h"

/* MyRocks header files */
#include "./rdb_datadic.h"

uint64_t rocksdb_num_sst_entry_put = 0;
uint64_t rocksdb_num_sst_entry_delete = 0;
uint64_t rocksdb_num_sst_entry_singledelete = 0;
uint64_t rocksdb_num_sst_entry_merge = 0;
uint64_t rocksdb_num_sst_entry_other = 0;

MyRocksTablePropertiesCollector::MyRocksTablePropertiesCollector(
  Table_ddl_manager* ddl_manager,
  CompactionParams params,
  uint32_t cf_id,
  const uint8_t table_stats_sampling_pct
) :
    cf_id_(cf_id),
    ddl_manager_(ddl_manager),
    rows_(0l), deleted_rows_(0l), max_deleted_rows_(0l),
    file_size_(0), params_(params),
    table_stats_sampling_pct_(table_stats_sampling_pct),
    seed_(time(nullptr)),
    card_adj_extra_(1.)
{
  // We need to adjust the index cardinality numbers based on the sampling
  // rate so that the output of "SHOW INDEX" command will reflect reality
  // more closely. It will still be an approximation, just a better one.
  if (table_stats_sampling_pct_ > 0) {
    card_adj_extra_ = 100. / table_stats_sampling_pct_;
  }

  deleted_rows_window_.resize(params_.window_, false);
}

/*
  This function is called by RocksDB for every key in the SST file
*/
rocksdb::Status
MyRocksTablePropertiesCollector::AddUserKey(
    const rocksdb::Slice& key, const rocksdb::Slice& value,
    rocksdb::EntryType type, rocksdb::SequenceNumber seq,
    uint64_t file_size
) {
  if (key.size() >= 4) {
    switch (type) {
    case rocksdb::kEntryPut:
      rocksdb_num_sst_entry_put++;
      break;
    case rocksdb::kEntryDelete:
      rocksdb_num_sst_entry_delete++;
      break;
    case rocksdb::kEntrySingleDelete:
      rocksdb_num_sst_entry_singledelete++;
      break;
    case rocksdb::kEntryMerge:
      rocksdb_num_sst_entry_merge++;
      break;
    case rocksdb::kEntryOther:
      rocksdb_num_sst_entry_other++;
      break;
    default:
      break;
    }

    if (params_.window_ > 0) {
      // record the "is deleted" flag into the sliding window
      // the sliding window is implemented as a circular buffer
      // in deleted_rows_window_ vector
      // the current position in the circular buffer is pointed at by
      // rows_ % deleted_rows_window_.size()
      // deleted_rows_ is the current number of 1's in the vector
      // --update the counter for the element which will be overridden
      bool is_delete= (type == rocksdb::kEntryDelete ||
                       type == rocksdb::kEntrySingleDelete);
      // Only make changes if the value at the current position needs to change
      uint64_t pos = rows_ % deleted_rows_window_.size();
      if (is_delete != deleted_rows_window_[pos]) {
        // Set or clear the flag at the current position as appropriate
        deleted_rows_window_[pos]= is_delete;
        if (!is_delete)
          deleted_rows_--;
        else if (++deleted_rows_ > max_deleted_rows_)
          max_deleted_rows_ = deleted_rows_;
      }
    }

    rows_++;

    CollectStatsForRow(key, value, type, file_size);
  }

  return rocksdb::Status::OK();
}

void MyRocksTablePropertiesCollector::CollectStatsForRow(
  const rocksdb::Slice& key, const rocksdb::Slice& value,
  rocksdb::EntryType type, uint64_t file_size) {
  // All the code past this line must deal ONLY with collecting the
  // statistics.
  GL_INDEX_ID gl_index_id;

  gl_index_id.cf_id = cf_id_;
  gl_index_id.index_id = read_big_uint4((const uchar*)key.data());

  if (stats_.empty() || gl_index_id != stats_.back().gl_index_id) {
    keydef_ = nullptr;
    // starting a new table
    // add the new element into stats_
    stats_.push_back(IndexStats(gl_index_id));
    if (ddl_manager_) {
      keydef_ = ddl_manager_->get_copy_of_keydef(gl_index_id);
    }
    if (keydef_) {
      // resize the array to the number of columns.
      // It will be initialized with zeroes
      stats_.back().distinct_keys_per_prefix.resize(
        keydef_->get_m_key_parts());
      stats_.back().name = keydef_->get_name();
    }
    last_key_.clear();
  }

  auto& stats = stats_.back();
  stats.data_size += key.size()+value.size();

  // Incrementing per-index entry-type statistics
  switch (type) {
  case rocksdb::kEntryPut:
    stats.rows++;
    break;
  case rocksdb::kEntryDelete:
    stats.entry_deletes++;
    break;
  case rocksdb::kEntrySingleDelete:
    stats.entry_singledeletes++;
    break;
  case rocksdb::kEntryMerge:
    stats.entry_merges++;
    break;
  case rocksdb::kEntryOther:
    stats.entry_others++;
    break;
  default:
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Unexpected entry type found: %u. "
                    "This should not happen so aborting the system.", type);
    abort_with_stack_traces();
    break;
  }

  stats.actual_disk_size += file_size - file_size_;
  file_size_ = file_size;

  bool collect_cardinality = ShouldCollectStats();

  if (keydef_ && collect_cardinality) {
    std::size_t column = 0;
    rocksdb::Slice last(last_key_.data(), last_key_.size());

    if (last_key_.empty()
        || (keydef_->compare_keys(&last, &key, &column) == 0)) {
      DBUG_ASSERT(column <= stats.distinct_keys_per_prefix.size());

      for (std::size_t i = column;
           i < stats.distinct_keys_per_prefix.size(); i++) {
        stats.distinct_keys_per_prefix[i]++;
      }

      // assign new last_key for the next call
      // however, we only need to change the last key
      // if one of the first n-1 columns is different
      // If the n-1 prefix is the same, no sense in storing
      // the new key
      if (column < stats.distinct_keys_per_prefix.size()) {
        last_key_.assign(key.data(), key.size());
      }
    }
  }
}

const char* MyRocksTablePropertiesCollector::INDEXSTATS_KEY = "__indexstats__";

/*
  This function is called by RocksDB to compute properties to store in sst file
*/
rocksdb::Status
MyRocksTablePropertiesCollector::Finish(
  rocksdb::UserCollectedProperties* properties
) {
  properties->insert({INDEXSTATS_KEY,
                     IndexStats::materialize(stats_, card_adj_extra_)});
  return rocksdb::Status::OK();
}

bool MyRocksTablePropertiesCollector::NeedCompact() const {
  return
    params_.deletes_ &&
    (params_.window_ > 0) &&
    (file_size_ > params_.file_size_) &&
    (max_deleted_rows_ > params_.deletes_);
}

bool MyRocksTablePropertiesCollector::ShouldCollectStats() {
  // Zero means that we'll use all the keys to update statistics.
  if (!table_stats_sampling_pct_ ||
      MYROCKS_SAMPLE_PCT_MAX == table_stats_sampling_pct_) {
    return true;
  }

  int val = rand_r(&seed_) % MYROCKS_SAMPLE_PCT_MAX + 1;

  DBUG_ASSERT(val >= MYROCKS_SAMPLE_PCT_MIN);
  DBUG_ASSERT(val <= MYROCKS_SAMPLE_PCT_MAX);

  return val <= table_stats_sampling_pct_;
}

/*
  Returns the same as above, but in human-readable way for logging
*/
rocksdb::UserCollectedProperties
MyRocksTablePropertiesCollector::GetReadableProperties() const {
  std::string s;
#ifdef DBUG_OFF
  s.append("[...");
  s.append(std::to_string(stats_.size()));
  s.append("  records...]");
#else
  bool first = true;
  for (auto it : stats_) {
    if (first) {
      first = false;
    } else {
      s.append(",");
    }
    s.append(GetReadableStats(it));
  }
 #endif
  return rocksdb::UserCollectedProperties{{INDEXSTATS_KEY, s}};
}

std::string
MyRocksTablePropertiesCollector::GetReadableStats(
  const MyRocksTablePropertiesCollector::IndexStats& it
) {
  std::string s;
  s.append("(");
  s.append(std::to_string(it.gl_index_id.cf_id));
  s.append(", ");
  s.append(std::to_string(it.gl_index_id.index_id));
  s.append("):{name:");
  s.append(it.name);
  s.append(", size:");
  s.append(std::to_string(it.data_size));
  s.append(", rows:");
  s.append(std::to_string(it.rows));
  s.append(", actual_disk_size:");
  s.append(std::to_string(it.actual_disk_size));
  s.append(", deletes:");
  s.append(std::to_string(it.entry_deletes));
  s.append(", single_deletes:");
  s.append(std::to_string(it.entry_singledeletes));
  s.append(", merges:");
  s.append(std::to_string(it.entry_merges));
  s.append(", others:");
  s.append(std::to_string(it.entry_others));
  s.append(", distincts per prefix: [");
  for (auto num : it.distinct_keys_per_prefix) {
    s.append(std::to_string(num));
    s.append(" ");
  }
  s.append("]}");
  return s;
}

/*
  Given the properties of an SST file, reads the stats from it and returns it.
*/
std::vector<MyRocksTablePropertiesCollector::IndexStats>
MyRocksTablePropertiesCollector::GetStatsFromTableProperties(
  const std::shared_ptr<const rocksdb::TableProperties>& table_props)
{
  std::vector<MyRocksTablePropertiesCollector::IndexStats> ret;
  const auto& user_properties = table_props->user_collected_properties;
  auto it2 = user_properties.find(std::string(INDEXSTATS_KEY));
  if (it2 != user_properties.end()) {
    IndexStats::unmaterialize(it2->second, ret);
  }

  return ret;
}

/*
  Given properties stored on a bunch of SST files, reads the stats from them
  and returns one IndexStats struct per index
*/
void MyRocksTablePropertiesCollector::GetStats(
  const rocksdb::TablePropertiesCollection& collection,
  const std::unordered_set<GL_INDEX_ID>& index_numbers,
  std::map<GL_INDEX_ID, MyRocksTablePropertiesCollector::IndexStats>& ret
) {
  for (auto it : collection) {
    const auto& user_properties = it.second->user_collected_properties;
    auto it2 = user_properties.find(std::string(INDEXSTATS_KEY));
    if (it2 != user_properties.end()) {
      std::vector<IndexStats> stats;
      if (IndexStats::unmaterialize(it2->second, stats) == 0) {
        for (auto it3 : stats) {
          if (index_numbers.count(it3.gl_index_id) != 0) {
            ret[it3.gl_index_id].merge(it3);
          }
        }
      }
    }
  }
}

/*
  Stores an array on IndexStats in string
*/
std::string MyRocksTablePropertiesCollector::IndexStats::materialize(
  std::vector<IndexStats> stats,
  const float card_adj_extra
) {
  String ret;
  write_short(&ret, INDEX_STATS_VERSION_ENTRY_TYPES);
  for (auto i : stats) {
    write_int(&ret, i.gl_index_id.cf_id);
    write_int(&ret, i.gl_index_id.index_id);
    DBUG_ASSERT(sizeof i.data_size <= 8);
    write_int64(&ret, i.data_size);
    write_int64(&ret, i.rows);
    write_int64(&ret, i.actual_disk_size);
    write_int64(&ret, i.distinct_keys_per_prefix.size());
    write_int64(&ret, i.entry_deletes);
    write_int64(&ret, i.entry_singledeletes);
    write_int64(&ret, i.entry_merges);
    write_int64(&ret, i.entry_others);
    for (auto num_keys : i.distinct_keys_per_prefix) {
      float upd_num_keys = num_keys * card_adj_extra;
      write_int64(&ret, static_cast<int64_t>(upd_num_keys));
    }
  }

  return std::string((char*) ret.ptr(), ret.length());
}

/*
  Reads an array of IndexStats from a string
*/
int MyRocksTablePropertiesCollector::IndexStats::unmaterialize(
  const std::string& s, std::vector<IndexStats>& ret
) {
  const char* p = s.data();
  const char* p2 = s.data() + s.size();

  if (p+2 > p2)
  {
    return 1;
  }

  int version= read_short(&p);
  IndexStats stats;
  // Make sure version is within supported range.
  if (version < INDEX_STATS_VERSION_INITIAL ||
      version > INDEX_STATS_VERSION_ENTRY_TYPES)
  {
    // NO_LINT_DEBUG
    sql_print_error("Index stats version %d was outside of supported range. "
                    "This should not happen so aborting the system.", version);
    abort_with_stack_traces();
  }

  size_t needed = sizeof(stats.gl_index_id.cf_id)+
                  sizeof(stats.gl_index_id.index_id)+
                  sizeof(stats.data_size)+
                  sizeof(stats.rows)+
                  sizeof(stats.actual_disk_size)+
                  sizeof(uint64);
  if (version >= INDEX_STATS_VERSION_ENTRY_TYPES)
  {
    needed += sizeof(stats.entry_deletes)+
              sizeof(stats.entry_singledeletes)+
              sizeof(stats.entry_merges)+
              sizeof(stats.entry_others);
  }

  while (p < p2)
  {
    if (p+needed > p2)
    {
      return 1;
    }
    stats.gl_index_id.cf_id = read_int(&p);
    stats.gl_index_id.index_id = read_int(&p);
    stats.data_size = read_int64(&p);
    stats.rows = read_int64(&p);
    stats.actual_disk_size = read_int64(&p);
    stats.distinct_keys_per_prefix.resize(read_int64(&p));
    if (version >= INDEX_STATS_VERSION_ENTRY_TYPES)
    {
      stats.entry_deletes = read_int64(&p);
      stats.entry_singledeletes = read_int64(&p);
      stats.entry_merges = read_int64(&p);
      stats.entry_others = read_int64(&p);
    }
    if (p+stats.distinct_keys_per_prefix.size()
        *sizeof(stats.distinct_keys_per_prefix[0]) > p2)
    {
      return 1;
    }
    for (std::size_t i= 0; i < stats.distinct_keys_per_prefix.size(); i++)
    {
      stats.distinct_keys_per_prefix[i] = read_int64(&p);
    }
    ret.push_back(stats);
  }
  return 0;
}

/*
  Merges one IndexStats into another. Can be used to come up with the stats
  for the index based on stats for each sst
*/
void MyRocksTablePropertiesCollector::IndexStats::merge(
  const IndexStats& s, bool increment
) {
  std::size_t i;

  gl_index_id = s.gl_index_id;
  if (distinct_keys_per_prefix.size() < s.distinct_keys_per_prefix.size())
  {
    distinct_keys_per_prefix.resize(s.distinct_keys_per_prefix.size());
  }
  if (increment)
  {
    rows += s.rows;
    data_size += s.data_size;
    actual_disk_size += s.actual_disk_size;
    entry_deletes += s.entry_deletes;
    entry_singledeletes += s.entry_singledeletes;
    entry_merges += s.entry_merges;
    entry_others += s.entry_others;
    for (i = 0; i < s.distinct_keys_per_prefix.size(); i++)
    {
      distinct_keys_per_prefix[i] += s.distinct_keys_per_prefix[i];
    }
  }
  else
  {
    rows -= s.rows;
    data_size -= s.data_size;
    actual_disk_size -= s.actual_disk_size;
    entry_deletes -= s.entry_deletes;
    entry_singledeletes -= s.entry_singledeletes;
    entry_merges -= s.entry_merges;
    entry_others -= s.entry_others;
    for (i = 0; i < s.distinct_keys_per_prefix.size(); i++)
    {
      distinct_keys_per_prefix[i] -= s.distinct_keys_per_prefix[i];
    }
  }
}
