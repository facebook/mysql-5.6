/*
   Copyright (c) 2016, Facebook, Inc.

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

/* This C++ file's header */
#include "./rdb_utils.h"

/* C++ standard header files */
#include <array>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

/* C standard header files */
#include <ctype.h>

/* MySQL header files */
#include "m_ctype.h"
#include "my_dir.h"
#include "mysys_err.h"
#include "sql/mysqld.h"

/* MyRocks header files */
#include "./ha_rocksdb.h"

/* RocksDB includes */
#include "rocksdb/env.h"
#include "rocksdb/file_system.h"
#include "rocksdb/io_status.h"

namespace myrocks {

/*
  Skip past any spaces in the input
*/
const char *rdb_skip_spaces(const struct CHARSET_INFO *const cs,
                            const char *str) {
  while (my_isspace(cs, *str)) {
    str++;
  }

  return str;
}

/*
  Compare (ignoring case) to see if str2 is the next data in str1.
  Note that str1 can be longer but we only compare up to the number
  of characters in str2.
*/
bool rdb_compare_strings_ic(const char *const str1, const char *const str2) {
  // Scan through the strings
  size_t ii;
  for (ii = 0; str2[ii]; ii++) {
    if (toupper(static_cast<int>(str1[ii])) !=
        toupper(static_cast<int>(str2[ii]))) {
      return false;
    }
  }

  return true;
}

/*
  Scan through an input string looking for pattern, ignoring case
  and skipping all data enclosed in quotes.
*/
const char *rdb_find_in_string(const char *str, const char *pattern,
                               bool *const succeeded) {
  char quote = '\0';
  bool escape = false;

  *succeeded = false;

  for (; *str; str++) {
    /* If we found a our starting quote character */
    if (*str == quote) {
      /* If it was escaped ignore it */
      if (escape) {
        escape = false;
      }
      /* Otherwise we are now outside of the quoted string */
      else {
        quote = '\0';
      }
    }
    /* Else if we are currently inside a quoted string? */
    else if (quote != '\0') {
      /* If so, check for the escape character */
      escape = !escape && *str == '\\';
    }
    /* Else if we found a quote we are starting a quoted string */
    else if (*str == '"' || *str == '\'' || *str == '`') {
      quote = *str;
    }
    /* Else we are outside of a quoted string - look for our pattern */
    else {
      if (rdb_compare_strings_ic(str, pattern)) {
        *succeeded = true;
        return str;
      }
    }
  }

  // Return the character after the found pattern or the null terminateor
  // if the pattern wasn't found.
  return str;
}

/*
  See if the next valid token matches the specified string
*/
const char *rdb_check_next_token(const struct CHARSET_INFO *const cs,
                                 const char *str, const char *const pattern,
                                 bool *const succeeded) {
  // Move past any spaces
  str = rdb_skip_spaces(cs, str);

  // See if the next characters match the pattern
  if (rdb_compare_strings_ic(str, pattern)) {
    *succeeded = true;
    return str + strlen(pattern);
  }

  *succeeded = false;
  return str;
}

/*
  Parse id
*/
const char *rdb_parse_id(const struct CHARSET_INFO *const cs, const char *str,
                         std::string *const id) {
  // Move past any spaces
  str = rdb_skip_spaces(cs, str);

  if (*str == '\0') {
    return str;
  }

  char quote = '\0';
  if (*str == '`' || *str == '"') {
    quote = *str++;
  }

  size_t len = 0;
  const char *start = str;

  if (quote != '\0') {
    for (;;) {
      if (*str == '\0') {
        return str;
      }

      if (*str == quote) {
        str++;
        if (*str != quote) {
          break;
        }
      }

      str++;
      len++;
    }
  } else {
    while (!my_isspace(cs, *str) && *str != '(' && *str != ')' && *str != '.' &&
           *str != ',' && *str != '\0') {
      str++;
      len++;
    }
  }

  // If the user requested the id create it and return it
  if (id != nullptr) {
    *id = std::string("");
    id->reserve(len);
    while (len--) {
      *id += *start;
      if (*start++ == quote) {
        start++;
      }
    }
  }

  return str;
}

/*
  Skip id
*/
const char *rdb_skip_id(const struct CHARSET_INFO *const cs, const char *str) {
  return rdb_parse_id(cs, str, nullptr);
}

/*
  Parses a given string into tokens (if any) separated by a specific delimiter.
*/
const std::vector<std::string> parse_into_tokens(const std::string &s,
                                                 const char delim) {
  std::vector<std::string> tokens;
  std::string t;
  std::stringstream ss(s);

  while (getline(ss, t, delim)) {
    tokens.push_back(t);
  }

  return tokens;
}

static const std::size_t rdb_hex_bytes_per_char = 2;
static const std::array<char, 16> rdb_hexdigit = {{'0', '1', '2', '3', '4', '5',
                                                   '6', '7', '8', '9', 'a', 'b',
                                                   'c', 'd', 'e', 'f'}};

/*
  Convert data into a hex string with optional maximum length.
  If the data is larger than the maximum length trancate it and append "..".
*/
std::string rdb_hexdump(const char *data, const std::size_t data_len,
                        const std::size_t maxsize) {
  // Count the elements in the string
  std::size_t elems = data_len;
  // Calculate the amount of output needed
  std::size_t len = elems * rdb_hex_bytes_per_char;
  std::string str;

  if (maxsize != 0 && len > maxsize) {
    // If the amount of output is too large adjust the settings
    // and leave room for the ".." at the end
    elems = (maxsize - 2) / rdb_hex_bytes_per_char;
    len = elems * rdb_hex_bytes_per_char + 2;
  }

  // Reserve sufficient space to avoid reallocations
  str.reserve(len);

  // Loop through the input data and build the output string
  for (std::size_t ii = 0; ii < elems; ii++, data++) {
    uint8_t ch = (uint8_t)*data;
    str += rdb_hexdigit[ch >> 4];
    str += rdb_hexdigit[ch & 0x0F];
  }

  // If we can't fit it all add the ".."
  if (elems != data_len) {
    str += "..";
  }

  return str;
}

// Return dir + '/' + file
std::string rdb_concat_paths(std::string_view dir, std::string_view file) {
  std::string result;
  result.reserve(dir.length() + file.length() + 2);
  result = dir;
  result += FN_LIBCHAR;
  result += file;
  return result;
}

/*
  Attempt to access the database subdirectory to see if it exists
*/
bool rdb_database_exists(std::string_view db_name) {
  const auto dir = rdb_concat_paths(mysql_real_data_home, db_name);
  struct MY_DIR *const dir_info =
      my_dir(dir.c_str(), MYF(MY_DONT_SORT | MY_WANT_STAT));
  if (dir_info == nullptr) {
    return false;
  }

  my_dirend(dir_info);
  return true;
}

void rdb_fatal_error(const char *msg) {
  // NO_LINT_DEBUG
  LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG, "%s", msg);
  abort();
}

void rdb_log_status_error(const rocksdb::Status &s, const char *msg) {
  if (msg == nullptr) {
    // NO_LINT_DEBUG
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "RocksDB: status error, code: %d, error message: %s",
                    s.code(), s.ToString().c_str());
    return;
  }

  // NO_LINT_DEBUG
  LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                  "RocksDB: %s, Status Code: %d, Status: %s", msg, s.code(),
                  s.ToString().c_str());
}

bool rdb_has_rocksdb_corruption() {
  rocksdb::DBOptions *rocksdb_db_options = get_rocksdb_db_options();
  assert(rocksdb_db_options->env != nullptr);
  const std::string fileName(myrocks::rdb_corruption_marker_file_name());

  const auto &fs = rocksdb_db_options->env->GetFileSystem();
  rocksdb::IOStatus io_s =
      fs->FileExists(fileName, rocksdb::IOOptions(), nullptr);

  return io_s.ok();
}

void rdb_persist_corruption_marker() {
  rocksdb::DBOptions *rocksdb_db_options = get_rocksdb_db_options();
  assert(rocksdb_db_options->env != nullptr);
  const std::string fileName(myrocks::rdb_corruption_marker_file_name());

  const auto &fs = rocksdb_db_options->env->GetFileSystem();
  std::unique_ptr<rocksdb::FSWritableFile> file;
  rocksdb::IOStatus io_s =
      fs->NewWritableFile(fileName, rocksdb::FileOptions(), &file, nullptr);

  if (!io_s.ok()) {
    // NO_LINT_DEBUG
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "RocksDB: Can't create file %s to mark rocksdb as "
                    "corrupted.",
                    fileName.c_str());
  } else {
    // NO_LINT_DEBUG
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "RocksDB: Creating the file %s to abort mysqld "
                    "restarts. Remove this file from the data directory "
                    "after fixing the corruption to recover. ",
                    fileName.c_str());
  }

  io_s = file->Close(rocksdb::IOOptions(), nullptr);
  if (!io_s.ok()) {
    // NO_LINT_DEBUG
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "RocksDB: Error (%s) closing the file %s",
                    io_s.ToString().c_str(), fileName.c_str());
  }
}

bool for_each_in_dir(const std::string &path, int flags,
                     std::function<bool(const fileinfo &)> fn) {
  auto *const dir_handle =
      my_dir(path.c_str(), MYF(flags | MY_WME | MY_DONT_SORT));

  if (dir_handle == nullptr) {
    if ((flags & MY_FAE) != 0) abort();
    return false;
  }

  for (uint i = 0; i < dir_handle->number_off_files; ++i) {
    const auto &f_info = dir_handle->dir_entry[i];
    if (strcmp(f_info.name, ".") == 0 || strcmp(f_info.name, "..") == 0)
      continue;
    if (!fn(f_info)) {
      my_dirend(dir_handle);
      return false;
    }
  }

  my_dirend(dir_handle);
  return true;
}

void rdb_mkdir_or_abort(const std::string &dir, mode_t mode) {
  const auto err = my_mkdir(dir.c_str(), mode, MYF(MY_WME | MY_FAE));
  if (err != 0) abort();
}

bool rdb_rmdir(const std::string &dir, bool fatal_error) {
  const auto ret = rmdir(dir.c_str());
  if (ret == -1) {
    set_my_errno(errno);
    const auto err = my_errno();
    char errbuf[MYSYS_STRERROR_SIZE];
    LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG,
                    "Failed to remove dir %s, errno = %d (%s)", dir.c_str(),
                    err, my_strerror(errbuf, sizeof(errbuf), err));
    if (fatal_error) abort();
  }
  assert(ret == 0 || !fatal_error);
  return ret == 0;
}

void rdb_file_delete_or_abort(const std::string &path) {
  const auto err = my_delete(path.c_str(), MYF(MY_WME | MY_FAE));
  if (err != 0) abort();
}

void rdb_file_copy_and_delete_or_abort(const std::string &source,
                                       const std::string &dest) {
  const auto err = my_copy(
      source.c_str(), dest.c_str(),
      MYF(MY_WME | MY_FAE | MY_HOLD_ORIGINAL_MODES | MY_DONT_OVERWRITE_FILE));
  if (err != 0) {
    rdb_fatal_error("Failed to copy %s to %s", source.c_str(), dest.c_str());
  }
  rdb_file_delete_or_abort(source);
}

void rdb_path_rename_or_abort(const std::string &source,
                              const std::string &dest) {
  LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                  "Renaming/moving %s to %s", source.c_str(), dest.c_str());
  int err = 0;
  if (!DBUG_EVALUATE_IF("simulate_myrocks_rename_exdev", true, false)) {
    err = my_rename(source.c_str(), dest.c_str(), MYF(0));
  } else {
    err = -1;
    set_my_errno(EXDEV);
  }

  if (err == -1) {
    const auto errn = my_errno();
    if (errn != EXDEV) {
      MyOsError(errn, EE_LINK, MYF(0), source.c_str(), dest.c_str());
      abort();
    }

    LogPluginErrMsg(
        INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
        "Source and destination paths are on different filesystems, reverting "
        "to copy and delete");

    struct stat stat_info;
    if (my_stat(source.c_str(), &stat_info, MYF(MY_WME | MY_FAE)) == nullptr)
      abort();
    if (S_ISDIR(stat_info.st_mode)) {
      rdb_mkdir_or_abort(dest, stat_info.st_mode);
      myrocks::for_each_in_dir(
          source, MY_FAE, [&source, &dest](const fileinfo &f_info) {
            const auto fn = std::string_view{f_info.name};

            const auto old_file = rdb_concat_paths(source, fn);
            const auto new_file = rdb_concat_paths(dest, fn);
            rdb_file_copy_and_delete_or_abort(old_file, new_file);
            return true;
          });
      rdb_rmdir(source, true);
    } else {
      rdb_file_copy_and_delete_or_abort(source, dest);
    }
  }
}

}  // namespace myrocks
