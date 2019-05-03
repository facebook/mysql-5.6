// Copyright 2004-present Facebook. All Rights Reserved.

#include <mysql/plugin.h>
#include <mysql/plugin_statistics.h>
#include <boost/algorithm/searching/boyer_moore_horspool.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <cctype>
#include <deque>
#include <iterator>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "field.h"
#include "mysqld.h"
#include "query_tag_perf_counter.h"
#include "sql_class.h"
#include "sql_show.h"
#include "table.h"

namespace qutils {

namespace pt = boost::property_tree;

using cpu_and_num_queries = std::tuple<uint64_t, uint64_t>;
static int64_t timespec_diff(const timespec &start, const timespec &stop);

static std::mutex stats_mutex;
static std::unordered_map<std::string, cpu_and_num_queries> stats;

query_tag_perf_counter::query_tag_perf_counter(THD *_thd)
    : started(false), thd(_thd) {
  if (!thd->query_type.empty() && thd->num_queries > 0) {
    this->started =
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &this->starttime) == 0;
  }
}

query_tag_perf_counter::~query_tag_perf_counter() {
  if (!this->started) return;
  struct timespec endtime;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &endtime) != 0) return;

  const int64_t cputime = timespec_diff(this->starttime, endtime);
  if (cputime < 0) return;  // skip if overflow

  DBUG_ASSERT(thd->num_queries > 0);

  std::lock_guard<std::mutex> lock(stats_mutex);
  cpu_and_num_queries &val = stats[thd->query_type];
  std::get<0>(val) += cputime;
  std::get<1>(val) += thd->num_queries;
}

int fill_query_tag_perf_counter(THD *thd, TABLE_LIST *tables, Item *) {
  std::lock_guard<std::mutex> lock(stats_mutex);

  DBUG_ENTER("fill_query_tag_perf_counter");

  TABLE *table = tables->table;
  for (const auto &row : stats) {
    restore_record(table, s->default_values);
    Field **field = table->field;

    const std::string &query_type = row.first;
    const cpu_and_num_queries &val = row.second;
    const uint64_t cpu_time = std::get<0>(val);
    const uint64_t num_queries = std::get<1>(val);

    field[0]->store(query_type.c_str(), query_type.length(),
                    system_charset_info);
    field[1]->store(cpu_time, true);
    field[2]->store(num_queries, true);

    if (schema_table_store_record(thd, table)) {
      DBUG_RETURN(-1);
    }
  }
  DBUG_RETURN(0);
}

const uint query_type_field_length = 254;

ST_FIELD_INFO query_tag_perf_fields_info[] = {
    {"QUERY_TYPE", query_type_field_length, MYSQL_TYPE_STRING, 0, 0, 0, 0},
    {"CPU", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0, 0},
    {"NUM_QUERIES", MY_INT64_NUM_DECIMAL_DIGITS, MYSQL_TYPE_LONGLONG, 0, 0, 0,
     0},
    {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, 0}};

static int64_t timespec_diff(const timespec &start, const timespec &stop) {
  const int64_t sec = stop.tv_sec - start.tv_sec;
  const int64_t nsec = stop.tv_nsec - start.tv_nsec;
  const int64_t diff = sec * 1000000000LL + nsec;
  return diff;
}

static const std::string region_fbend{".facebook.com"};
static const std::string region_undefined{"undefined"};

static constexpr std::size_t expected_region_length = 3;

static std::string get_host_region(const char *host) {
  std::string hostname{host};

  // remove .facebook.com suffix
  if (boost::algorithm::ends_with(hostname, region_fbend))
    hostname.erase(hostname.size() - region_fbend.size());

  // find last suffix delimited by .
  auto pos = hostname.find_last_of('.');
  auto region =
      (pos != std::string::npos) ? hostname.substr(pos + 1) : hostname;

  // remove trailing digits from region name
  while (!region.empty() && std::isdigit(region.back())) region.pop_back();

  // if region parsed incorrectly
  if (region.size() != expected_region_length) region = region_undefined;
  return region;
}

static const std::string shard_undefined{"undefined"};
static const std::string &get_shard_id(const THD *thd) {
  return thd->shard_id.empty() ? shard_undefined : thd->shard_id;
}

static std::string make_ratelim_key(const std::string &tag, const THD *thd) {
  // here 'precalculated_region' is deliberately defined as a magic static
  // rather than as a global variable to avoid initialization order fiasco
  // when a value of a global variable defined in one translation unit is
  // needed to initialize a value of a global variable in another.
  static const std::string precalculated_region =
      get_host_region(glob_hostname);
  return tag + "/" +
         DBUG_EVALUATE_IF("emulate_facebook_hostname",
                          get_host_region("eur777.facebook.com"),
                          precalculated_region) +
         "/" + get_shard_id(thd);
}

using optional_string = boost::optional<std::string>;

static constexpr char async_prefix[] = "async-";
static const boost::algorithm::boyer_moore_horspool<const char *> searcher{
    std::cbegin(async_prefix), std::prev(std::cend(async_prefix))};

// search only in first 100 characters
static constexpr std::size_t find_async_limit = 100;

static optional_string find_async_tag(const LEX_CSTRING query, const THD *thd) {
  const char *query_limited_end =
      query.str +
      (query.length > find_async_limit ? find_async_limit : query.length);
  auto sub = searcher(query.str, query_limited_end);
  if (sub.first != query_limited_end) {
    const char *subend = sub.second;
    const char *query_end = query.str + query.length;
    while (subend < query_end && std::isdigit(*subend)) {
      subend++;
    }
    return make_ratelim_key(std::string(sub.first, subend), thd);
  }
  return optional_string{};
}

async_query_counter::async_query_counter(THD *_thd)
    : started(false), thd(_thd) {
  if (async_query_counter_enabled) {
    auto tag = find_async_tag(thd->query(), thd);
    if (tag) {
      this->started = true;
      this->tag = std::move(*tag);
      clock_gettime(CLOCK_THREAD_CPUTIME_ID, &this->starttime);
    }
  }
}

static bool plugins_dispatch(THD *thd, plugin_ref plugin, void *arg) {
  auto pl = static_cast<const st_mysql_statistics *>(plugin_decl(plugin)->info);
  if (pl != nullptr)
    pl->publish_stmt_stats(thd, static_cast<mysql_stmt_stats_t *>(arg));
  return false;
}

async_query_counter::~async_query_counter() {
  if (!this->started) return;

  timespec endtime;
  clock_gettime(CLOCK_THREAD_CPUTIME_ID, &endtime);
  // nanoseconds
  int64_t cputime = timespec_diff(this->starttime, endtime);
  if (cputime < 0) cputime = 0;

  mysql_stmt_stats_t stats;
  stats_measurements measurements;
  stats.tag = this->tag.c_str();
  measurements.thd_cpu_time =
      static_cast<uint64_t>(cputime / 1000);  // microseconds
  stats.stats = &measurements;
  plugin_foreach(thd, plugins_dispatch, MYSQL_QUERY_PERF_STATS_PLUGIN, &stats);
}

}  // namespace qutils
