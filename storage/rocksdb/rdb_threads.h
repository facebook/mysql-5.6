/*
   Portions Copyright (c) 2015-Present, Facebook, Inc.
   Portions Copyright (c) 2012, Monty Program Ab

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

/* C++ standard header files */
#include <deque>
#include <map>
#include <string>
#include <utility>

/* MySQL includes */
#include <mysql/psi/mysql_table.h>
#include <mysql/thread_pool_priv.h>
#include "sql/sql_class.h"

/* MyRocks header files */
#include "./rdb_global.h"
#include "./rdb_utils.h"
#include "rocksdb/db.h"

namespace myrocks {

class Rdb_thread : public Ensure_initialized {
 private:
  // Disable Copying
  Rdb_thread(const Rdb_thread &);
  Rdb_thread &operator=(const Rdb_thread &);

  // Make sure we run only once
  std::atomic_bool m_run_once;

  my_thread_handle m_handle;
  bool m_thread_created;

  std::string m_name;

 protected:
  mysql_mutex_t m_signal_mutex;
  mysql_cond_t m_signal_cond;

  // TODO: When porting to 8.0 we should move to std::atomic
  // instead of volatile
  std::atomic<THD::killed_state> m_killed;

  virtual void on_init() {}
  virtual void on_uninit() {}

 public:
  Rdb_thread()
      : m_run_once(false), m_thread_created(false), m_killed(THD::NOT_KILLED) {}

#ifdef HAVE_PSI_INTERFACE
  void init(my_core::PSI_mutex_key stop_bg_psi_mutex_key,
            my_core::PSI_cond_key stop_bg_psi_cond_key);
  int create_thread(const std::string &thread_name,
                    my_core::PSI_thread_key background_psi_thread_key);
#else
  void init();
  int create_thread(const std::string &thread_name);
#endif

  virtual void run(void) = 0;

  void signal(const bool stop_thread = false);

  int join() {
    if (!m_run_once) return EINVAL;
    return pthread_join(m_handle.thread, nullptr);
  }

  void setname() {
    /*
      mysql_thread_create() ends up doing some work underneath and setting the
      thread name as "my-func". This isn't what we want. Our intent is to name
      the threads according to their purpose so that when displayed under the
      debugger then they'll be more easily identifiable. Therefore we'll reset
      the name if thread was successfully created.
    */

    /*
      We originally had the creator also set the thread name, but that seems to
      not work correctly in all situations.  Having the created thread do the
      pthread_setname_np resolves the issue.
    */
    assert(!m_name.empty());
#ifdef __linux__
    int err = pthread_setname_np(m_handle.thread, m_name.c_str());
    if (err) {
      // NO_LINT_DEBUG
      sql_print_warning(
          "MyRocks: Failed to set name (%s) for current thread, errno=%d,%d",
          m_name.c_str(), errno, err);
    }
#endif
  }

  void uninit();

  virtual ~Rdb_thread() {}

 private:
  static void *thread_func(void *const thread_ptr);
};

/**
  MyRocks background thread control
  N.B. This is on top of RocksDB's own background threads
       (@see rocksdb::CancelAllBackgroundWork())
*/

class Rdb_background_thread : public Rdb_thread {
 private:
  bool m_save_stats = false;

  void reset() {
    mysql_mutex_assert_owner(&m_signal_mutex);
    m_killed = THD::NOT_KILLED;
    m_save_stats = false;
  }

 public:
  virtual void run() override;

  void request_save_stats() {
    RDB_MUTEX_LOCK_CHECK(m_signal_mutex);

    m_save_stats = true;

    RDB_MUTEX_UNLOCK_CHECK(m_signal_mutex);
  }
};

class Rdb_index_stats_thread : public Rdb_thread {
 private:
  mysql_mutex_t m_is_mutex;
  std::deque<std::reference_wrapper<const std::string>> m_requests;
  std::unordered_set<std::string> m_tbl_names;

  bool m_tid_set;
  pid_t m_tid;

 public:
  Rdb_index_stats_thread() : m_tid_set(false), m_tid(0) {}

  virtual void on_init() override {
    mysql_mutex_init(0, &m_is_mutex, MY_MUTEX_INIT_FAST);
  }

  virtual void on_uninit() override { mysql_mutex_destroy(&m_is_mutex); }

  virtual void run() override;
  bool get_index_stats_request(std::string *tbl_name);
  void add_index_stats_request(const std::string &tbl_name);
  void clear_all_index_stats_requests();
  size_t get_request_queue_size();
  int renice(int nice_val);
};

class Rdb_manual_compaction_thread : public Rdb_thread {
 public:
  struct Manual_compaction_request {
    int mc_id;
    enum mc_state {
      PENDING = 0,
      RUNNING = 1,
      SUCCESS = 2,
      FAILURE = 3,
      CANCEL = 4,
    } state;
    std::shared_ptr<rocksdb::ColumnFamilyHandle> cf;
    rocksdb::Slice *start;
    rocksdb::Slice *limit;
    rocksdb::CompactRangeOptions option;
    std::shared_ptr<std::atomic<bool>> canceled;
    // state is modified by user threads
    bool client_done;
  };

  int m_latest_mc_id;
  mysql_mutex_t m_mc_mutex;
  std::map<int, Manual_compaction_request> m_requests;

 public:
  Rdb_manual_compaction_thread() {}

  virtual void on_init() override {
    mysql_mutex_init(0, &m_mc_mutex, MY_MUTEX_INIT_FAST);
  }

  virtual void on_uninit() override { mysql_mutex_destroy(&m_mc_mutex); }

  virtual void run() override;
  int request_manual_compaction(
      std::shared_ptr<rocksdb::ColumnFamilyHandle> cf, rocksdb::Slice *start,
      rocksdb::Slice *limit, const uint manual_compaction_threads,
      const rocksdb::BottommostLevelCompaction bottommost_level_compaction);
  Manual_compaction_request::mc_state manual_compaction_state(const int mc_id);
  void set_state(Manual_compaction_request *mcr,
                 const Manual_compaction_request::mc_state state);
  bool set_client_done(const int mc_id);
  void clear_all_manual_compaction_requests();
  void cancel_all_pending_manual_compaction_requests();
  bool cancel_manual_compaction_request(const int mc_id,
                                        const int timeout_100ms);
};

/*
  Drop index thread control
*/

struct Rdb_drop_index_thread : public Rdb_thread {
  virtual void run() override;
};

}  // namespace myrocks
