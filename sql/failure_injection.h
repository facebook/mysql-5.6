#include <algorithm>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "sql/mysqld.h"  // enable_failure_injection

#ifndef FAILURE_INJECTION_INCLUDED
#define FAILURE_INJECTION_INCLUDED

/**
 * Different failure points are defined in this enum. Add new failure points as
 * needed. Also remember to keep this in sync with
 * Failure_injection::available_points which is initialized in
 * failure_injection.cc
 */
enum Failure_points {

  // Dump thread introduces corruption when shipping binlog events to
  // secondaries
  DUMP_THREAD_CORRUPT_BINLOG_PAYLOAD = 0,
  // Induce delay/stalls when binlog file is rotated. Optionally takes a value
  // indicating the duration (in milli-seconds) of the stall. Default duration
  // is 10ms
  // Ex: SET GLOBAL failure_injection_point = "STALL_BINLOG_ROTATE=100"
  STALL_BINLOG_ROTATE = 1,
  FAILURE_INJECTION_END
};

/**
 * Class which maintains different failure points and the associated data in a
 * running server. This needs to be explicitly enabled.
 */
class Failure_injection {
 public:
  Failure_injection() {}

  /*
   * Disable the failure injector. All failure points are cleared
   */
  void disable();

  /*
   * Add a single failure point
   *
   * @param point The Failure_points to add
   * @return 0 on success, 1 on failure
   */
  int add_failure_point(const std::string &point);

  /*
   * Add a list of failure points. See Failure_points and
   * Failure_injection::available_points for the currently available failure
   * points
   *
   * @param points A list of failure points to add.
   * @return 0 on success, 1 on failure
   *
   * @note Sucess (returning 0) does not mean that all points were added. Adding
   * some failure points might have failed. Users are expected to check the
   * information schema table 'failure_injection_points' to get the current list
   * of failure points managed by the server
   */
  int add_failure_points(const std::vector<std::string> &points);

  /*
   * Remove a single failure point
   *
   * @param point The Failure_points to remove
   * @return 0 on success, 1 on failure
   */
  int remove_failure_point(const std::string &point);

  /*
   * Fetch a list of all failure points (and its associated value) that is
   * currently managed. The map is keyed by the failure point name (string) and
   * the value is the value associated with the failure point
   *
   * @return 0 on success, 1 on failure
   */
  std::unordered_map<std::string, std::string> get_current_failure_points();

  /*
   * Check if a specific failure point is enables
   *
   * @param point The Failure_points to check
   * @return true if enabled, false if not enabled
   */
  bool is_set(Failure_points point);

  /*
   * Clear all failure injection points
   *
   */
  void clear();

  /*
   * Fetch the value of a single failure point
   *
   * @param point The Failure_points to get the value of
   * @return The value (in string form) of a failure point. Empty string if the
   * failure point is not associated with any value
   */
  std::string get_point_value(Failure_points point);

  /*
   * A list of currently available failure points. The key to this map is the
   * string representation of the failure point and the value is the enum
   * representation of the failure point. This is primarily used by add and
   * remove methods which take in the string representation of the failure point
   * name when the associated sysvar (failure_injection_points) is updated.
   */
  static std::unordered_map<std::string, Failure_points> available_points;

 private:
  /*
   * Fetch the value of a single failure point
   *
   * @param point The Failure_points to get the value of
   * @return The value (in string form) of a failure point. Empty string if the
   * failure point is not associated with any value
   */
  std::pair<std::string, std::string> split(const std::string &point);

  // Mutex protecting access to all members of this class
  std::mutex failure_injection_mutex_;

  /*
   * A list of failure points. This is used to identify if a particular failure
   * point is activated and to get its associated value. It is better for
   * the lookup key to be an enum instead of a string for efficiency
   */
  std::unordered_map<Failure_points, std::string> injection_points_;

  /*
   * Same as injection_points_, but the key is the string representaton of the
   * failure point name. This is primarily used for reporting (for example, the
   * information schema table is built on top of this)
   */
  std::unordered_map<std::string, std::string> injection_points_string_;
};

extern Failure_injection failure_injection;

/* Useful macros */

/*
 * Execute the statement in 'func' if 'point' is set. Note that
 * failure_injection.is_set() takes a lock. When failure injection is not
 * enabled, this is a cheap check and will break out based on
 * "enable_failure_injection". If the feature is enabled and failure point
 * needs to be checked, failure_injection.is_set() takes a lock. If this failure
 * point is checked by multiple threads, this could lead to contention (only
 * when the failure injection is enabled)
 */
#define FAILURE_INJECTION_EXEC_IF(point, func) \
  do {                                         \
    if (unlikely(enable_failure_injection) &&  \
        failure_injection.is_set((point))) {   \
      func                                     \
    }                                          \
  } while (0)

/*
 * As mentioned earlier, FAILURE_INJECTION_EXEC_IF(point, func) can lead to
 * mutex contention to check if a failure point is enabled. In such situation,
 * users can use this macro instead. Any checks on failure point being active
 * needs to be performed by the user (using some sort of caching for the value
 * locally)
 *
 * WARNING: Use with caution and have enough checks to safeguard against wrong
 * failure points getting activated
 */
#define FAILURE_INJECTION_EXEC(func)          \
  do {                                        \
    if (unlikely(enable_failure_injection)) { \
      func                                    \
    }                                         \
  } while (0)

#endif /* FAILURE_INJECTION_INCLUDED */
