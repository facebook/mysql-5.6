#include "event_listener.h"
#include <mysql/plugin.h>
#include <my_global.h>

#include "ha_rocksdb.h"
#include "rdb_datadic.h"
#include "properties_collector.h"

static std::vector<MyRocksTablePropertiesCollector::IndexStats>
extract_index_stats(
  const std::vector<std::string>& files,
  const rocksdb::TablePropertiesCollection& props
) {
  std::vector<MyRocksTablePropertiesCollector::IndexStats> ret;
  for (auto fn : files) {
    auto it = props.find(fn);
    assert(it != props.end());
    if (it != props.end()) {
      std::vector<MyRocksTablePropertiesCollector::IndexStats> stats =
        MyRocksTablePropertiesCollector::GetStatsFromTableProperties(
          it->second);
      ret.insert(ret.end(), stats.begin(), stats.end());
    }
  }
  return ret;
}


void MyRocksEventListener::OnCompactionCompleted(
  rocksdb::DB *db,
  const rocksdb::CompactionJobInfo& ci
) {
  if (ci.status.ok()) {
    fprintf(stderr, "RocksDB: OnCompactionCompleted\n");
    ddl_manager_->adjust_stats(
      extract_index_stats(ci.input_files, ci.table_properties),
      extract_index_stats(ci.output_files, ci.table_properties)
    );
  }
}

void  MyRocksEventListener::OnFlushCompleted(
  rocksdb::DB* db,
  const rocksdb::FlushJobInfo& flush_job_info
) {
    fprintf(stderr, "RocksDB: OnFlushCompleted\n");

    auto p_props = std::make_shared<const rocksdb::TableProperties>(
      flush_job_info.table_properties);

    ddl_manager_->adjust_stats(
      MyRocksTablePropertiesCollector::GetStatsFromTableProperties(p_props)
    );
}
