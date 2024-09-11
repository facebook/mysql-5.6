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
#include "./rdb_utils.h"
#include "include/scope_guard.h"

namespace myrocks {

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
    default:
      assert(false);
      return "UNKNOWN";
  }
}

class Rdb_bulk_load_manager {
 public:
  Rdb_bulk_load_manager() { mysql_mutex_init(0, &m_mutex, MY_MUTEX_INIT_FAST); }
  ~Rdb_bulk_load_manager() { mysql_mutex_destroy(&m_mutex); }

  void start_bulk_load_session(my_thread_id thread_id, std::string &id,
                               Rdb_bulk_load_type type) {
    RDB_MUTEX_LOCK_CHECK(m_mutex);
    auto lock_guard =
        create_scope_guard([this]() { RDB_MUTEX_UNLOCK_CHECK(m_mutex); });
    id = "sys-" + std::to_string(m_id_seed++);
    [[maybe_unused]] auto rtn =
        m_sessions.emplace(id, Rdb_bulk_load_session(id, thread_id, type));
    assert(rtn.second);
  }

  uint add_table_to_session(const std::string &id, std::string_view db_name,
                            std::string_view table_base_name) {
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
    if (type == Rdb_bulk_load_type::DDL) {
      // if any other session actively bulk load current table, we cannot do
      // ddl.
      bool exists =
          std::any_of(m_sessions.cbegin(), m_sessions.cend(), [&](auto &entry) {
            return entry.first != id &&
                   entry.second.is_table_active(table_name);
          });
      if (exists) {
        return HA_EXIT_FAILURE;
      }
    } else if (type == Rdb_bulk_load_type::SST_FILE_WRITER) {
      // do not allow ddl and bulk load at the same time.
      bool exists =
          std::any_of(m_sessions.cbegin(), m_sessions.cend(), [&](auto &entry) {
            return entry.first != id &&
                   entry.second.type() != Rdb_bulk_load_type::SST_FILE_WRITER &&
                   entry.second.is_table_active(table_name);
          });
      if (exists) {
        return HA_EXIT_FAILURE;
      }
    } else {
      assert(false);
      LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                      "unexpected bulk load type %d", static_cast<int>(type));
      return HA_EXIT_FAILURE;
    }

    existing_session->add_table(table_name);
    return HA_EXIT_SUCCESS;
  }

  uint complete_session(const std::string &id, int rtn_code,
                        uint num_sst_files) {
    return finish_session(id,
                          rtn_code ? Rdb_bulk_load_status::FAILED
                                   : Rdb_bulk_load_status::COMPLETED,
                          rtn_code, num_sst_files);
  }

  uint abort_session(const std::string &id, uint num_sst_files) {
    return finish_session(id, Rdb_bulk_load_status::ABORTED, HA_EXIT_FAILURE,
                          num_sst_files);
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
  std::map<std::string, Rdb_bulk_load_session> m_sessions;
  // completed sessions
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
    assert(existing_session);
    if (!existing_session) {
      return HA_EXIT_FAILURE;
    }
    existing_session->complete(status, rtn_code, num_sst_files);
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

void rdb_bulk_load_init() {
  bulk_load_manger = std::make_unique<Rdb_bulk_load_manager>();
}

void rdb_bulk_load_deinit() { bulk_load_manger = nullptr; }

void rdb_set_bulk_load_history_size(uint size) {
  bulk_load_manger->set_history_size(size);
}

std::vector<Rdb_bulk_load_session> rdb_dump_bulk_load_sessions() {
  return bulk_load_manger->dump_sessions();
}

Rdb_bulk_load_context::Rdb_bulk_load_context(THD *thd) : m_thd(thd) {}

Rdb_bulk_load_context::~Rdb_bulk_load_context() {
  if (m_active) {
    bulk_load_manger->abort_session(m_bulk_load_session_id,
                                    m_num_commited_sst_files);
  }
}

void Rdb_bulk_load_context::clear_current_bulk_load() {
  m_curr_bulk_load.clear();
  m_key_merge.clear();
  m_bulk_load_index_registry.clear();
  m_curr_db_name.clear();
  m_curr_table_name.clear();
};

bool Rdb_bulk_load_context::table_changed(std::string_view db_name,
                                          std::string_view table_base_name) {
  return (m_curr_db_name != db_name || m_curr_table_name != table_base_name);
}

uint Rdb_bulk_load_context::set_curr_table(std::string_view db_name,
                                           std::string_view table_base_name) {
  m_curr_db_name = db_name;
  m_curr_table_name = table_base_name;
  if (!m_active) {
    // start a new session
    bulk_load_manger->start_bulk_load_session(
        m_thd->thread_id(), m_bulk_load_session_id,
        Rdb_bulk_load_type::SST_FILE_WRITER);
    m_active = true;
  }
  return bulk_load_manger->add_table_to_session(m_bulk_load_session_id, db_name,
                                                table_base_name);
}

uint Rdb_bulk_load_context::notify_ddl(std::string_view db_name,
                                       std::string_view table_base_name) {
  if (m_active) {
    // do not allow alter table while the current transaction has an active bulk
    // load.
    return HA_EXIT_FAILURE;
  }
  // start a new session
  bulk_load_manger->start_bulk_load_session(
      m_thd->thread_id(), m_bulk_load_session_id, Rdb_bulk_load_type::DDL);
  m_active = true;
  m_curr_db_name = db_name;
  m_curr_table_name = table_base_name;
  const auto rtn = bulk_load_manger->add_table_to_session(
      m_bulk_load_session_id, db_name, table_base_name);
  if (rtn) {
    // ddl is not allowed, clean up the bulk load session right away
    complete_bulk_load_session(rtn);
  }
  return rtn;
}

Rdb_sst_info *Rdb_bulk_load_context::add_sst_info(
    rocksdb::DB *rdb, const std::string &tablename, const Rdb_key_def &kd,
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
    m_active = false;
    m_num_commited_sst_files = 0;
  }
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

}  // namespace myrocks
