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

#pragma once

#include <boost/algorithm/string.hpp>
#include <set>
#include <sstream>
#include <string>
#include <unordered_set>

/**
 * Tracks a sequence of ids, tracks low/high watermarks and out of order ids
 */
class SequenceTracker {
 public:
  SequenceTracker(uint64_t init_low_watermark = 0) : lwm_(init_low_watermark) {}

  bool from_string(const std::string &str, const std::string &range_delim = "-",
                   const std::string &hole_delim = ":");

  std::string to_string(const std::string &range_delim = "-",
                        const std::string &hole_delim = ":") const;

  bool insert(uint64_t val) {
    if (val < lwm_) {
      return false;
    }

    if (val - lwm_ <= 1) {
      lwm_ = val;
    } else if (!out_of_order_.insert(val).second) {
      return false;
    }

    collapse();

    return true;
  }

  bool erase(uint64_t val) {
    if (val == lwm_) {
      lwm_ -= 1;
      return true;
    }

    if (val > lwm_) {
      return out_of_order_.erase(val) > 0;
    }

    for (uint64_t i = val; i <= lwm_; ++i) {
      out_of_order_.insert(i);
    }

    lwm_ = val - 1;
    out_of_order_.erase(val);

    return true;
  }

  bool contains(uint64_t val) const {
    if (val <= lwm_) {
      return true;
    }
    return out_of_order_.find(val) != out_of_order_.end();
  }

  void merge(const SequenceTracker &other) {
    lwm_ = std::max(lwm_, other.lwm_);
    out_of_order_.insert(other.out_of_order_.lower_bound(lwm_ + 1),
                         other.out_of_order_.end());
    collapse();
  }

  bool empty() const { return lwm_ == 0 && out_of_order_.empty(); }

  void clear() {
    lwm_ = 0;
    out_of_order_.clear();
  }

  uint64_t get_low_watermark() const { return lwm_; }

  uint64_t get_high_watermark() const {
    return out_of_order_.empty() ? lwm_ : *out_of_order_.rbegin();
  }

 private:
  // Updates low watermark and shrinks out of order set
  void collapse() {
    auto itr = out_of_order_.begin();
    while (itr != out_of_order_.end()) {
      if (*itr - lwm_ > 1) {
        break;
      }
      lwm_ = *itr;
      ++itr;
    }
    out_of_order_.erase(out_of_order_.begin(), itr);
  }

  // The low watermark id (all ids <= to this has been encountered
  uint64_t lwm_;
  // Set of out of order ids
  std::set<uint64_t> out_of_order_;
};
