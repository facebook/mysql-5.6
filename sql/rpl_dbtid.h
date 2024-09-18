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

#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>

#include "sql/sequence_tracker.h"

/**
 * Represents a set of dbtids (Database Transaction Identifiers)
 * A dbtid set is essentially a set of DB:ID pairs similar to Gtid_set
 */
class Dbtid_set {
 public:
  bool from_string(const std::string &str);

  uint64_t get_next_tid(const std::string &db) {
    std::unique_lock<std::mutex> lock(lock_);
    uint64_t max = get_max_tid(db);
    add(db, max + 1, false);
    return max + 1;
  }

  void update(const std::unordered_map<std::string, uint64_t> &tids,
              bool force = false) {
    std::unique_lock<std::mutex> lock(lock_);
    if (force && tids.empty()) {
      dbtids_.clear();
      return;
    }
    for (const auto &elem : tids) {
      add(elem.first, elem.second, force);
    }
  }

  void update(const Dbtid_set &dbtid_set, bool force = false) {
    std::unique_lock<std::mutex> lock(lock_);
    if (force && dbtid_set.empty()) {
      dbtids_.clear();
      return;
    }
    for (const auto &elem : dbtid_set.dbtids_) {
      if (force) {
        dbtids_[elem.first] = elem.second;
        continue;
      }
      dbtids_[elem.first].merge(elem.second);
    }
  }

  void rollback(const std::unordered_map<std::string, uint64_t> &tids) {
    std::unique_lock<std::mutex> lock(lock_);
    for (const auto &elem : tids) {
      remove(elem.first, elem.second);
    }
  }

  void rm_db(const std::string &db) {
    std::unique_lock<std::mutex> lock(lock_);
    dbtids_.erase(db);
  }

  bool contains(const std::unordered_map<std::string, uint64_t> &tids) const {
    std::unique_lock<std::mutex> lock(lock_);
    for (const auto &elem : tids) {
      auto itr = dbtids_.find(elem.first);
      if (itr == dbtids_.end()) {
        return false;
      }
      if (!itr->second.contains(elem.second)) {
        return false;
      }
    }
    return true;
  }

  std::string to_string() const {
    std::unique_lock<std::mutex> lock(lock_);
    std::stringstream ss;
    for (const auto &elem : dbtids_) {
      ss << elem.first << ":" << elem.second.to_string() << ",\n";
    }
    std::string ret = ss.str();
    if (!ret.empty()) {
      ret.pop_back();
      ret.pop_back();
    }
    return ret;
  }

  std::unordered_set<std::string> dbs() const {
    std::unique_lock<std::mutex> lock(lock_);
    std::unordered_set<std::string> ret;
    for (const auto &elem : dbtids_) {
      ret.insert(elem.first);
    }
    return ret;
  }

  void clear() {
    std::unique_lock<std::mutex> lock(lock_);
    dbtids_.clear();
  }

  bool empty() const {
    std::unique_lock<std::mutex> lock(lock_);
    return dbtids_.empty();
  }

 private:
  void add(const std::string &db, uint64_t tid, bool force) {
    const auto &itr = dbtids_.find(db);

    if (itr == dbtids_.end() || force) {
      dbtids_[db] = SequenceTracker(tid);
      return;
    }

    itr->second.insert(tid);
  }

  void remove(const std::string &db, uint64_t tid) {
    const auto &itr = dbtids_.find(db);
    if (itr == dbtids_.end()) {
      return;
    }

    itr->second.erase(tid);
  }

  void insert(std::string &&db, SequenceTracker &&seq) { dbtids_[db] = seq; }

  uint64_t get_max_tid(const std::string &db) const {
    const auto &itr = dbtids_.find(db);
    if (itr == dbtids_.end()) {
      return 0;
    }
    return itr->second.get_high_watermark();
  }

  mutable std::mutex lock_;
  std::unordered_map<std::string, SequenceTracker> dbtids_;
};
