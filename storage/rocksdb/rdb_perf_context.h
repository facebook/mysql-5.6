#ifndef _rdb_perf_context_h
#define _rdb_perf_context_h

#include <atomic>
#include <cstdint>
#include <string>

enum {
  PC_USER_KEY_COMPARISON_COUNT = 0,
  PC_BLOCK_CACHE_HIT_COUNT,
  PC_BLOCK_READ_COUNT,
  PC_BLOCK_READ_BYTE,
  PC_BLOCK_READ_TIME,
  PC_BLOCK_CHECKSUM_TIME,
  PC_BLOCK_DECOMPRESS_TIME,
  PC_KEY_SKIPPED,
  PC_DELETE_SKIPPED,
  PC_GET_SNAPSHOT_TIME,
  PC_GET_FROM_MEMTABLE_TIME,
  PC_GET_FROM_MEMTABLE_COUNT,
  PC_GET_POST_PROCESS_TIME,
  PC_GET_FROM_OUTPUT_FILES_TIME,
  PC_SEEK_ON_MEMTABLE_TIME,
  PC_SEEK_ON_MEMTABLE_COUNT,
  PC_SEEK_CHILD_SEEK_TIME,
  PC_SEEK_CHILD_SEEK_COUNT,
  PC_SEEK_MIN_HEAP_TIME,
  PC_SEEK_INTERNAL_SEEK_TIME,
  PC_FIND_NEXT_USER_ENTRY_TIME,
  PC_WRITE_WAL_TIME,
  PC_WRITE_MEMTABLE_TIME,
  PC_WRITE_DELAY_TIME,
  PC_WRITE_PRE_AND_POST_PROCESSS_TIME,
  PC_DB_MUTEX_LOCK_NANOS,
  PC_DB_CONDITION_WAIT_NANOS,
  PC_MERGE_OPERATOR_TIME_NANOS,
  PC_READ_INDEX_BLOCK_NANOS,
  PC_READ_FILTER_BLOCK_NANOS,
  PC_NEW_TABLE_BLOCK_ITER_NANOS,
  PC_NEW_TABLE_ITERATOR_NANOS,
  PC_BLOCK_SEEK_NANOS,
  PC_FIND_TABLE_NANOS,
  PC_IO_THREAD_POOL_ID,
  PC_IO_BYTES_WRITTEN,
  PC_IO_BYTES_READ,
  PC_IO_OPEN_NANOS,
  PC_IO_ALLOCATE_NANOS,
  PC_IO_WRITE_NANOS,
  PC_IO_READ_NANOS,
  PC_IO_RANGE_SYNC_NANOS,
  PC_IO_LOGGER_NANOS,
  PC_MAX_IDX
};

struct rdb_perf_context_shared {
  std::atomic_ullong value[PC_MAX_IDX];
};

struct rdb_perf_context_local {
  uint64_t value[PC_MAX_IDX];
};

extern std::string pc_stat_types[PC_MAX_IDX];

void rdb_perf_context_start(rdb_perf_context_local &local_perf_context);

void rdb_perf_context_stop(rdb_perf_context_local &local_perf_context,
                           rdb_perf_context_shared *table_perf_context,
                           rdb_perf_context_shared &global_perf_context);

typedef rdb_perf_context_local SHARE_PERF_COUNTERS;

void rdb_perf_context_collect(rdb_perf_context_shared &perf_context,
                              SHARE_PERF_COUNTERS *counters);


// RAII utility to automatically call stop/start on scope entry/exit
struct rdb_perf_context_guard {
  rdb_perf_context_local &local_perf_context;
  rdb_perf_context_shared *table_perf_context;
  rdb_perf_context_shared &global_perf_context;

  rdb_perf_context_guard(rdb_perf_context_local &local_perf_context,
                         rdb_perf_context_shared *table_perf_context,
                         rdb_perf_context_shared &global_perf_context)
  : local_perf_context(local_perf_context),
    table_perf_context(table_perf_context),
    global_perf_context(global_perf_context)
  {
    rdb_perf_context_start(local_perf_context);
  }

  ~rdb_perf_context_guard() {
    rdb_perf_context_stop(local_perf_context,
                          table_perf_context,
                          global_perf_context);
  }
};

#define RDB_PERF_CONTEXT_GUARD(_local_, _table_, _global_) \
  rdb_perf_context_guard rdb_perf_context_guard_(_local_, _table_, _global_)

#endif
