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

#ifndef CLONE_COMMON_H_
#define CLONE_COMMON_H_

#include <sys/types.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_set>

#include "my_inttypes.h"
#include "mysql/plugin.h"
#include "mysql/psi/mysql_cond.h"
#include "mysql/psi/mysql_mutex.h"
#include "mysql/psi/mysql_rwlock.h"
#include "mysqld_error.h"
#include "sql/handler.h"

#include "../rdb_buff.h"
#include "../rdb_psi.h"

namespace myrocks {

namespace clone {

constexpr std::string_view checkpoint_name_prefix = ".clone_checkpoint-";
constexpr char in_place_temp_datadir[] = ".rocksdb.cloned";
constexpr char in_place_temp_wal_dir[] = ".rocksdb.wal.cloned";
constexpr std::string_view in_progress_marker_file = ".clone_in_progress";

constexpr auto dir_mode = S_IRWXU;

constexpr std::uint64_t invalid_file_id =
    std::numeric_limits<std::uint64_t>::max();
constexpr my_off_t invalid_file_offset = std::numeric_limits<my_off_t>::max();
constexpr std::string_view invalid_file_name = "";

[[nodiscard]] std::string checkpoint_base_dir();

[[nodiscard]] bool remove_dir(const std::string &dir_name, bool fatal_error);

void fixup_on_startup();

[[nodiscard]] int return_error(int code, const char *message);

[[nodiscard]] const char *get_da_error_message(const THD &thd);

enum class mode_for_direct_io { DONOR, CLIENT };

[[nodiscard]] bool should_use_direct_io(std::string_view file_name,
                                        enum mode_for_direct_io mode);

enum class metadata_type : std::uint32_t {
  LOCATOR_V1,
  FILE_NAME_MAP_V1,
  FILE_CHUNK_V1,
  ADD_ESTIMATE_V1,
  MAX_VALID_TYPE = ADD_ESTIMATE_V1,
  INVALID
};

// The header starts every metadata packet. Format:
// uint32_t     version         currently constant 1; will be bumped if the
//                              header format will change
// uint32_t     payload_len     the payload length without this header
// uint32_t     type            one of the valid metadata_type enumerators
// This format happens to be identical to InnoDB clone metadata header format at
// the moment but there is no requirement for it to be this way.
//
// This metadata packet is not associated with any data packet.
class [[nodiscard]] metadata_header final {
 public:
  constexpr metadata_header(std::uint32_t payload_len, metadata_type type)
      : m_payload_len{payload_len}, m_type{type} {}

  void serialize(myrocks::Rdb_string_writer &buf) const {
    assert(is_valid());
    assert(buf.is_empty());

    buf.write_uint32(m_version);
    buf.write_uint32(m_payload_len);
    buf.write_uint32(
        static_cast<std::underlying_type_t<decltype(m_type)>>(m_type));

    assert(buf.get_current_pos() == m_length);
  }

  [[nodiscard]] static auto deserialize(Rdb_string_reader &buf) {
    std::uint32_t read_version;
    if (buf.read_uint32(&read_version)) return invalid();
    if (read_version != m_version) return invalid();
    std::uint32_t read_payload_len;
    if (buf.read_uint32(&read_payload_len)) return invalid();
    std::uint32_t read_metadata_type;
    if (buf.read_uint32(&read_metadata_type)) return invalid();
    if (read_metadata_type >
        static_cast<std::uint32_t>(metadata_type::MAX_VALID_TYPE))
      return invalid();

    metadata_header result{read_payload_len,
                           static_cast<metadata_type>(read_metadata_type)};
    assert(result.is_valid());
    return result;
  }

  [[nodiscard]] constexpr auto get_payload_len() const noexcept {
    assert(is_valid());
    return m_payload_len;
  }

  [[nodiscard]] constexpr auto get_type() const noexcept {
    assert(is_valid());
    return m_type;
  }

  [[nodiscard]] static constexpr metadata_header invalid() noexcept {
    return metadata_header{m_invalid_payload_len, metadata_type::INVALID};
  }

  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return m_payload_len != m_invalid_payload_len &&
           m_type <= metadata_type::MAX_VALID_TYPE;
  }

  static constexpr std::size_t m_length = 12;

 private:
  static constexpr std::uint32_t m_version = 1;
  const std::uint32_t m_payload_len;
  const metadata_type m_type;

  static_assert(sizeof(m_version) + sizeof(m_payload_len) + sizeof(m_type) <=
                m_length);

  static constexpr std::uint32_t m_invalid_payload_len =
      std::numeric_limits<std::uint32_t>::max();
};

// LOCATOR_V1: the locator either identifies a MyRocks donor clone session
// uniquely, with an optional current applier state, either prompts the version
// negotiation.
// Format:
// uint64_t     id      If zero, this is a client-created locator for version
//                      negotiation, and the applier state must be absent. If
//                      non-zero, this identifies a single MyRocks clone session
//                      on the donor. The applier state may be present or
//                      absent.
// The applier state consists of a variable number of file state pairs in the
// format below. The existence and number of these pairs is derived from the
// payload_len field in the header.
// uint64_t     file_id         A clone file id as established by a
//                              FILE_NAME_MAP_V1 metadata packet.
// uint64_t     applied_offset  The clone has fully applied file data up to this
//                              offset.
// The locator consists of an ID, which is monotonically increasing for each
// clone session on the donor, starting with one. An ID value of zero is
// reserved for client-side locators that are sent to the server as a part of
// version negotiation.
//
// This metadata packet is not associated with any data packet.
class [[nodiscard]] locator final {
 public:
  // A single file info pair in the applier state
  class [[nodiscard]] file_state final {
   public:
    constexpr file_state(std::uint64_t id, my_off_t applied_offset) noexcept
        : m_id{id}, m_applied_offset{applied_offset} {}

    void serialize(myrocks::Rdb_string_writer &buf) const {
#ifndef NDEBUG
      assert(is_valid());
      const auto pos_before = buf.get_current_pos();
#endif
      buf.write_uint64(m_id);
      buf.write_uint64(m_applied_offset);
      assert(buf.get_current_pos() - pos_before == m_serialized_length);
    }

    [[nodiscard]] static auto deserialize(myrocks::Rdb_string_reader &buf) {
#ifndef NDEBUG
      const auto remaining_before = buf.remaining_bytes();
#endif
      std::uint64_t read_file_id;
      if (buf.read_uint64(&read_file_id)) return invalid();
      std::uint64_t read_applied_offset;
      if (buf.read_uint64(&read_applied_offset)) return invalid();
      assert(remaining_before - buf.remaining_bytes() == m_serialized_length);

      file_state result{read_file_id,
                        static_cast<my_off_t>(read_applied_offset)};
      assert(result.is_valid());
      return result;
    }

    [[nodiscard]] constexpr auto get_id() const noexcept {
      assert(is_valid());
      return m_id;
    }

    [[nodiscard]] constexpr auto get_applied_offset() const noexcept {
      assert(is_valid());
      return m_applied_offset;
    }

    [[nodiscard]] static constexpr file_state invalid() noexcept {
      return {m_invalid_id, invalid_file_offset};
    }

    [[nodiscard]] constexpr bool is_valid() const noexcept {
      return m_id != m_invalid_id && m_applied_offset != invalid_file_offset;
    }

    static constexpr auto m_serialized_length = 16;

   private:
    const std::uint64_t m_id;
    const my_off_t m_applied_offset;

    static_assert(sizeof(m_id) + sizeof(m_applied_offset) <=
                  m_serialized_length);
  };

  locator(const locator &base,
          std::vector<file_state> &&applier_status) noexcept
      : m_id{base.m_id}, m_applier_status{std::move(applier_status)} {
    assert(base.is_snapshot());
  }

  [[nodiscard]] static auto get_unique_locator() noexcept {
    const auto result =
        locator{m_next_id.fetch_add(1, std::memory_order_relaxed)};

    assert(result.is_valid());
    assert(!result.is_version_check());
    assert(result.is_snapshot());
    assert(!result.has_applier_status());

    return result;
  }

  [[nodiscard]] constexpr const auto &get_file_states() const noexcept {
    return m_applier_status;
  }

  [[nodiscard]] static auto invalid() noexcept { return locator{m_invalid_id}; }

  [[nodiscard]] static auto version_check() noexcept {
    return locator{m_version_check_id};
  }

  [[nodiscard]] constexpr bool is_snapshot() const noexcept {
    return is_valid() && !is_version_check();
  }

  [[nodiscard]] constexpr bool is_same_snapshot(
      const locator &other) const noexcept {
    assert(is_snapshot());
    assert(other.is_snapshot());

    return m_id == other.m_id;
  }

  [[nodiscard]] bool has_applier_status() const noexcept {
    return !m_applier_status.empty();
  }

  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return m_id != m_invalid_id;
  }

  [[nodiscard]] constexpr bool is_version_check() const noexcept {
    return m_id == m_version_check_id;
  }

  // Note that this only compares ID and ignores applier status, if any.
  // Provided for std::unordered_map, otherwise better to use other methods most
  // of the time.
  [[nodiscard]] constexpr bool operator==(const locator &other) const noexcept {
    return m_id == other.m_id;
  }

  void serialize(myrocks::Rdb_string_writer &buf) const {
    assert(is_valid());
    assert(buf.is_empty());

    const auto payload_len =
        m_total_fixed_length +
        m_applier_status.size() * file_state::m_serialized_length;

    assert(payload_len < std::numeric_limits<std::uint32_t>::max());
    const metadata_header header{static_cast<std::uint32_t>(payload_len),
                                 metadata_type::LOCATOR_V1};
    header.serialize(buf);
    buf.write_uint64(m_id);
    for (const auto &file : m_applier_status) file.serialize(buf);

    assert(buf.get_current_pos() == payload_len);
  }

  void serialize(myrocks::Rdb_string_writer &buf, const uchar *&loc,
                 uint &loc_len) const {
    serialize(buf);
    loc = buf.ptr();
    loc_len = buf.get_current_pos();
  }

  [[nodiscard]] static locator deserialize(const uchar *loc, uint loc_len) {
    if (loc == nullptr || loc_len < m_total_fixed_length) return invalid();

    auto buf = Rdb_string_reader{loc, loc_len};

    const auto header = metadata_header::deserialize(buf);
    if (!header.is_valid()) return invalid();
    if (header.get_type() != metadata_type::LOCATOR_V1) return invalid();

    const auto payload_len = header.get_payload_len();
    if (payload_len < m_locator_fixed_length) return invalid();

    const auto state_len = payload_len - m_total_fixed_length;
    if ((state_len % file_state::m_serialized_length) != 0) return invalid();
    const auto file_count = state_len / file_state::m_serialized_length;

    std::uint64_t read_id;
    if (buf.read_uint64(&read_id)) return invalid();

    std::vector<file_state> read_applier_status;
    read_applier_status.reserve(file_count);
    for (unsigned i = 0; i < file_count; ++i) {
      auto file_status = file_state::deserialize(buf);
      if (!file_status.is_valid()) return invalid();
      read_applier_status.push_back(std::move(file_status));
    }

    locator result{read_id, std::move(read_applier_status)};
    assert(result.is_valid());
    return result;
  }

  [[nodiscard]] auto get_id() const noexcept { return m_id; }

 private:
  static constexpr std::size_t m_locator_fixed_length = 8;
  static constexpr std::size_t m_total_fixed_length =
      metadata_header::m_length + m_locator_fixed_length;

  static constexpr std::uint64_t m_version_check_id = 0;
  static constexpr std::uint64_t m_invalid_id =
      std::numeric_limits<std::uint64_t>::max();
  static_assert(m_version_check_id != m_invalid_id);

  locator(std::uint64_t id_,
          std::vector<file_state> &&applier_status_ = {}) noexcept
      : m_id{id_}, m_applier_status{std::move(applier_status_)} {}

  std::uint64_t m_id;
  std::vector<file_state> m_applier_status;

  friend struct std::hash<locator>;

  static std::atomic<std::uint64_t> m_next_id;
};

// FILE_NAME_MAP_V1: establish a relation between a file name, its size and id.
// Format:
// uint64_t             id              new file id
// uint64_t             size            the size of this file
// uint32_t             name_length     the length of the file name in the
//                                      packet
// char[name_length]    name            the name of this file
//
// This metadata packet is not associated with any data packet.
class [[nodiscard]] file_metadata {
 public:
  file_metadata(std::uint64_t id, std::string &&name, my_off_t size)
      : m_id{id}, m_size{size}, m_name{std::move(name)} {}

  [[nodiscard]] constexpr std::uint64_t get_id() const noexcept {
    assert(is_valid());
    return m_id;
  }

  [[nodiscard]] constexpr const std::string &get_name() const noexcept {
    assert(is_valid());
    return m_name;
  }

  [[nodiscard]] constexpr std::string &&take_away_name() noexcept {
    return std::move(m_name);
  }

  [[nodiscard]] constexpr my_off_t get_size() const noexcept {
    assert(is_valid());
    return m_size;
  }

  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return m_id != invalid_file_id && m_size != invalid_file_offset &&
           m_name != invalid_file_name;
  }

 private:
  const std::uint64_t m_id;
  const my_off_t m_size;
  std::string m_name;
};

// FILE_CHUNK_V1: metadata for the file chunk in the associated data packet.
// Format:
// uint64_t     id              file id as established in a FILE_NAME_MAP_V1
//                              packet
// uint64_t     chunk_length    file data in the associated data packet length.
class [[nodiscard]] chunk_metadata {
 public:
  constexpr chunk_metadata(std::uint64_t file_id, uint chunk_size) noexcept
      : m_file_id{file_id}, m_size{chunk_size} {}

 public:
  [[nodiscard]] constexpr auto get_file_id() const noexcept {
    assert(is_valid());
    return m_file_id;
  }

  [[nodiscard]] constexpr auto get_size() const noexcept {
    assert(is_valid());
    return m_size;
  }

  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return m_file_id != invalid_file_id && m_size != m_invalid_size;
  }

 protected:
  static constexpr uint m_invalid_size = std::numeric_limits<uint>::max();

 private:
  const std::uint64_t m_file_id;
  const my_off_t m_size;
};

// ADD_ESTIMATE_V1: send the estimate of MyRocks clone data size total. Format:
// uint64_t     estimate_delta  the size in bytes of MyRocks clone data
// It is "delta" because it is added to the already-existing estimate from other
// storage engines.
//
// This metadata packet is not associated with any data packet.
class [[nodiscard]] estimate_metadata {
 public:
  constexpr explicit estimate_metadata(std::uint64_t estimate_delta) noexcept
      : m_estimate_delta{estimate_delta} {}

  [[nodiscard]] constexpr auto get_estimate_delta() const noexcept {
    assert(is_valid());

    return m_estimate_delta;
  }

  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return m_estimate_delta != m_invalid_delta;
  }

 protected:
  static constexpr auto m_invalid_delta =
      std::numeric_limits<std::uint64_t>::max();

 private:
  const std::uint64_t m_estimate_delta;
};

// A base class for both donor and client for task ID set and error handling.
// A task ID is associated with every clone session thread. The main thread ID
// is a constant zero. During restarts there may be two main threads--the old
// one that received a network error, and the new one that is trying to
// reconnect--and they both will have the ID of zero. If their presence has to
// be considered separately, other means rather than ID has to be used.
class [[nodiscard]] session {
 public:
  void shutdown() {
    mysql_rwlock_destroy(&m_task_ids_lock);
    mysql_mutex_destroy(&m_main_task_remaining_mutex);
    mysql_cond_destroy(&m_main_task_remaining_signal);
    mysql_mutex_destroy(&m_error_mutex);
  }

  void register_main_task() {
    assert(no_tasks());
    register_task_id(m_main_task_id);
  }

  [[nodiscard]] auto register_new_task_id() {
    const auto new_task_id =
        m_next_task_id.fetch_add(1, std::memory_order_relaxed);
    register_task_id(new_task_id);
    return new_task_id;
  }

  [[nodiscard]] static constexpr auto is_main_task_id(uint task_id) noexcept {
    return task_id == m_main_task_id;
  }

  [[nodiscard]] auto owns_task_id(uint task_id) const {
    mysql_rwlock_rdlock(&m_task_ids_lock);
    const auto count = m_task_ids.count(task_id);
    mysql_rwlock_unlock(&m_task_ids_lock);
    return count == 1;
  }

  [[nodiscard]] bool no_tasks() const {
    mysql_rwlock_rdlock(&m_task_ids_lock);
    const auto result = m_task_ids.empty();
    mysql_rwlock_unlock(&m_task_ids_lock);
    return result;
  }

#ifndef NDEBUG
  [[nodiscard]] auto owns_main_task_id_only() const {
    mysql_rwlock_rdlock(&m_task_ids_lock);
    const auto result =
        (m_task_ids.size() == 1) && (m_task_ids.count(m_main_task_id) == 1);
    mysql_rwlock_unlock(&m_task_ids_lock);
    return result;
  }
#endif

  void unregister_task_id(uint task_id) {
    mysql_rwlock_wrlock(&m_task_ids_lock);
    const auto remove_result [[maybe_unused]] = m_task_ids.erase(task_id);
    const auto one_remaining = (m_task_ids.size() == 1);
    assert(!one_remaining || m_task_ids.count(m_main_task_id) == 1);
    mysql_rwlock_unlock(&m_task_ids_lock);

    assert(remove_result == 1);

    if (is_main_task_id(task_id)) {
      assert(!one_remaining);
      assert(no_tasks());
    } else if (one_remaining) {
      mysql_cond_signal(&m_main_task_remaining_signal);
    }
  }

  void wait_until_one_task() const {
    auto count = task_count();
    assert(count > 0);
    while (count > 1) {
      timespec ts;
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_sec += 1;
      mysql_mutex_lock(&m_main_task_remaining_mutex);
      mysql_cond_timedwait(&m_main_task_remaining_signal,
                           &m_main_task_remaining_mutex, &ts);
      mysql_mutex_unlock(&m_main_task_remaining_mutex);
      count = task_count();
      assert(count > 0);
    }
    assert(count == 1);
  }

  [[nodiscard]] std::size_t task_count() const {
    mysql_rwlock_rdlock(&m_task_ids_lock);
    const auto result = m_task_ids.size();
    mysql_rwlock_unlock(&m_task_ids_lock);
    return result;
  }

  [[nodiscard]] static auto is_restartable_error(int error) noexcept {
    return error == ER_NET_ERROR_ON_WRITE || error == ER_NET_READ_ERROR ||
           error == ER_NET_WRITE_INTERRUPTED ||
           error == ER_NET_READ_INTERRUPTED || error == ER_NET_WAIT_ERROR;
  }

  [[nodiscard]] int get_error() const noexcept {
    mysql_mutex_lock(&m_error_mutex);
    const auto result = m_error;
    mysql_mutex_unlock(&m_error_mutex);
    return result;
  }

  void reset_error() {
    assert(owns_main_task_id_only() || no_tasks());

    mysql_mutex_lock(&m_error_mutex);
    m_error = 0;
    m_error_path.clear();
    m_saved_errno = 0;
    mysql_mutex_unlock(&m_error_mutex);
  }

  void save_error(int new_error, std::string_view new_error_path = "",
                  int errno_to_save = 0) {
    // Implementation similar to InnoDB Clone_Task_Manager::set_error in
    // clone0clone.h
    if (new_error == 0) return;

    mysql_mutex_lock(&m_error_mutex);
    if (m_error == 0 || is_restartable_error(m_error)) {
      LogPluginErrMsg(
          INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
          "MyRocks clone session setting error %d (file \"%.*s\", errno %d), "
          "previous error %d",
          new_error, static_cast<int>(new_error_path.length()),
          new_error_path.data(), errno_to_save, m_error);
      m_error = new_error;
      if (!new_error_path.empty()) m_error_path = new_error_path;
      if (errno_to_save != 0) m_saved_errno = errno_to_save;
    }
    mysql_mutex_unlock(&m_error_mutex);
  }

  [[nodiscard]] int save_and_return_error(int error, const char *message) {
    save_error(error);
    return return_error(error, message);
  }

  [[nodiscard]] int handle_any_error(const THD *thd) const {
    assert(thd != nullptr);

    mysql_mutex_lock(&m_error_mutex);
    const auto error_copy = m_error;
    const auto error_path_copy = m_error_path;
    const auto errno_copy = m_saved_errno;
    mysql_mutex_unlock(&m_error_mutex);

    if (error_copy != 0) {
      LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                      "MyRocks clone error in another thread: %d", error_copy);
    }

    if (thd_killed(thd)) {
      my_error(ER_QUERY_INTERRUPTED, MYF(0));
      return ER_QUERY_INTERRUPTED;
    }

    switch (error_copy) {
      case 0:
        break;
      case ER_CLONE_DDL_IN_PROGRESS:
      case ER_QUERY_INTERRUPTED:
        // Network errors
      case ER_NET_PACKET_TOO_LARGE:
      case ER_NET_PACKETS_OUT_OF_ORDER:
      case ER_NET_UNCOMPRESS_ERROR:
      case ER_NET_READ_ERROR:
      case ER_NET_READ_INTERRUPTED:
      case ER_NET_ERROR_ON_WRITE:
      case ER_NET_WRITE_INTERRUPTED:
      case ER_NET_WAIT_ERROR:
        my_error(error_copy, MYF(0));
        break;
      case ER_CLONE_PROTOCOL:
      case ER_INTERNAL_ERROR:
        my_error(error_copy, MYF(0), "MyRocks clone error in another thread");
        break;
        // Errors with specific file paths
      case ER_FILE_EXISTS_ERROR:
      case ER_ERROR_ON_WRITE:
      case ER_CANT_OPEN_FILE:
      case ER_FSEEK_FAIL: {
        // Yet the specific path may be empty if i.e. this is an error returned
        // from the client to donor.
        char errbuf[MYSYS_STRERROR_SIZE];
        my_error(error_copy, MYF(0), error_path_copy.c_str(), errno_copy,
                 my_strerror(errbuf, sizeof(errbuf), errno_copy));
        break;
      }
      case ER_CLONE_DONOR:
        /* Will get the error message from remote */
        break;
      default:
        // TODO(laurynas): remove this assert at the end of development, until
        // then, keep on adding more specific cases above as needed. InnoDB
        // handles:
        // - ER_CANT_CREATE_FILE
        // - ER_ERROR_ON_READ
        // - ER_WRONG_VALUE
        assert(0);

        my_error(ER_INTERNAL_ERROR, MYF(0),
                 "MyRocks clone error in another thread");
    }

    return error_copy;
  }

  // The clone plugin always expects the main task ID to be zero, including
  // the case of multiple threads using the same ID at the same time.
  static constexpr auto m_main_task_id = 0;

  session(const session &) = delete;
  session(session &&) = delete;
  session &operator=(const session &) = delete;
  session &operator=(session &&) = delete;

 protected:
  session() {
#ifdef HAVE_PSI_INTERFACE
    mysql_rwlock_init(key_rwlock_clone_task_id_set, &m_task_ids_lock);
    mysql_mutex_init(clone_main_task_remaining_mutex_key,
                     &m_main_task_remaining_mutex, MY_MUTEX_INIT_FAST);
    mysql_cond_init(rdb_signal_clone_main_task_remaining_key,
                    &m_main_task_remaining_signal);
    mysql_mutex_init(clone_error_mutex_key, &m_error_mutex, MY_MUTEX_INIT_FAST);
#else
    mysql_rwlock_init(nullptr, &m_task_ids_lock);
    mysql_mutex_init(nullptr, &m_main_task_remaining_mutex, MY_MUTEX_INIT_FAST);
    mysql_cond_init(nullptr, &m_main_task_remaining_signal);
    mysql_mutex_init(nullptr, &m_error_mutex, MY_MUTEX_INIT_FAST);
#endif
  }

  ~session() = default;

 private:
  void register_task_id(uint task_id) {
    mysql_rwlock_wrlock(&m_task_ids_lock);
    const auto insert_result [[maybe_unused]] = m_task_ids.emplace(task_id);
    mysql_rwlock_unlock(&m_task_ids_lock);

    assert(insert_result.second);
  }

  mutable mysql_rwlock_t m_task_ids_lock;
  std::unordered_set<uint> m_task_ids;
  static std::atomic<uint> m_next_task_id;

  mutable mysql_cond_t m_main_task_remaining_signal;
  mutable mysql_mutex_t m_main_task_remaining_mutex;

  mutable mysql_mutex_t m_error_mutex;
  int m_error = 0;
  std::string m_error_path;
  int m_saved_errno;
};

}  // namespace clone

void rocksdb_clone_get_capability(Ha_clone_flagset &flags) noexcept;

}  // namespace myrocks

template <>
struct std::hash<myrocks::clone::locator> {
  std::size_t operator()(const myrocks::clone::locator &k) const noexcept {
    return std::hash<decltype(k.m_id)>{}(k.m_id);
  }
};

#endif  // CLONE_COMMON_H_
