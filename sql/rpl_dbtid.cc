/* Copyright (c) 2009, 2022, Oracle and/or its affiliates.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License, version 2.0,
   as published by the Free Software Foundation.

   This program is also distributed with certain software (including
   but not limited to OpenSSL) that is licensed under separate terms,
   as designated in a particular file or component or in included license
   documentation.  The authors of MySQL hereby grant you an additional
   permission to link the program and your derivative works with the
   separately licensed software that they have included with MySQL.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License, version 2.0, for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

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
