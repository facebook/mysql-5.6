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
#pragma once

/* C++ standard header files */
#include <chrono>
#include <string>
#include <string_view>
#include <vector>

/* MySQL header files */
#include "./my_stacktrace.h"
#include "./sql_string.h"
#include "my_compiler.h"
#define LOG_COMPONENT_TAG "rocksdb"
#include "mysql/components/services/log_builtins.h"
#include "mysqld_error.h"

/* RocksDB header files */
#include "rocksdb/slice.h"
#include "rocksdb/status.h"

/* MyRocks header files */
#include "./rdb_global.h"

#ifdef HAVE_JEMALLOC
#include <jemalloc/jemalloc.h>
#endif

struct fileinfo;

namespace myrocks {

/*
  Guess what?
  An interface is a class where all members are public by default.
*/

#ifndef interface
#define interface struct
#endif  // interface

/*
  Introduce C-style pseudo-namespaces, a handy way to make code more readble
  when calling into a legacy API, which does not have any namespace defined.
  Since we cannot or don't want to change the API in any way, we can use this
  mechanism to define readability tokens that look like C++ namespaces, but are
  not enforced in any way by the compiler, since the pre-compiler strips them
  out. However, on the calling side, code looks like my_core::thd_ha_data()
  rather than plain a thd_ha_data() call. This technique adds an immediate
  visible cue on what type of API we are calling into.
*/

#ifndef my_core
// C-style pseudo-namespace for MySQL Core API, to be used in decorating calls
// to non-obvious MySQL functions, like the ones that do not start with well
// known prefixes: "my_", "sql_", and "mysql_".
#define my_core
#endif  // my_core

/*
  The intent behind a SHIP_ASSERT() macro is to have a mechanism for validating
  invariants in retail builds. Traditionally assertions (such as macros defined
  in <cassert>) are evaluated for performance reasons only in debug builds and
  become NOOP in retail builds when NDEBUG is defined.

  This macro is intended to validate the invariants which are critical for
  making sure that data corruption and data loss won't take place. Proper
  intended usage can be described as "If a particular condition is not true then
  stop everything what's going on and terminate the process because continued
  execution will cause really bad things to happen".

  Use the power of SHIP_ASSERT() wisely.
*/

#ifndef SHIP_ASSERT
#define SHIP_ASSERT(expr)                                              \
  do {                                                                 \
    if (!(expr)) {                                                     \
      my_safe_printf_stderr("\nShip assert failure: \'%s\'\n", #expr); \
      abort();                                                         \
    }                                                                  \
  } while (0)
#endif  // SHIP_ASSERT

/*
  Assert a implies b.
  If a is true, then b must be true.
  If a is false, then the value is b does not matter.
*/
#ifndef assert_IMP
#define assert_IMP(a, b) assert(!(a) || (b))
#endif

/*
  Assert a if and only if b.
  a and b must be both true or both false.
*/
#ifndef assert_IFF
#define assert_IFF(a, b) assert(static_cast<bool>(a) == static_cast<bool>(b))
#endif

/*
  Intent behind this macro is to avoid manually typing the function name every
  time we want to add the debugging statement and use the compiler for this
  work. This avoids typical refactoring problems when one renames a function,
  but the tracing message doesn't get updated.

  We could use __func__ or __FUNCTION__ macros, but __PRETTY_FUNCTION__
  contains the signature of the function as well as its bare name and provides
  therefore more context when interpreting the logs.
*/
#define DBUG_ENTER_FUNC() DBUG_ENTER(__PRETTY_FUNCTION__)

/*
  Error handling pattern used across MySQL abides by the following rules: "All
  functions that can report an error (usually an allocation error), should
  return 0/false/false on success, 1/true/true on failure."

  https://dev.mysql.com/doc/internals/en/additional-suggestions.html has more
  details.

  To increase the comprehension and readability of MyRocks codebase we'll use
  constants similar to ones from C standard (EXIT_SUCCESS and EXIT_FAILURE) to
  make sure that both failure and success paths are clearly identifiable.
*/
#define HA_EXIT_SUCCESS 0
#define HA_EXIT_FAILURE 1

/*
  Macros to better convey the intent behind checking the results from locking
  and unlocking mutexes.
*/
#define RDB_MUTEX_LOCK_CHECK(m) \
  rdb_check_mutex_call_result(__PRETTY_FUNCTION__, true, mysql_mutex_lock(&m))
#define RDB_MUTEX_UNLOCK_CHECK(m)                         \
  rdb_check_mutex_call_result(__PRETTY_FUNCTION__, false, \
                              mysql_mutex_unlock(&m))

// The default limit for rdb_hexdump output length, happens to fit the longest
// possible keys (16K)
constexpr size_t RDB_MAX_HEXDUMP_LEN = 2 * 16 * 1024;

/*
  Helper function to get an NULL terminated uchar* out of a given MySQL String.
*/

inline uchar *rdb_mysql_str_to_uchar_str(my_core::String *str) {
  assert(str != nullptr);
  return reinterpret_cast<uchar *>(str->c_ptr());
}

/*
  Helper function to get plain (not necessary NULL terminated) uchar* out of a
  given STL string.
*/

inline const uchar *rdb_std_str_to_uchar_ptr(const std::string &str) {
  return reinterpret_cast<const uchar *>(str.data());
}

/*
  Helper function to convert seconds to milliseconds.
*/

constexpr int rdb_convert_sec_to_ms(int sec) {
  return std::chrono::milliseconds(std::chrono::seconds(sec)).count();
}

/*
  Helper function to get plain (not necessary NULL terminated) uchar* out of a
  given RocksDB item.
*/

inline const uchar *rdb_slice_to_uchar_ptr(const rocksdb::Slice *item) {
  assert(item != nullptr);
  return reinterpret_cast<const uchar *>(item->data());
}

/*
  Call this function in cases when you can't rely on garbage collector and need
  to explicitly purge all unused dirty pages. This should be a relatively rare
  scenario for cases where it has been verified that this intervention has
  noticeable benefits.
*/
inline int purge_all_jemalloc_arenas() {
#ifdef HAVE_JEMALLOC
  unsigned narenas = 0;
  size_t sz = sizeof(unsigned);
  char name[25] = {0};

  // Get the number of arenas first. Please see `jemalloc` documentation for
  // all the various options.
  int result = mallctl("arenas.narenas", &narenas, &sz, nullptr, 0);

  // `mallctl` returns 0 on success and we really want caller to know if all the
  // trickery actually works.
  if (result) {
    return result;
  }

  // Form the command to be passed to `mallctl` and purge all the unused dirty
  // pages.
  snprintf(name, sizeof(name) / sizeof(char), "arena.%d.purge", narenas);
  result = mallctl(name, nullptr, nullptr, nullptr, 0);

  return result;
#else
  return EXIT_SUCCESS;
#endif
}

MY_ATTRIBUTE((cold, noreturn, noinline))
void rdb_fatal_error(const char *msg);

#if defined(__clang__) && (__clang_major__ >= 15)
#define RDB_VARIADIC_TEMPLATE_FORMAT
#endif

template <typename... Params>
#ifdef RDB_VARIADIC_TEMPLATE_FORMAT
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgcc-compat"
MY_ATTRIBUTE((cold, noreturn, noinline, format(printf, 1, 2)))
#else
MY_ATTRIBUTE((cold, noreturn, noinline))
#endif

void rdb_fatal_error(const char *fmt, Params &&...params) {
  // NO_LINT_DEBUG
  LogPluginErrMsg(ERROR_LEVEL, ER_LOG_PRINTF_MSG, fmt,
                  std::forward<Params>(params)...);
  abort();
}

#ifdef RDB_VARIADIC_TEMPLATE_FORMAT
#pragma clang diagnostic pop
#endif

/*
  Helper function to check the result of locking or unlocking a mutex. We'll
  intentionally abort in case of a failure because it's better to terminate
  the process instead of continuing in an undefined state and corrupting data
  as a result.
*/
inline void rdb_check_mutex_call_result(const char *function_name,
                                        const bool attempt_lock,
                                        const int result) {
  if (unlikely(result)) {
    // This will hopefully result in a meaningful stack trace which we can use
    // to efficiently debug the root cause.
    rdb_fatal_error(
        "%s a mutex inside %s failed with an "
        "error code %d.",
        attempt_lock ? "Locking" : "Unlocking", function_name, result);
  }
}

void rdb_log_status_error(const rocksdb::Status &s, const char *msg = nullptr);

// return true if the marker file exists which indicates that the corruption
// has been detected
bool rdb_has_rocksdb_corruption();

// stores a marker file in the data directory so that after restart server
// is still aware that rocksdb data is corrupted
MY_ATTRIBUTE((cold, noinline))
void rdb_persist_corruption_marker();

// Perform the given function on each directory entry. Supported flags are
// MY_FAE to make any errors fatal and MY_WANT_STAT if each directory entry
// should be stat'ed too. These flags may be combined.
// The fn function should
// return false if the directory iteration should be stopped and error returned.
bool for_each_in_dir(const std::string &path, int flags,
                     std::function<bool(const fileinfo &)> fn);

// A wrapper for mkdir that aborts in the case of error
void rdb_mkdir_or_abort(const std::string &dir, mode_t mode);

// A wrapper for rmdir that optionally aborts in the case of error
bool rdb_rmdir(const std::string &dir, bool fatal_error);

// A wrapper for my_delete that aborts in the case of error
void rdb_file_delete_or_abort(const std::string &path);

// Move a file by copying and deleting it. Errors are fatal.
void rdb_file_copy_and_delete_or_abort(const std::string &source,
                                       const std::string &dest);

// Rename/move a file or directory, supporting the case where source and
// destination are on different file systems by reverting to copy and delete.
// Errors are fatal.
void rdb_path_rename_or_abort(const std::string &source,
                              const std::string &dest);

/*
  Helper functions to parse strings.
*/

const char *rdb_skip_spaces(const struct CHARSET_INFO *const cs,
                            const char *str)
    MY_ATTRIBUTE((__nonnull__, __warn_unused_result__));

bool rdb_compare_strings_ic(const char *const str1, const char *const str2)
    MY_ATTRIBUTE((__nonnull__, __warn_unused_result__));

const char *rdb_find_in_string(const char *str, const char *pattern,
                               bool *const succeeded)
    MY_ATTRIBUTE((__nonnull__, __warn_unused_result__));

const char *rdb_check_next_token(const struct CHARSET_INFO *const cs,
                                 const char *str, const char *const pattern,
                                 bool *const succeeded)
    MY_ATTRIBUTE((__nonnull__, __warn_unused_result__));

const char *rdb_parse_id(const struct CHARSET_INFO *const cs, const char *str,
                         std::string *const id)
    MY_ATTRIBUTE((__nonnull__(1, 2), __warn_unused_result__));

const char *rdb_skip_id(const struct CHARSET_INFO *const cs, const char *str)
    MY_ATTRIBUTE((__nonnull__, __warn_unused_result__));

const std::vector<std::string> parse_into_tokens(const std::string &s,
                                                 const char delim);

/*
  Helper functions to populate strings.
*/

[[nodiscard]] std::string rdb_hexdump(
    const char *data, std::size_t data_len,
    std::size_t maxsize = RDB_MAX_HEXDUMP_LEN);

/*
  Helper function to return dir + '/' + file
 */
std::string rdb_concat_paths(std::string_view dir, std::string_view file);

/*
  Helper function to see if a database exists
 */
bool rdb_database_exists(std::string_view db_name);

/**
  Helper class wrappers to meansure startup time
  Warning: not thread safe. It is mainly designed to
  be used during the server startup to collect stats
  on startup function's exection time.

Usage:
  * member function: MyClass::func(args...)
  *   Rdb_exec_time::record(
  *     str, std::mem_fn(MyClass::func), &obj, args...);
  * static function: static MyClass::fun(args...)
  *   Rdb_exec_time::record(
  *     str, &MyClass::func, args...);
  *
  * To report:
  *   Rdb_exec_time::report();
*/
class Rdb_exec_time {
 private:
  std::unordered_map<std::string, uint64_t> entries_;

  struct Auto_timer {
    explicit Auto_timer(std::function<void(uint64_t &)> &&cb)
        : start_(std::chrono::high_resolution_clock::now()), callback_(cb) {}

    ~Auto_timer() {
      auto end = std::chrono::high_resolution_clock::now();
      uint64_t elapsed =
          std::chrono::duration_cast<std::chrono::microseconds>(end - start_)
              .count();

      callback_(elapsed);
    }

    std::chrono::high_resolution_clock::time_point start_;
    std::function<void(uint64_t &)> callback_;
  };

 public:
  /**
   * Currently overloaded functions are not cleanly supported as
   * template (static_cast on the designated function is required
   * so that compiler can select the right overloaded version,
   * but the syntax is ugly). Boost has a lift API (BOOST_HOF_LIFT)
   * which solves this problem but it only supports c++14 and
   * we're currently on c++11. Until then, this API requires
   * static cast on overloaded functions passed in as template param.
   */
  template <class Fn, class... Args>
  auto exec(const std::string &key, const Fn &&fn, Args &&...args)
      -> decltype(fn(args...)) {
    Auto_timer timer([&](uint64_t &e) { entries_.emplace(key, e); });
    return fn(args...);
  }

  void report() {
    if (entries_.size() == 0) {
      return;
    }

    std::string result = "\n{\n";
    for (auto const &t : entries_) {
      result += "  \"" + t.first + "\" : ";
      result += std::to_string(t.second) + "\n";
    }
    entries_.clear();

    result += "}";

    /* NO_LINT_DEBUG */
    LogPluginErrMsg(INFORMATION_LEVEL, ER_LOG_PRINTF_MSG,
                    "MyRocks: rdb execution report (microsec): %s",
                    result.c_str());
  }
};

/*
  Helper class to make sure cleanup always happens. Helpful for complicated
  logic where there can be multiple exits/returns requiring cleanup
 */
class Ensure_cleanup {
 public:
  explicit Ensure_cleanup(std::function<void()> cleanup)
      : m_cleanup(cleanup), m_skip_cleanup(false) {}

  ~Ensure_cleanup() {
    if (!m_skip_cleanup) {
      m_cleanup();
    }
  }

  // If you want to skip cleanup (such as when the operation is successful)
  void skip() { m_skip_cleanup = true; }

 private:
  std::function<void()> m_cleanup;
  bool m_skip_cleanup;
};

class Ensure_initialized {
 public:
  bool is_initialized() const { return initialized; }

 protected:
  bool initialized = false;
};

// extension must start with a dot.
[[nodiscard]] inline bool has_file_extension(std::string_view fn,
                                             std::string_view extension) {
  const auto ext_len = extension.length();
  assert(ext_len > 0);
  assert(extension[0] == '.');

  const auto fn_len = fn.length();
  if (fn_len < ext_len) return false;
  return fn.compare(fn_len - ext_len, ext_len, extension) == 0;
}

// return <db_name>.<table_name>
inline std::string get_full_tablename(const char *const db_name,
                                      const char *const table_name) {
  std::string fullname;
  // + 1 for '.'
  fullname.reserve(strlen(db_name) + strlen(table_name) + 1);
  fullname.append(db_name);
  fullname += '.';
  fullname.append(table_name);
  return fullname;
}

}  // namespace myrocks
