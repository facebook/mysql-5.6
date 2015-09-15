/*
   Copyright (c) 2012, Monty Program Ab

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

/* The C++ file's header */
#include "./event_listener.h"

/* C++ standard header files */
#include <string>
#include <vector>

/* MySQL includes */
#include <mysql/plugin.h>
#include <my_global.h>

/* MyRocks includes */
#include "./ha_rocksdb.h"
#include "./properties_collector.h"
#include "./rdb_datadic.h"

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
    ddl_manager_->adjust_stats(
      extract_index_stats(ci.input_files, ci.table_properties),
      extract_index_stats(ci.output_files, ci.table_properties));
  }
}

void  MyRocksEventListener::OnFlushCompleted(
  rocksdb::DB* db,
  const rocksdb::FlushJobInfo& flush_job_info
) {
    auto p_props = std::make_shared<const rocksdb::TableProperties>(
      flush_job_info.table_properties);

    ddl_manager_->adjust_stats(
      MyRocksTablePropertiesCollector::GetStatsFromTableProperties(p_props));
}
