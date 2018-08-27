// Copyright 2004-present Facebook. All Rights Reserved.

#ifndef QUERY_TAG_PERF_COUNTER_H_
#define QUERY_TAG_PERF_COUNTER_H_

#include <ctime>
#include <string>

class Item;
class THD;
class Table_ref;
struct ST_FIELD_INFO;

namespace qutils {

extern ST_FIELD_INFO query_tag_perf_fields_info[];
extern int fill_query_tag_perf_counter(THD *thd, Table_ref *tables,
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

}  // namespace qutils

#endif /* QUERY_TAG_PERF_COUNTER_H_ */
