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

#ifndef PROPERTIES_COLLECTOR_H
#define PROPERTIES_COLLECTOR_H

/* C++ system header files */
#include <map>
#include <memory>
#include <unordered_set>
#include <vector>

/* RocksDB header files */
#include "rocksdb/db.h"

/* MyRocks header files */
#include "./ha_rocksdb.h"

class Table_ddl_manager;
class RDBSE_KEYDEF;

extern uint64_t rocksdb_num_sst_entry_put;
extern uint64_t rocksdb_num_sst_entry_delete;
extern uint64_t rocksdb_num_sst_entry_singledelete;
extern uint64_t rocksdb_num_sst_entry_merge;
extern uint64_t rocksdb_num_sst_entry_other;

struct CompactionParams {
  uint64_t deletes_, window_, file_size_;
};

class MyRocksTablePropertiesCollector
    : public rocksdb::TablePropertiesCollector {
 public:
  struct IndexStats {
    enum {
      INDEX_STATS_VERSION_INITIAL= 1,
      INDEX_STATS_VERSION_ENTRY_TYPES= 2,
    };
    GL_INDEX_ID gl_index_id;
    int64_t data_size, rows, actual_disk_size;
    int64_t entry_deletes, entry_singledeletes;
    int64_t entry_merges, entry_others;
    std::vector<int64_t> distinct_keys_per_prefix;
    std::string name; // name is not persisted

    static std::string materialize(std::vector<IndexStats>,
                                   const float card_adj_extra);
    static int unmaterialize(const std::string& s, std::vector<IndexStats>&);
    IndexStats() : IndexStats({0, 0}) {}
    explicit IndexStats(GL_INDEX_ID _gl_index_id) :
        gl_index_id(_gl_index_id),
        data_size(0),
        rows(0),
        actual_disk_size(0),
        entry_deletes(0),
        entry_singledeletes(0),
        entry_merges(0),
        entry_others(0) {}
    void merge(const IndexStats& s, bool increment = true,
               int64_t estimated_data_len = 0);
  };

  MyRocksTablePropertiesCollector(
    Table_ddl_manager* ddl_manager,
    CompactionParams params,
    uint32_t cf_id,
    const uint8_t table_stats_sampling_pct
  );

  virtual rocksdb::Status AddUserKey(
    const rocksdb::Slice& key, const rocksdb::Slice& value,
    rocksdb::EntryType type, rocksdb::SequenceNumber seq,
    uint64_t file_size);
  virtual rocksdb::Status Finish(rocksdb::UserCollectedProperties* properties) override;

  virtual const char* Name() const override {
    return "MyRocksTablePropertiesCollector";
  }

  static std::string
  GetReadableStats(const MyRocksTablePropertiesCollector::IndexStats& it);
  rocksdb::UserCollectedProperties GetReadableProperties() const override;

  static std::vector<IndexStats> GetStatsFromTableProperties(
    const std::shared_ptr<const rocksdb::TableProperties>& table_props
  );

  static void GetStats(
    const rocksdb::TablePropertiesCollection& collection,
    const std::unordered_set<GL_INDEX_ID>& index_numbers,
    std::map<GL_INDEX_ID, MyRocksTablePropertiesCollector::IndexStats>& stats
  );

  bool NeedCompact() const;
  uint64_t GetMaxDeletedRows() const {
    return max_deleted_rows_;
  }

 private:
  bool ShouldCollectStats();
  void CollectStatsForRow(const rocksdb::Slice& key,
    const rocksdb::Slice& value, rocksdb::EntryType type, uint64_t file_size);

  uint32_t cf_id_;
  std::unique_ptr<RDBSE_KEYDEF> keydef_;
  Table_ddl_manager* ddl_manager_;
  std::vector<IndexStats> stats_;
  static const char* INDEXSTATS_KEY;

  // last added key
  std::string last_key_;

  // floating window to count deleted rows
  std::vector<bool> deleted_rows_window_;
  uint64_t rows_, deleted_rows_, max_deleted_rows_;
  uint64_t file_size_;

  CompactionParams params_;
  uint8_t table_stats_sampling_pct_;
  unsigned int seed_;
  float card_adj_extra_;
};


class MyRocksTablePropertiesCollectorFactory
    : public rocksdb::TablePropertiesCollectorFactory {
 public:
  explicit MyRocksTablePropertiesCollectorFactory(
    Table_ddl_manager* ddl_manager
  ) : ddl_manager_(ddl_manager) {
  }

  virtual rocksdb::TablePropertiesCollector* CreateTablePropertiesCollector(
      rocksdb::TablePropertiesCollectorFactory::Context context) override {
    return new MyRocksTablePropertiesCollector(
      ddl_manager_, params_, context.column_family_id,
      table_stats_sampling_pct_);
  }

  virtual const char* Name() const override {
    return "MyRocksTablePropertiesCollectorFactory";
  }

  void SetCompactionParams(const CompactionParams& params) {
    params_ = params;
  }

  void SetTableStatsSamplingPct(const uint8_t table_stats_sampling_pct) {
    table_stats_sampling_pct_ = table_stats_sampling_pct;
  }
 private:
  Table_ddl_manager* ddl_manager_;
  CompactionParams params_;
  uint8_t table_stats_sampling_pct_;
};

#endif
