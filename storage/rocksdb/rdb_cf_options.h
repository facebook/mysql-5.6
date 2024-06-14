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
#include <string_view>
#include <unordered_map>

/* MySQL header files */
#include "my_compiler.h"

/* RocksDB header files */
#include "rocksdb/table.h"

/* MyRocks header files */
#include "./rdb_global.h"

namespace myrocks {

/*
  Per-column family options configs.

  Per-column family option can be set
  - Globally (the same value applies to all column families)
  - Per column family: there is a {cf_name -> value} map,
    and also there is a default value which applies to column
    families not found in the map.
*/
class Rdb_cf_options {
 public:
  // Convert the getters to use std::string_view keys once on C++20 (8.3.0)
  // where std::unordered_map has the heterogeneous lookup.
  using Name_to_config_t = std::unordered_map<std::string, std::string>;

  Rdb_cf_options(const Rdb_cf_options &) = delete;
  Rdb_cf_options &operator=(const Rdb_cf_options &) = delete;
  Rdb_cf_options(Rdb_cf_options &&) = delete;
  Rdb_cf_options &operator=(Rdb_cf_options &&) = delete;

  Rdb_cf_options() = default;

  void get(const std::string &cf_name,
           rocksdb::ColumnFamilyOptions *const opts);

  void update(const std::string &cf_name, const std::string &cf_options);

  [[nodiscard]] bool init(
      const rocksdb::BlockBasedTableOptions &table_options,
      std::shared_ptr<rocksdb::TablePropertiesCollectorFactory>
          prop_coll_factory,
      std::string_view default_cf_options,
      std::string_view override_cf_options);

  const rocksdb::ColumnFamilyOptions &get_defaults() const {
    return m_default_cf_opts;
  }

  /* return true when success */
  bool get_cf_options(const std::string &cf_name,
                      rocksdb::ColumnFamilyOptions *const opts)
      MY_ATTRIBUTE((__nonnull__));

  [[nodiscard]] static bool parse_cf_options(
      std::string_view cf_options, Name_to_config_t &option_map,
      std::stringstream *output = nullptr);

 private:
  [[nodiscard]] bool set_default(std::string_view default_config);
  [[nodiscard]] bool set_override(std::string_view override_config);

  [[nodiscard]] static const rocksdb::Comparator *get_cf_comparator(
      std::string_view cf_name);

  [[nodiscard]] static std::shared_ptr<rocksdb::MergeOperator>
  get_cf_merge_operator(std::string_view cf_name);

  /* Helper string manipulation functions */
  static void skip_spaces(std::string_view input, size_t &pos);
  [[nodiscard]] static bool find_column_family(std::string_view input,
                                               size_t &pos, std::string &key);
  [[nodiscard]] static bool find_options(std::string_view input, size_t &pos,
                                         std::string &options);
  [[nodiscard]] static bool find_cf_options_pair(std::string_view input,
                                                 size_t &pos, std::string &cf,
                                                 std::string &opt_str);

  /* CF name -> value map */
  Name_to_config_t m_name_map;

  /* The default value (if there is only one value, it is stored here) */
  std::string m_default_config;

  rocksdb::ColumnFamilyOptions m_default_cf_opts;
};

}  // namespace myrocks
