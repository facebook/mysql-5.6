// Copyright 2004-present Facebook. All Rights Reserved.

#ifndef QUERY_TAG_PERF_COUNTER_H_
#define QUERY_TAG_PERF_COUNTER_H_

#include <ctime>
#include <string>

class Item;
class THD;
struct TABLE_LIST;
struct ST_FIELD_INFO;

namespace qutils {

extern ST_FIELD_INFO query_tag_perf_fields_info[];
extern int fill_query_tag_perf_counter(THD *thd, TABLE_LIST *tables,
                                       Item *cond);

class query_tag_perf_counter {
 public:
  query_tag_perf_counter(THD *thd);
  ~query_tag_perf_counter();

 private:
  struct timespec starttime;  // query start timestamp
  bool started;               // query_info attribute detected
  THD *thd;                   // reference to outer THD
};

class async_query_counter {
 public:
  async_query_counter(THD *thd);
  ~async_query_counter();

  // Not copy/move constructible or copy/move assignable.
  async_query_counter(const async_query_counter &) = delete;
  async_query_counter &operator=(const async_query_counter &) = delete;
  async_query_counter(async_query_counter &&) = delete;
  async_query_counter &operator=(async_query_counter &&) = delete;

 private:
  timespec starttime;  // query start timestamp
  bool started;        // query_info attribute detected
  THD *const thd;      // reference to outer THD
  std::string tag;
};

}  // namespace qutils

#endif /* QUERY_TAG_PERF_COUNTER_H_ */
