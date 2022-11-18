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

/* The C++ file's header */
#include "./event_listener.h"

/* C++ standard header files */
#include <string>
#include <vector>

/* MySQL includes */
#include <mysql/plugin.h>

/* MyRocks includes */
#include "./ha_rocksdb.h"
#include "./ha_rocksdb_proto.h"
#include "./properties_collector.h"
#include "./rdb_datadic.h"

namespace myrocks {

static std::vector<Rdb_index_stats> extract_index_stats(
    const std::vector<std::string> &files,
    const rocksdb::TablePropertiesCollection &props) {
  std::vector<Rdb_index_stats> ret;
  for (const auto &fn : files) {
    const auto it = props.find(fn);
    assert(it != props.end());
    std::vector<Rdb_index_stats> stats;
    Rdb_tbl_prop_coll::read_stats_from_tbl_props(it->second, &stats);
    ret.insert(ret.end(), stats.begin(), stats.end());
  }
  return ret;
}

void Rdb_event_listener::update_index_stats(
    const rocksdb::TableProperties &props) {
  assert(m_ddl_manager != nullptr);
  const auto tbl_props =
      std::make_shared<const rocksdb::TableProperties>(props);

  std::vector<Rdb_index_stats> stats;
  Rdb_tbl_prop_coll::read_stats_from_tbl_props(tbl_props, &stats);

  // In the new approach cardinality and non-cardinality stats
  // for a table are calculated at the same time. That is,
  // when the table has been modified significantly. This way,
  // cardinality and non-cardinality stats are consistent.
  //
  // It can happen that some non-cardinality stats may change after
  // a compaction without significant updates to the table.
  // So the cached values are not up-to-date.
  //
  // This lag is acceptable now and we will change when it becomes
  // an issue.
  if (rdb_is_table_scan_index_stats_calculation_enabled()) {
    return;
  }

  m_ddl_manager->adjust_stats(stats);
}

void Rdb_event_listener::OnCompactionBegin(
    rocksdb::DB *db MY_ATTRIBUTE((__unused__)),
    const rocksdb::CompactionJobInfo &ci) {
  // pull the compaction stats of ongoing compaction job
  compaction_stats.record_start(ci);
}

void Rdb_event_listener::OnCompactionCompleted(
    rocksdb::DB *db MY_ATTRIBUTE((__unused__)),
    const rocksdb::CompactionJobInfo &ci) {
  assert(db != nullptr);
  assert(m_ddl_manager != nullptr);

  if (rdb_is_table_scan_index_stats_calculation_enabled()) {
    return;
  }

  if (ci.status.ok()) {
    m_ddl_manager->adjust_stats(
        extract_index_stats(ci.output_files, ci.table_properties),
        extract_index_stats(ci.input_files, ci.table_properties));
  }
  // pull the compaction stats of a completed compaction job
  compaction_stats.record_end(ci);
}

void Rdb_event_listener::OnFlushCompleted(
    rocksdb::DB *db MY_ATTRIBUTE((__unused__)),
    const rocksdb::FlushJobInfo &flush_job_info) {
  assert(db != nullptr);
  update_index_stats(flush_job_info.table_properties);
}

void Rdb_event_listener::OnExternalFileIngested(
    rocksdb::DB *db MY_ATTRIBUTE((__unused__)),
    const rocksdb::ExternalFileIngestionInfo &info) {
  assert(db != nullptr);
  update_index_stats(info.table_properties);
}

void Rdb_event_listener::OnBackgroundError(
    rocksdb::BackgroundErrorReason reason, rocksdb::Status *status) {
  rdb_log_status_error(*status, "Error detected in background");
  // NO_LINT_DEBUG
  LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                  "RocksDB: BackgroundErrorReason: %d", (int)reason);
  if (status->IsCorruption()) {
    rdb_persist_corruption_marker();
    abort();
  }
}
}  // namespace myrocks
