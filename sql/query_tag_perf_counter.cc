// Copyright 2004-present Facebook. All Rights Reserved.

#include <unordered_map>
#include <mutex>
#include <memory>
#include <deque>
#include <boost/optional.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>


#include "query_tag_perf_counter.h"
#include "sql_class.h"
#include "sql_show.h"

namespace qutils {

namespace pt = boost::property_tree;

static int64_t timespec_diff(const timespec& start, const timespec& stop);

static std::mutex stats_mutex;
static std::unordered_map<std::string, uint64_t> stats;

query_tag_perf_counter::query_tag_perf_counter(THD* _thd)
  : started(false), thd(_thd)
{
  if(!thd->query_type.empty() && thd->num_queries > 0)
  {
      this->started =
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &this->starttime) == 0;
  }
}

query_tag_perf_counter::~query_tag_perf_counter()
{
  if(!this->started)
    return;
  struct timespec endtime;
  if(clock_gettime(CLOCK_THREAD_CPUTIME_ID, &endtime) != 0)
    return;

  int64_t cputime = timespec_diff(this->starttime, endtime);
  if(cputime < 0)
    return; // skip if overflow

  assert(thd->num_queries > 0);
  int64_t cputime_per_query = cputime / thd->num_queries;

  std::lock_guard<std::mutex> lock(stats_mutex);
  stats[thd->query_type] += cputime_per_query;
}

int fill_query_tag_perf_counter(THD* thd, TABLE_LIST* tables, Item* cond)
{
  std::lock_guard<std::mutex> lock(stats_mutex);

  DBUG_ENTER("fill_query_tag_perf_counter");

  TABLE* table= tables->table;
  for (const auto& row : stats)
  {

    restore_record(table, s->default_values);
    Field **field = table->field;

    const std::string& query_type = row.first;
    uint64_t cpu_time = row.second;

    field[0]->store(query_type.c_str(),
        query_type.length(), system_charset_info);
    field[1]->store(cpu_time, TRUE);

    if (schema_table_store_record(thd, table))
    {
      DBUG_RETURN(-1);
    }
  }
  DBUG_RETURN(0);
}

const uint query_type_field_length = 254;

ST_FIELD_INFO query_tag_perf_fields_info[]=
{
  {"QUERY_TYPE", query_type_field_length, MYSQL_TYPE_STRING,
		  0, 0, 0, SKIP_OPEN_TABLE},
  {"CPU", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
		  0, 0, 0, SKIP_OPEN_TABLE},
  {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}
};

static int64_t timespec_diff(const timespec& start, const timespec& stop)
{
  const int64_t sec = stop.tv_sec - start.tv_sec;
  const int64_t nsec = stop.tv_nsec - start.tv_nsec;
  const int64_t diff = sec * 1000000000LL + nsec;
  return diff;
}
} // namespace qutils

