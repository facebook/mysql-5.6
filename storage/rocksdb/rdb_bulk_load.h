/*
   Copyright (c) 2024, Facebook, Inc.

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

#pragma once

#include <string_view>
#include "./rdb_sst_partitioner_factory.h"
#include "./rdb_utils.h"
#include "ha_rocksdb_proto.h"
#include "sql/sql_class.h"

namespace myrocks {

void rdb_bulk_load_init(
    const char *rocksdb_default_cf_options,
    const char *rocksdb_override_cf_options,
    const rocksdb::BlockBasedTableOptions &main_rdb_tbl_options);
void rdb_bulk_load_deinit();
constexpr uint RDB_BULK_LOAD_HISTORY_DEFAULT_SIZE = 10;
void rdb_set_bulk_load_history_size(uint size);

enum class Rdb_bulk_load_status { NONE, ACTIVE, COMPLETED, FAILED, ABORTED };
enum class Rdb_bulk_load_type {
  NONE,
  // ddl operations, not necessarily using
  // bulk load, store it to garantee mutual exclusion
  // of ddl and bulk load
  DDL,
  // bulk load from inserts, load data, etc.
  // using the sst file writer to write the data.
  SST_FILE_WRITER,
  // bulk load using temporary rdb and cf
  TEMPORARY_RDB,
};

std::string_view rdb_bulk_load_status_to_string(Rdb_bulk_load_status status);
std::string_view rdb_bulk_load_type_to_string(Rdb_bulk_load_type type);

class Rdb_bulk_load_worker {
 public:
  Rdb_bulk_load_worker(my_thread_id thread_id)
      : m_thread_id{thread_id}, m_status{Rdb_bulk_load_status::ACTIVE} {
    m_created_micro = my_micro_time();
  }

  Rdb_bulk_load_worker() = default;
  Rdb_bulk_load_worker(const Rdb_bulk_load_worker &) = default;
  Rdb_bulk_load_worker &operator=(const Rdb_bulk_load_worker &) = default;

  void complete(Rdb_bulk_load_status status, int rtn_code) {
    m_status = status;
    m_rtn_code = rtn_code;
    m_completed_micro = my_micro_time();
  }

  void set_curr_table(const std::string &table_name) {
    m_curr_table = table_name;
  }

  void add_table(const std::string &table_name) { m_tables.insert(table_name); }

  bool is_table_active(const std::string &table_name) const {
    return m_status == Rdb_bulk_load_status::ACTIVE &&
           m_curr_table == table_name;
  }

  const Rdb_bulk_load_status &status() const { return m_status; }

 private:
  my_thread_id m_thread_id = 0;
  int m_rtn_code = 0;
  std::uint64_t m_created_micro = 0;
  std::uint64_t m_completed_micro = 0;
  Rdb_bulk_load_status m_status = Rdb_bulk_load_status::NONE;
  std::string m_curr_table;
  std::unordered_set<std::string> m_tables;
};

class Rdb_bulk_load_session {
 public:
  Rdb_bulk_load_session(const std::string &id, Rdb_bulk_load_type type)
      : m_id{id}, m_type{type}, m_status{Rdb_bulk_load_status::ACTIVE} {
    m_created_micro = my_micro_time();
  }

  // simple copy constructor
  Rdb_bulk_load_session(const Rdb_bulk_load_session &) = default;

  // For Rdb_bulk_load_type::TEMPORARY_RDB, create relative CF in the bulk load
  // rdb and store table in m_tables For other types, set table as current
  // working table and store table in m_tables
  uint add_table(const std::string &table_name, Rdb_bulk_load_type type,
                 Rdb_cf_manager &cf_manager,
                 std::unordered_set<std::string> non_default_cf = {});

  // Record end status change for the whole bulk load session. For
  // Rdb_bulk_load_type::TEMPORARY_RDB, also drop temp cfs.
  uint complete(Rdb_bulk_load_status status, int rtn_code, uint num_sst_files,
                Rdb_cf_manager &cf_manager);

  void add_thread_to_session(my_thread_id thread_id) {
    // the same thread can finish and restart, thus replace
    m_workers[thread_id] = Rdb_bulk_load_worker(thread_id);
  }

  void finish_thread(my_thread_id thread_id, Rdb_bulk_load_status status,
                     int rtn_code) {
    auto it = m_workers.find(thread_id);
    if (it != m_workers.end() &&
        it->second.status() == Rdb_bulk_load_status::ACTIVE) {
      // this call can be called twice for the same thread
      // for example, when the thread completes bulk load(complete) and
      // exit(abort) only update status for the first time(when it is active)
      it->second.complete(status, rtn_code);
    }
  }

  void set_curr_table(const std::string &table_name) {
    m_workers.begin()->second.set_curr_table(table_name);
  }

  std::string convert_cf_to_temp_cf_name(const std::string &cf_name) {
    return id() + "_" + cf_name;
  }

  std::string convert_temp_cf_to_cf_name(const std::string &cf_name) {
    return cf_name.substr(id().length() + 1);
  }

  // Indexes in sst partitioner(in main rdb) are registered during the
  // lifetime of bulk load, to prevent compaction in main rdb producing files
  // that overlap with bulk load indexes. Now that bulk load finishes, we may
  // remove those constraints from main rdb.
  void remove_index_from_sst_partitioner() {
    for (const auto &pair : m_cf_indexes) {
      rocksdb::ColumnFamilyHandle *cf =
          rdb_get_cf_manager().get_cf(convert_temp_cf_to_cf_name(pair.first));
      auto *const sst_partitioner_factory =
          rdb_get_rocksdb_db().GetOptions(cf).sst_partitioner_factory.get();
      auto *const rdb_sst_partitioner_factory =
          dynamic_cast<Rdb_sst_partitioner_factory *>(sst_partitioner_factory);
      for (auto &index : pair.second) {
        rdb_sst_partitioner_factory->remove_index(index);
      }
    }
  }

  const std::string &id() const { return m_id; }

  void update_cf_indexes(std::unordered_map<rocksdb::ColumnFamilyHandle *,
                                            std::set<Index_id>> &cf_indexes) {
    for (auto &pair : cf_indexes) {
      m_cf_indexes[convert_cf_to_temp_cf_name(pair.first->GetName())].insert(
          pair.second.begin(), pair.second.end());
    }
  }

  Rdb_bulk_load_type type() const { return m_type; }

  const std::map<my_thread_id, Rdb_bulk_load_worker> &workers() const {
    return m_workers;
  }

  int rtn_code() const { return m_rtn_code; }

  Rdb_bulk_load_status status() const { return m_status; }

  uint num_sst_files() const { return m_num_sst_files; }

  std::uint64_t created_micro() const { return m_created_micro; }

  std::uint64_t completed_micro() const { return m_completed_micro; }

  const std::set<std::string> &tables() const { return m_tables; }

  std::vector<std::string> cf_names() const {
    std::vector<std::string> ret;
    for (const auto &pair : m_cf_indexes) {
      ret.push_back(pair.first);
    }
    return ret;
  }

  bool is_table_active(const std::string &table_name) const {
    if (m_type == Rdb_bulk_load_type::DDL ||
        m_type == Rdb_bulk_load_type::TEMPORARY_RDB) {
      // for ddl, the 'active' table might be the temp table
      // created for copying, so we need to check if the table
      // appears in the full list of tables.
      return m_status == Rdb_bulk_load_status::ACTIVE &&
             m_tables.find(table_name) != m_tables.end();
    }
    return std::any_of(m_workers.cbegin(), m_workers.cend(), [&](auto &entry) {
      return entry.second.is_table_active(table_name);
    });
  }

 private:
  std::string m_id;
  std::map<my_thread_id, Rdb_bulk_load_worker> m_workers;
  Rdb_bulk_load_type m_type = Rdb_bulk_load_type::NONE;
  int m_rtn_code = 0;
  Rdb_bulk_load_status m_status = Rdb_bulk_load_status::NONE;
  std::map<std::string, std::set<Index_id>> m_cf_indexes;
  std::set<std::string> m_tables;
  uint m_num_sst_files = 0;
  std::uint64_t m_created_micro = 0;
  std::uint64_t m_completed_micro = 0;
};

std::vector<Rdb_bulk_load_session> rdb_dump_bulk_load_sessions();

/**
  holds state of bulk load for the current thread.
  not thread safe, should always be accessed from the same thread.
*/
class Rdb_bulk_load_context {
 public:
  Rdb_bulk_load_context(THD *thd);
  ~Rdb_bulk_load_context();

  /**
    notify a ddl is going to take place.
    return error when there is a conflict bulk load session.
  */
  [[nodiscard]] uint notify_ddl(std::string_view db_name,
                                std::string_view table_base_name);

  /**
    free up resources for the current bulk load
  */
  void clear_current_bulk_load();

  void clear_bulk_load_session_ctx();

  void complete_bulk_load_session(int rtn);

  size_t num_bulk_load() const { return m_curr_bulk_load.size(); }

  const Rdb_bulk_load_type &type() const { return m_type; }

  size_t num_key_merge() const { return m_key_merge.size(); }

  Rdb_sst_info *find_sst_info(GL_INDEX_ID index_id) const {
    const auto it = m_curr_bulk_load.find(index_id);
    if (it == m_curr_bulk_load.end()) {
      return nullptr;
    }
    return it->second.get();
  }

  [[nodiscard]] Rdb_sst_info *add_sst_info(rocksdb::DB &rdb,
                                           const std::string &tablename,
                                           const Rdb_key_def &kd,
                                           rocksdb::DBOptions &db_option,
                                           bool trace_sst_api,
                                           bool compression_parallel_threads);

  Rdb_index_merge *find_key_merge(GL_INDEX_ID index_id) {
    const auto it = m_key_merge.find(index_id);
    if (it == m_key_merge.end()) {
      return nullptr;
    }
    return &it->second;
  }

  [[nodiscard]] uint add_key_merge(const std::string &table_name,
                                   const Rdb_key_def &kd,
                                   const char *tmpfile_path,
                                   ulonglong merge_buf_size,
                                   ulonglong merge_combine_read_size,
                                   ulonglong merge_tmp_file_removal_delay,
                                   Rdb_index_merge **key_merge);

  bool table_changed(std::string_view db_name,
                     std::string_view table_base_name);

  [[nodiscard]] uint add_table(
      std::string_view db_name, std::string_view table_base_name,
      Rdb_bulk_load_type type,
      std::unordered_set<std::string> non_default_cf = {});

  [[nodiscard]] uint set_curr_table(std::string_view db_name,
                                    std::string_view table_base_name,
                                    Rdb_bulk_load_type type);

  const char *table_name() const { return m_curr_table_name.c_str(); }

  std::map<GL_INDEX_ID, std::unique_ptr<Rdb_sst_info>> &curr_bulk_load() {
    return m_curr_bulk_load;
  }

  std::map<GL_INDEX_ID, Rdb_index_merge> &key_merge() { return m_key_merge; }

  Rdb_bulk_load_index_registry &bulk_load_index_registry() {
    return m_bulk_load_index_registry;
  }

  bool active() const { return m_active; }

  void increment_num_commited_sst_files(uint count);

  void set_bulk_load_session_id(const std::string &bulk_load_session_id) {
    m_bulk_load_session_id = bulk_load_session_id;
  }

  const std::string &bulk_load_session_id() const {
    return m_bulk_load_session_id;
  }

  uint bulk_load_rollback();

  uint update_cf_indexes(std::unordered_map<rocksdb::ColumnFamilyHandle *,
                                            std::set<Index_id>> &cf_indexes);

 private:
  THD *m_thd;
  bool m_active = false;
  std::string m_bulk_load_session_id;
  Rdb_bulk_load_type m_type = Rdb_bulk_load_type::NONE;
  // The Rdb_sst_info structures we are currently loading.  In a partitioned
  // table this can have more than one entry
  std::map<GL_INDEX_ID, std::unique_ptr<Rdb_sst_info>> m_curr_bulk_load;
  std::string m_curr_table_name;
  std::string m_curr_db_name;
  uint m_num_commited_sst_files = 0;

  /* External merge sorts for bulk load: key ID -> merge sort instance */
  std::map<GL_INDEX_ID, Rdb_index_merge> m_key_merge;

  // register indexes used in bulk load to Rdb_sst_partitioner_factory, see
  // comments in Rdb_sst_partitioner_factory for details
  Rdb_bulk_load_index_registry m_bulk_load_index_registry;
};

}  // namespace myrocks
