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

#include <atomic>
#include <cstdint>
#include <cstring>

#include "my_compiler.h"
#include "my_dir.h"
#include "my_sys.h"

#include "sql/sql_class.h"

#include "../ha_rocksdb.h"

namespace {

constexpr char in_place_old_datadir[] = ".rocksdb.saved";
constexpr char old_wal_suffix[] = ".saved";

// Must be kept in sync with CLONE_OTHER_ENGINES_MYROCKS_ROLLBACK_FILE in
// storage/innobase/include/clone0clone.h.
constexpr char force_rollback_marker[] = "#clone/#force_other_engines_rollback";

void mkdir_or_abort(const std::string &dir) {
  const auto err =
      my_mkdir(dir.c_str(), myrocks::clone::dir_mode, MYF(MY_WME | MY_FAE));
  if (err != 0) abort();
}

[[nodiscard]] bool is_dir_empty(const std::string &dir) {
  auto *const dir_handle =
      my_dir(dir.c_str(), MYF(MY_DONT_SORT | MY_WME | MY_FAE));
  if (dir_handle == nullptr) abort();
  const auto file_count = dir_handle->number_off_files;
  my_dirend(dir_handle);
  return file_count == 2;
}

[[nodiscard]] bool temp_dir_exists_abort_if_not_dir(const std::string &path) {
  struct stat stat_info;
  if (my_stat(path.c_str(), &stat_info, MYF(0)) == nullptr) return false;

  if (!S_ISDIR(stat_info.st_mode)) {
    myrocks::rdb_fatal_error("Temp path %s exists but is not a directory",
                             path.c_str());
  }

  return true;
}

void remove_temp_dir(const std::string &dir) {
  if (!temp_dir_exists_abort_if_not_dir(dir)) return;
  const auto success [[maybe_unused]] = myrocks::clone::remove_dir(dir, true);
  assert(success);
}

[[nodiscard]] bool path_exists(const std::string &path) {
  struct stat stat_info;
  return (my_stat(path.c_str(), &stat_info, MYF(0)) != nullptr);
}

void move_temp_dir_to_destination(const std::string &temp,
                                  const std::string &old,
                                  const std::string &dest, bool empty_allowed) {
  if (!temp_dir_exists_abort_if_not_dir(temp)) return;
  LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                  "Found in-place clone temp dir %s", temp.c_str());

  if (path_exists(old)) {
    myrocks::rdb_fatal_error("Found in-place clone saved old data dir %s",
                             old.c_str());
  }

  const auto temp_dir_marker_path =
      myrocks::rdb_concat_paths(temp, myrocks::clone::in_progress_marker_file);
  if (path_exists(temp_dir_marker_path)) {
    myrocks::rdb_fatal_error("Found in-place clone in-progress marker file %s",
                             temp_dir_marker_path.c_str());
  }

  if (!empty_allowed && is_dir_empty(temp)) {
    myrocks::rdb_fatal_error("In-place clone temp directory %s is empty",
                             temp.c_str());
  }

  const auto dest_exists = path_exists(dest);
  if (dest_exists) myrocks::rdb_path_rename_or_abort(dest, old);

  myrocks::rdb_path_rename_or_abort(temp, dest);

  if (dest_exists) {
    const auto success [[maybe_unused]] =
        myrocks::clone::remove_dir(old.c_str(), true);
    assert(success);
  }
}

// More involved than move_temp_dir_to_destination when cannot do a
// directory-level move because we may not necessarily have the permissions
// for the target directory.
void move_temp_dir_contents_to_dest(const std::string &temp,
                                    const std::string &dest) {
  const auto temp_exists = path_exists(temp);
  if (temp_exists) {
    const auto dest_existed = path_exists(dest);
    if (!dest_existed) {
      LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                      "Directory %s not found, creating", dest.c_str());
      mkdir_or_abort(dest);
    }
    // Pass 1: rename old files in the destination directory
    if (dest_existed) {
      myrocks::for_each_in_dir(dest, MY_FAE, [&dest](const fileinfo &f_info) {
        if (myrocks::has_file_extension(f_info.name, old_wal_suffix))
          myrocks::rdb_fatal_error("MyRocks clone fixup temp file %s found",
                                   f_info.name);

        const auto old_path = myrocks::rdb_concat_paths(dest, f_info.name);
        const auto saved_old_path = old_path + old_wal_suffix;
        if (my_rename(old_path.c_str(), saved_old_path.c_str(),
                      MYF(MY_WME | MY_FAE))) {
          myrocks::rdb_fatal_error("Failed to rename %s to %s",
                                   old_path.c_str(), saved_old_path.c_str());
        }
        return true;
      });
    }
    // Pass 2: move new files to the destination
    myrocks::for_each_in_dir(
        temp, MY_FAE, [&temp, &dest](const fileinfo &f_info) {
          const auto old_path = myrocks::rdb_concat_paths(temp, f_info.name);
          const auto new_path = myrocks::rdb_concat_paths(dest, f_info.name);
          myrocks::rdb_path_rename_or_abort(old_path, new_path);
          return true;
        });
    myrocks::rdb_rmdir(temp, true);
    if (dest_existed) {
      // Pass 3: remove old files
      myrocks::for_each_in_dir(dest, MY_FAE, [&dest](const fileinfo &f_info) {
        if (!myrocks::has_file_extension(f_info.name, old_wal_suffix))
          return true;
        const auto saved_old_path =
            myrocks::rdb_concat_paths(dest, f_info.name);
        myrocks::rdb_file_delete_or_abort(saved_old_path);
        return true;
      });
    }
  }
}

}  // namespace

namespace myrocks {

namespace clone {

std::atomic<std::uint64_t> locator::m_next_id{1};

std::atomic<uint> session::m_next_task_id{session::m_main_task_id + 1};

std::string checkpoint_base_dir() { return std::string{rocksdb_datadir}; }

[[nodiscard]] bool remove_dir(const std::string &dir, bool fatal_error) {
  LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "Removing %s",
                  dir.c_str());
  const auto fatal_flag = fatal_error ? MY_FAE : 0;
  if (!for_each_in_dir(
          dir, fatal_flag, [fatal_flag, &dir](const fileinfo &f_info) {
            const auto fn = rdb_concat_paths(dir, f_info.name);
            const auto ret = my_delete(fn.c_str(), MYF(MY_WME | fatal_flag));
            assert(ret == 0 || fatal_flag == 0);
            return ret == 0;
          }))
    return false;

  return rdb_rmdir(dir, fatal_error);
}

// Perform MyRocks clone fixup on the instance startup:
// - check whether InnoDB clone fixup has aborted the clone, do the same if so
//   by removing the temporary cloned data directories
// - check if the current RocksDB data directory has the in-progress clone
//   marker. That would mean an instance startup attempt on a half-cloned data,
//   thus abort.
// - move the cloned data and WAL directories to their right places, with an
//   intermediate step of saving any pre-existing data there, which is deleted
//   after a successful move.
// - delete any clone checkpoint directories found. This cleans up donor-side
//   after e.g. a crash.
void fixup_on_startup() {
  struct stat stat_info;

  if (my_stat(force_rollback_marker, &stat_info, MYF(0)) != nullptr) {
    LogPluginErrMsg(
        INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
        "Found %s, rolling back MyRocks clone due to InnoDB clone rollback",
        force_rollback_marker);

    if (!S_ISREG(stat_info.st_mode)) {
      rdb_fatal_error("MyRocks clone marker %s is not a regular file",
                      force_rollback_marker);
    }

    remove_temp_dir(in_place_temp_datadir);
    remove_temp_dir(in_place_temp_wal_dir);

    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG, "Removing %s",
                    force_rollback_marker);
    myrocks::rdb_file_delete_or_abort(force_rollback_marker);
  }

  const auto current_datadir_in_progress_marker_path =
      rdb_concat_paths(rocksdb_datadir, in_progress_marker_file);
  if (path_exists(current_datadir_in_progress_marker_path))
    rdb_fatal_error("In-progress clone marker found in the MyRocks datadir");

  move_temp_dir_to_destination(in_place_temp_datadir, in_place_old_datadir,
                               rocksdb_datadir, false);

  if (is_wal_dir_separate()) {
    move_temp_dir_contents_to_dest(in_place_temp_wal_dir, rocksdb_wal_dir);
  } else if (path_exists(in_place_temp_wal_dir)) {
    for_each_in_dir(in_place_temp_wal_dir, MY_FAE, [](const fileinfo &f_info) {
      const auto old_log_path =
          rdb_concat_paths(in_place_temp_wal_dir, f_info.name);
      const auto new_log_path = rdb_concat_paths(rocksdb_datadir, f_info.name);
      myrocks::rdb_path_rename_or_abort(old_log_path, new_log_path);
      return true;
    });
    rdb_rmdir(in_place_temp_wal_dir, true);
  }

  const auto checkpoint_base_dir_str = checkpoint_base_dir();
  auto *const dir = ::my_dir(checkpoint_base_dir_str.c_str(),
                             MYF(MY_WANT_STAT | MY_DONT_SORT));
  if (dir == nullptr) {
    const auto errno_copy = my_errno();
    if (errno_copy != ENOENT) {
      rdb_fatal_error("Failed to open directory %s, errno %d",
                      checkpoint_base_dir().c_str(), errno_copy);
    }
    return;
  }

  for (uint i = 0; i < dir->number_off_files; ++i) {
    const auto &f_info = dir->dir_entry[i];
    if (!S_ISDIR(f_info.mystat->st_mode) ||
        strncmp(f_info.name, checkpoint_name_prefix,
                sizeof(checkpoint_name_prefix) - 1) != 0)
      continue;
    const auto checkpoint_dir_path =
        rdb_concat_paths(checkpoint_base_dir_str, f_info.name);
    const auto success [[maybe_unused]] = remove_dir(checkpoint_dir_path, true);
    assert(success);
  }
  my_dirend(dir);
}

[[nodiscard]] int return_error(int code, const char *message) {
  my_error(code, MYF(0), message);
  return code;
}

[[nodiscard]] const char *get_da_error_message(const THD &thd) {
  const auto *const da = thd.get_stmt_da();
  return (da != nullptr && da->is_error()) ? da->message_text() : "";
}

[[nodiscard]] bool should_use_direct_io(const std::string &file_name,
                                        enum mode_for_direct_io mode) {
  const auto is_sst = has_file_extension(file_name, ".sst");
  if (!is_sst) return false;

  const auto &rdb_opts = *myrocks::get_rocksdb_db_options();
  switch (mode) {
    case mode_for_direct_io::DONOR:
      return rdb_opts.use_direct_reads;
    case mode_for_direct_io::CLIENT:
      return rdb_opts.use_direct_io_for_flush_and_compaction;
  }

  MY_ASSERT_UNREACHABLE();
}

}  // namespace clone

// Return the capabilities supported by MyRocks clone. For the purposes of
// MySQL, it supports all the possible capabilities, same as InnoDB.
void rocksdb_clone_get_capability(Ha_clone_flagset &flags) noexcept {
  flags.reset();
  // This flag is supposed to indicate that the SE uses both page tracking and
  // redo log archiving. Neither of these apply for MyRocks, nevertheless set
  // the maximum capability flag: 1) to be most compatible with InnoDB, 2) the
  // other options are not used in MySQL, thus poorly supported.
  flags.set(HA_CLONE_HYBRID);
  flags.set(HA_CLONE_MULTI_TASK);
  flags.set(HA_CLONE_RESTART);
}

}  // namespace myrocks
