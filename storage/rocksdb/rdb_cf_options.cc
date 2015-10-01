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

#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation        // gcc: Class implementation
#endif

/* This C++ files header file */
#include "./rdb_cf_options.h"

/* C++ system header files */
#include <fstream>
#include <algorithm>
#include <functional>
#include <cctype>
#include <locale>

/* RocksDB header files */
#include "rocksdb/utilities/convenience.h"

void Cf_options::Get(const std::string &cf_name,
                     rocksdb::ColumnFamilyOptions *opts) {

  // set defaults
  rocksdb::GetColumnFamilyOptionsFromString(*opts,
                                            default_config_,
                                            opts);

  // set per-cf config if we have one
  NameToConfig::iterator it = name_map_.find(cf_name);
  if (it != name_map_.end()) {
    rocksdb::GetColumnFamilyOptionsFromString(*opts,
                                              it->second,
                                              opts);
  }
}

static void trim(std::string &s) {
  //ltrim
  s.erase(s.begin(),
          std::find_if(s.begin(),
                       s.end(),
                       std::not1(std::ptr_fun<int, int>(std::isspace))));
  //rtrim
  s.erase(std::find_if(s.rbegin(),
                       s.rend(),
                       std::not1(std::ptr_fun<int, int>(std::isspace))).base(),
          s.end());
}


bool Cf_options::SetDefault(const std::string &default_config) {
  rocksdb::ColumnFamilyOptions options;

  if (!default_config.empty() &&
      !rocksdb::GetColumnFamilyOptionsFromString(options,
                                                 default_config,
                                                 &options).ok()) {
    fprintf(stderr,
            "Invalid default column family config: %s\n",
            default_config.c_str());
    return false;
  }

  default_config_ = default_config;
  return true;
}

bool Cf_options::ParseConfigFile(const std::string &path) {
  // TODO: support updates?

  if (path.empty()) {
    return true;
  }

  NameToConfig configs;

  std::ifstream input(path);
  if (!input) {
    fprintf(stderr,
            "Couldn't open CF config file %s\n",
            path.c_str());
    return false;
  }

  rocksdb::ColumnFamilyOptions options;

  std::string line;
  while (getline(input, line)) {
    // strip out comments
    size_t i = line.find_first_of('#');
    if (i != std::string::npos) {
      line = line.substr(0, i);
    }
    // remove whitespace
    trim(line);
    if (line.empty()) {
      continue;
    }

    i = line.find_first_of('=');
    if (i == std::string::npos) {
      fprintf(stderr,
              "Invalid cf entry in file %s: %s\n",
              path.c_str(),
              line.c_str());
      return false;
    }
    std::string cf = line.substr(0, i);
    std::string config = line.substr(i+1);
    trim(cf);
    trim(config);

    if (configs.find(cf) != configs.end()) {
      fprintf(stderr,
              "Duplicate entry for %s in file %s\n",
              cf.c_str(),
              path.c_str());
      return false;
    }

    if (!rocksdb::GetColumnFamilyOptionsFromString(
        options, config, &options).ok()) {
      fprintf(stderr,
              "Invalid column family config for %s in file %s: %s\n",
              cf.c_str(),
              path.c_str(),
              config.c_str());
      return false;
    }

    configs[cf] = config;
  }

  name_map_ = configs;
  return true;
}
