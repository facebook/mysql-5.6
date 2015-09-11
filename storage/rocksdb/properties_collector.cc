#include <mysql/plugin.h>
#include "ha_rocksdb.h"
#include "sql_class.h"
#include "sql_array.h"

#include "my_bit.h"

#include <sstream>

#include "rdb_datadic.h"
#include "properties_collector.h"

MyRocksTablePropertiesCollector::MyRocksTablePropertiesCollector(
  Table_ddl_manager* ddl_manager,
  CompactionParams params
) :
    ddl_manager_(ddl_manager),
    rows_(0l), deleted_rows_(0l), max_deleted_rows_(0l),
    params_(params)
{
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
    uint32_t index_number = read_big_uint4((const uchar*)key.data());
    if (stats_.empty() || index_number != stats_.back().index_number) {
      keydef_ = NULL;
      // starting a new table
      // add the new element into stats_
      stats_.push_back(IndexStats(index_number));
      if (ddl_manager_) {
        keydef_ = ddl_manager_->get_copy_of_keydef(index_number);
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
    stats.rows++;

    if (params_.window_ > 0) {
      // record the "is deleted" flag into the sliding window
      // the sliding window is implemented as a circular buffer
      // in deleted_rows_window_ vector
      // the current position in the circular buffer is pointed at by
      // rows_ % deleted_rows_window_.size()
      // deleted_rows_ is the current number of 1's in the vector
      // --update the counter for the element which will be overridden
      if (deleted_rows_window_[rows_ % deleted_rows_window_.size()]) {
        // correct the current number based on the element we about to override
        deleted_rows_--;
      }
      // --override the element with the new value
      deleted_rows_window_[rows_ % deleted_rows_window_.size()]
        = (type == rocksdb::kEntryDelete);
      // --update the counter
      if (type == rocksdb::kEntryDelete) {
        deleted_rows_++;
      }
      // --we are looking for the maximum deleted_rows_
      max_deleted_rows_ = std::max(deleted_rows_, max_deleted_rows_);
    }
    rows_++;

    if (keydef_) {
      std::size_t column = 0;
      rocksdb::Slice last(last_key_.data(), last_key_.size());
      if (last_key_.empty()
          || (keydef_->compare_keys(&last, &key, &column) == 0)) {
        assert(column <= stats.distinct_keys_per_prefix.size());
        for (std::size_t i=column;
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
  file_size_ = file_size;

  return rocksdb::Status::OK();
}

const char* MyRocksTablePropertiesCollector::INDEXSTATS_KEY = "__indexstats__";

/*
  This function is called by RocksDB to compute properties to store in sst file
*/
rocksdb::Status
MyRocksTablePropertiesCollector::Finish(
  rocksdb::UserCollectedProperties* properties
) {
  std::vector<uint32_t> changed_indexes;
  changed_indexes.resize(stats_.size());
  std::transform(
    stats_.begin(), stats_.end(),
    changed_indexes.begin(),
    [](const IndexStats& s) {return s.index_number;}
  );
  ddl_manager_->add_changed_indexes(changed_indexes);
  properties->insert({INDEXSTATS_KEY, IndexStats::materialize(stats_)});
  return rocksdb::Status::OK();
}

bool MyRocksTablePropertiesCollector::NeedCompact() const {
  return
    params_.deletes_ &&
    (params_.window_ > 0) &&
    (file_size_ > params_.file_size_) &&
    (max_deleted_rows_ > params_.deletes_);
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
  s.append(std::to_string(it.index_number));
  s.append(":{name:");
  s.append(it.name);
  s.append(", number:");
  s.append(std::to_string(it.index_number));
  s.append(", size:");
  s.append(std::to_string(it.data_size));
  s.append(", rows:");
  s.append(std::to_string(it.rows));
  s.append(", distincts per prefix: [");
  for (auto num : it.distinct_keys_per_prefix) {
    s.append(std::to_string(num));
    s.append(" ");
  }
  s.append("]}");
  return s;
}

/*
  Given properties stored on a bunch of SST files, reads the stats from them
  and returns one IndexStats struct per index
*/
void MyRocksTablePropertiesCollector::GetStats(
  const rocksdb::TablePropertiesCollection& collection,
  const std::unordered_set<uint32_t>& index_numbers,
  std::map<uint32_t, MyRocksTablePropertiesCollector::IndexStats>& ret
) {
  for (auto it : collection) {
    const auto& user_properties = it.second->user_collected_properties;
    auto it2 = user_properties.find(std::string(INDEXSTATS_KEY));
    if (it2 != user_properties.end()) {
      std::vector<IndexStats> stats;
      if (IndexStats::unmaterialize(it2->second, stats) == 0) {
        for (auto it3 : stats) {
          if (index_numbers.count(it3.index_number) != 0) {
            ret[it3.index_number].merge(it3);
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
  std::vector<IndexStats> stats
) {
  String ret;
  write_short(&ret, INDEX_STATS_VERSION);
  for (auto i : stats) {
    write_int(&ret, i.index_number);
    assert(sizeof i.data_size <= 8);
    write_int64(&ret, i.data_size);
    write_int64(&ret, i.rows);
    write_int64(&ret, i.approximate_size);
    write_int64(&ret, i.distinct_keys_per_prefix.size());
    for (auto num_keys : i.distinct_keys_per_prefix) {
      write_int64(&ret, num_keys);
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

  if (p+2 > p2 || read_short(&p) != INDEX_STATS_VERSION) {
    return 1;
  }

  while (p < p2) {
    IndexStats stats;
    if (p+
       sizeof(stats.index_number)+
       sizeof(stats.data_size)+
       sizeof(stats.rows)+
       sizeof(stats.approximate_size)+
       sizeof(uint64) > p2)
    {
      return 1;
    }
    stats.index_number = read_int(&p);
    stats.data_size = read_int64(&p);
    stats.rows = read_int64(&p);
    stats.approximate_size = read_int64(&p);
    stats.distinct_keys_per_prefix.resize(read_int64(&p));
    if (p+stats.distinct_keys_per_prefix.size()
        *sizeof(stats.distinct_keys_per_prefix[0]) > p2)
    {
      return 1;
    }
    for (std::size_t i=0; i < stats.distinct_keys_per_prefix.size(); i++) {
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
void MyRocksTablePropertiesCollector::IndexStats::merge(const IndexStats& s) {
  index_number = s.index_number;
  rows += s.rows;
  data_size += s.data_size;
  if (distinct_keys_per_prefix.size() < s.distinct_keys_per_prefix.size()) {
    distinct_keys_per_prefix.resize(s.distinct_keys_per_prefix.size());
  }
  for (std::size_t i=0; i < s.distinct_keys_per_prefix.size(); i++) {
    distinct_keys_per_prefix[i] += s.distinct_keys_per_prefix[i];
  }
}
