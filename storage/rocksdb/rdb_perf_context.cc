#include "rdb_perf_context.h"

#include "rocksdb/perf_context.h"
#include "rocksdb/iostats_context.h"

// To add a new metric:
//   1. Update the PC enum in rdb_perf_context.h
//   2. Update sections (A), (B), and (C) below
//   3. Update perf_context.test and show_engine.test

std::string pc_stat_types[]=
{
  // (A) These should be in the same order as the PC enum
  "INTERNAL_KEY_SKIPPED_COUNT",
  "INTERNAL_DELETE_SKIPPED_COUNT",
  "DB_MUTEX_LOCK_NANOS",
  "DB_CONDITION_WAIT_NANOS",
  "IO_OPEN_NANOS",
  "IO_ALLOCATE_NANOS",
  "IO_WRITE_NANOS",
  "IO_READ_NANOS",
  "IO_RANGE_SYNC_NANOS",
  "IO_LOGGER_NANOS"
};

#define IO_PERF_INIT(_field_) \
  local_perf_context.value[idx++]= rocksdb::perf_context._field_
#define IO_STAT_INIT(_field_) \
  local_perf_context.value[idx++]= rocksdb::iostats_context._field_

void rdb_perf_context_start(rdb_perf_context_local &local_perf_context)
{
  // (B) These should be in the same order as the PC enum
  size_t idx= 0;
  IO_PERF_INIT(internal_key_skipped_count);
  IO_PERF_INIT(internal_delete_skipped_count);
  IO_PERF_INIT(db_mutex_lock_nanos);
  IO_PERF_INIT(db_condition_wait_nanos);
  IO_STAT_INIT(open_nanos);
  IO_STAT_INIT(allocate_nanos);
  IO_STAT_INIT(write_nanos);
  IO_STAT_INIT(read_nanos);
  IO_STAT_INIT(range_sync_nanos);
  IO_STAT_INIT(logger_nanos);
}

#define IO_PERF_DIFF(_field_)                                    \
  local_perf_context.value[idx]= rocksdb::perf_context._field_ - \
                                 local_perf_context.value[idx];  \
  idx++
#define IO_STAT_DIFF(_field_)                                       \
  local_perf_context.value[idx]= rocksdb::iostats_context._field_ - \
                                 local_perf_context.value[idx];     \
  idx++

void rdb_perf_context_diff(rdb_perf_context_local &local_perf_context)
{
  // (C) These should be in the same order as the PC enum
  size_t idx= 0;
  IO_PERF_DIFF(internal_key_skipped_count);
  IO_PERF_DIFF(internal_delete_skipped_count);
  IO_PERF_DIFF(db_mutex_lock_nanos);
  IO_PERF_DIFF(db_condition_wait_nanos);
  IO_STAT_DIFF(open_nanos);
  IO_STAT_DIFF(allocate_nanos);
  IO_STAT_DIFF(write_nanos);
  IO_STAT_DIFF(read_nanos);
  IO_STAT_DIFF(range_sync_nanos);
  IO_STAT_DIFF(logger_nanos);
}

#undef IO_PERF_INIT
#undef IO_STAT_INIT
#undef IO_PERF_DIFF
#undef IO_STAT_DIFF

void rdb_perf_context_stop(rdb_perf_context_local &local_perf_context,
                           rdb_perf_context_shared *table_perf_context,
                           rdb_perf_context_shared &global_perf_context)
{
  rdb_perf_context_diff(local_perf_context);
  for (int i= 0; i < PC_MAX_IDX; i++) {
    if (local_perf_context.value[i]) {
      if (table_perf_context) {
        table_perf_context->value[i] += local_perf_context.value[i];
      }
      global_perf_context.value[i] += local_perf_context.value[i];
    }
  }
}

void rdb_perf_context_collect(rdb_perf_context_shared &perf_context,
                              SHARE_PERF_COUNTERS *counters)
{
  for (int i= 0; i < PC_MAX_IDX; i++) {
    counters->value[i]= perf_context.value[i].load(std::memory_order_relaxed);
  }
}
