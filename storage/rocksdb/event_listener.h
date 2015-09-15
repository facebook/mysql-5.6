#ifndef EVENT_LISTENER_H
#define EVENT_LISTENER_H

struct Table_ddl_manager;

#include "rocksdb/listener.h"

class MyRocksEventListener : public rocksdb::EventListener {
 public:
  MyRocksEventListener(Table_ddl_manager* ddl_manager) :
      ddl_manager_(ddl_manager) {
  }

  void OnCompactionCompleted(
    rocksdb::DB *db, const rocksdb::CompactionJobInfo& ci);
  void OnFlushCompleted(
    rocksdb::DB* db, const rocksdb::FlushJobInfo& flush_job_info);

 private:
  Table_ddl_manager* ddl_manager_;
};

#endif
