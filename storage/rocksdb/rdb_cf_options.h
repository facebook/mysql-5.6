/*
   Copyright (c) 2014, SkySQL Ab

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

/* C++ system header files */
#include <string>
#include <unordered_map>

namespace rocksdb {
  class ColumnFamilyOptions;
}

bool is_cf_name_reverse(const char *name);

/*
  Per-column family options configs.

  Per-column family option can be set
  - Globally (the same value applies to all column families)
  - Per column family: there is a {cf_name -> value} map,
    and also there is a default value which applies to column
    families not found in the map.
*/
class Cf_options {
public:
  void Get(const std::string &cf_name, rocksdb::ColumnFamilyOptions *opts);

  bool SetDefault(const std::string &default_config);
  bool SetOverride(const std::string &overide_config);

private:
  typedef std::unordered_map<std::string, std::string> NameToConfig;

  /* cf_name -> value map */
  NameToConfig name_map_;

  /* The default value (if there is only one value, it is stored here) */
  std::string default_config_;
};
