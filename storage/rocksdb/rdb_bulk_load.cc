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

#include "./rdb_bulk_load.h"
#include "include/scope_guard.h"
#include "sql/mysqld.h"

namespace myrocks {

static rocksdb::TransactionDB *bulk_load_rdb = nullptr;

std::string_view rdb_bulk_load_status_to_string(Rdb_bulk_load_status status) {
  switch (status) {
    case Rdb_bulk_load_status::ACTIVE:
      return "ACTIVE";
    case Rdb_bulk_load_status::COMPLETED:
      return "COMPLETED";
    case Rdb_bulk_load_status::FAILED:
      return "FAILED";
    case Rdb_bulk_load_status::ABORTED:
      return "ABORTED";
    default:
      assert(false);
      return "UNKNOWN";
  }
}

std::string_view rdb_bulk_load_type_to_string(Rdb_bulk_load_type type) {
  switch (type) {
    case Rdb_bulk_load_type::DDL:
      return "DDL";
    case Rdb_bulk_load_type::SST_FILE_WRITER:
      return "SST_FILE_WRITER";
    case Rdb_bulk_load_type::TEMPORARY_RDB:
      return "TEMPORARY_RDB";
    default:
      assert(false);
      return "UNKNOWN";
  }
}

// TODO: change to data dir to support resumption
const std::string bulk_load_rdb_dir() {
  return std::string(opt_mysql_tmpdir) + "/bulk_load";
}

class Rdb_bulk_load_manager {
 public:
  Rdb_bulk_load_manager(
      const char *rocksdb_default_cf_options,
      const char *rocksdb_override_cf_options,
      const rocksdb::BlockBasedTableOptions &main_rdb_tbl_options)
      : m_rdb_default_cf_options{rocksdb_default_cf_options},
        m_rdb_override_cf_options{rocksdb_override_cf_options},
        m_main_rdb_tbl_options{main_rdb_tbl_options} {
    mysql_mutex_init(0, &m_mutex, MY_MUTEX_INIT_FAST);
  }
  ~Rdb_bulk_load_manager() {
    m_cf_manager.cleanup();
    delete bulk_load_rdb;
    bulk_load_rdb = nullptr;
    mysql_mutex_destroy(&m_mutex);
  }

  bool check_duplicate_bulk_load(const std::string &id, std::string table_name,
                                 std::string &duplicate_id,
                                 Rdb_bulk_load_type type) {
    for (auto &entry : m_sessions) {
      if (entry.second.type() == type) {
        // Rdb_bulk_load_type::TEMPORARY_RDB allows multiple threads to operate
        // on the same table with the same session id when session is active
        if (type == Rdb_bulk_load_type::TEMPORARY_RDB) {
          if ((entry.second.is_table_active(table_name) && entry.first != id) ||
              (entry.first == id &&
               entry.second.status() != Rdb_bulk_load_status::ACTIVE)) {
            duplicate_id = entry.first;
            return true;
          }
        }
        // Rdb_bulk_load_type::SST_FILE_WRITER allows multiple threads to
        // operate on the same table with different session ids regardless of
        // session status, so no constraint.
        // Rdb_bulk_load_type::DDL allows the same session id to operate on the
        // same table
        if (type == Rdb_bulk_load_type::DDL && id != entry.first &&
            entry.second.is_table_active(table_name)) {
          duplicate_id = entry.first;
          return true;
        }
      } else if (entry.second.type() != type &&
                 entry.second.is_table_active(table_name)) {
        // 3 types cannot work on the same table at the same time
        duplicate_id = entry.first;
        return true;
      }
    }
    return false;
  }

  int get_rdb() {
    mysql_mutex_assert_owner(&m_mutex);
    if (bulk_load_rdb) {
      assert(m_cf_manager.is_initialized());
      return HA_EXIT_SUCCESS;
    }
    std::unique_ptr<Rdb_cf_options> cf_options_map(new Rdb_cf_options());
    std::vector<rocksdb::ColumnFamilyHandle *> cf_handles;
    // follow the most of main rdb table options except for some special
    // configurations
    rocksdb::BlockBasedTableOptions bulk_load_rdb_tbl_options =
        m_main_rdb_tbl_options;
    // for the temporary CFs, aside from comparator, settings can be different
    // from original CFs pass in the same settings here just in case
    // TODO: set up properties_collector_factory for collecting table stats
    if (!cf_options_map->init(bulk_load_rdb_tbl_options, nullptr,
                              m_rdb_default_cf_options,
                              m_rdb_override_cf_options)) {
      LogPluginErrMsg(
          ERROR_LEVEL, ER_LOG_PRINTF_MSG,
          "RocksDB: Failed to initialize bulk load CF options map.");
      return HA_EXIT_FAILURE;
    }
    cf_options_map->set_bulk_load_cf_options();
    rocksdb::ColumnFamilyOptions opts;
    if (!cf_options_map->get_cf_options(DEFAULT_CF_NAME, &opts)) {
      LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                      "RocksDB: Failed to get default family options.");
      return HA_EXIT_FAILURE;
    }
    std::vector<rocksdb::ColumnFamilyDescriptor> cf_descr;
    // DEFAULT_CF_NAME will not be used, but we need to pass it in
    cf_descr.push_back(rocksdb::ColumnFamilyDescriptor(DEFAULT_CF_NAME, opts));
    auto db_options = rocksdb::Options();
    db_options.create_if_missing = true;
    db_options.info_log_level = rocksdb::InfoLogLevel::INFO_LEVEL;
    db_options.allow_concurrent_memtable_write = false;
    db_options.PrepareForBulkLoad();
    rocksdb::Options main_opts(db_options, cf_options_map->get_defaults());
    rocksdb::TransactionDBOptions tx_db_options;
    rocksdb::Status status = rocksdb::TransactionDB::Open(
        main_opts, tx_db_options, bulk_load_rdb_dir(), cf_descr, &cf_handles,
        &bulk_load_rdb);
    if (!status.ok()) {
      std::string err =
          "Fail to open db for bulk loading: " + bulk_load_rdb_dir();
      rdb_log_status_error(status, err.c_str());
      return HA_EXIT_FAILURE;
    }
    assert(bulk_load_rdb);
    // set up cf manager
    if (m_cf_manager.init(bulk_load_rdb, std::move(cf_options_map),
                          &cf_handles)) {
      LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                      "Fail to init cf manager for temporary rdb");
      return HA_EXIT_FAILURE;
    };
    return HA_EXIT_SUCCESS;
  }

  uint start_bulk_load_session(
      my_thread_id thread_id, std::string &id, Rdb_bulk_load_type type,
      std::string_view db_name, std::string_view table_base_name,
      std::unordered_set<std::string> non_default_cf = {}) {
    RDB_MUTEX_LOCK_CHECK(m_mutex);
    auto lock_guard =
        create_scope_guard([this]() { RDB_MUTEX_UNLOCK_CHECK(m_mutex); });
    if (id.empty()) {
      assert(type != Rdb_bulk_load_type::TEMPORARY_RDB);
      id = "sys-" + std::to_string(m_id_seed++);
    }
    std::string table_name =
        std::string{db_name} + "." + std::string{table_base_name};
    std::string duplicate_id;
    if (check_duplicate_bulk_load(id, table_name, duplicate_id, type)) {
      std::string err =
          "Conflict bulk load " + duplicate_id + " on table " + table_name;
      my_error(ER_DA_BULK_LOAD, MYF(0), err.c_str());
      return HA_EXIT_FAILURE;
    }
    Rdb_bulk_load_session *existing_session = find_active_session(id);
    if (!existing_session) {
      if (type == Rdb_bulk_load_type::TEMPORARY_RDB) {
        if (get_rdb()) {
          my_error(ER_DA_BULK_LOAD, MYF(0),
                   "Fail to create/fetch temporary rdb");
          return HA_EXIT_FAILURE;
        }
      }
      auto rtn = m_sessions.emplace(id, Rdb_bulk_load_session(id, type));
      assert(rtn.second);
      existing_session = &rtn.first->second;
    }
    existing_session->add_thread_to_session(thread_id);
    return existing_session->add_table(table_name, type, m_cf_manager,
                                       non_default_cf);
  }

  uint add_table_to_session(const std::string &id, std::string_view db_name,
                            std::string_view table_base_name,
                            std::unordered_set<std::string> &non_default_cf) {
    RDB_MUTEX_LOCK_CHECK(m_mutex);
    auto lock_guard =
        create_scope_guard([this]() { RDB_MUTEX_UNLOCK_CHECK(m_mutex); });
    Rdb_bulk_load_session *existing_session = find_active_session(id);
    assert(existing_session);
    if (!existing_session) {
      return HA_EXIT_FAILURE;
    }
    const auto type = existing_session->type();
    std::string table_name =
        std::string{db_name} + "." + std::string{table_base_name};
    std::string duplicate_id;
    if (check_duplicate_bulk_load(id, table_name, duplicate_id, type)) {
      std::string err = "Conflict bulk load " + duplicate_id +
                        " is running on table " + table_name;
      my_error(ER_DA_BULK_LOAD, MYF(0), err.c_str());
      return HA_EXIT_FAILURE;
    }
    return existing_session->add_table(table_name, type, m_cf_manager,
                                       non_default_cf);
  }

  uint update_cf_indexes(const std::string &id,
                         std::unordered_map<rocksdb::ColumnFamilyHandle *,
                                            std::set<Index_id>> &cf_indexes) {
    RDB_MUTEX_LOCK_CHECK(m_mutex);
    auto lock_guard =
        create_scope_guard([this]() { RDB_MUTEX_UNLOCK_CHECK(m_mutex); });
    Rdb_bulk_load_session *existing_session = find_active_session(id);
    assert(existing_session);
    if (!existing_session) {
      return HA_EXIT_FAILURE;
    }
    existing_session->update_cf_indexes(cf_indexes);
    return HA_EXIT_SUCCESS;
  }

  uint complete_session(const std::string &id, int rtn_code,
                        uint num_sst_files) {
    return finish_session(id,
                          rtn_code ? Rdb_bulk_load_status::FAILED
                                   : Rdb_bulk_load_status::COMPLETED,
                          rtn_code, num_sst_files);
  }

  uint abort_session(const std::string &id, uint num_sst_files = 0) {
    return finish_session(id, Rdb_bulk_load_status::ABORTED, HA_EXIT_FAILURE,
                          num_sst_files);
  }

  void finish_thread(const std::string &id, my_thread_id thread_id,
                     Rdb_bulk_load_status status) {
    RDB_MUTEX_LOCK_CHECK(m_mutex);
    auto lock_guard =
        create_scope_guard([this]() { RDB_MUTEX_UNLOCK_CHECK(m_mutex); });
    Rdb_bulk_load_session *existing_session = find_active_session(id);
    assert(existing_session);
    existing_session->finish_thread(thread_id, status,
                                    status == Rdb_bulk_load_status::ABORTED
                                        ? HA_EXIT_FAILURE
                                        : HA_EXIT_SUCCESS);
  }

  std::vector<Rdb_bulk_load_session> dump_sessions() {
    RDB_MUTEX_LOCK_CHECK(m_mutex);
    auto lock_guard =
        create_scope_guard([this]() { RDB_MUTEX_UNLOCK_CHECK(m_mutex); });
    std::vector<Rdb_bulk_load_session> result;
    result.reserve(m_sessions.size());
    for (auto &it : m_sessions) {
      result.push_back(it.second);
    }
    return result;
  }

  void set_history_size(uint size) {
    RDB_MUTEX_LOCK_CHECK(m_mutex);
    auto lock_guard =
        create_scope_guard([this]() { RDB_MUTEX_UNLOCK_CHECK(m_mutex); });
    m_history_size = size;
    enforce_history_size();
  }

 private:
  mysql_mutex_t m_mutex;
  const char *m_rdb_default_cf_options;
  const char *m_rdb_override_cf_options;
  const rocksdb::BlockBasedTableOptions m_main_rdb_tbl_options;
  std::map<std::string, Rdb_bulk_load_session> m_sessions;
  Rdb_cf_manager m_cf_manager;
  std::deque<std::string> m_completed_sessions;
  std::atomic<int> m_id_seed{0};
  uint m_history_size = RDB_BULK_LOAD_HISTORY_DEFAULT_SIZE;

  Rdb_bulk_load_session *find_active_session(const std::string &session_id) {
    mysql_mutex_assert_owner(&m_mutex);
    auto it = m_sessions.find(session_id);
    if (it == m_sessions.end()) {
      return nullptr;
    }
    auto existing_session = &it->second;
    if (existing_session->status() == Rdb_bulk_load_status::ACTIVE) {
      return existing_session;
    }
    return nullptr;
  }

  uint finish_session(const std::string &id, Rdb_bulk_load_status status,
                      int rtn_code, uint num_sst_files) {
    RDB_MUTEX_LOCK_CHECK(m_mutex);
    auto lock_guard =
        create_scope_guard([this]() { RDB_MUTEX_UNLOCK_CHECK(m_mutex); });
    Rdb_bulk_load_session *existing_session = find_active_session(id);
    if (!existing_session) {
      my_error(ER_DA_BULK_LOAD, MYF(0),
               "Bulk load session is not active, nothing to finish");
      return HA_EXIT_FAILURE;
    }
    if (existing_session->complete(status, rtn_code, num_sst_files,
                                   m_cf_manager)) {
      return HA_EXIT_FAILURE;
    };
    m_completed_sessions.push_back(id);
    enforce_history_size();
    return HA_EXIT_SUCCESS;
  }

  // remove the oldest completed sessions
  void enforce_history_size() {
    mysql_mutex_assert_owner(&m_mutex);
    if (m_completed_sessions.size() <= m_history_size) {
      return;
    }

    auto to_remove = m_completed_sessions.size() - m_history_size;
    // remove the oldest completed sessions
    for (uint i = 0; i < to_remove; i++) {
      auto id = m_completed_sessions.front();
      m_completed_sessions.pop_front();
      [[maybe_unused]] auto erased = m_sessions.erase(id);
      assert(erased);
    }

    assert(m_completed_sessions.size() == m_history_size);
  }
};

static std::unique_ptr<Rdb_bulk_load_manager> bulk_load_manger;

void rdb_bulk_load_init(
    const char *rocksdb_default_cf_options,
    const char *rocksdb_override_cf_options,
    const rocksdb::BlockBasedTableOptions &main_rdb_table_options) {
  bulk_load_manger = std::make_unique<Rdb_bulk_load_manager>(
      rocksdb_default_cf_options, rocksdb_override_cf_options,
      main_rdb_table_options);
}

void rdb_bulk_load_deinit() {
  bool need_destroy_db = bulk_load_rdb ? true : false;
  bulk_load_manger = nullptr;
  // TODO: support resumption
  if (need_destroy_db) {
    const auto status =
        rocksdb::DestroyDB(bulk_load_rdb_dir(), rocksdb::Options());
    assert(status.ok());
  }
}

void rdb_set_bulk_load_history_size(uint size) {
  bulk_load_manger->set_history_size(size);
}

std::vector<Rdb_bulk_load_session> rdb_dump_bulk_load_sessions() {
  return bulk_load_manger->dump_sessions();
}

Rdb_bulk_load_context::Rdb_bulk_load_context(THD *thd) : m_thd(thd) {}

Rdb_bulk_load_context::~Rdb_bulk_load_context() {
  if (m_active) {
    if (m_type != Rdb_bulk_load_type::TEMPORARY_RDB) {
      bulk_load_manger->abort_session(m_bulk_load_session_id,
                                      m_num_commited_sst_files);
    } else {
      bulk_load_manger->finish_thread(m_bulk_load_session_id,
                                      m_thd->thread_id(),
                                      Rdb_bulk_load_status::ABORTED);
    }
  }
}

void Rdb_bulk_load_context::clear_current_bulk_load() {
  m_curr_bulk_load.clear();
  m_key_merge.clear();
  m_bulk_load_index_registry.clear();
  m_curr_db_name.clear();
  m_curr_table_name.clear();
};

void Rdb_bulk_load_context::clear_bulk_load_session_ctx() {
  m_num_commited_sst_files = 0;
  m_active = false;
  m_type = Rdb_bulk_load_type::NONE;
  m_bulk_load_session_id.clear();
  clear_current_bulk_load();
}

bool Rdb_bulk_load_context::table_changed(std::string_view db_name,
                                          std::string_view table_base_name) {
  return (m_curr_db_name != db_name || m_curr_table_name != table_base_name);
}

uint Rdb_bulk_load_context::add_table(
    std::string_view db_name, std::string_view table_base_name,
    Rdb_bulk_load_type type, std::unordered_set<std::string> non_default_cf) {
  if (!m_active) {
    // start a new session
    if (bulk_load_manger->start_bulk_load_session(
            m_thd->thread_id(), m_bulk_load_session_id, type, db_name,
            table_base_name, non_default_cf)) {
      return HA_EXIT_FAILURE;
    }
    m_active = true;
    m_type = type;
    return HA_EXIT_SUCCESS;
  }
  return bulk_load_manger->add_table_to_session(
      m_bulk_load_session_id, db_name, table_base_name, non_default_cf);
}

uint Rdb_bulk_load_context::bulk_load_rollback() {
  auto rtn = bulk_load_manger->abort_session(m_bulk_load_session_id);
  clear_bulk_load_session_ctx();
  return rtn;
}

uint Rdb_bulk_load_context::set_curr_table(std::string_view db_name,
                                           std::string_view table_base_name,
                                           Rdb_bulk_load_type type) {
  m_curr_db_name = db_name;
  m_curr_table_name = table_base_name;
  return add_table(db_name, table_base_name, type);
}

uint Rdb_bulk_load_context::notify_ddl(std::string_view db_name,
                                       std::string_view table_base_name) {
  if (m_active) {
    // do not allow alter table while the current transaction has an active bulk
    // load.
    std::string err = "Conflict bulk load " + m_bulk_load_session_id +
                      " is running in the current transaction";
    my_error(ER_DA_BULK_LOAD, MYF(0), err.c_str());
    return HA_EXIT_FAILURE;
  }
  // start a new session
  const auto rtn = bulk_load_manger->start_bulk_load_session(
      m_thd->thread_id(), m_bulk_load_session_id, Rdb_bulk_load_type::DDL,
      db_name, table_base_name);
  if (rtn) {
    return rtn;
  }
  m_active = true;
  m_curr_db_name = db_name;
  m_curr_table_name = table_base_name;
  return rtn;
}

Rdb_sst_info *Rdb_bulk_load_context::add_sst_info(
    rocksdb::DB &rdb, const std::string &tablename, const Rdb_key_def &kd,
    rocksdb::DBOptions &db_option, bool trace_sst_api,
    bool compression_parallel_threads) {
  auto sst_info_ptr = std::make_unique<Rdb_sst_info>(
      rdb, tablename, kd.get_name(), kd.get_cf(), db_option, trace_sst_api,
      compression_parallel_threads);
  Rdb_sst_info *sst_info = sst_info_ptr.get();
  m_curr_bulk_load.emplace(kd.get_gl_index_id(), std::move(sst_info_ptr));
  return sst_info;
}

void Rdb_bulk_load_context::complete_bulk_load_session(int rtn) {
  if (m_active) {
    bulk_load_manger->complete_session(m_bulk_load_session_id, rtn,
                                       m_num_commited_sst_files);
  }
  clear_bulk_load_session_ctx();
}

uint Rdb_bulk_load_context::add_key_merge(
    const std::string &table_name, const Rdb_key_def &kd,
    const char *tmpfile_path, ulonglong merge_buf_size,
    ulonglong merge_combine_read_size, ulonglong merge_tmp_file_removal_delay,
    Rdb_index_merge **key_merge) {
  auto kd_gl_id = kd.get_gl_index_id();
  m_key_merge.emplace(
      std::piecewise_construct, std::make_tuple(kd_gl_id),
      std::make_tuple(table_name, kd.get_name(), tmpfile_path, merge_buf_size,
                      merge_combine_read_size, merge_tmp_file_removal_delay,
                      std::ref(kd.get_cf())));
  Rdb_index_merge *find = find_key_merge(kd_gl_id);
  assert(find);
  int res;
  if ((res = find->init()) != 0) {
    return res;
  }
  *key_merge = find;
  return HA_EXIT_SUCCESS;
}

void Rdb_bulk_load_context::increment_num_commited_sst_files(uint count) {
  assert(m_active);
  m_num_commited_sst_files += count;
}

uint Rdb_bulk_load_context::update_cf_indexes(
    std::unordered_map<rocksdb::ColumnFamilyHandle *, std::set<Index_id>>
        &cf_indexes) {
  auto rtn =
      bulk_load_manger->update_cf_indexes(m_bulk_load_session_id, cf_indexes);
  if (rtn) {
    return rtn;
  }
  for (auto &pair : cf_indexes) {
    for (const Index_id &id : pair.second) {
      auto *const sst_partitioner_factory = rdb_get_rocksdb_db()
                                                .GetOptions(pair.first)
                                                .sst_partitioner_factory.get();
      auto *const rdb_sst_partitioner_factory =
          dynamic_cast<Rdb_sst_partitioner_factory *>(sst_partitioner_factory);
      if (rdb_sst_partitioner_factory == nullptr) {
        // should never happen
        // NO_LINT_DEBUG
        LogPluginErrMsg(
            WARNING_LEVEL, ER_LOG_PRINTF_MSG,
            "MyRocks: Rdb_sst_partitioner_factory not registered for cf %s ",
            pair.first->GetName().c_str());
        return HA_EXIT_FAILURE;
      }
      rdb_sst_partitioner_factory->add_index(id);
    }
  }
  return HA_EXIT_SUCCESS;
}

uint Rdb_bulk_load_session::add_table(
    const std::string &table_name, Rdb_bulk_load_type type,
    Rdb_cf_manager &cf_manager,
    std::unordered_set<std::string> non_default_cf) {
  if (m_tables.find(table_name) != m_tables.end()) {
    return HA_EXIT_SUCCESS;
  }
  if (type == Rdb_bulk_load_type::TEMPORARY_RDB) {
    if (!cf_manager.get_or_create_cf(
            convert_cf_to_temp_cf_name(DEFAULT_CF_NAME))) {
      LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                      "Fail to create/get default cf");
      return HA_EXIT_FAILURE;
    }
    for (const auto &cf_name : non_default_cf) {
      std::string temp_cf_name = convert_cf_to_temp_cf_name(cf_name);
      if (!cf_manager.get_or_create_cf(temp_cf_name)) {
        LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                        "Fail to create/get cf %s", temp_cf_name.c_str());
        return HA_EXIT_FAILURE;
      }
      LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "Create/get cf %s",
                      temp_cf_name.c_str());
    }
  } else {
    set_curr_table(table_name);
  }
  m_tables.insert(table_name);
  return HA_EXIT_SUCCESS;
}

uint Rdb_bulk_load_session::complete(Rdb_bulk_load_status status, int rtn_code,
                                     uint num_sst_files,
                                     Rdb_cf_manager &cf_manager) {
  if (m_type == Rdb_bulk_load_type::TEMPORARY_RDB) {
    for (auto &pair : m_workers) {
      if (pair.second.status() == Rdb_bulk_load_status::ACTIVE) {
        std::string err = "Thread " + std::to_string(pair.first) +
                          " is still active, please complete/exit thread first";
        my_error(ER_DA_BULK_LOAD, MYF(0), err.c_str());
        return HA_EXIT_FAILURE;
      }
    }
    assert(cf_manager.is_initialized());
    remove_index_from_sst_partitioner();
    // delete cfs for this session
    for (const auto &pair : m_cf_indexes) {
      rocksdb::ColumnFamilyHandle *cf = cf_manager.get_cf(pair.first);
      if (cf == nullptr) continue;
      rocksdb::Status s = bulk_load_rdb->DropColumnFamily(cf);
      DBUG_EXECUTE_IF("bulk_load_complete_error",
                      s = rocksdb::Status::Incomplete(););
      if (!s.ok()) {
        std::string err = "Fail to drop cf " + pair.first;
        rdb_log_status_error(s, err.c_str());
        m_status = Rdb_bulk_load_status::FAILED;
        m_rtn_code = HA_EXIT_FAILURE;
        m_completed_micro = my_micro_time();
        return HA_EXIT_FAILURE;
      }
      cf_manager.drop_cf_from_map(pair.first, cf->GetID());
      LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                      "Dropped and destroyed cf %s", pair.first.c_str());
    }
  } else {
    m_workers.begin()->second.complete(status, rtn_code);
  }
  m_rtn_code = rtn_code;
  m_status = status;
  m_num_sst_files = num_sst_files;
  m_completed_micro = my_micro_time();
  return HA_EXIT_SUCCESS;
}

}  // namespace myrocks
