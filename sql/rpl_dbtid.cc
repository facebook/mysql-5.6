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

#include "sql/rpl_dbtid.h"
#include <boost/algorithm/string.hpp>

bool Dbtid_set::from_string(const std::string &tids_str) {
  clear();

  if (tids_str.empty()) {
    return true;
  }

  std::vector<std::string> kvs;
  boost::split(kvs, tids_str, boost::is_any_of(","));
  for (const auto &kv : kvs) {
    size_t pos = kv.find(":");
    if (pos == std::string::npos) {
      return false;
    }
    std::string db = kv.substr(0, pos);
    boost::trim(db);
    std::string tids = kv.substr(pos + 1);
    boost::trim(tids);
    SequenceTracker seq;
    if (!seq.from_string(tids)) {
      return false;
    }
    insert(std::move(db), std::move(seq));
  }
  return true;
}
