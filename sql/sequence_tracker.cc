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

#include "sql/sequence_tracker.h"
#include <boost/algorithm/string.hpp>

bool SequenceTracker::from_string(const std::string &str,
                                  const std::string &range_delim,
                                  const std::string &hole_delim) {
  clear();

  if (str.empty()) {
    return true;
  }

  std::vector<std::string> ranges;
  boost::split(ranges, str, boost::is_any_of(hole_delim));

  if (ranges.empty()) {
    return false;
  }

  auto range_itr = ranges.begin();

  auto first_range = ranges[0];
  std::vector<std::string> first_bounds;
  boost::split(first_bounds, first_range, boost::is_any_of(range_delim));
  if (first_bounds.size() > 2 || first_bounds.size() < 1) {
    return false;
  }

  for (auto &bound : first_bounds) {
    boost::trim(bound);
  }

  if (first_bounds.size() == 2) {
    uint64_t lb = std::stoull(first_bounds[0]);
    uint64_t ub = std::stoull(first_bounds[1]);
    if (lb == 1) {
      lwm_ = ub;
      ++range_itr;
    }
  }

  while (range_itr != ranges.end()) {
    std::vector<std::string> bounds;
    boost::split(bounds, *range_itr, boost::is_any_of(range_delim));
    if (bounds.size() > 2 || bounds.size() < 1) {
      return false;
    }
    for (auto &bound : bounds) {
      boost::trim(bound);
    }
    uint64_t lb = std::stoull(bounds[0]);
    uint64_t ub = bounds.size() == 2 ? std::stoull(bounds[1]) : lb;
    if (lb > ub || lb == 0 || ub == 0) {
      return false;
    }
    for (uint64_t i = lb; i <= ub; ++i) {
      insert(i);
    }
    ++range_itr;
  }
  return true;
}

std::string SequenceTracker::to_string(const std::string &range_delim,
                                       const std::string &hole_delim) const {
  auto print_range = [range_delim](uint64_t start, uint64_t end,
                                   std::stringstream &ss) {
    if (start == end) {
      ss << start;
    } else {
      ss << start << range_delim << end;
    }
  };

  std::stringstream ss;

  if (lwm_) {
    print_range(1, lwm_, ss);
  }

  if (out_of_order_.empty()) {
    return ss.str();
  }

  if (lwm_) {
    ss << hole_delim;
  }

  auto itr = out_of_order_.begin();
  uint64_t start_range = *itr;
  uint64_t end_range = start_range;

  ++itr;
  while (itr != out_of_order_.end()) {
    if (end_range + 1 != *itr) {
      print_range(start_range, end_range, ss);
      ss << hole_delim;
      start_range = end_range = *itr;
    } else {
      ++end_range;
    }
    ++itr;
  }

  print_range(start_range, end_range, ss);

  return ss.str();
}
