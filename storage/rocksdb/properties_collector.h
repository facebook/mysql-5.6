#ifndef PROPERTIES_COLLECTOR_H
#define PROPERTIES_COLLECTOR_H

#include "rocksdb/table_properties.h"

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

  MyRocksTablePropertiesCollector(Table_ddl_manager* ddl_manager) :
      ddl_manager_(ddl_manager) {
  }

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

  static std::map<uint32_t, IndexStats> GetStats(
    const rocksdb::TablePropertiesCollection& collection
  );

 private:
  std::unique_ptr<RDBSE_KEYDEF> keydef_;
  Table_ddl_manager* ddl_manager_;

  std::vector<IndexStats> stats_;
  static const char* INDEXSTATS_KEY;

  // last added
  std::string last_key_;
};


class MyRocksTablePropertiesCollectorFactory
    : public rocksdb::TablePropertiesCollectorFactory {
 public:
  MyRocksTablePropertiesCollectorFactory(Table_ddl_manager* ddl_manager) :
      ddl_manager_(ddl_manager) {
  }

  virtual rocksdb::TablePropertiesCollector* CreateTablePropertiesCollector() override {
    return new MyRocksTablePropertiesCollector(ddl_manager_);
  }

  virtual const char* Name() const override {
    return "MyRocksTablePropertiesCollectorFactory";
  }
 private:
  Table_ddl_manager* ddl_manager_;
};

#endif
