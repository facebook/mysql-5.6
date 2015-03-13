#ifndef PROPERTIES_COLLECTOR_H
#define PROPERTIES_COLLECTOR_H

#include "rocksdb/table_properties.h"

class MyRocksTablePropertiesCollector
    : public rocksdb::TablePropertiesCollector {
 public:
  virtual rocksdb::Status Add(const rocksdb::Slice& key, const rocksdb::Slice& value) override;

  virtual rocksdb::Status Finish(rocksdb::UserCollectedProperties* properties) override;

  virtual const char* Name() const override {
    return "MyRocksTablePropertiesCollector";
  }

  rocksdb::UserCollectedProperties GetReadableProperties() const override;

  static std::size_t GetRows(
    uint32_t index_number,
    const rocksdb::TablePropertiesCollection&
  );

 private:
  struct IndexStats {
    uint32_t index_number;
    std::ptrdiff_t data_size, rows;
  };
  std::vector<IndexStats> stats_;
  static const char* INDEXSTATS_KEY;
};


class MyRocksTablePropertiesCollectorFactory
    : public rocksdb::TablePropertiesCollectorFactory {
 public:
  virtual rocksdb::TablePropertiesCollector* CreateTablePropertiesCollector() override {
    return new MyRocksTablePropertiesCollector();
  }

  virtual const char* Name() const override {
    return "MyRocksTablePropertiesCollectorFactory";
  }
};

#endif
