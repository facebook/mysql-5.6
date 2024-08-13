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
#include "sql/sql_class.h"

namespace myrocks {

void rdb_bulk_load_init();
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
  SST_FILE_WRITER
};

std::string_view rdb_bulk_load_status_to_string(Rdb_bulk_load_status status);
std::string_view rdb_bulk_load_type_to_string(Rdb_bulk_load_type type);

class Rdb_bulk_load_worker {
 public:
  Rdb_bulk_load_worker(my_thread_id thread_id)
      : m_thread_id{thread_id}, m_status{Rdb_bulk_load_status::ACTIVE} {
    m_created_micro = my_micro_time();
  }

  // simple copy constructor
  Rdb_bulk_load_worker(const Rdb_bulk_load_worker &) = default;

  void complete(Rdb_bulk_load_status status, int rtn_code) {
    m_status = status;
    m_rtn_code = rtn_code;
    m_completed_micro = my_micro_time();
  }

  void set_curr_table(const std::string &table_name) {
    m_curr_table = table_name;
  }

  bool is_table_active(const std::string &table_name) const {
    return m_status == Rdb_bulk_load_status::ACTIVE &&
           m_curr_table == table_name;
  }

 private:
  my_thread_id m_thread_id = 0;
  int m_rtn_code = 0;
  std::uint64_t m_created_micro = 0;
  std::uint64_t m_completed_micro = 0;
  Rdb_bulk_load_status m_status = Rdb_bulk_load_status::NONE;
  std::string m_curr_table;
};

class Rdb_bulk_load_session {
 public:
  Rdb_bulk_load_session(const std::string &id, my_thread_id thread_id,
                        Rdb_bulk_load_type type)
      : m_id{id}, m_type{type}, m_status{Rdb_bulk_load_status::ACTIVE} {
    m_created_micro = my_micro_time();
    m_workers.emplace(thread_id, Rdb_bulk_load_worker(thread_id));
  }

  // simple copy constructor
  Rdb_bulk_load_session(const Rdb_bulk_load_session &) = default;

  void add_table(const std::string &table_name) {
    m_tables.insert(table_name);
    m_workers.begin()->second.set_curr_table(table_name);
  }

  void complete(Rdb_bulk_load_status status, int rtn_code, uint num_sst_files) {
    m_workers.begin()->second.complete(status, rtn_code);
    m_rtn_code = rtn_code;
    m_status = status;
    m_num_sst_files = num_sst_files;
    m_completed_micro = my_micro_time();
  }

  const std::string &id() const { return m_id; }

  Rdb_bulk_load_type type() const { return m_type; }

  int rtn_code() const { return m_rtn_code; }

  Rdb_bulk_load_status status() const { return m_status; }

  uint num_sst_files() const { return m_num_sst_files; }

  std::uint64_t created_micro() const { return m_created_micro; }

  std::uint64_t completed_micro() const { return m_completed_micro; }

  const std::set<std::string> &tables() const { return m_tables; }

  bool is_table_active(const std::string &table_name) const {
    if (m_type == Rdb_bulk_load_type::DDL) {
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
  // there is one worker per thread, in the future we may have multiple.
  std::map<my_thread_id, Rdb_bulk_load_worker> m_workers;
  Rdb_bulk_load_type m_type = Rdb_bulk_load_type::NONE;
  int m_rtn_code = 0;
  Rdb_bulk_load_status m_status = Rdb_bulk_load_status::NONE;
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

  void complete_bulk_load_session(int rtn);

  size_t num_bulk_load() const { return m_curr_bulk_load.size(); }

  size_t num_key_merge() const { return m_key_merge.size(); }

  Rdb_sst_info *find_sst_info(GL_INDEX_ID index_id) const {
    const auto it = m_curr_bulk_load.find(index_id);
    if (it == m_curr_bulk_load.end()) {
      return nullptr;
    }
    return it->second.get();
  }

  Rdb_sst_info *add_sst_info(rocksdb::DB *rdb, const std::string &tablename,
                             const Rdb_key_def &kd,
                             rocksdb::DBOptions &db_option, bool trace_sst_api,
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

  [[nodiscard]] uint set_curr_table(std::string_view db_name,
                                    std::string_view table_base_name);

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

 private:
  THD *m_thd;
  bool m_active = false;
  std::string m_bulk_load_session_id;
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
