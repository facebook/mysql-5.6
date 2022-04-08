#include "sql/failure_injection.h"

/*
 * Initialize the static map, any new failure points should be initialized
 * here
 */
std::unordered_map<std::string, Failure_points>
    Failure_injection::available_points{
        {"DUMP_THREAD_CORRUPT_BINLOG_PAYLOAD",
         DUMP_THREAD_CORRUPT_BINLOG_PAYLOAD},
        {"STALL_BINLOG_ROTATE", STALL_BINLOG_ROTATE}};

void Failure_injection::disable() {
  std::lock_guard<std::mutex> guard(failure_injection_mutex_);
  injection_points_.clear();
  injection_points_string_.clear();
}

int Failure_injection::add_failure_point(const std::string &point) {
  std::lock_guard<std::mutex> guard(failure_injection_mutex_);

  // Split the point into 'point_name' and 'point_value'
  auto point_pair = split(point);

  // Check if 'point_name' is a valid failure point
  auto insert = Failure_injection::available_points.find(point_pair.first);
  if (insert == Failure_injection::available_points.end()) {
    return 1;
  }

  // Delete the existing entry (if one exists)
  auto exists = injection_points_.find(insert->second);
  auto string_exists = injection_points_string_.find(insert->first);

  if (exists != injection_points_.end()) {
    injection_points_.erase(exists);
  }

  if (string_exists != injection_points_string_.end()) {
    injection_points_string_.erase(string_exists);
  }

  // Insert new entry (point_name is the key and point_value is the value
  injection_points_string_.emplace(insert->first, point_pair.second);
  injection_points_.emplace(insert->second, point_pair.second);

  return 0;
}

int Failure_injection::add_failure_points(
    const std::vector<std::string> &points) {
  int error = 0;

  std::lock_guard<std::mutex> guard(failure_injection_mutex_);
  for (const auto &point : points) {
    // Split the point into 'point_name' and 'point_value'
    auto point_pair = split(point);

    // Check if 'point_name' is a valid failure point
    auto insert = Failure_injection::available_points.find(point_pair.first);
    if (insert == Failure_injection::available_points.end()) {
      error = 1;
      continue;
    }

    // Delete the existing entry (if one exists)
    auto exists = injection_points_.find(insert->second);
    auto string_exists = injection_points_string_.find(insert->first);

    if (exists != injection_points_.end()) {
      injection_points_.erase(exists);
    }

    if (string_exists != injection_points_string_.end()) {
      injection_points_string_.erase(string_exists);
    }

    // Insert new entry (point_name is the key and point_value is the value
    injection_points_string_.emplace(insert->first, point_pair.second);
    injection_points_.emplace(insert->second, point_pair.second);
  }

  return error;
}

int Failure_injection::remove_failure_point(const std::string &point) {
  std::lock_guard<std::mutex> guard(failure_injection_mutex_);
  auto remove = Failure_injection::available_points.find(point);
  if (remove == Failure_injection::available_points.end()) {
    return 1;
  }

  auto exists = injection_points_.find(remove->second);
  auto string_exists = injection_points_string_.find(remove->first);

  if (exists != injection_points_.end()) {
    injection_points_.erase(exists);
  }

  if (string_exists != injection_points_string_.end()) {
    injection_points_string_.erase(string_exists);
  }

  return 0;
}

std::unordered_map<std::string, std::string>
Failure_injection::get_current_failure_points() {
  std::lock_guard<std::mutex> guard(failure_injection_mutex_);
  return injection_points_string_;
}

std::pair<std::string, std::string> Failure_injection::split(
    const std::string &point) {
  // 'point' is of two forms
  // 1. "point_name1=value" OR
  // 2. "point_name" i.e no value associated with this point
  //
  // This method retruns a pair of strings of the form
  // (point_name, point_value). For #2, "point_value" will be a empty string
  std::string::size_type n;
  if ((n = point.find('=')) == std::string::npos) {
    return std::make_pair(point, "");  // #2
  } else if (n == point.length()) {
    // No value specified, degrade to #2
    return std::make_pair(point, "");
  }

  auto point_name = point.substr(0, n);
  auto point_value = point.substr(n + 1);

  return std::make_pair(point_name, point_value);
}

bool Failure_injection::is_set(Failure_points point) {
  std::lock_guard<std::mutex> guard(failure_injection_mutex_);
  return (enable_failure_injection &&
          injection_points_.find(point) != injection_points_.end());
}

void Failure_injection::clear() {
  std::lock_guard<std::mutex> guard(failure_injection_mutex_);
  injection_points_string_.clear();
  injection_points_.clear();
}

std::string Failure_injection::get_point_value(Failure_points point) {
  std::lock_guard<std::mutex> guard(failure_injection_mutex_);
  auto inject_point = injection_points_.find(point);

  if (inject_point == injection_points_.end()) {
    return "";
  }

  return inject_point->second;
}

Failure_injection failure_injection;
