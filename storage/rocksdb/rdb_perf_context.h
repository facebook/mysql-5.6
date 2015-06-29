#ifndef _rdb_perf_context_h
#define _rdb_perf_context_h

#include <atomic>
#include <cstdint>
#include <string>

enum {
  PC_KEY_SKIPPED= 0,
  PC_DELETE_SKIPPED,
  PC_DB_MUTEX_LOCK_NANOS,
  PC_DB_CONDITION_WAIT_NANOS,
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
