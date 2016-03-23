/*
   Copyright (c) 2015, Facebook, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/* This C++ file's header file */
#include "./properties_collector.h"

/* Standard C++ header files */
#include <algorithm>
#include <map>
#include <string>
#include <vector>

/* MySQL header files */
#include "./log.h"
#include "./my_stacktrace.h"
#include "./sql_array.h"

/* MyRocks header files */
#include "./rdb_datadic.h"

namespace myrocks {

uint64_t rocksdb_num_sst_entry_put = 0;
uint64_t rocksdb_num_sst_entry_delete = 0;
uint64_t rocksdb_num_sst_entry_singledelete = 0;
uint64_t rocksdb_num_sst_entry_merge = 0;
uint64_t rocksdb_num_sst_entry_other = 0;
my_bool rocksdb_compaction_sequential_deletes_count_sd = false;

Rdb_tbl_prop_coll::Rdb_tbl_prop_coll(
  Table_ddl_manager* ddl_manager,
  Rdb_compact_params params,
  uint32_t cf_id,
  const uint8_t table_stats_sampling_pct
) :
    m_cf_id(cf_id),
    m_ddl_manager(ddl_manager),
    m_rows(0l), m_deleted_rows(0l), m_max_deleted_rows(0l),
    m_file_size(0), m_params(params),
    m_table_stats_sampling_pct(table_stats_sampling_pct),
    m_seed(time(nullptr)),
    m_card_adj_extra(1.)
{
  // We need to adjust the index cardinality numbers based on the sampling
  // rate so that the output of "SHOW INDEX" command will reflect reality
  // more closely. It will still be an approximation, just a better one.
  if (m_table_stats_sampling_pct > 0) {
    m_card_adj_extra = 100. / m_table_stats_sampling_pct;
  }

  m_deleted_rows_window.resize(m_params.m_window, false);
}

/*
  This function is called by RocksDB for every key in the SST file
*/
rocksdb::Status
Rdb_tbl_prop_coll::AddUserKey(
    const rocksdb::Slice& key, const rocksdb::Slice& value,
    rocksdb::EntryType type, rocksdb::SequenceNumber seq,
    uint64_t file_size
) {
  if (key.size() >= 4) {
    switch (type) {
    case rocksdb::kEntryPut:
      rocksdb_num_sst_entry_put++;
      break;
    case rocksdb::kEntryDelete:
      rocksdb_num_sst_entry_delete++;
      break;
    case rocksdb::kEntrySingleDelete:
      rocksdb_num_sst_entry_singledelete++;
      break;
    case rocksdb::kEntryMerge:
      rocksdb_num_sst_entry_merge++;
      break;
    case rocksdb::kEntryOther:
      rocksdb_num_sst_entry_other++;
      break;
    default:
      break;
    }

    if (m_params.m_window > 0) {
      // record the "is deleted" flag into the sliding window
      // the sliding window is implemented as a circular buffer
      // in m_deleted_rows_window vector
      // the current position in the circular buffer is pointed at by
      // m_rows % m_deleted_rows_window.size()
      // m_deleted_rows is the current number of 1's in the vector
      // --update the counter for the element which will be overridden
      bool is_delete= (type == rocksdb::kEntryDelete ||
       (type == rocksdb::kEntrySingleDelete &&
         rocksdb_compaction_sequential_deletes_count_sd));
      // Only make changes if the value at the current position needs to change
      uint64_t pos = m_rows % m_deleted_rows_window.size();
      if (is_delete != m_deleted_rows_window[pos]) {
        // Set or clear the flag at the current position as appropriate
        m_deleted_rows_window[pos]= is_delete;
        if (!is_delete)
          m_deleted_rows--;
        else if (++m_deleted_rows > m_max_deleted_rows)
          m_max_deleted_rows = m_deleted_rows;
      }
    }

    m_rows++;

    CollectStatsForRow(key, value, type, file_size);
  }

  return rocksdb::Status::OK();
}

void Rdb_tbl_prop_coll::CollectStatsForRow(
  const rocksdb::Slice& key, const rocksdb::Slice& value,
  rocksdb::EntryType type, uint64_t file_size) {
  // All the code past this line must deal ONLY with collecting the
  // statistics.
  GL_INDEX_ID gl_index_id;

  gl_index_id.cf_id = m_cf_id;
  gl_index_id.index_id = read_big_uint4((const uchar*)key.data());

  if (m_stats.empty() || gl_index_id != m_stats.back().m_gl_index_id) {
    m_keydef = nullptr;
    // starting a new table
    // add the new element into m_stats
    m_stats.push_back(Rdb_index_stats(gl_index_id));
    if (m_ddl_manager) {
      m_keydef = m_ddl_manager->get_copy_of_keydef(gl_index_id);
    }
    if (m_keydef) {
      // resize the array to the number of columns.
      // It will be initialized with zeroes
      m_stats.back().m_distinct_keys_per_prefix.resize(
        m_keydef->get_m_key_parts());
      m_stats.back().m_name = m_keydef->get_name();
    }
    m_last_key.clear();
  }

  auto& stats = m_stats.back();
  stats.m_data_size += key.size()+value.size();

  // Incrementing per-index entry-type statistics
  switch (type) {
  case rocksdb::kEntryPut:
    stats.m_rows++;
    break;
  case rocksdb::kEntryDelete:
    stats.m_entry_deletes++;
    break;
  case rocksdb::kEntrySingleDelete:
    stats.m_entry_single_deletes++;
    break;
  case rocksdb::kEntryMerge:
    stats.m_entry_merges++;
    break;
  case rocksdb::kEntryOther:
    stats.m_entry_others++;
    break;
  default:
    // NO_LINT_DEBUG
    sql_print_error("RocksDB: Unexpected entry type found: %u. "
                    "This should not happen so aborting the system.", type);
    abort_with_stack_traces();
    break;
  }

  stats.m_actual_disk_size += file_size - m_file_size;
  m_file_size = file_size;

  bool collect_cardinality = ShouldCollectStats();

  if (m_keydef && collect_cardinality) {
    std::size_t column = 0;
    rocksdb::Slice last(m_last_key.data(), m_last_key.size());

    if (m_last_key.empty()
        || (m_keydef->compare_keys(&last, &key, &column) == 0)) {
      DBUG_ASSERT(column <= stats.m_distinct_keys_per_prefix.size());

      for (std::size_t i = column;
           i < stats.m_distinct_keys_per_prefix.size(); i++) {
        stats.m_distinct_keys_per_prefix[i]++;
      }

      // assign new last_key for the next call
      // however, we only need to change the last key
      // if one of the first n-1 columns is different
      // If the n-1 prefix is the same, no sense in storing
      // the new key
      if (column < stats.m_distinct_keys_per_prefix.size()) {
        m_last_key.assign(key.data(), key.size());
      }
    }
  }
}

const char* Rdb_tbl_prop_coll::INDEXSTATS_KEY = "__indexstats__";

/*
  This function is called by RocksDB to compute properties to store in sst file
*/
rocksdb::Status
Rdb_tbl_prop_coll::Finish(
  rocksdb::UserCollectedProperties* properties
) {
  properties->insert({INDEXSTATS_KEY,
                     Rdb_index_stats::materialize(m_stats, m_card_adj_extra)});
  return rocksdb::Status::OK();
}

bool Rdb_tbl_prop_coll::NeedCompact() const {
  return
    m_params.m_deletes &&
    (m_params.m_window > 0) &&
    (m_file_size > m_params.m_file_size) &&
    (m_max_deleted_rows > m_params.m_deletes);
}

bool Rdb_tbl_prop_coll::ShouldCollectStats() {
  // Zero means that we'll use all the keys to update statistics.
  if (!m_table_stats_sampling_pct ||
      MYROCKS_SAMPLE_PCT_MAX == m_table_stats_sampling_pct) {
    return true;
  }

  int val = rand_r(&m_seed) % MYROCKS_SAMPLE_PCT_MAX + 1;

  DBUG_ASSERT(val >= MYROCKS_SAMPLE_PCT_MIN);
  DBUG_ASSERT(val <= MYROCKS_SAMPLE_PCT_MAX);

  return val <= m_table_stats_sampling_pct;
}

/*
  Returns the same as above, but in human-readable way for logging
*/
rocksdb::UserCollectedProperties
Rdb_tbl_prop_coll::GetReadableProperties() const {
  std::string s;
#ifdef DBUG_OFF
  s.append("[...");
  s.append(std::to_string(m_stats.size()));
  s.append("  records...]");
#else
  bool first = true;
  for (auto it : m_stats) {
    if (first) {
      first = false;
    } else {
      s.append(",");
    }
    s.append(GetReadableStats(it));
  }
 #endif
  return rocksdb::UserCollectedProperties{{INDEXSTATS_KEY, s}};
}

std::string
Rdb_tbl_prop_coll::GetReadableStats(
  const Rdb_index_stats& it
) {
  std::string s;
  s.append("(");
  s.append(std::to_string(it.m_gl_index_id.cf_id));
  s.append(", ");
  s.append(std::to_string(it.m_gl_index_id.index_id));
  s.append("):{name:");
  s.append(it.m_name);
  s.append(", size:");
  s.append(std::to_string(it.m_data_size));
  s.append(", m_rows:");
  s.append(std::to_string(it.m_rows));
  s.append(", m_actual_disk_size:");
  s.append(std::to_string(it.m_actual_disk_size));
  s.append(", deletes:");
  s.append(std::to_string(it.m_entry_deletes));
  s.append(", single_deletes:");
  s.append(std::to_string(it.m_entry_single_deletes));
  s.append(", merges:");
  s.append(std::to_string(it.m_entry_merges));
  s.append(", others:");
  s.append(std::to_string(it.m_entry_others));
  s.append(", distincts per prefix: [");
  for (auto num : it.m_distinct_keys_per_prefix) {
    s.append(std::to_string(num));
    s.append(" ");
  }
  s.append("]}");
  return s;
}

/*
  Given the properties of an SST file, reads the stats from it and returns it.
*/
std::vector<Rdb_index_stats>
Rdb_tbl_prop_coll::read_stats_from_tbl_props(
  const std::shared_ptr<const rocksdb::TableProperties>& table_props)
{
  std::vector<Rdb_index_stats> ret;
  const auto& user_properties = table_props->user_collected_properties;
  auto it2 = user_properties.find(std::string(INDEXSTATS_KEY));
  if (it2 != user_properties.end()) {
    Rdb_index_stats::unmaterialize(it2->second, ret);
  }

  return ret;
}


/*
  Serializes an array of Rdb_index_stats into a string.
*/
std::string Rdb_index_stats::materialize(
  std::vector<Rdb_index_stats> stats,
  const float card_adj_extra
) {
  String ret;
  write_short(&ret, INDEX_STATS_VERSION_ENTRY_TYPES);
  for (auto i : stats) {
    write_int(&ret, i.m_gl_index_id.cf_id);
    write_int(&ret, i.m_gl_index_id.index_id);
    DBUG_ASSERT(sizeof i.m_data_size <= 8);
    write_int64(&ret, i.m_data_size);
    write_int64(&ret, i.m_rows);
    write_int64(&ret, i.m_actual_disk_size);
    write_int64(&ret, i.m_distinct_keys_per_prefix.size());
    write_int64(&ret, i.m_entry_deletes);
    write_int64(&ret, i.m_entry_single_deletes);
    write_int64(&ret, i.m_entry_merges);
    write_int64(&ret, i.m_entry_others);
    for (auto num_keys : i.m_distinct_keys_per_prefix) {
      float upd_num_keys = num_keys * card_adj_extra;
      write_int64(&ret, static_cast<int64_t>(upd_num_keys));
    }
  }

  return std::string((char*) ret.ptr(), ret.length());
}

/*
  Reads an array of Rdb_index_stats from a string.
*/
int Rdb_index_stats::unmaterialize(
  const std::string& s, std::vector<Rdb_index_stats>& ret
) {
  const char* p = s.data();
  const char* p2 = s.data() + s.size();

  if (p+2 > p2)
  {
    return 1;
  }

  int version= read_short(&p);
  Rdb_index_stats stats;
  // Make sure version is within supported range.
  if (version < INDEX_STATS_VERSION_INITIAL ||
      version > INDEX_STATS_VERSION_ENTRY_TYPES)
  {
    // NO_LINT_DEBUG
    sql_print_error("Index stats version %d was outside of supported range. "
                    "This should not happen so aborting the system.", version);
    abort_with_stack_traces();
  }

  size_t needed = sizeof(stats.m_gl_index_id.cf_id)+
                  sizeof(stats.m_gl_index_id.index_id)+
                  sizeof(stats.m_data_size)+
                  sizeof(stats.m_rows)+
                  sizeof(stats.m_actual_disk_size)+
                  sizeof(uint64);
  if (version >= INDEX_STATS_VERSION_ENTRY_TYPES)
  {
    needed += sizeof(stats.m_entry_deletes)+
              sizeof(stats.m_entry_single_deletes)+
              sizeof(stats.m_entry_merges)+
              sizeof(stats.m_entry_others);
  }

  while (p < p2)
  {
    if (p+needed > p2)
    {
      return 1;
    }
    stats.m_gl_index_id.cf_id = read_int(&p);
    stats.m_gl_index_id.index_id = read_int(&p);
    stats.m_data_size = read_int64(&p);
    stats.m_rows = read_int64(&p);
    stats.m_actual_disk_size = read_int64(&p);
    stats.m_distinct_keys_per_prefix.resize(read_int64(&p));
    if (version >= INDEX_STATS_VERSION_ENTRY_TYPES)
    {
      stats.m_entry_deletes = read_int64(&p);
      stats.m_entry_single_deletes = read_int64(&p);
      stats.m_entry_merges = read_int64(&p);
      stats.m_entry_others = read_int64(&p);
    }
    if (p+stats.m_distinct_keys_per_prefix.size()
        *sizeof(stats.m_distinct_keys_per_prefix[0]) > p2)
    {
      return 1;
    }
    for (std::size_t i= 0; i < stats.m_distinct_keys_per_prefix.size(); i++)
    {
      stats.m_distinct_keys_per_prefix[i] = read_int64(&p);
    }
    ret.push_back(stats);
  }
  return 0;
}

/*
  Merges one Rdb_index_stats into another. Can be used to come up with the stats
  for the index based on stats for each sst
*/
void Rdb_index_stats::merge(
  const Rdb_index_stats& s, bool increment, int64_t estimated_data_len)
{
  std::size_t i;

  m_gl_index_id = s.m_gl_index_id;
  if (m_distinct_keys_per_prefix.size() < s.m_distinct_keys_per_prefix.size())
  {
    m_distinct_keys_per_prefix.resize(s.m_distinct_keys_per_prefix.size());
  }
  if (increment)
  {
    m_rows += s.m_rows;
    m_data_size += s.m_data_size;

    /*
      The Data_length and Avg_row_length are trailing statistics, meaning
      they don't get updated for the current SST until the next SST is
      written.  So, if rocksdb reports the data_length as 0,
      we make a reasoned estimate for the data_file_length for the
      index in the current SST.
    */
    m_actual_disk_size += s.m_actual_disk_size ? s.m_actual_disk_size :
                                                 estimated_data_len * s.m_rows;
    m_entry_deletes += s.m_entry_deletes;
    m_entry_single_deletes += s.m_entry_single_deletes;
    m_entry_merges += s.m_entry_merges;
    m_entry_others += s.m_entry_others;
    for (i = 0; i < s.m_distinct_keys_per_prefix.size(); i++)
    {
      m_distinct_keys_per_prefix[i] += s.m_distinct_keys_per_prefix[i];
    }
  }
  else
  {
    m_rows -= s.m_rows;
    m_data_size -= s.m_data_size;
    m_actual_disk_size -= s.m_actual_disk_size ? s.m_actual_disk_size :
                                                 estimated_data_len * s.m_rows;
    m_entry_deletes -= s.m_entry_deletes;
    m_entry_single_deletes -= s.m_entry_single_deletes;
    m_entry_merges -= s.m_entry_merges;
    m_entry_others -= s.m_entry_others;
    for (i = 0; i < s.m_distinct_keys_per_prefix.size(); i++)
    {
      m_distinct_keys_per_prefix[i] -= s.m_distinct_keys_per_prefix[i];
    }
  }
}

}  // namespace myrocks
