// Copyright 2004-present Facebook. All Rights Reserved.

#include <unordered_map>
#include <mutex>
#include <memory>
#include <deque>
#include <cctype>
#include <boost/optional.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/algorithm/string.hpp>

#include "query_tag_perf_counter.h"
#include "sql_class.h"
#include "sql_show.h"
#include <mysql/plugin.h>
#include <mysql/plugin_statistics.h>

namespace qutils {

namespace pt = boost::property_tree;

using cpu_and_num_queries = std::tuple<uint64_t, uint64_t>;
static int64_t timespec_diff(const timespec& start, const timespec& stop);

static std::mutex stats_mutex;
static std::unordered_map<std::string, cpu_and_num_queries> stats;

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

  const int64_t cputime = timespec_diff(this->starttime, endtime);
  if(cputime < 0)
    return; // skip if overflow

  DBUG_ASSERT(thd->num_queries > 0);

  std::lock_guard<std::mutex> lock(stats_mutex);
  cpu_and_num_queries& val = stats[thd->query_type];
  std::get<0>(val) += cputime;
  std::get<1>(val) += thd->num_queries;
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
    const cpu_and_num_queries& val = row.second;
    const uint64_t cpu_time = std::get<0>(val);
    const uint64_t num_queries = std::get<1>(val);

    field[0]->store(query_type.c_str(),
        query_type.length(), system_charset_info);
    field[1]->store(cpu_time, TRUE);
    field[2]->store(num_queries, TRUE);

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
	{"NUM_QUERIES", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG,
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

static std::string get_host_region()
{
  std::string hostname(glob_hostname);
  std::string fbend(".facebook.com");

  // remove .facebook.com suffix
  if(boost::algorithm::ends_with(hostname, fbend))
    hostname.erase(hostname.size() - fbend.size());

  // find last suffix delimited by .
  size_t pos = hostname.find_last_of('.');
  std::string region = (pos != std::string::npos) ?
    hostname.substr(pos + 1) : hostname;

  // remove trailing digits from region name
  while(!region.empty() && std::isdigit(region.back()))
    region.pop_back();

  // if region parsed incorrectly
  if(region.size() != 3)
    region = "undefined";
  return region;
}

static const std::string& get_shard_id(const THD* thd)
{
  static const std::string undefined = "undefined";
  return thd->shard_id.empty() ? undefined : thd->shard_id;
}

static std::string make_ratelim_key(const std::string& tag, const THD* thd)
{
  static const std::string region = get_host_region();
  const std::string& shard = get_shard_id(thd);
  return tag + "/" + region + "/" + shard;
}

static boost::optional<std::string>
find_async_tag(const char *query, uint32 query_length, const THD* thd)
{
  static const std::string async_word("async-");
  // search only in first 100 characters
  std::string query100(query, query_length > 100 ? 100 : query_length);
  std::string::size_type pos = query100.find(async_word);
  if(pos != std::string::npos) {
    std::string::size_type epos = pos + async_word.size();
    while(epos < query100.size() && std::isdigit(query100[epos])) {
      epos++;
    }
    return make_ratelim_key(query100.substr(pos, epos - pos), thd);
  }
  return boost::optional<std::string>{};
}

async_query_counter::async_query_counter(THD* _thd)
  : started(false), thd(_thd)
{
  if(async_query_counter_enabled) {
    boost::optional<std::string> tag = find_async_tag(thd->query(),
        thd->query_length(), thd);
    if(tag) {
      this->started = true;
      this->tag = std::move(*tag);
      clock_gettime(CLOCK_THREAD_CPUTIME_ID, &this->starttime);

    }
  }
}

static my_bool plugins_dispatch(THD *thd, plugin_ref plugin,
                                 void *arg)
{
  const struct st_mysql_statistics *pl =
      (struct st_mysql_statistics *) plugin_decl(plugin)->info;
  mysql_stmt_stats_t *stats = (mysql_stmt_stats_t *)arg;
  if(pl != nullptr)
    pl->publish_stmt_stats(thd, stats);
  return FALSE;
}

async_query_counter::~async_query_counter()
{
  if(!this->started)
    return;

  struct timespec endtime;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &endtime);
  // nanoseconds
  int64_t cputime = timespec_diff(this->starttime, endtime);
  if(cputime < 0)
    cputime = 0;

  mysql_stmt_stats_t stats;
  stats_measurements measurements;
  stats.tag = this->tag.c_str();
  measurements.thd_cpu_time = cputime / 1000; // microseconds
  stats.stats = &measurements;
  plugin_foreach(thd, plugins_dispatch, MYSQL_QUERY_PERF_STATS_PLUGIN, &stats);
}

} // namespace qutils

