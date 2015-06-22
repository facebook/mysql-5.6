#include <iostream>
#include <mysql/plugin.h>
#include "../ha_rocksdb.h"
#include "../ha_rocksdb_proto.h"

//#include "sql_class.h"
#include "sql_array.h"

#include "my_bit.h"
#include "my_stacktrace.h"

#include <sstream>

#include "../rdb_datadic.h"
#include "../rdb_locks.h"
#include "../rdb_rowmods.h"

#include "../rdb_cf_options.h"
#include "../rdb_cf_manager.h"
#include "../rdb_dropped_indices.h"
#include "../rdb_i_s.h"

#include "rocksdb/compaction_filter.h"
#include "rocksdb/env.h"
#include "rocksdb/rate_limiter.h"
#include "rocksdb/slice.h"
#include "rocksdb/slice_transform.h"
#include "rocksdb/table.h"
#include "rocksdb/metadata.h"
#include "rocksdb/utilities/convenience.h"
#include "rocksdb/utilities/flashcache.h"
#include "rocksdb/slice_transform.h"
#include "rocksdb/perf_context.h"
#include "../properties_collector.h"

void putKeys(MyRocksTablePropertiesCollector& coll, int num, bool is_delete,
             uint64_t expected_deleted) {
  std::string str("aaaaaaaaaaaaaa");
  rocksdb::Slice sl(str.data(), str.size());

  for (int i=0; i < num; i++) {
    coll.AddUserKey(
      sl, sl,
      is_delete?rocksdb::kEntryDelete:rocksdb::kEntryPut, 0, 100);
  }
  assert(coll.GetMaxDeletedRows() ==  expected_deleted);
}

int main(int argc, char** argv) {
  // test the circular buffer for delete flags
  CompactionParams params;
  params.file_size_= 333;
  params.deletes_= 333; //irrelevant
  params.window_= 10;

  MyRocksTablePropertiesCollector coll(NULL, params);
  putKeys(coll, 2, true, 2); // [xx]
  putKeys(coll, 3, false, 2); // [xxo]
  putKeys(coll, 1, true, 3); // [xxox]
  putKeys(coll, 6, false, 3); // [xxoxoooooo]
  putKeys(coll, 3, true, 4); // xxo[xooooooxxx]
  putKeys(coll, 1, false, 4); // xxox[ooooooxxxo]
  putKeys(coll, 100, false, 4); // ....[oooooooooo]
  putKeys(coll, 100, true, 10); // ....[xxxxxxxxxx]
  putKeys(coll, 100, true, 10); // ....[oooooooooo]

  return 0;
}
