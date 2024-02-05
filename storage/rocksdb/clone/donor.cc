/*
  Copyright (C) 2022, 2023, Laurynas Biveinis

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

#include "donor.h"

#include <atomic>
#include <chrono>
#include <cstdbool>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "my_dbug.h"
#include "my_io.h"
#include "mysql/psi/mysql_file.h"
#include "mysql/psi/mysql_mutex.h"
#include "mysql/psi/mysql_rwlock.h"
#include "mysqld_error.h"
#include "mysys_err.h"
#include "sql-common/json_dom.h"
#include "sql/sql_class.h"

// RocksDB includes
#include "file/filename.h"

// MyRocks includes
#include "../ha_rocksdb.h"
#include "../ha_rocksdb_proto.h"
#include "common.h"

using namespace std::string_view_literals;

namespace {

// Represents a checkpoint for a single clone session. The checkpoint path will
// be a new directory under the RocksDB data directory that is named
// .clone-checkpoint-<id>. This way any parallel clone sessions will have their
// own independent checkpoints. The RocksDB data directory is chosed as the
// parent directory and not i.e. MySQL tmpdir because checkpointing relies on
// creating hard links to the immutable files, requiring checkpoint dir and
// datadir to be on the same filesystem.
//
// The checkpoint will be re-created aka rolled several times during the clone.
// This does not bump its ID.
class [[nodiscard]] rdb_checkpoint final {
 public:
  rdb_checkpoint()
      : m_prefix_dir{make_dir_prefix_name(
            m_next_id.fetch_add(1, std::memory_order_relaxed))} {}

  ~rdb_checkpoint() {
    // Ignore the return value - at this point the clone operation is completing
    // with success or failure already set, nowhere to return any errors.
    (void)cleanup();
  }

  // Returns MySQL error code
  [[nodiscard]] int init() {
    assert(!m_active);
    m_dir = make_dir_name(m_prefix_dir, m_next_sub_id++);
    const auto result = myrocks::rocksdb_create_checkpoint(m_dir);
    m_active = (result == HA_EXIT_SUCCESS);
    return m_active ? 0 : ER_INTERNAL_ERROR;
  }

  // Returns MySQL error code
  [[nodiscard]] int cleanup() {
    if (!m_active) return 0;
    m_active = false;
    return myrocks::rocksdb_remove_checkpoint(m_dir) == HA_EXIT_SUCCESS
               ? 0
               : ER_INTERNAL_ERROR;
  }

  [[nodiscard]] constexpr const std::string &get_dir() const noexcept {
    assert(m_active);
    return m_dir;
  }

  [[nodiscard]] std::string path(std::string_view file_name) const {
    // We might be calling this for inactive checkpoint too, if the donor is in
    // the middle of a checkpoint roll. The caller will handle any ENOENTs as
    // needed.
    return myrocks::rdb_concat_paths(m_dir, file_name);
  }

  rdb_checkpoint(const rdb_checkpoint &) = delete;
  rdb_checkpoint(rdb_checkpoint &&) = delete;
  rdb_checkpoint &operator=(const rdb_checkpoint &) = delete;
  rdb_checkpoint &operator=(rdb_checkpoint &&) = delete;

 private:
  const std::string m_prefix_dir;

  std::string m_dir;

  bool m_active = false;

  static std::atomic<std::uint64_t> m_next_id;

  std::uint64_t m_next_sub_id = 1;

  [[nodiscard]] static std::string make_dir_prefix_name(std::uint64_t id) {
    const auto base_str = myrocks::clone::checkpoint_base_dir();
    const auto id_str = std::to_string(id);
    std::string result;
    result.reserve(base_str.length() +
                   myrocks::clone::checkpoint_name_prefix.length() +
                   id_str.length() + 2);  // +2 for FN_LIBCHAR and '\0'
    result = base_str;
    result += FN_LIBCHAR;
    result += myrocks::clone::checkpoint_name_prefix;
    result += id_str;
    return result;
  }

  [[nodiscard]] static std::string make_dir_name(
      std::string_view dir_name_prefix, std::uint64_t id) {
    const auto id_str = std::to_string(id);
    std::string result;
    // +2 for '-' and '\0'
    result.reserve(dir_name_prefix.length() + id_str.length() + 2);
    result = dir_name_prefix;
    result += '-';
    result += id_str;
    return result;
  }
};

std::atomic<std::uint64_t> rdb_checkpoint::m_next_id{1};

class [[nodiscard]] donor_file_metadata final
    : public myrocks::clone::file_metadata {
 public:
  using file_metadata::file_metadata;

  [[nodiscard]] constexpr my_off_t get_sent_offset() const noexcept {
    assert(is_valid());
    return m_sent_offset;
  }

  void serialize(myrocks::Rdb_string_writer &buf) const;

  void reset_for_restart() {
    assert(is_valid());
    m_sent_offset = 0;
  }

  [[nodiscard]] constexpr bool reset_state(
      myrocks::clone::locator::file_state new_state) {
    assert(is_valid());
    assert(m_sent_offset == 0);

    const auto new_offset = new_state.get_applied_offset();
    if (new_offset > get_size()) return false;
    m_sent_offset = new_offset;
    return true;
  }

  [[nodiscard]] constexpr auto started() const noexcept {
    assert(is_valid());
    assert(m_sent_offset <= get_size());

    return m_sent_offset > 0;
  }

  [[nodiscard]] constexpr auto eof() const noexcept {
    assert(is_valid());
    assert(m_sent_offset <= get_size());

    return m_sent_offset == get_size();
  }

  [[nodiscard]] constexpr auto chunk_size(uint buf_size) const noexcept {
    assert(is_valid());
    assert(m_sent_offset < get_size());

    return (m_sent_offset + buf_size) < get_size()
               ? buf_size
               : static_cast<uint>(get_size() - m_sent_offset);
  }

  constexpr void advance(uint delta) noexcept {
    assert(is_valid());
    assert(m_sent_offset < get_size());

    m_sent_offset += delta;

    assert(m_sent_offset <= get_size());
  }

 private:
  my_off_t m_sent_offset{0};
};

void donor_file_metadata::serialize(myrocks::Rdb_string_writer &buf) const {
  assert(is_valid());
  assert(buf.is_empty());

  const auto name_length = get_name().length();
  const std::uint32_t payload_length =
      myrocks::clone::metadata_header::m_length + sizeof(get_id()) +
      sizeof(get_size()) + 4 + name_length;
  const myrocks::clone::metadata_header header{
      payload_length, myrocks::clone::metadata_type::FILE_NAME_MAP_V1};
  header.serialize(buf);

  buf.write_uint64(get_id());
  buf.write_uint64(get_size());
  buf.write_uint32(name_length);
  buf.write(reinterpret_cast<const uchar *>(get_name().data()), name_length);

  assert(buf.get_current_pos() == payload_length);
}

class [[nodiscard]] donor_chunk_metadata final
    : public myrocks::clone::chunk_metadata {
 public:
  using myrocks::clone::chunk_metadata::chunk_metadata;

  void serialize(myrocks::Rdb_string_writer &buf) const {
    assert(is_valid());
    assert(buf.is_empty());

    static constexpr std::uint32_t payload_length =
        myrocks::clone::metadata_header::m_length + 16;
    constexpr myrocks::clone::metadata_header header{
        payload_length, myrocks::clone::metadata_type::FILE_CHUNK_V1};
    header.serialize(buf);
    buf.write_uint64(get_file_id());
    buf.write_uint64(get_size());

    assert(buf.get_current_pos() == payload_length);
  }
};

class [[nodiscard]] donor_estimate final
    : public myrocks::clone::estimate_metadata {
 public:
  using myrocks::clone::estimate_metadata::estimate_metadata;

  void serialize(myrocks::Rdb_string_writer &buf) const {
    assert(is_valid());
    assert(buf.is_empty());

    static constexpr std::uint32_t payload_length =
        8 + myrocks::clone::metadata_header::m_length;
    static constexpr myrocks::clone::metadata_header header{
        payload_length, myrocks::clone::metadata_type::ADD_ESTIMATE_V1};
    header.serialize(buf);
    buf.write_uint64(get_estimate_delta());

    assert(buf.get_current_pos() == payload_length);
  }
};

// A single donor clone session. It has a unique locator for identification, a
// unique associated checkpoint, and several sets of file metadata objects. Each
// file goes through the three states: not started, in progress, completed.
// There are separate metadata sets for each state and the state change is
// performed by moving the metadata object between the sets. A clone restart may
// reset the file state.
class [[nodiscard]] donor final : public myrocks::clone::session {
 public:
  // The clone session as a whole also goes through these states, in sequence:
  enum class donor_state {
    // The clone has been started but the RocksDB checkpoint has not been
    // created yet. rocksdb_clone_begin does not create the checkpoint -
    // pre-copy does.
    INITIAL,
    // The clone pre-copy is in progress: a checkpoint has been created and
    // rolled as necessary.
    ROLLING_CHECKPOINT,
    // RocksDB file deletions have been disabled and the final RocksDB
    // checkpoint that will not be rolled again has been created.
    FINAL_CHECKPOINT,
    // In addition to the previous state, the live WAL information has been
    // added to the clone file set.
    FINAL_CHECKPOINT_WITH_LOGS
  };

  donor(const myrocks::clone::locator &l, const uchar *&loc, uint &loc_len);

  ~donor();

  // To be called when the passed loc pointer will be freed soon, to replace it
  // with our locator copy, which has the lifetime of the whole clone.
  void refresh_loc_ptr(const uchar *&loc, uint &loc_len) const {
    const auto l = myrocks::clone::locator::deserialize(loc, loc_len);
    assert(l.is_snapshot());

    if (l.has_applier_status()) {
      loc_len = m_locator_buf.get_current_pos();
    } else {
      assert(!memcmp(loc, m_locator_buf.ptr(), loc_len));
      assert(loc_len == m_locator_buf.get_current_pos());
    }
    loc = m_locator_buf.ptr();

#ifndef NDEBUG
    const auto ret_l = myrocks::clone::locator::deserialize(loc, loc_len);
    assert(ret_l.is_snapshot());
    assert(!ret_l.has_applier_status());
#endif
  }

  // Returns MySQL error code
  [[nodiscard]] int init_checkpoint(Ha_clone_cbk &cbk) {
    mysql_mutex_lock(&m_donor_mutex);
    if (m_state != donor_state::INITIAL) {
      mysql_mutex_unlock(&m_donor_mutex);
      return 0;
    }
    assert(m_checkpoint_count == 0);
    std::size_t initial_checkpoint_size;
    const auto err = next_checkpoint_locked(false, initial_checkpoint_size);
    mysql_mutex_unlock(&m_donor_mutex);
    if (err == 0) {
      const donor_estimate initial_clone_size_estimate{initial_checkpoint_size};
      return send_metadata(initial_clone_size_estimate, cbk);
    }
    return err;
  }

  // Returns MySQL error code
  [[nodiscard]] int roll_to_final_checkpoint() {
    mysql_mutex_lock(&m_donor_mutex);
    if (m_state == donor_state::FINAL_CHECKPOINT) {
      mysql_mutex_unlock(&m_donor_mutex);
      return 0;
    }
    assert(m_checkpoint_count >= 1);
    // Not used because the final checkpoint data size difference will be sent
    // later in donor::copy
    std::size_t unused;
    const auto err = next_checkpoint_locked(true, unused);
    mysql_mutex_unlock(&m_donor_mutex);
    return err;
  }

  void add_log(std::string &&name, my_off_t size);

  void set_logs_added() noexcept {
    mysql_mutex_lock(&m_donor_mutex);
    assert(m_state == donor_state::FINAL_CHECKPOINT);
    m_state = donor_state::FINAL_CHECKPOINT_WITH_LOGS;

    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "MyRocks clone state change: FINAL_CHECKPOINT -> "
                    "FINAL_CHECKPOINT_WITH_LOGS");

    mysql_mutex_unlock(&m_donor_mutex);
  }

  [[nodiscard]] donor_state get_state() const noexcept {
    mysql_mutex_lock(&m_donor_mutex);
    const auto state_copy = m_state;
    mysql_mutex_unlock(&m_donor_mutex);
    return state_copy;
  }

  [[nodiscard]] int copy(const THD *thd, uint task_id, Ha_clone_cbk &cbk);

  [[nodiscard]] bool restart(const myrocks::clone::locator &restart_locator);

  // Must be called with manager::instance()::m_active_clones_lock R-locked
  [[nodiscard]] bool new_tasks_allowed() const noexcept {
    return m_allow_new_tasks_flag;
  }

  // Must be called with manager::instance()::m_active_clones_lock W-locked
  void reject_new_tasks() noexcept {
    assert(m_allow_new_tasks_flag);
    m_allow_new_tasks_flag = false;
  }

  // Must be called with manager::instance()::m_active_clones_lock W-locked
  void allow_new_tasks() noexcept {
    assert(!m_allow_new_tasks_flag);
    m_allow_new_tasks_flag = true;
  }

  [[nodiscard]] auto get_main_task_ref_count() const noexcept {
    const auto result = m_main_task_ref_count.load(std::memory_order_acquire);
    assert(result == 1 || result == 2);
    return result;
  }

  void inc_main_task_ref_count() noexcept {
    const auto old_ref_count [[maybe_unused]] =
        m_main_task_ref_count.fetch_add(1, std::memory_order_relaxed);
    assert(old_ref_count == 1);
  }

  // Returns true if rc reached zero
  [[nodiscard]] auto dec_main_task_ref_count() noexcept {
    const auto old_ref_count =
        m_main_task_ref_count.fetch_sub(1, std::memory_order_relaxed);
    assert(old_ref_count == 1 || old_ref_count == 2);
    return old_ref_count == 1;
  }

  void wait_for_reconnect() const {
    if (get_main_task_ref_count() == 1) {
      timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_sec += 1;
      mysql_mutex_lock(&m_donor_mutex);
      mysql_cond_timedwait(&m_reconnection_signal, &m_donor_mutex, &ts);
      mysql_mutex_unlock(&m_donor_mutex);
    }
  }

  donor() = delete;
  donor(const donor &) = delete;
  donor &operator=(const donor &) = delete;
  donor(donor &&other) = delete;
  donor &operator=(donor &&) = delete;

 private:
  myrocks::Rdb_string_writer m_locator_buf;

  // Protects most of the fields below, unless stated otherwise
  mutable mysql_mutex_t m_donor_mutex;

  donor_state m_state{donor_state::INITIAL};

  rdb_checkpoint m_checkpoint;
  bool m_rdb_file_deletes_disabled{false};

  // Collected clone data size to be sent to the client when it needs to be sent
  // in the next stage, i.e. when we change from the rolling to the final
  // checkpoint. Not used when the data size can be reported in the same stage,
  // then it is sent immediately.
  std::size_t m_data_size_for_the_next_stage{0};

  std::uint64_t m_next_file_id{0};

  std::unordered_map<std::string, std::uint64_t> m_name_to_id_map;

  using id_metadata_map =
      std::unordered_map<std::uint64_t, donor_file_metadata>;

  id_metadata_map m_not_started_files;
  id_metadata_map m_in_progress_files;
  id_metadata_map m_completed_files;

  std::size_t last_reported_file_count{0};

  uint m_checkpoint_count{0};
  std::chrono::time_point<std::chrono::steady_clock> m_checkpoint_start_time;

  mutable mysql_cond_t m_reconnection_signal;

  // Protected by manager::m_active_clones_lock
  bool m_allow_new_tasks_flag{true};

  // Since multiple threads will be using the same task ID 0 in the case of
  // reconnects, the task ID set is not enough to track the donor instance
  // lifetime, and the task ID 0 must be reference counted:
  // - it starts at one
  // - increments are protected by manager::m_active_clones_lock locked in
  //   shared mode
  // - decrements, including the case of going to zero and freeing the instance,
  //   are protected by manager::m_active_clones_lock locked in exclusive mode
  std::atomic<unsigned> m_main_task_ref_count{1};

  [[nodiscard]] auto should_roll_checkpoint() const noexcept {
    mysql_mutex_assert_owner(&m_donor_mutex);
    assert(m_state != donor_state::INITIAL);
    assert(m_checkpoint_count > 0);

    if (m_state == donor_state::FINAL_CHECKPOINT ||
        m_state == donor_state::FINAL_CHECKPOINT_WITH_LOGS ||
        myrocks::rocksdb_clone_checkpoint_max_age == 0)
      return false;

    assert(m_state == donor_state::ROLLING_CHECKPOINT);
    if (myrocks::rocksdb_clone_checkpoint_max_count != 0 &&
        m_checkpoint_count >= myrocks::rocksdb_clone_checkpoint_max_count)
      return false;

    const auto checkpoint_age =
        std::chrono::steady_clock::now() - m_checkpoint_start_time;
    const std::chrono::seconds max_age{
        myrocks::rocksdb_clone_checkpoint_max_age};
    return checkpoint_age >= max_age;
  }

  [[nodiscard]] int next_checkpoint_locked(bool final,
                                           std::size_t &total_new_size);

  void add_file(std::string &&name, my_off_t size) {
    mysql_mutex_assert_owner(&m_donor_mutex);
    assert(m_state != donor_state::FINAL_CHECKPOINT_WITH_LOGS);

    if (m_name_to_id_map.count(name) != 0) {
#ifndef NDEBUG
      assert_known_file_size_same(name, size);
#endif
      return;
    }

    const auto id = m_next_file_id++;

    assert(m_name_to_id_map.count(name) == 0);
    const auto name_to_id_map_res [[maybe_unused]] =
        m_name_to_id_map.emplace(name, id);
    assert(name_to_id_map_res.second);

    const auto not_started_files_res [[maybe_unused]] =
        m_not_started_files.emplace(
            std::piecewise_construct, std::forward_as_tuple(id),
            std::forward_as_tuple(id, std::move(name), size));
    assert(not_started_files_res.second);
  }

  [[nodiscard]] int add_checkpoint_files(bool final,
                                         std::size_t &total_new_size);

  static void move_file_metadata(id_metadata_map &from, id_metadata_map &to,
                                 id_metadata_map::iterator &&itr) {
    assert(itr != from.end());
    assert(itr->first == itr->second.get_id());
    assert(to.find(itr->first) == to.cend());

    auto metadata_node = from.extract(itr->first);
    const auto res = to.insert(std::move(metadata_node));
    assert(res.inserted);
  }

  [[nodiscard]] std::string path(std::string_view fn) const {
    assert(m_state != donor_state::INITIAL);

    return myrocks::has_file_extension(fn, ".log"sv)
               ? myrocks::rdb_concat_paths(myrocks::get_wal_dir(), fn)
               : m_checkpoint.path(fn);
  }

  // Returns MySQL error code
  [[nodiscard]] int send_metadata(const myrocks::Rdb_string_writer &buf,
                                  Ha_clone_cbk &cbk) {
    assert(get_state() != donor_state::INITIAL);

    cbk.set_data_desc(buf.ptr(), buf.get_current_pos());
    cbk.set_source_name(nullptr);
    const auto err = cbk.buffer_cbk(nullptr, 0);
    if (err == 0) return 0;

    save_error(err);
    return err;
  }

  // Returns MySQL error code
  template <class T>
  [[nodiscard]] int send_metadata(const T &metadata, Ha_clone_cbk &cbk) {
    assert(get_state() != donor_state::INITIAL);

    myrocks::Rdb_string_writer buf;
    metadata.serialize(buf);
    return send_metadata(buf, cbk);
  }

#ifndef NDEBUG
  // Cannot use std::string_view for fn because we need std::string for
  // searching in the containers
  void assert_known_file_size_same(const std::string &fn, my_off_t size) const {
    assert(!myrocks::has_file_extension(fn, ".log"sv));
    const auto name_to_id_map_itr = m_name_to_id_map.find(fn);
    assert(name_to_id_map_itr != m_name_to_id_map.cend());
    const auto id = name_to_id_map_itr->second;
    const auto not_started_itr = m_not_started_files.find(id);
    if (not_started_itr != m_not_started_files.cend()) {
      assert(not_started_itr->second.get_size() == size);
      return;
    }
    const auto in_progress_itr = m_in_progress_files.find(id);
    if (in_progress_itr != m_in_progress_files.cend()) {
      assert(in_progress_itr->second.get_size() == size);
      return;
    }
    const auto completed_itr = m_completed_files.find(id);
    assert(completed_itr != m_completed_files.cend());
    assert(completed_itr->second.get_size() == size);
  }
#endif
};

donor::donor(const myrocks::clone::locator &l, const uchar *&loc,
             uint &loc_len) {
  assert(!l.has_applier_status());
  l.serialize(m_locator_buf, loc, loc_len);
#ifdef HAVE_PSI_INTERFACE
  mysql_mutex_init(myrocks::clone_donor_file_metadata_mutex_key, &m_donor_mutex,
                   MY_MUTEX_INIT_FAST);
  mysql_cond_init(myrocks::rdb_signal_clone_reconnection_key,
                  &m_reconnection_signal);
#else
  mysql_mutex_init(nullptr, &m_donor_mutex, MY_MUTEX_INIT_FAST);
  mysql_cond_init(nullptr, &m_reconnection_signal);
#endif
  register_main_task();
}

donor::~donor() {
  if (m_rdb_file_deletes_disabled) {
    auto *const rdb = myrocks::rdb_get_rocksdb_db();
    const auto result = rdb->EnableFileDeletions(false);
    if (!result.ok()) {
      myrocks::rdb_log_status_error(result,
                                    "RocksDB file deletion re-enable failed");
    }
  }
  mysql_cond_destroy(&m_reconnection_signal);
  mysql_mutex_destroy(&m_donor_mutex);
  session::shutdown();
}

// Returns MySQL error code
int donor::add_checkpoint_files(bool final, std::size_t &total_new_size) {
  mysql_mutex_assert_owner(&m_donor_mutex);

  total_new_size = 0;

  const auto result = myrocks::for_each_in_dir(
      m_checkpoint.get_dir(), MY_WANT_STAT,
      [final, this, &total_new_size](const fileinfo &f_info) {
        std::string_view fn{f_info.name};
        // The last WAL in the checkpoint WAL set will be growing until the
        // cross-engine consistency point. To avoid tracking that, postpone
        // copying any WALs until then.
        if (myrocks::has_file_extension(fn, ".log"sv)) return true;
        if (!final && !myrocks::has_file_extension(fn, ".sst"sv)) return true;
        const my_off_t size = f_info.mystat->st_size;
        add_file(std::string{fn}, size);
        total_new_size += size;
        return true;
      });

  if (result) {
    if (final) {
      assert(m_data_size_for_the_next_stage == 0);
      m_data_size_for_the_next_stage = total_new_size;
    }
    return 0;
  }

  return save_and_return_error(ER_INTERNAL_ERROR, "RocksDB checkpoint error");
}

/// Roll to the next RocksDB checkpoint
/// @param[in]  final           whether the next checkpoint is the final one for
///                             this clone session
/// @param[out] total_new_size  the extra size of the new checkpoint compared to
///                             the previous one(s). Not set if rolling to the
///                             final checkpoint, because its size will be sent
///                             to the client later.
/// @return MySQL error code
int donor::next_checkpoint_locked(bool final, std::size_t &total_new_size) {
  mysql_mutex_assert_owner(&m_donor_mutex);
  assert(m_state != donor_state::FINAL_CHECKPOINT);
  assert(m_state != donor_state::FINAL_CHECKPOINT_WITH_LOGS);
  assert(m_state == donor_state::ROLLING_CHECKPOINT || m_checkpoint_count == 0);

  m_checkpoint_start_time = std::chrono::steady_clock::now();
  ++m_checkpoint_count;

  if (m_state == donor_state::INITIAL) {
    assert(!final);
    auto err = m_checkpoint.init();
    if (err != 0) return save_and_return_error(err, "RocksDB checkpoint error");

    DBUG_EXECUTE_IF("myrocks_clone_donor_init_crash", DBUG_SUICIDE(););

    err = add_checkpoint_files(false, total_new_size);
    if (err) {
      // Eat the return value because we are already returning an error.
      (void)m_checkpoint.cleanup();
      return err;
    }

    m_state = donor_state::ROLLING_CHECKPOINT;
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "MyRocks clone state change: INIT -> ROLLING_CHECKPOINT");
    return 0;
  }

  assert(m_state == donor_state::ROLLING_CHECKPOINT);

  auto err = m_checkpoint.cleanup();
  if (err != 0) return save_and_return_error(err, "RocksDB checkpoint error");

  auto *const rdb = final ? myrocks::rdb_get_rocksdb_db() : nullptr;
  if (rdb != nullptr) {
    const auto dfd_result = rdb->DisableFileDeletions();
    m_rdb_file_deletes_disabled = dfd_result.ok();
    if (!m_rdb_file_deletes_disabled) {
      myrocks::rdb_log_status_error(dfd_result,
                                    "RocksDB file deletion disable failed");
      return save_and_return_error(ER_INTERNAL_ERROR,
                                   "RocksDB file deletion disable error");
    }
  }

  err = m_checkpoint.init();
  if (err != 0) {
    if (rdb) rdb->EnableFileDeletions(false);
    return save_and_return_error(err, "RocksDB checkpoint error");
  }

  LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                  "MyRocks clone checkpoint roll counter now %u",
                  m_checkpoint_count);

  err = add_checkpoint_files(final, total_new_size);
  if (err != 0) {
    // Ignore the return value because we are already returning an error
    (void)m_checkpoint.cleanup();
    if (rdb) rdb->EnableFileDeletions(false);
    return err;
  }

  if (final) {
    m_state = donor_state::FINAL_CHECKPOINT;
    LogPluginErrMsg(
        INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
        "MyRocks clone state change: ROLLING_CHECKPOINT -> FINAL_CHECKPOINT");
  }
  return 0;
}

void donor::add_log(std::string &&name, my_off_t size) {
  mysql_mutex_lock(&m_donor_mutex);

  assert(m_state == donor_state::FINAL_CHECKPOINT);
  // We could not have added a checkpoint without any files
  assert(m_next_file_id > 0);
  assert(m_name_to_id_map.count(name) == 0);

  add_file(std::move(name), size);
  m_data_size_for_the_next_stage += size;

  mysql_mutex_unlock(&m_donor_mutex);
}

// In a loop, pick one not-yet-started file that neeeds to be cloned and move it
// to the in-progress file set. Then prepare and send FILE_NAME_MAP_V1 metadata
// packet, and iterate over the file to send its data accompanied by
// FILE_CHUNK_V1 metadata packets. Once completed, move the file from
// in-progress to completed set, and roll the checkpoint as needed.
// This can and will be called from multiple clone threads and will send
// different files to the client in parallel.
// Returns MySQL error code.
int donor::copy(const THD *thd, uint task_id, Ha_clone_cbk &cbk) {
  const auto initial_state = get_state();
  assert(initial_state != donor_state::INITIAL);
  const auto final_copy =
      initial_state == donor_state::FINAL_CHECKPOINT_WITH_LOGS;

  if (final_copy && is_main_task_id(task_id)) {
    const donor_estimate clone_size{m_data_size_for_the_next_stage};
    const auto err = send_metadata(clone_size, cbk);
    if (err != 0) return err;
    m_data_size_for_the_next_stage = 0;
  }

  while (true) {
    mysql_mutex_lock(&m_donor_mutex);
    assert(m_checkpoint_count > 0);
    auto not_started_files_itr = m_not_started_files.begin();
    if (not_started_files_itr == m_not_started_files.end()
        // Stop copying the rolling checkpoint if it became final
        ||
        (!final_copy && (m_state == donor_state::FINAL_CHECKPOINT ||
                         m_state == donor_state::FINAL_CHECKPOINT_WITH_LOGS))) {
      mysql_mutex_unlock(&m_donor_mutex);
      return 0;
    }

    auto &metadata = not_started_files_itr->second;
    move_file_metadata(m_not_started_files, m_in_progress_files,
                       std::move(not_started_files_itr));

    // Only SSTs may appear in the rolling checkpoints
    // WALs, METADATA, OPTIONS-*, MANIFEST-* may appear in the final checkpoint
    assert(myrocks::has_file_extension(metadata.get_name(), ".sst"sv) ||
           m_state == donor_state::FINAL_CHECKPOINT ||
           m_state == donor_state::FINAL_CHECKPOINT_WITH_LOGS);

    const auto &name = metadata.get_name();
    const auto donor_file_path = path(name);
    auto open_flags = O_RDONLY;
    const auto use_direct_io = myrocks::clone::should_use_direct_io(
        name, myrocks::clone::mode_for_direct_io::DONOR);
#ifndef __APPLE__
    if (use_direct_io) open_flags |= O_DIRECT;
#endif
    const auto fd =
        mysql_file_open(myrocks::rdb_clone_donor_file_key,
                        donor_file_path.c_str(), open_flags, MYF(0));
    if (fd < 0) {
      const auto errn = my_errno();
      if (errn == ENOENT) {
        // Assume this was a file from an older rolling checkpoint. Drop it.
        LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                        "Not found, assuming old checkpoint: %s",
                        donor_file_path.c_str());
        assert(myrocks::has_file_extension(donor_file_path, ".sst"sv));
        const auto erased_count [[maybe_unused]] =
            m_in_progress_files.erase(metadata.get_id());
        assert(erased_count == 1);
        mysql_mutex_unlock(&m_donor_mutex);
        continue;
      }
      mysql_mutex_unlock(&m_donor_mutex);

      MyOsError(errn, EE_FILENOTFOUND, MYF(0), donor_file_path.c_str());
      save_error(ER_CANT_OPEN_FILE, donor_file_path, errn);
      return ER_CANT_OPEN_FILE;
    }

    mysql_mutex_unlock(&m_donor_mutex);

    auto err = handle_any_error(thd);
    if (err != 0) return err;

#ifdef __APPLE__
    if (use_direct_io && fcntl(fd, F_NOCACHE, 1) == -1) {
      const auto errn = my_errno();
      // EE_STAT is not an exact match, but this is enough of a corner case
      MyOsError(errn, EE_STAT, MYF(0), donor_file_path.c_str());
      save_error(ER_CANT_OPEN_FILE, donor_file_path, errn);
      mysql_file_close(fd, MYF(MY_WME));
      return ER_CANT_OPEN_FILE;
    }
#endif

    const auto new_pos =
        my_seek(fd, metadata.get_sent_offset(), SEEK_SET, MYF(MY_WME));
    if (new_pos != metadata.get_sent_offset()) {
      assert(new_pos == MY_FILEPOS_ERROR);
      mysql_file_close(fd, MYF(MY_WME));
      save_error(ER_FSEEK_FAIL, donor_file_path, my_errno());
      return ER_FSEEK_FAIL;
    }

    if (!metadata.started()) {
      err = send_metadata(metadata, cbk);
      if (err != 0) return err;
    }

    Ha_clone_file clone_file{
#ifndef __APPLE__
        use_direct_io,
#else
        false,
#endif
        Ha_clone_file::FILE_DESC,
        {fd}};
    cbk.set_source_name(metadata.get_name().c_str());

    auto buf_size = cbk.get_client_buffer_size();
    const auto remote_clone = buf_size == 0;

    cbk.clear_flags();
    if (!use_direct_io) cbk.set_os_buffer_cache();
    if (!remote_clone) cbk.set_zero_copy();

    if (remote_clone) {
      // 1M value matches InnoDB clone behavior. We could also consider deriving
      // the value from i.e. max_allowed_packet value.
      buf_size = 1024 * 1024;
    }
    assert(buf_size > 0);

    while (!metadata.eof()) {
      err = handle_any_error(thd);
      if (err != 0) {
        mysql_file_close(fd, MYF(MY_WME));
        return err;
      }

      const auto chunk_size = metadata.chunk_size(buf_size);
      donor_chunk_metadata chunk{metadata.get_id(), chunk_size};
      myrocks::Rdb_string_writer buf;
      chunk.serialize(buf);
      cbk.set_data_desc(buf.ptr(), buf.get_current_pos());

      DBUG_EXECUTE_IF("myrocks_clone_donor_copy_file_crash", DBUG_SUICIDE(););

      err = cbk.file_cbk(clone_file, chunk_size);
      if (err != 0) {
        save_error(err, donor_file_path, my_errno());
        mysql_file_close(fd, MYF(MY_WME));
        return err;
      }
      metadata.advance(chunk_size);
    }

    mysql_mutex_lock(&m_donor_mutex);
    auto in_progress_files_itr = m_in_progress_files.find(metadata.get_id());
    move_file_metadata(m_in_progress_files, m_completed_files,
                       std::move(in_progress_files_itr));

    const auto completed_count = m_completed_files.size();
    const auto in_progress_count = m_in_progress_files.size();
    const auto not_started_count = m_not_started_files.size();
    if (completed_count >= last_reported_file_count + 10 ||
        (not_started_count == 0 && in_progress_count == 0)) {
      LogPluginErrMsg(
          INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
          "MyRocks clone file totals: completed %zu, in progress %zu, not "
          "started %zu, total %zu",
          completed_count, in_progress_count, not_started_count,
          completed_count + in_progress_count + not_started_count);
      last_reported_file_count = completed_count;
    }

    err = mysql_file_close(fd, MYF(MY_WME));
    if (err != 0) {
      mysql_mutex_unlock(&m_donor_mutex);
      save_error(err, donor_file_path, my_errno());
      return err;
    }

    err = handle_any_error(thd);
    if (err != 0) {
      mysql_mutex_unlock(&m_donor_mutex);
      return err;
    }

    if (should_roll_checkpoint()) {
      std::size_t checkpoint_size_delta;
      err = next_checkpoint_locked(false, checkpoint_size_delta);
      mysql_mutex_unlock(&m_donor_mutex);
      if (err != 0) return err;

      const donor_estimate clone_size_estimate_delta{checkpoint_size_delta};
      err = send_metadata(clone_size_estimate_delta, cbk);
      if (err != 0) return err;
    } else {
      mysql_mutex_unlock(&m_donor_mutex);
    }
  }
}

bool donor::restart(const myrocks::clone::locator &restart_locator) {
  mysql_mutex_lock(&m_donor_mutex);
  mysql_cond_signal(&m_reconnection_signal);
  mysql_mutex_unlock(&m_donor_mutex);

  mysql_mutex_lock(&m_donor_mutex);
  assert(m_state != donor_state::INITIAL ||
         !restart_locator.has_applier_status());

  assert(m_state != donor_state::INITIAL || m_completed_files.empty());
  m_not_started_files.merge(m_completed_files);

  assert(m_state != donor_state::INITIAL || m_in_progress_files.empty());
  m_not_started_files.merge(m_in_progress_files);

  assert(m_completed_files.empty());
  assert(m_in_progress_files.empty());

  for (auto &metadata : m_not_started_files)
    metadata.second.reset_for_restart();

  for (const auto &new_file_metadata : restart_locator.get_file_states()) {
    auto itr = m_not_started_files.find(new_file_metadata.get_id());
    if (itr == m_not_started_files.end()) {
      // It is legal to not find a file from the restart locator if that file is
      // an obsolete SST from a rolled checkpoint. We don't have enough
      // information to fully confirm this, but do at least some sanity checking
      if (new_file_metadata.get_id() >= m_next_file_id) {
        mysql_mutex_unlock(&m_donor_mutex);
        return false;
      }
      LogPluginErrMsg(
          INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
          "Restart locator contains file ID %" PRIu64
          ", which is not present. Assuming it belonged to an old checkpoint",
          new_file_metadata.get_id());
      continue;
    }

    auto &file_metadata = itr->second;

    if (!file_metadata.reset_state(new_file_metadata)) {
      mysql_mutex_unlock(&m_donor_mutex);
      return false;
    }

    if (file_metadata.eof())
      move_file_metadata(m_not_started_files, m_completed_files,
                         std::move(itr));
  }
  mysql_mutex_unlock(&m_donor_mutex);

  reset_error();

  return true;
}

// Supports multiple parallel clones (and clone restarts) by maintaining a
// locator to donor map
class [[nodiscard]] manager final {
 public:
  [[nodiscard]] static manager &instance() {
    static manager singleton;
    return singleton;
  }

  [[nodiscard]] donor *start_donor(const uchar *&loc, uint &loc_len);

  [[nodiscard]] donor *lookup_donor(const myrocks::clone::locator &l,
                                    const uchar *&loc, uint &loc_len,
                                    Ha_clone_mode mode = HA_CLONE_MODE_VERSION);

  void maybe_end_donor(const myrocks::clone::locator &loc,
                       bool must_be_last_task);

  void shutdown();

  void reject_new_tasks(donor &donor_instance) {
    mysql_rwlock_wrlock(&m_active_clones_lock);
    donor_instance.reject_new_tasks();
    mysql_rwlock_unlock(&m_active_clones_lock);
  }

  void allow_new_tasks(donor &donor_instance) {
    mysql_rwlock_wrlock(&m_active_clones_lock);
    donor_instance.allow_new_tasks();
    mysql_rwlock_unlock(&m_active_clones_lock);
  }

  manager(const manager &) = delete;
  manager(manager &&) = delete;
  manager &operator=(const manager &) = delete;
  manager &operator=(manager &&) = delete;

 private:
  manager();
  ~manager();

  [[nodiscard]] donor *add_donor(const myrocks::clone::locator &l,
                                 const uchar *&loc, uint &loc_len,
                                 bool failure_allowed);

  mutable mysql_rwlock_t m_active_clones_lock;
  std::unordered_map<myrocks::clone::locator, donor> m_active_clones;
};

manager::manager() {
#ifdef HAVE_PSI_INTERFACE
  mysql_rwlock_init(myrocks::key_rwlock_clone_active_clones,
                    &m_active_clones_lock);
#else
  mysql_rwlock_init(nullptr, &m_active_clones_lock);
#endif
}

void manager::shutdown() {
  assert(m_active_clones.empty());
  mysql_rwlock_destroy(&m_active_clones_lock);
}

manager::~manager() { assert(m_active_clones.empty()); }

donor *manager::start_donor(const uchar *&loc, uint &loc_len) {
  if (loc == nullptr) {
    const auto locator = myrocks::clone::locator::get_unique_locator();
    return add_donor(locator, loc, loc_len, false);
  }

  const auto passed_locator =
      myrocks::clone::locator::deserialize(loc, loc_len);
  if (!passed_locator.is_valid()) return nullptr;

  if (passed_locator.is_version_check()) {
    const auto locator = myrocks::clone::locator::get_unique_locator();
    return add_donor(locator, loc, loc_len, false);
  }

  return add_donor(passed_locator, loc, loc_len, true);
}

[[nodiscard]] donor *manager::add_donor(const myrocks::clone::locator &l,
                                        const uchar *&loc, uint &loc_len,
                                        bool failure_allowed) {
  assert(l.is_snapshot());

  mysql_rwlock_wrlock(&m_active_clones_lock);
  const auto insert_result = m_active_clones.emplace(
      std::piecewise_construct, std::forward_as_tuple(l),
      std::forward_as_tuple(l, loc, loc_len));

  if (insert_result.second) {
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "MyRocks clone new session for locator %" PRIu64,
                    l.get_id());
  } else {
    LogPluginErrMsg(
        ERROR_LEVEL, ER_LOG_PRINTF_MSG,
        "Failed to create MyRocks clone session for locator %" PRIu64,
        l.get_id());
  }

  mysql_rwlock_unlock(&m_active_clones_lock);

  if (failure_allowed)
    return insert_result.second ? &insert_result.first->second : nullptr;

  assert(insert_result.second);
  return &insert_result.first->second;
}

donor *manager::lookup_donor(const myrocks::clone::locator &l,
                             const uchar *&loc, uint &loc_len,
                             Ha_clone_mode mode) {
  if (!l.is_snapshot()) return nullptr;

  mysql_rwlock_rdlock(&m_active_clones_lock);
  const auto res_itr = m_active_clones.find(l);
  auto *result =
      (res_itr == m_active_clones.cend()) ? nullptr : &res_itr->second;
  if (result != nullptr) {
    switch (mode) {
      case HA_CLONE_MODE_RESTART:
        result->inc_main_task_ref_count();
        break;
      case HA_CLONE_MODE_ADD_TASK:
        if (!result->new_tasks_allowed()) result = nullptr;
        break;
        // HA_CLONE_MODE_VERSION is passed by the callers that don't have mode
        // themselves to do nothing here.
      case HA_CLONE_MODE_VERSION:
        break;
      case HA_CLONE_MODE_START:
      case HA_CLONE_MODE_MAX:
        assert(0);
    }
  }
  mysql_rwlock_unlock(&m_active_clones_lock);

  if (result == nullptr) return nullptr;

  result->refresh_loc_ptr(loc, loc_len);
  return result;
}

void manager::maybe_end_donor(const myrocks::clone::locator &loc,
                              bool must_be_last_task [[maybe_unused]]) {
  mysql_rwlock_wrlock(&m_active_clones_lock);
  auto active_clones_itr = m_active_clones.find(loc);
  assert(active_clones_itr != m_active_clones.end());
  auto &instance = active_clones_itr->second;
  assert(instance.task_count() == 1 || !must_be_last_task);
  assert(instance.owns_task_id(myrocks::clone::session::m_main_task_id));

  if (instance.dec_main_task_ref_count()) {
    assert(instance.owns_main_task_id_only());
    m_active_clones.erase(active_clones_itr);
  }
  mysql_rwlock_unlock(&m_active_clones_lock);
}

[[nodiscard]] donor *lookup_donor_and_task_id(const myrocks::clone::locator &l,
                                              const uchar *loc, uint loc_len,
                                              uint task_id) {
  auto *const donor_instance =
      manager::instance().lookup_donor(l, loc, loc_len);
  if (donor_instance == nullptr) {
    return nullptr;
  }

  const auto task_id_found = donor_instance->owns_task_id(task_id);
  assert(task_id_found);
  if (!task_id_found) {
    return nullptr;
  }

  return donor_instance;
}

}  // namespace

namespace myrocks {

// Start the donor-side clone session for this storage engine.
// mode can be one of:
// - HA_CLONE_MODE_START: a brand new clone session is starting, create a new
//   locator and a new donor object. Register main task id.
// - HA_CLONE_MODE_RESTART: an ongoing remote clone session was interrupted by a
//   network error and is attempting to reconnect to resume now. Check that the
//   passed locator matches one of the existing donor locators, and resume that
//   donor instance.
// - HA_CLONE_MODE_ADD_TASK: a new worker thread is joining an ongoing clone to
//   copy in parallel. Check that the passed locator matches one of the existing
//   donor locators, and register a new task id for that instance.
int rocksdb_clone_begin(handlerton *, THD *, const uchar *&loc, uint &loc_len,
                        uint &task_id, Ha_clone_type, Ha_clone_mode mode) {
  switch (mode) {
    case HA_CLONE_MODE_START: {
      // For local client, loc == nullptr, for remote client, loc != nullptr
      if ((loc == nullptr) != (loc_len == 0)) {
        return clone::return_error(ER_INTERNAL_ERROR,
                                   "Internal clone plugin error");
      }

      auto *const donor_instance =
          manager::instance().start_donor(loc, loc_len);
      if (donor_instance == nullptr)
        return clone::return_error(ER_CLONE_PROTOCOL, "Invalid clone locator");

      task_id = clone::session::m_main_task_id;
      break;
    }
    case HA_CLONE_MODE_ADD_TASK: {
      const auto search_locator =
          myrocks::clone::locator::deserialize(loc, loc_len);
      if (!search_locator.is_snapshot())
        return clone::return_error(ER_CLONE_PROTOCOL, "Invalid clone locator");

      auto *const donor_instance =
          manager::instance().lookup_donor(search_locator, loc, loc_len, mode);
      if (donor_instance == nullptr) {
        return clone::return_error(ER_INTERNAL_ERROR,
                                   "Attempting to add task to a non-existing "
                                   "(likely already finished) clone");
      }

      assert(!donor_instance->no_tasks());
      assert(donor_instance->get_main_task_ref_count() == 1);
      task_id = donor_instance->register_new_task_id();

      LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                      "MyRocks clone adding task to locator %" PRIu64,
                      search_locator.get_id());
      break;
    }
    case HA_CLONE_MODE_RESTART: {
      const auto restart_locator =
          myrocks::clone::locator::deserialize(loc, loc_len);
      if (!restart_locator.is_snapshot())
        return clone::return_error(ER_CLONE_PROTOCOL, "Invalid clone locator");

      auto *const donor_instance =
          manager::instance().lookup_donor(restart_locator, loc, loc_len, mode);
      if (donor_instance == nullptr) {
        return clone::return_error(ER_CLONE_PROTOCOL, "Invalid clone locator");
      }

      auto local_main_task_ref_count =
          donor_instance->get_main_task_ref_count();
      while (local_main_task_ref_count == 2) {
        usleep(10 * 1000);  // 10ms
        local_main_task_ref_count = donor_instance->get_main_task_ref_count();
      }
      assert(local_main_task_ref_count == 1);
      assert(donor_instance->owns_main_task_id_only());

      if (!donor_instance->restart(restart_locator)) {
        manager::instance().maybe_end_donor(restart_locator, true);
        return clone::return_error(ER_CLONE_PROTOCOL, "Invalid clone locator");
      }
      manager::instance().allow_new_tasks(*donor_instance);

      task_id = clone::session::m_main_task_id;
      LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                      "MyRocks clone restarting session locator %" PRIu64,
                      restart_locator.get_id());
      break;
    }
    case HA_CLONE_MODE_VERSION:
    case HA_CLONE_MODE_MAX:
      assert(false);
      return clone::return_error(ER_INTERNAL_ERROR,
                                 "Internal clone plugin error");
      break;
  }

  LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                  "MyRocks clone begin task ID %u", task_id);

  return HA_EXIT_SUCCESS;
}

// In all threads, initialize the checkpoint if not already done so, send its
// data to the client, roll the checkpoint as necessary. Once done, in the main
// thread only, disable the RocksDB file deletions and prepare the final
// checkpoint for the subsequent copy.
// Returns MySQL error code.
int rocksdb_clone_precopy(handlerton *hton, THD *thd, const uchar *loc,
                          uint loc_len, uint task_id, Ha_clone_cbk *cbk) {
  const auto l = myrocks::clone::locator::deserialize(loc, loc_len);
  assert(l.is_snapshot());
  assert(!l.has_applier_status());
  auto *const donor_instance =
      manager::instance().lookup_donor(l, loc, loc_len);

  cbk->set_hton(hton);

  auto err = donor_instance->init_checkpoint(*cbk);
  if (err != 0) return err;

  err = donor_instance->copy(thd, task_id, *cbk);
  if (err != 0) return err;

  if (task_id != donor::m_main_task_id) return HA_EXIT_SUCCESS;

  err = donor_instance->handle_any_error(thd);
  if (err != 0) return err;

  err = donor_instance->roll_to_final_checkpoint();
  if (err != 0) return err;

  return 0;
}

void rocksdb_clone_set_log_stop(const uchar *loc, uint loc_len,
                                const Json_dom &log_stop_pos) {
  const auto l = myrocks::clone::locator::deserialize(loc, loc_len);
  assert(l.is_snapshot());
  assert(!l.has_applier_status());
  auto *const donor_instance =
      manager::instance().lookup_donor(l, loc, loc_len);

  assert(donor_instance->get_state() == donor::donor_state::FINAL_CHECKPOINT);

  assert(log_stop_pos.json_type() == enum_json_type::J_OBJECT);
  const auto &json_obj = static_cast<const Json_object &>(log_stop_pos);

  static const std::string WAL_files_key_str = "wal_files";
  const auto &wal_json = *json_obj.get(WAL_files_key_str);

  assert(wal_json.json_type() == enum_json_type::J_ARRAY);
  const auto &wal_array = static_cast<const Json_array &>(wal_json);

  for (const auto &log_file_dom : wal_array) {
    assert(log_file_dom->json_type() == enum_json_type::J_OBJECT);
    const auto &log_file_obj = static_cast<const Json_object &>(*log_file_dom);

    static const std::string path_str = "path_name";
    const auto &path_json = *log_file_obj.get(path_str);
    assert(path_json.json_type() == enum_json_type::J_STRING);
    const auto &path_str_json = static_cast<const Json_string &>(path_json);
    const auto &path = path_str_json.value();
    assert(path[0] == '/');
    assert(path.length() > sizeof("/x.log"));
    assert(myrocks::has_file_extension(path, ".log"sv));
    auto log_name = path.substr(1, path.length() - 1);

    static const std::string size_str = "size_file_bytes";
    const auto &size_json = *log_file_obj.get(size_str);
    assert(size_json.json_type() == enum_json_type::J_UINT);
    const auto &size_uint_json = static_cast<const Json_uint &>(size_json);
    const auto size = static_cast<my_off_t>(size_uint_json.value());

    donor_instance->add_log(std::move(log_name), size);
  }

  donor_instance->set_logs_added();
}

// Send the consistent clone data to the client. It comes from 1) the final
// RocksDB checkpoint that is never rolled at this state, 2) WALs from
// performance_schema.log_status query result.
int rocksdb_clone_copy(handlerton *hton, THD *thd, const uchar *loc,
                       uint loc_len, uint task_id, Ha_clone_cbk *cbk) {
  const auto l = myrocks::clone::locator::deserialize(loc, loc_len);
  if (l.has_applier_status())
    return clone::return_error(ER_CLONE_PROTOCOL, "Invalid clone locator");
  auto *const donor_instance =
      lookup_donor_and_task_id(l, loc, loc_len, task_id);
  if (donor_instance == nullptr)
    return clone::return_error(ER_CLONE_PROTOCOL, "Invalid clone locator");

  assert(donor_instance->get_state() ==
         donor::donor_state::FINAL_CHECKPOINT_WITH_LOGS);

  const auto err = donor_instance->handle_any_error(thd);
  if (err != 0) return err;

  cbk->set_hton(hton);
  return donor_instance->copy(thd, task_id, *cbk);
}

// Acknowledge clone application errors that are received from the client. In
// general, this callback is used to acknowledge the client advancing to the
// next clone stage, but this capability is unused in MyRocks clone.
int rocksdb_clone_ack(handlerton *, THD *thd, const uchar *loc, uint loc_len,
                      uint task_id, int in_err, Ha_clone_cbk *) {
  const auto l = myrocks::clone::locator::deserialize(loc, loc_len);
  if (!l.is_valid() || l.has_applier_status())
    return clone::return_error(ER_CLONE_PROTOCOL, "Invalid clone locator");
  auto *const donor_instance =
      lookup_donor_and_task_id(l, loc, loc_len, task_id);
  if (donor_instance == nullptr)
    return clone::return_error(ER_CLONE_PROTOCOL, "Invalid clone locator");

  // ACK is only used to report errors for MyRocks clone
  if (in_err == 0) {
    return donor_instance->save_and_return_error(
        ER_CLONE_PROTOCOL, "Invalid ACK received from client");
  }

  if (thd_killed(thd)) {
    my_error(ER_QUERY_INTERRUPTED, MYF(0));
    in_err = ER_QUERY_INTERRUPTED;
  }

  donor_instance->save_error(in_err);

  return 0;
}

// Finish the donor-side clone session for this storage engine.
int rocksdb_clone_end(handlerton *, THD *thd, const uchar *loc, uint loc_len,
                      uint task_id, int in_err) {
  const auto end_locator = myrocks::clone::locator::deserialize(loc, loc_len);
  if (!end_locator.is_valid() || end_locator.has_applier_status())
    return clone::return_error(ER_CLONE_PROTOCOL, "Invalid clone locator");

  auto *const donor_instance =
      lookup_donor_and_task_id(end_locator, loc, loc_len, task_id);
  if (donor_instance == nullptr) {
    LogPluginErrMsg(
        ERROR_LEVEL, ER_LOG_PRINTF_MSG,
        "MyRocks clone end did not find the session for locator %" PRIu64,
        end_locator.get_id());
    return clone::return_error(ER_CLONE_PROTOCOL, "Invalid clone locator");
  }

  const auto is_main_task_id = clone::session::is_main_task_id(task_id);
  if (is_main_task_id) manager::instance().reject_new_tasks(*donor_instance);

  const auto err = donor_instance->get_error();

  if (thd_killed(thd)) {
    my_error(ER_QUERY_INTERRUPTED, MYF(0));
    in_err = ER_QUERY_INTERRUPTED;
  } else if (err == ER_QUERY_INTERRUPTED) {
    in_err = err;
  }
  donor_instance->save_error(in_err);

  const auto clone_successful = in_err == 0;

  if (clone_successful) {
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "Clone end: locator %" PRIu64 ", task ID: %u, success",
                    end_locator.get_id(), task_id);
  } else {
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "Clone end: locator %" PRIu64
                    ", task ID: %u, failed with code %d: %s",
                    end_locator.get_id(), task_id, in_err,
                    clone::get_da_error_message(*thd));
  }

  if (is_main_task_id) {
    donor_instance->wait_until_one_task();

    const auto reconnect_possible = donor::is_restartable_error(err);
    if (reconnect_possible) {
      donor_instance->wait_for_reconnect();
    }
    manager::instance().maybe_end_donor(end_locator, reconnect_possible);
  } else {
    donor_instance->unregister_task_id(task_id);
  }

  return HA_EXIT_SUCCESS;
}

namespace clone {

void donor_shutdown() { manager::instance().shutdown(); }

}  // namespace clone

}  // namespace myrocks
