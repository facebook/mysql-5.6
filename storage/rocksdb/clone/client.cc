/*
  Copyright (C) 2022, Laurynas Biveinis

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

#include "common.h"

#include <sys/types.h>
#include <cstdint>
#include <forward_list>
#include <string_view>
#include <unordered_map>

#include "my_compiler.h"
#include "mysql/psi/mysql_file.h"
#include "mysqld_error.h"
#include "sql/handler.h"
#include "sql/sql_class.h"

#include "common.h"

using namespace std::string_view_literals;

namespace {

[[nodiscard]] int return_file_error(std::string_view message,
                                    std::string_view fn) {
  char errbuf[MYSYS_STRERROR_SIZE];
  const auto errn = my_errno();
  my_strerror(errbuf, sizeof(errbuf), errn);
  // Size good as anything else
  char msgbuf[MYSYS_ERRMSG_SIZE];
  snprintf(msgbuf, sizeof(msgbuf), "%.*s, name %.*s, errno %d (%s)",
           static_cast<int>(message.length()), message.data(),
           static_cast<int>(fn.length()), fn.data(), errn, errbuf);
  return myrocks::clone::return_error(ER_INTERNAL_ERROR, msgbuf);
}

[[nodiscard]] int remove_in_place_temp_dir(const std::string &path) {
  struct stat stat_info;
  if (my_stat(path.c_str(), &stat_info, MYF(0)) != nullptr) {
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "Found in-place clone temp directory %s", path.c_str());
    if (!S_ISDIR(stat_info.st_mode)) {
      // Should never happen: something at the in-place clone dir path found but
      // it's not a dir - do not try do anything about it.
      return myrocks::clone::return_error(
          ER_INTERNAL_ERROR,
          "Incorrect type of stale in-place clone directory");
    }

    const auto success = myrocks::clone::remove_dir(path, false);
    if (!success) {
      return return_file_error("Failed to remove in-place temp clone directory",
                               myrocks::clone::in_place_temp_datadir);
    }
  } else {
    const auto err = my_errno();
    if (err != ENOENT) {
      return return_file_error("Failed to stat in-place clone directory",
                               myrocks::clone::in_place_temp_datadir);
    }
  }

  return HA_EXIT_SUCCESS;
}

[[nodiscard]] bool metadata_buf_valid(
    const char *payload_start_pos, const char *payload_end_pos,
    const myrocks::clone::metadata_header &header) noexcept {
  const auto payload_len = header.get_payload_len();

  if (payload_len < myrocks::clone::metadata_header::m_length) return false;

  return payload_end_pos - payload_start_pos ==
         static_cast<std::ptrdiff_t>(payload_len -
                                     myrocks::clone::metadata_header::m_length);
}

class [[nodiscard]] file_in_progress final {
 public:
  file_in_progress(Ha_clone_file file, my_off_t target_size, std::string &&name)
      : m_file{file}, m_target_size{target_size}, m_name{std::move(name)} {
    assert(m_target_size != myrocks::clone::invalid_file_offset);
    assert(m_name != myrocks::clone::invalid_file_name);
  }

  [[nodiscard]] constexpr auto advance_pos(my_off_t delta) noexcept {
    assert(m_pos < m_target_size);
    m_pos += delta;
    assert(m_pos <= m_target_size);
    return m_pos == m_target_size;
  }

  [[nodiscard]] constexpr auto &get_file() const noexcept {
    assert(m_pos <= m_target_size);
    return m_file;
  }

  [[nodiscard]] constexpr auto get_applied_offset() const noexcept {
    assert(m_pos <= m_target_size);
    return m_pos;
  }

  [[nodiscard]] std::string_view get_name() const noexcept {
    assert(m_pos <= m_target_size);
    return m_name;
  }

 private:
  const Ha_clone_file m_file;
  my_off_t m_pos{0};
  const my_off_t m_target_size;
  const std::string m_name;
};

struct [[nodiscard]] completed_file final {
  constexpr completed_file(std::uint64_t id, my_off_t size) noexcept
      : m_id{id}, m_size{size} {
    assert(m_id != myrocks::clone::invalid_file_id);
    assert(m_size != myrocks::clone::invalid_file_offset);
  }

  const std::uint64_t m_id;
  const my_off_t m_size;
};

class [[nodiscard]] client_file_metadata final
    : public myrocks::clone::file_metadata {
 public:
  [[nodiscard]] static client_file_metadata deserialize(
      myrocks::Rdb_string_reader &buf,
      const myrocks::clone::metadata_header &header);

 private:
  using myrocks::clone::file_metadata::file_metadata;

  [[nodiscard]] static const client_file_metadata &invalid() {
    static const client_file_metadata instance{
        myrocks::clone::invalid_file_id,
        std::string{myrocks::clone::invalid_file_name},
        myrocks::clone::invalid_file_offset,
    };
    return instance;
  }
};

client_file_metadata client_file_metadata::deserialize(
    myrocks::Rdb_string_reader &buf,
    const myrocks::clone::metadata_header &header) {
  assert(header.get_type() == myrocks::clone::metadata_type::FILE_NAME_MAP_V1);
  const auto payload_start_pos = buf.get_current_ptr();

  std::uint64_t read_id;
  if (buf.read_uint64(&read_id)) return invalid();
  std::uint64_t read_size;
  if (buf.read_uint64(&read_size)) return invalid();
  std::uint32_t read_fn_len;
  if (buf.read_uint32(&read_fn_len)) return invalid();
  const auto *const read_fn = buf.read(read_fn_len);
  if (read_fn == nullptr) return invalid();

  const auto payload_end_pos = buf.get_current_ptr();
  if (!metadata_buf_valid(payload_start_pos, payload_end_pos, header))
    return invalid();

  std::string fn{read_fn, read_fn_len};

  client_file_metadata result{read_id, std::move(fn),
                              static_cast<my_off_t>(read_size)};
  assert(result.is_valid());
  return result;
}

class [[nodiscard]] client_chunk_metadata final
    : public myrocks::clone::chunk_metadata {
 public:
  using myrocks::clone::chunk_metadata::chunk_metadata;

  [[nodiscard]] static auto deserialize(
      myrocks::Rdb_string_reader &buf,
      const myrocks::clone::metadata_header &header) {
    assert(header.get_type() == myrocks::clone::metadata_type::FILE_CHUNK_V1);

    const auto payload_start_pos = buf.get_current_ptr();

    std::uint64_t read_file_id;
    if (buf.read_uint64(&read_file_id)) return invalid();
    std::uint64_t read_size;
    if (buf.read_uint64(&read_size)) return invalid();

    const auto payload_end_pos = buf.get_current_ptr();
    if (!metadata_buf_valid(payload_start_pos, payload_end_pos, header))
      return invalid();

    client_chunk_metadata result{read_file_id, static_cast<uint>(read_size)};
    assert(result.is_valid());
    return result;
  }

 private:
  [[nodiscard]] static constexpr client_chunk_metadata invalid() noexcept {
    return client_chunk_metadata{myrocks::clone::invalid_file_id,
                                 m_invalid_size};
  }
};

class [[nodiscard]] client_estimate final
    : public myrocks::clone::estimate_metadata {
 public:
  using myrocks::clone::estimate_metadata::estimate_metadata;

  [[nodiscard]] static auto deserialize(
      myrocks::Rdb_string_reader &buf,
      const myrocks::clone::metadata_header &header) {
    assert(header.get_type() == myrocks::clone::metadata_type::ADD_ESTIMATE_V1);
    const auto payload_start_pos = buf.get_current_ptr();

    std::uint64_t read_estimate_delta;
    if (buf.read_uint64(&read_estimate_delta)) return invalid();

    const auto payload_end_pos = buf.get_current_ptr();
    if (!metadata_buf_valid(payload_start_pos, payload_end_pos, header))
      return invalid();

    client_estimate result{read_estimate_delta};
    assert(result.is_valid());
    return result;
  }

 private:
  [[nodiscard]] static constexpr client_estimate invalid() noexcept {
    return client_estimate{m_invalid_delta};
  }
};

// The singleton MyRocks clone client object. Maintains a mapping from file ids
// to the currently-being-applied files, and a list of completed file ids. The
// latter is used to create a restart locator, if needed.
class [[nodiscard]] client final : public myrocks::clone::session {
 public:
  [[nodiscard]] static client &instance() {
    static client singleton;
    return singleton;
  }

  [[nodiscard]] int init(const uchar *&loc, uint &loc_len,
                         const char *data_dir);

  [[nodiscard]] int restart(const uchar *&loc, uint &loc_len);

  [[nodiscard]] int finish_successful_clone();

  [[nodiscard]] int finish_failed_clone();

  void reset();

  void shutdown();

  [[nodiscard]] const auto &get_locator() const noexcept {
    assert(!m_client_loc.has_applier_status());
    return m_client_loc;
  }

  [[nodiscard]] bool data_dir_matches(const char *data_dir) const noexcept {
    return (data_dir != nullptr) ? (m_root_dir == data_dir)
                                 : (m_root_dir == "./");
  }

  [[nodiscard]] int register_file(myrocks::clone::file_metadata &&new_file,
                                  const THD *thd);

  [[nodiscard]] int apply_chunk(client_chunk_metadata &&chunk,
                                Ha_clone_cbk &cbk, const THD *thd);

  void update_estimate(Ha_clone_cbk &cbk, std::uint64_t estimate_delta);

  void assert_inactive() const noexcept {
#ifndef NDEBUG
    assert(m_files_in_progress.empty());
    assert(m_completed_files.empty());
    assert(m_completed_files_count == 0);
    assert(!m_client_loc.is_valid());
    assert(m_rdb_data_dir.empty());
    assert(m_client_loc_buf.is_empty());
    assert(!m_estimate_updated);
#endif
  }

 private:
  client();
  ~client();

  [[nodiscard]] int save_and_handle_error(const THD *thd, int new_error,
                                          std::string_view new_error_path) {
    save_error(new_error, new_error_path, my_errno());
    return handle_any_error(thd);
  }

  [[nodiscard]] myrocks::clone::locator make_restart_locator() const;

  [[nodiscard]] std::string make_in_progress_marker_path() const {
    return myrocks::rdb_concat_paths(m_rdb_data_dir,
                                     myrocks::clone::in_progress_marker_file);
  }

  mutable mysql_rwlock_t m_files_lock;
  std::unordered_map<std::uint64_t, file_in_progress> m_files_in_progress;
  std::forward_list<completed_file> m_completed_files;
  std::size_t m_completed_files_count{0};

  myrocks::clone::locator m_client_loc{myrocks::clone::locator::invalid()};
  myrocks::Rdb_string_writer m_client_loc_buf;

  std::string m_root_dir;
  std::string m_rdb_data_dir;
  std::string m_rdb_wal_dir;

  bool m_estimate_updated{false};

  static constexpr auto m_clone_file_mode = S_IRUSR | S_IWUSR;
};

int client::init(const uchar *&loc, uint &loc_len, const char *data_dir) {
  assert_inactive();

  m_client_loc = myrocks::clone::locator::deserialize(loc, loc_len);
  if (!m_client_loc.is_snapshot() || m_client_loc.has_applier_status()) {
    return myrocks::clone::return_error(ER_CLONE_PROTOCOL,
                                        "Received invalid clone locator");
  }

  LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                  "MyRocks clone apply task_id = 0 received locator %" PRIu64,
                  m_client_loc.get_id());

  const auto inplace_clone = data_dir == nullptr;

  m_root_dir = std::string{inplace_clone ? "./" : data_dir};

  m_rdb_data_dir = m_root_dir + myrocks::clone::in_place_temp_datadir;
  auto ret = remove_in_place_temp_dir(m_rdb_data_dir);
  if (ret != 0) return ret;

  m_rdb_wal_dir = m_root_dir + myrocks::clone::in_place_temp_wal_dir;
  ret = remove_in_place_temp_dir(m_rdb_wal_dir);
  if (ret != 0) return ret;

  // In the case of non-inplace clone, the parent directories should have been
  // created by innodb_clone_apply_begin
  LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                  inplace_clone ? "Creating %s for in-place clone client"
                                : "Creating %s for clone client",
                  m_rdb_data_dir.c_str());
  auto err =
      my_mkdir(m_rdb_data_dir.c_str(), myrocks::clone::dir_mode, MYF(MY_WME));
  if (err == -1) {
    return return_file_error("Failed to create clone directory",
                             m_rdb_data_dir);
  }

  LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                  inplace_clone ? "Creating %s for in-place clone client"
                                : "Creating %s for clone client",
                  m_rdb_wal_dir.c_str());
  err = my_mkdir(m_rdb_wal_dir.c_str(), myrocks::clone::dir_mode, MYF(MY_WME));
  if (err == -1) {
    return return_file_error("Failed to create clone directory", m_rdb_wal_dir);
  }

  const auto in_progress_marker_path = make_in_progress_marker_path();
  const auto fd = mysql_file_create(
      myrocks::rdb_clone_client_file_key, in_progress_marker_path.c_str(),
      m_clone_file_mode, O_EXCL | O_WRONLY, MYF(MY_WME));
  if (fd < 0) {
    return return_file_error("Failed to create in-progress clone marker file",
                             myrocks::clone::in_progress_marker_file);
  }
  err = mysql_file_close(fd, MYF(MY_WME));
  if (err != 0) {
    return return_file_error("Failed to close in-progress clone marker file",
                             myrocks::clone::in_progress_marker_file);
  }

  register_main_task();

#ifndef NDEBUG
  const auto old_loc_len = loc_len;
  assert(m_client_loc_buf.is_empty());
#endif

  m_client_loc.serialize(m_client_loc_buf, loc, loc_len);
  assert(old_loc_len == loc_len);

  return HA_EXIT_SUCCESS;
}

int client::restart(const uchar *&loc, uint &loc_len) {
  const auto passed_locator =
      myrocks::clone::locator::deserialize(loc, loc_len);
  if (!passed_locator.is_snapshot()) {
    return myrocks::clone::return_error(ER_CLONE_PROTOCOL,
                                        "Received invalid clone locator");
  }
  if (!passed_locator.is_same_snapshot(get_locator())) {
    return myrocks::clone::return_error(ER_CLONE_PROTOCOL,
                                        "Received invalid clone locator");
  }

  LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                  "MyRocks clone apply task_id = 0 for locator %" PRIu64
                  " restarting",
                  passed_locator.get_id());

  reset_error();

  assert(!m_client_loc_buf.is_empty());
  m_client_loc_buf.clear();

  const auto restart_locator = make_restart_locator();
  restart_locator.serialize(m_client_loc_buf, loc, loc_len);

  return HA_EXIT_SUCCESS;
}

int client::finish_failed_clone() {
  int result = 0;
  mysql_rwlock_wrlock(&m_files_lock);
  for (auto &file_in_progress : m_files_in_progress) {
    const auto err = mysql_file_close(
        file_in_progress.second.get_file().file_desc, MYF(MY_WME));
    result = (result != 0) ? result : err;
  }
  m_files_in_progress.clear();
  mysql_rwlock_unlock(&m_files_lock);
  return result;
}

void client::reset() {
  assert(no_tasks());

  mysql_rwlock_rdlock(&m_files_lock);
  assert(m_files_in_progress.empty());
  m_completed_files.clear();
  m_completed_files_count = 0;
  mysql_rwlock_unlock(&m_files_lock);

  m_client_loc = myrocks::clone::locator::invalid();
  m_root_dir.clear();
  m_rdb_data_dir.clear();
  m_rdb_wal_dir.clear();
  m_client_loc_buf.clear();
  m_estimate_updated = false;

  reset_error();
}

void client::update_estimate(Ha_clone_cbk &cbk, std::uint64_t estimate_delta) {
  // Idempotent for restarts
  if (m_estimate_updated) return;

  cbk.add_to_data_size_estimate(estimate_delta);
  m_estimate_updated = true;
}

int client::finish_successful_clone() {
  assert(m_estimate_updated);
  assert(m_files_in_progress.empty());

  const auto in_progress_marker_path = make_in_progress_marker_path();
  const auto ret = my_delete(in_progress_marker_path.c_str(), MYF(MY_WME));
  // Do not the save error at this point – this is the end of successful clone,
  // nothing will pick it up.
  return (ret == 0) ? 0
                    : return_file_error(
                          "Failed to delete in-progress clone marker file",
                          myrocks::clone::in_progress_marker_file);
}

int client::register_file(myrocks::clone::file_metadata &&new_file,
                          const THD *thd) {
  assert(!no_tasks());

  auto &&name = new_file.take_away_name();
  const auto path = myrocks::rdb_concat_paths(
      myrocks::has_file_extension(name, ".log"sv) ? m_rdb_wal_dir
                                                  : m_rdb_data_dir,
      name);

  mysql_rwlock_wrlock(&m_files_lock);
  const auto existing_metadata = m_files_in_progress.find(new_file.get_id());
  if (existing_metadata != m_files_in_progress.cend()) {
    if (existing_metadata->second.get_name() != name) {
      mysql_rwlock_unlock(&m_files_lock);
      return save_and_return_error(
          ER_CLONE_PROTOCOL, "Incorrect file metadata received from donor");
    }
    if (existing_metadata->second.get_applied_offset() > 0) {
      mysql_rwlock_unlock(&m_files_lock);
      return save_and_return_error(
          ER_CLONE_PROTOCOL, "Incorrect file metadata received from donor");
    }
    mysql_rwlock_unlock(&m_files_lock);

  } else {
    auto create_flags = O_TRUNC | O_WRONLY;
    const auto use_direct_io = myrocks::clone::should_use_direct_io(
        name, myrocks::clone::mode_for_direct_io::CLIENT);
#ifndef __APPLE__
    if (use_direct_io) create_flags |= O_DIRECT;
#endif
    const auto fd =
        mysql_file_create(myrocks::rdb_clone_client_file_key, path.c_str(),
                          m_clone_file_mode, create_flags, MYF(MY_WME));
    if (fd < 0) {
      mysql_rwlock_unlock(&m_files_lock);
      return save_and_handle_error(thd, ER_CANT_CREATE_FILE, path);
    }

#ifdef __APPLE__
    if (use_direct_io && fcntl(fd, F_NOCACHE, 1) == -1) {
      mysql_rwlock_unlock(&m_files_lock);
      mysql_file_close(fd, MYF(MY_WME));
      return save_and_return_error(
          ER_INTERNAL_ERROR, "Failed to set fcntl(F_NOCACHE) on a clone file");
    }
#endif

    const Ha_clone_file clone_file{
#ifndef __APPLE__
        use_direct_io,
#else
        false,
#endif
        Ha_clone_file::FILE_DESC,
        {fd}};
    // We could check file names for duplicates, but doing that efficiently
    // would require another data structure.
    const auto insert_result = m_files_in_progress.emplace(
        std::piecewise_construct, std::forward_as_tuple(new_file.get_id()),
        std::forward_as_tuple(clone_file, new_file.get_size(),
                              std::move(name)));
    mysql_rwlock_unlock(&m_files_lock);

    if (!insert_result.second) {
      mysql_file_close(clone_file.file_desc, MYF(MY_WME));
      return save_and_return_error(
          ER_CLONE_PROTOCOL, "Incorrect file metadata received from donor");
    }
  }

  return HA_EXIT_SUCCESS;
}

int client::apply_chunk(client_chunk_metadata &&chunk, Ha_clone_cbk &cbk,
                        const THD *thd) {
  mysql_rwlock_rdlock(&m_files_lock);
  const auto itr = m_files_in_progress.find(chunk.get_file_id());
  if (itr == m_files_in_progress.end()) {
    mysql_rwlock_unlock(&m_files_lock);
    char msgbuf[MYSYS_ERRMSG_SIZE];
    snprintf(msgbuf, sizeof(msgbuf),
             "Incorrect chunk metadata (file id = %" PRIu64 ", size = %" PRIu64
             ") received from the donor",
             chunk.get_file_id(), static_cast<std::uint64_t>(chunk.get_size()));
    return save_and_return_error(ER_CLONE_PROTOCOL, msgbuf);
  }
  auto &file_in_progress = itr->second;
  mysql_rwlock_unlock(&m_files_lock);

  const auto file_handle = file_in_progress.get_file();

  auto err = cbk.apply_file_cbk(file_handle);

  DBUG_EXECUTE_IF("myrocks_clone_apply_fail", {
    err = ER_ERROR_ON_WRITE;
    set_my_errno(ENOSPC);
  });

  if (err != 0)
    return save_and_handle_error(thd, err, file_in_progress.get_name());

  if (file_in_progress.advance_pos(chunk.get_size())) {
    err = mysql_file_close(file_in_progress.get_file().file_desc, MYF(MY_WME));
    if (err != 0)
      return save_and_handle_error(thd, err, file_in_progress.get_name());

    mysql_rwlock_wrlock(&m_files_lock);
    m_completed_files.emplace_front(chunk.get_file_id(),
                                    file_in_progress.get_applied_offset());
    ++m_completed_files_count;
    const auto erase_result = m_files_in_progress.erase(chunk.get_file_id());
    mysql_rwlock_unlock(&m_files_lock);

    if (erase_result != 1) {
      char msgbuf[MYSYS_ERRMSG_SIZE];
      snprintf(msgbuf, sizeof(msgbuf),
               "Invalid metadata received or client state corrupted: the file "
               "structure for chunk metadata (file id = %" PRIu64
               ", size = %" PRIu64 ") can no longer be found",
               chunk.get_file_id(),
               static_cast<std::uint64_t>(chunk.get_size()));
      return save_and_return_error(ER_CLONE_PROTOCOL, msgbuf);
    }
  }

  return HA_EXIT_SUCCESS;
}

client::client() {
#ifdef HAVE_PSI_INTERFACE
  mysql_rwlock_init(myrocks::key_rwlock_clone_client_files, &m_files_lock);
#else
  mysql_rwlock_init(nullptr, &m_files_lock);
#endif
}

void client::shutdown() {
  assert(no_tasks());
  assert_inactive();
  mysql_rwlock_destroy(&m_files_lock);
  session::shutdown();
}

client::~client() { assert_inactive(); }

myrocks::clone::locator client::make_restart_locator() const {
  std::vector<myrocks::clone::locator::file_state> file_states;

  mysql_rwlock_rdlock(&m_files_lock);
  file_states.reserve(m_completed_files_count + m_files_in_progress.size());
  for (const auto &file : m_completed_files)
    file_states.emplace_back(file.m_id, file.m_size);
  for (const auto &file : m_files_in_progress)
    file_states.emplace_back(file.first, file.second.get_applied_offset());
  mysql_rwlock_unlock(&m_files_lock);

  return {m_client_loc, std::move(file_states)};
}

[[nodiscard]] myrocks::Rdb_string_writer make_version_locator_buf() {
  static myrocks::Rdb_string_writer buf;
  static auto version_check_locator = myrocks::clone::locator::version_check();
  version_check_locator.serialize(buf);
  return buf;
}

}  // namespace

namespace myrocks {

// Start the client-side clone session for this storage engine.
// mode can be one of:
// - HA_CLONE_MODE_START: a brand new clone session is starting. Store the
//   locator for later validation, create the target directories, and register
//   the main task ID.
// - HA_CLONE_MODE_RESTART: an ongoing remote clone session was interrupted by a
//   network error and is attempting to reconnect to resume now. Serialize the
//   information about how much data has been written to each cloned file and
//   send that as the restart locator to the donor. Register the main task ID.
// - HA_CLONE_MODE_ADD_TASK: a new worker thread is joining an ongoing clone to
//   copy in parallel. Verify the passed locator and register a new task ID.
// - HA_CLONE_MODE_VERSION: the client is asked to send its supported clone
//   version information for a remote clone session
int rocksdb_clone_apply_begin(handlerton *, THD *, const uchar *&loc,
                              uint &loc_len, uint &task_id, Ha_clone_mode mode,
                              const char *data_dir) {
  switch (mode) {
    case HA_CLONE_MODE_START: {
      const auto err = client::instance().init(loc, loc_len, data_dir);
      if (err != 0) return err;

      task_id = myrocks::clone::session::m_main_task_id;
      break;
    }
    case HA_CLONE_MODE_ADD_TASK: {
      const auto passed_locator = clone::locator::deserialize(loc, loc_len);
      if (!passed_locator.is_snapshot()) {
        return clone::return_error(ER_CLONE_PROTOCOL,
                                   "Received invalid clone locator");
      }
      if (!passed_locator.is_same_snapshot(client::instance().get_locator())) {
        return clone::return_error(ER_CLONE_PROTOCOL,
                                   "Received invalid clone locator");
      }

      task_id = client::instance().register_new_task_id();
      LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                      "MyRocks clone application for locator %" PRIu64
                      " begin task ID %u\n",
                      passed_locator.get_id(), task_id);
      break;
    }
    case HA_CLONE_MODE_RESTART: {
      const auto is_main = clone::session::is_main_task_id(task_id);
      assert(is_main);
      if (!is_main) {
        return clone::return_error(ER_INTERNAL_ERROR,
                                   "Internal clone plugin error");
      }

      const auto data_dir_matches =
          client::instance().data_dir_matches(data_dir);
      assert(data_dir_matches);
      if (!data_dir_matches) {
        return clone::return_error(ER_INTERNAL_ERROR,
                                   "Internal clone plugin error");
      }

      const auto err = client::instance().restart(loc, loc_len);
      if (err != 0) return err;
      break;
    }
    case HA_CLONE_MODE_VERSION:
      client::instance().assert_inactive();
      assert(loc == nullptr);
      assert(loc_len == 0);

      static const auto version_loc_buf = make_version_locator_buf();
      loc = version_loc_buf.ptr();
      loc_len = version_loc_buf.get_current_pos();
      break;
    case HA_CLONE_MODE_MAX:
      return clone::return_error(ER_INTERNAL_ERROR,
                                 "Internal clone plugin error");
  }

  return HA_EXIT_SUCCESS;
}

// Deserialize the received metadata packet and handle it:
// - FILE_NAME_MAP_V1: create a file with a given name and ID, save its fd and
//   target size in the in-progress file map.
// - FILE_CHUNK_V1: use the clone callback to apply the next file chunk to the
//   target file. Check if the target size has been reached, in that case close
//   it and move it to the completed file set.
// - ADD_ESTIMATE_V1: pass through the MyRocks clone data size to the plugin.
int rocksdb_clone_apply(handlerton *, THD *thd, const uchar *loc, uint loc_len,
                        uint task_id, int in_err, Ha_clone_cbk *cbk) {
  assert(cbk != nullptr || in_err != 0);

  client::instance().save_error(in_err);

  if (in_err != 0) {
    assert(cbk == nullptr);
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "MyRocks clone apply set error code %d: %s", in_err,
                    clone::get_da_error_message(*thd));
    return HA_EXIT_SUCCESS;
  }

  assert(cbk != nullptr);

  const auto err = client::instance().handle_any_error(thd);
  if (err != 0) {
    return err;
  }

  const auto passed_locator = clone::locator::deserialize(loc, loc_len);
  if (!passed_locator.is_same_snapshot(client::instance().get_locator())) {
    return client::instance().save_and_return_error(
        ER_CLONE_PROTOCOL, "Received invalid clone locator");
  }

  const auto task_id_found = client::instance().owns_task_id(task_id);
  if (!task_id_found) {
    return client::instance().save_and_return_error(
        ER_INTERNAL_ERROR, "Internal clone plugin error");
  }

  uint data_desc_len;
  const auto *const data_desc = cbk->get_data_desc(&data_desc_len);
  auto buf = myrocks::Rdb_string_reader{data_desc, data_desc_len};
  const auto header = clone::metadata_header::deserialize(buf);
  if (!header.is_valid()) {
    return client::instance().save_and_return_error(
        ER_CLONE_PROTOCOL, "Received invalid clone data descriptor");
  }

  switch (header.get_type()) {
    case clone::metadata_type::ADD_ESTIMATE_V1: {
      const auto estimate_delta = client_estimate::deserialize(buf, header);
      if (!estimate_delta.is_valid()) {
        return client::instance().save_and_return_error(
            ER_CLONE_PROTOCOL, "Received invalid estimate packet");
      }
      client::instance().update_estimate(*cbk,
                                         estimate_delta.get_estimate_delta());
      return HA_EXIT_SUCCESS;
    }
    case clone::metadata_type::FILE_NAME_MAP_V1: {
      auto file_metadata = client_file_metadata::deserialize(buf, header);
      if (!file_metadata.is_valid()) {
        return client::instance().save_and_return_error(
            ER_CLONE_PROTOCOL, "Received invalid file map packet");
      }
      return client::instance().register_file(std::move(file_metadata), thd);
    }
    case clone::metadata_type::FILE_CHUNK_V1: {
      auto file_chunk = client_chunk_metadata::deserialize(buf, header);
      if (!file_chunk.is_valid()) {
        return client::instance().save_and_return_error(
            ER_CLONE_PROTOCOL, "Received invalid file chunk packet");
      }
      return client::instance().apply_chunk(std::move(file_chunk), *cbk, thd);
      break;
    }
    case clone::metadata_type::LOCATOR_V1:
    case clone::metadata_type::INVALID:
      return client::instance().save_and_return_error(
          ER_CLONE_PROTOCOL, "Received invalid clone data descriptor");
  }

  MY_ASSERT_UNREACHABLE();
}

// Finish the client-side clone session for this storage engine.
int rocksdb_clone_apply_end(handlerton *, THD *thd, const uchar *loc,
                            uint loc_len, uint task_id, int in_err) {
  if (client::instance().no_tasks()) {
    // The end of COM_VERSION, no actual application happened
    client::instance().assert_inactive();

    const auto main_task = clone::session::is_main_task_id(task_id);
    assert(main_task);
    if (!main_task) {
      return clone::return_error(ER_INTERNAL_ERROR,
                                 "Internal clone plugin error");
    }
    return HA_EXIT_SUCCESS;
  }

  if (thd_killed(thd)) {
    my_error(ER_QUERY_INTERRUPTED, MYF(0));
    in_err = ER_QUERY_INTERRUPTED;
  }

  client::instance().save_error(in_err);

  const auto clone_successful = in_err == 0;

  const auto passed_locator = clone::locator::deserialize(loc, loc_len);
  if (!passed_locator.is_same_snapshot(client::instance().get_locator())) {
    // This should never happen, but if it does, should finish_failed_clone be
    // called?
    return clone::return_error(ER_CLONE_PROTOCOL,
                               "Received invalid clone locator");
  }

  if (clone::session::is_main_task_id(task_id)) {
    client::instance().wait_until_one_task();
  }

  client::instance().unregister_task_id(task_id);

  if (clone_successful) {
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "MyRocks clone application locator %" PRIu64
                    " end task ID %u, success\n",
                    passed_locator.get_id(), task_id);
  } else {
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "MyRocks clone application locator %" PRIu64
                    " end: task ID: %u, failed with code %d: %s",
                    passed_locator.get_id(), task_id, in_err,
                    clone::get_da_error_message(*thd));
  }

  if (clone::session::is_main_task_id(task_id)) {
    const auto ret = clone_successful
                         ? client::instance().finish_successful_clone()
                         : client::instance().finish_failed_clone();
    client::instance().reset();
    return ret;
  }

  return HA_EXIT_SUCCESS;
}

namespace clone {

void client_shutdown() { client::instance().shutdown(); }

}  // namespace clone

}  // namespace myrocks
