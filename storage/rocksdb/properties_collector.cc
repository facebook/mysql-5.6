#include <mysql/plugin.h>
#include "ha_rocksdb.h"
#include "sql_class.h"
#include "sql_array.h"

#include "my_bit.h"

#include <sstream>

#include "rdb_datadic.h"
#include "properties_collector.h"

rocksdb::Status
MyRocksTablePropertiesCollector::Add(
  const rocksdb::Slice& key,
  const rocksdb::Slice& value
) {
  if (key.size() >= 4) {
    uint32_t index_number = read_big_uint4((const uchar*)key.data());
    if (stats_.empty() || index_number != stats_.back().index_number) {
      stats_.push_back( IndexStats {index_number, 0l, 0l});
    }
    stats_.back().data_size += key.size()+value.size();
    stats_.back().rows++;
  }

  return rocksdb::Status::OK();
}

const char* MyRocksTablePropertiesCollector::INDEXSTATS_KEY = "__indexstats__";

rocksdb::Status
MyRocksTablePropertiesCollector::Finish(
  rocksdb::UserCollectedProperties* properties
) {
  // TODO: support machine-independent format
  std::string s((char*)stats_.data(), stats_.size()*sizeof(stats_[0]));
  properties->insert({INDEXSTATS_KEY, s});
  return rocksdb::Status::OK();
}

rocksdb::UserCollectedProperties
MyRocksTablePropertiesCollector::GetReadableProperties() const {
  rocksdb::UserCollectedProperties ret;
  for (auto it : stats_) {
    ret.insert(
      {
        std::to_string(it.index_number),
        std::string("size:")+
          std::to_string(it.data_size)+
          std::string(", rows:") + std::to_string(it.rows)
      }
    );
  }
  return ret;
}

std::size_t
MyRocksTablePropertiesCollector::GetRows(
  uint32_t index_number,
  const rocksdb::TablePropertiesCollection& collection
) {
  std::size_t ret = 0ul;
  for (auto it : collection) {
    const auto& user_properties = it.second->user_collected_properties;
    auto it2 = user_properties.find(std::string(INDEXSTATS_KEY));
    if (it2 != user_properties.end()) {
      auto s = it2->second;
      const IndexStats* p = (const IndexStats*) s.data();
      const IndexStats* p2 = (const IndexStats*) (s.data()+s.size());
      auto it3 = std::lower_bound(
        p, p2, index_number,
        [](const IndexStats& s, uint32_t index_number) {
          return s.index_number < index_number;
        }
      );
      if (it3 != p2 && it3->index_number == index_number) {
          ret += it3->rows;
      }
    }
  }
  return ret;
}
