#include <native_procedure.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

C_MODE_START;
int nodeGet(ExecutionContext *ec, const char *packet, size_t length);
int linkGetRange(ExecutionContext *ec, const char *packet, size_t length);
int linkGetId2s(ExecutionContext *ec, const char *packet, size_t length);
int countGet(ExecutionContext *ec, const char *packet, size_t length);
int sleepRange(ExecutionContext *ec, const char *packet, size_t length);
int rangeQuery(ExecutionContext *ec, const char *packet, size_t length);
int invalidKey1(ExecutionContext *ec, const char *packet, size_t length);
int invalidKey2(ExecutionContext *ec, const char *packet, size_t length);
int invalidOpen1(ExecutionContext *ec, const char *packet, size_t length);
int invalidOpen2(ExecutionContext *ec, const char *packet, size_t length);
int invalidOpen3(ExecutionContext *ec, const char *packet, size_t length);
int invalidOpen4(ExecutionContext *ec, const char *packet, size_t length);
C_MODE_END;

struct string_ref {
  const char *val;
  size_t len;
};

static int init_query(ExecutionContext *ec, const char *db, const char *table,
                      const std::vector<const char *> &columns,
                      const char *index) {
  int ret;
  if ((ret = ec->open_table(db, table)) ||
      (ret = ec->set_read_columns(columns)) || (ret = ec->send_metadata()) ||
      (ret = ec->index_init(index))) {
    return ret;
  }
  return ret;
}

// Gets the next field assuming space as separators. Repeated spaces are
// ignored.
static bool next_field(const char **packet, size_t *len, string_ref *s) {
  DBUG_ASSERT(s != nullptr);

  while (*len && **packet == ' ') {
    (*packet)++;
    (*len)--;
  }

  if (*len == 0) {
    return false;
  }

  s->val = *packet;
  while (*len && **packet != ' ') {
    (*packet)++;
    (*len)--;
  }
  s->len = *packet - s->val;

  return true;
}

// Calls next_field repeatedly with error checking.
static bool next_fields(const char **packet, size_t *len,
                        const std::vector<string_ref *> &vs) {
  for (auto s : vs) {
    if (!next_field(packet, len, s)) {
      return false;
    }
  }

  return true;
}

// strtoull with error checking.
static bool checked_strtoull(const string_ref &s, ulonglong *r) {
  char *endptr;
  ulonglong ret = strtoull(s.val, &endptr, 10);
  if (errno == ERANGE || endptr == s.val || endptr != (s.val + s.len)) {
    return false;
  }
  *r = ret;
  return true;
}

// Expected input:
//  nodeGet x1;
//
// This does the equivalent of:
//  SELECT id, type, version, time, data FROM linkdb.nodetable WHERE id = x1;
int nodeGet(ExecutionContext *ec, const char *packet, size_t length) {
  int ret;
  string_ref arg_id;

  if (!next_field(&packet, &length, &arg_id)) {
    return EC_NP_WRONG_ARGUMENTS;
  }

  if ((ret =
           init_query(ec, "linkdb", "nodetable",
                      {"id", "type", "version", "time", "data"}, "PRIMARY"))) {
    return ret;
  }

  // Set the key at index 0 to be ("id" => arg_id).
  if ((ret = ec->key_write(
           0, {{"id", ExecutionContext::Value(arg_id.val, arg_id.len)}}))) {
    return ret;
  }

  // Do lookup using key at index 0.
  ret = ec->index_read_map(0, HA_READ_KEY_EXACT);
  if (!ret) {
    if ((ret = ec->send_row())) {
      return ret;
    }
  } else if (ret != HA_ERR_KEY_NOT_FOUND) {
    return -1;
  }

  return 0;
}

// Expected input:
//  linkGetRange x1 x2 x3 x4 x5 x6;
//
// This does the equivalent of:
//  SELECT id1, id2, link_type, visibility, data, time, version
//   FROM linkdb.linktable WHERE id1 = x1 and link_type = x2 and visibility = 1
//   and time >= x3 and time <= x4 limit x5, x6;
int linkGetRange(ExecutionContext *ec, const char *packet, size_t length) {
  int ret;
  string_ref arg_id, arg_link_type, arg_time1, arg_time2, arg_offset, arg_limit;
  ulonglong offset, limit;

  if (!next_fields(&packet, &length, {&arg_id, &arg_link_type, &arg_time1,
                                      &arg_time2, &arg_offset, &arg_limit})) {
    return EC_NP_WRONG_ARGUMENTS;
  }

  if (!checked_strtoull(arg_offset, &offset) ||
      !checked_strtoull(arg_limit, &limit)) {
    return EC_NP_WRONG_ARGUMENTS;
  }

  if ((ret = init_query(
           ec, "linkdb", "linktable",
           {"id1", "id2", "link_type", "visibility", "data", "time", "version"},
           "id1_type"))) {
    return ret;
  }

  // Write start and end key into index 0 and 1 respectively.
  if ((ret = ec->key_write(
           0, {{"id1", ExecutionContext::Value(arg_id.val, arg_id.len)},
               {"link_type",
                ExecutionContext::Value(arg_link_type.val, arg_link_type.len)},
               {"visibility", ExecutionContext::Value(1, false)},
               {"time",
                ExecutionContext::Value(arg_time1.val, arg_time1.len)}}))) {
    return ret;
  }

  if ((ret = ec->key_write(
           1, {{"id1", ExecutionContext::Value(arg_id.val, arg_id.len)},
               {"link_type",
                ExecutionContext::Value(arg_link_type.val, arg_link_type.len)},
               {"visibility", ExecutionContext::Value(1, false)},
               {"time",
                ExecutionContext::Value(arg_time2.val, arg_time2.len)}}))) {
    return ret;
  }

  // read_range_first will use the keys in index 0 and 1 as the start and end
  // key range.
  ret = ec->read_range_first(0, false);
  if (ret == HA_ERR_KEY_NOT_FOUND || ret == HA_ERR_END_OF_FILE) {
    return 0;
  }

  ulonglong rows_read = 0;
  ulonglong rows_returned = 0;
  while (!ret && rows_returned < limit) {
    if (rows_read >= offset) {
      if ((ret = ec->send_row())) {
        return ret;
      }
      rows_returned++;
    }

    ret = ec->read_range_next();
    rows_read++;
  }

  if (ret != HA_ERR_END_OF_FILE) {
    return ret;
  }

  return 0;
}

static bool parseKey(const char **packet, size_t *length,
                     std::map<std::string, ExecutionContext::Value> *out) {
  std::map<std::string, ExecutionContext::Value> ret;
  string_ref tmp;
  ulonglong num;

  if (!next_field(packet, length, &tmp) || !checked_strtoull(tmp, &num)) {
    return false;
  }

  for (ulonglong i = 0; i < num; i++) {
    if (!next_field(packet, length, &tmp)) {
      return false;
    }
    std::string k{tmp.val, tmp.len};
    if (!next_field(packet, length, &tmp)) {
      return false;
    }
    (*out)[k] = ExecutionContext::Value(tmp.val, tmp.len);
  }
  return true;
}

// Expected input:
//  rangeQuery asc start_excl end_excl
//   n1 [keyname keyvalue]* n2 [keyname keyvalue]*;
//
// This function is meant to test read_range_first and read_range_next. It
// takes two sets of keys/values and uses them as the start and end key for
// the range. The start_excl/end_excl variable indicates whether the start/end
// keys are exclusive or not, and asc indicates whether the results are
// returned in ascending order or not.
int rangeQuery(ExecutionContext *ec, const char *packet, size_t length) {
  int ret;
  std::map<std::string, ExecutionContext::Value> start;
  std::map<std::string, ExecutionContext::Value> end;
  std::map<const char *, ExecutionContext::Value> arg;
  string_ref tmp;
  ulonglong num;
  uint range_flag = 0;
  bool asc;

  // Parse asc start_excl and end_excl.
  if (!next_field(&packet, &length, &tmp) || !checked_strtoull(tmp, &num)) {
    return EC_NP_WRONG_ARGUMENTS;
  }

  asc = num;

  if (!next_field(&packet, &length, &tmp) || !checked_strtoull(tmp, &num)) {
    return EC_NP_WRONG_ARGUMENTS;
  }

  if (num) {
    range_flag |= NEAR_MIN;
  }

  if (!next_field(&packet, &length, &tmp) || !checked_strtoull(tmp, &num)) {
    return EC_NP_WRONG_ARGUMENTS;
  }

  if (num) {
    range_flag |= NEAR_MAX;
  }

  // Parse the start and end keys.
  if (!parseKey(&packet, &length, &start) ||
      !parseKey(&packet, &length, &end)) {
    return EC_NP_WRONG_ARGUMENTS;
  }

  if ((ret = init_query(
           ec, "linkdb", "linktable",
           {"id1", "id2", "link_type", "visibility", "data", "time", "version"},
           "id1_type"))) {
    return ret;
  }

  // Convert map<string, Value> to map<const char*, Value>, and write the keys
  // out.
  for (auto &k : start) {
    arg[k.first.c_str()] = k.second;
  }

  if ((ret = ec->key_write(0, arg))) {
    return ret;
  }

  arg.clear();
  for (auto &k : end) {
    arg[k.first.c_str()] = k.second;
  }

  if ((ret = ec->key_write(1, arg))) {
    return ret;
  }

  ret = ec->read_range_first(range_flag, asc);
  if (ret == HA_ERR_KEY_NOT_FOUND || ret == HA_ERR_END_OF_FILE) {
    return 0;
  }

  while (!ret) {
    if ((ret = ec->send_row())) {
      return ret;
    }

    ret = ec->read_range_next();
  }

  if (ret != HA_ERR_KEY_NOT_FOUND && ret != HA_ERR_END_OF_FILE) {
    return ret;
  }

  return 0;
}

// Expected input:
//  linkGetId2s x1 x2 n [x31 x32 ... x3n]
//
// This does the equivalent of:
//  SELECT id1, id2, link_type, visibility, data, time, version FROM
//   linkdb.linktable
//   WHERE id1 = x1 and link_type = x2 and id2 in (x31, x32 ... x3n);
int linkGetId2s(ExecutionContext *ec, const char *packet, size_t length) {
  int ret;
  string_ref arg_id1, arg_link_type, arg_id2_count;
  ulonglong id2_count;
  std::vector<string_ref> arg_id2s;
  std::vector<string_ref *> arg_id2s_ref;

  if (!next_fields(&packet, &length,
                   {&arg_id1, &arg_link_type, &arg_id2_count}) ||
      !checked_strtoull(arg_id2_count, &id2_count)) {
    return EC_NP_WRONG_ARGUMENTS;
  }

  arg_id2s.resize(id2_count);
  arg_id2s_ref.resize(id2_count);
  for (ulonglong i = 0; i < id2_count; i++) {
    arg_id2s_ref[i] = &arg_id2s[i];
  }

  if (!next_fields(&packet, &length, arg_id2s_ref)) {
    return EC_NP_WRONG_ARGUMENTS;
  }

  if ((ret = init_query(
           ec, "linkdb", "linktable",
           {"id1", "id2", "link_type", "visibility", "data", "time", "version"},
           "PRIMARY"))) {
    return ret;
  }

  for (const auto &arg_id2 : arg_id2s) {
    if ((ret = ec->key_write(
             0,
             {{"id1", ExecutionContext::Value(arg_id1.val, arg_id1.len)},
              {"link_type",
               ExecutionContext::Value(arg_link_type.val, arg_link_type.len)},
              {"id2", ExecutionContext::Value(arg_id2.val, arg_id2.len)}}))) {
      return ret;
    }

    ret = ec->index_read_map(0, HA_READ_KEY_EXACT);
    if (!ret) {
      if ((ret = ec->send_row())) {
        return ret;
      }
    } else if (ret != HA_ERR_KEY_NOT_FOUND) {
      return ret;
    }
  }

  return 0;
}

// Expected input:
//  countGet x1 x2;
//
// This does the equivalent of:
//  SELECT count FROM linkdb.counttable WHERE id = x1 and link_type = x2;
int countGet(ExecutionContext *ec, const char *packet, size_t length) {
  int ret;
  string_ref arg_id, arg_link_type;
  std::vector<string_ref *> v = {&arg_id, &arg_link_type};

  if (!next_fields(&packet, &length, {&arg_id, &arg_link_type})) {
    return EC_NP_WRONG_ARGUMENTS;
  }

  if ((ret = init_query(ec, "linkdb", "counttable", {"count"}, "PRIMARY"))) {
    return ret;
  }

  if ((ret = ec->key_write(
           0, {{"id", ExecutionContext::Value(arg_id.val, arg_id.len)},
               {"link_type", ExecutionContext::Value(arg_link_type.val,
                                                     arg_link_type.len)}}))) {
    return ret;
  }

  ret = ec->index_read_map(0, HA_READ_KEY_EXACT);

  if (!ret) {
    if ((ret = ec->send_row())) {
      return ret;
    }
  } else if (ret != HA_ERR_KEY_NOT_FOUND) {
    return ret;
  }

  return 0;
}

int sleepRange(ExecutionContext *ec, const char *packet, size_t length) {
  int ret = 0;
  string_ref tmp;
  ulonglong num;

  if (!next_field(&packet, &length, &tmp) || !checked_strtoull(tmp, &num)) {
    return EC_NP_WRONG_ARGUMENTS;
  }

  if ((ret = init_query(ec, "linkdb", "counttable", {"count"}, "PRIMARY"))) {
    return ret;
  }

  ret = ec->read_range_first(0, true);

  while (!ret) {
    if (num && (ret = ec->send_row())) {
      return ret;
    }
    ret = ec->read_range_next();
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }

  if (ret != HA_ERR_KEY_NOT_FOUND && ret != HA_ERR_END_OF_FILE) {
    return ret;
  }

  return 0;
}

int invalidKey1(ExecutionContext *ec, const char *packet, size_t length) {
  int ret;

  if ((ret = init_query(ec, "linkdb", "counttable", {"count"}, "PRIMARY"))) {
    return ret;
  }

  if ((ret = ec->key_write(0, {{"asdf", ExecutionContext::Value(1, false)}}))) {
    return ret;
  }

  assert(false);
  return 0;
}

int invalidOpen1(ExecutionContext *ec, const char *packet, size_t length) {
  int ret;

  if ((ret = ec->open_table("linkdb", "counttable"))) {
    return ret;
  }

  if ((ret = ec->open_table("linkdb", "linktable"))) {
    return ret;
  }

  assert(false);
  return 0;
}

int invalidOpen2(ExecutionContext *ec, const char *packet, size_t length) {
  int ret;

  if ((ret = ec->open_table("linkdb", "counttable")) ||
      (ret = ec->set_read_columns({"count"}))) {
    return ret;
  }

  if ((ret = ec->set_read_columns({"count"}))) {
    return ret;
  }

  assert(false);
  return 0;
}

int invalidOpen3(ExecutionContext *ec, const char *packet, size_t length) {
  int ret;

  if ((ret = ec->open_table("linkdb", "counttable")) ||
      (ret = ec->set_read_columns({"count"})) || (ret = ec->send_metadata())) {
    return ret;
  }

  if ((ret = ec->send_metadata())) {
    return ret;
  }

  assert(false);
  return 0;
}

int invalidOpen4(ExecutionContext *ec, const char *packet, size_t length) {
  int ret;

  if ((ret = init_query(ec, "linkdb", "counttable", {"count"}, "PRIMARY"))) {
    return ret;
  }

  if ((ret = ec->index_init("PRIMARY"))) {
    return ret;
  }

  assert(false);
  return 0;
}
