#ifndef PROPERTIES_COLLECTOR_H
#define PROPERTIES_COLLECTOR_H

#include <vector>
#include "rocksdb/table_properties.h"
#include "rdb_datadic.h"

struct CompactionParams {
  uint64_t deletes_, window_, file_size_;
};

class MyRocksTablePropertiesCollector
    : public rocksdb::TablePropertiesCollector {
 public:
  struct IndexStats {
    enum {
      INDEX_STATS_VERSION= 1,
    };
    uint32_t index_number;
    int64_t data_size, rows, approximate_size;
    std::vector<int64_t> distinct_keys_per_prefix;
    std::string name; // name is not persisted

    static std::string materialize(std::vector<IndexStats>);
    static int unmaterialize(const std::string& s, std::vector<IndexStats>&);
    IndexStats() : IndexStats(0) {}
    IndexStats(uint32_t _index_number) :
        index_number(_index_number),
        data_size(0),
        rows(0),
        approximate_size(0) {}
    void merge(const IndexStats& s);
  };

  MyRocksTablePropertiesCollector(
    Table_ddl_manager* ddl_manager,
    CompactionParams params
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

  static void GetStats(
    const rocksdb::TablePropertiesCollection& collection,
    const std::unordered_set<uint32_t>& index_numbers,
    std::map<uint32_t, MyRocksTablePropertiesCollector::IndexStats>& stats
  );

  bool NeedCompact() const;
  uint64_t GetMaxDeletedRows() const {
    return max_deleted_rows_;
  }

 private:
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
};


class MyRocksTablePropertiesCollectorFactory
    : public rocksdb::TablePropertiesCollectorFactory {
 public:
  MyRocksTablePropertiesCollectorFactory(
    Table_ddl_manager* ddl_manager
  ) : ddl_manager_(ddl_manager) {
  }

  virtual rocksdb::TablePropertiesCollector* CreateTablePropertiesCollector() override {
    return new MyRocksTablePropertiesCollector(
      ddl_manager_, params_);
  }

  virtual const char* Name() const override {
    return "MyRocksTablePropertiesCollectorFactory";
  }
  void SetCompactionParams(const CompactionParams& params) {
    params_ = params;
  }
 private:
  Table_ddl_manager* ddl_manager_;
  CompactionParams params_;
};

#endif
