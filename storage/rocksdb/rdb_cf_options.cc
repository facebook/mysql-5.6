/*
   Portions Copyright (c) 2016-Present, Facebook, Inc.
   Portions Copyright (c) 2014, SkySQL Ab

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
#pragma implementation  // gcc: Class implementation
#endif

/* This C++ files header file */
#include "rdb_cf_options.h"

/* C++ system header files */
#include <string>

/* RocksDB header files */
#include "rocksdb/utilities/options_util.h"

/* MyRocks header files */
#include "rdb_cf_manager.h"
#include "rdb_compact_filter.h"
#include "rdb_sst_partitioner_factory.h"
#include "sysvars.h"

namespace myrocks {

bool Rdb_cf_options::init(
    const rocksdb::BlockBasedTableOptions &table_options,
    std::shared_ptr<rocksdb::TablePropertiesCollectorFactory> prop_coll_factory,
    std::string_view default_cf_options, std::string_view override_cf_options) {
  if (sysvars::enable_udt_in_mem) {
    m_default_cf_opts.comparator = rocksdb::BytewiseComparatorWithU64Ts();
  } else {
    m_default_cf_opts.comparator = rocksdb::BytewiseComparator();
  }

  m_default_cf_opts.compaction_filter_factory.reset(
      new Rdb_compact_filter_factory);

  m_default_cf_opts.table_factory.reset(
      rocksdb::NewBlockBasedTableFactory(table_options));

  if (prop_coll_factory) {
    m_default_cf_opts.table_properties_collector_factories.push_back(
        std::move(prop_coll_factory));
  }

  if (!set_default(default_cf_options) || !set_override(override_cf_options)) {
    return false;
  }

  if (m_default_cf_opts.sst_partitioner_factory != nullptr) {
    // NO_LINT_DEBUG
    LogPluginErrMsg(
        WARNING_LEVEL, ER_LOG_PRINTF_MSG,
        "Invalid cf options, sst_partitioner_factory should not be set");
    return false;
  }

  return true;
}

void Rdb_cf_options::get(const std::string &cf_name,
                         rocksdb::ColumnFamilyOptions *const opts) {
  assert(opts != nullptr);

  rocksdb::ConfigOptions config_options;
  config_options.input_strings_escaped = false;
  config_options.ignore_unknown_options = false;

  // Get defaults.
  rocksdb::GetColumnFamilyOptionsFromString(config_options, *opts,
                                            m_default_config, opts);

  // Get a custom confguration if we have one.
  Name_to_config_t::iterator it = m_name_map.find(cf_name);

  if (it != m_name_map.end()) {
    rocksdb::GetColumnFamilyOptionsFromString(config_options, *opts, it->second,
                                              opts);
  }
}

void Rdb_cf_options::update(const std::string &cf_name,
                            const std::string &cf_options) {
  assert(!cf_name.empty());
  assert(!cf_options.empty());

  // Always update. If we didn't have an entry before then add it.
  m_name_map[cf_name] = cf_options;

  assert(!m_name_map.empty());
}

bool Rdb_cf_options::set_default(std::string_view default_config) {
  rocksdb::ColumnFamilyOptions options;
  rocksdb::ConfigOptions config_options;
  config_options.input_strings_escaped = false;
  config_options.ignore_unknown_options = false;

  if (!default_config.empty()) {
    const auto default_config_str = std::string{default_config};
    rocksdb::Status s = rocksdb::GetColumnFamilyOptionsFromString(
        config_options, options, default_config_str, &options);
    if (!s.ok()) {
      // NO_LINT_DEBUG
      fprintf(stderr,
              "Invalid default column family config: %s (options: %s)\n",
              s.getState(), default_config_str.c_str());
      return false;
    }
  }

  m_default_config = default_config;
  return true;
}

// Skip over any spaces in the input string.
void Rdb_cf_options::skip_spaces(std::string_view input, size_t &pos) {
  while (pos < input.size() && isspace(input[pos])) ++pos;
}

// Find a valid column family name.  Note that all characters except a
// semicolon are valid (should this change?) and all spaces are trimmed from
// the beginning and end but are not removed between other characters.
bool Rdb_cf_options::find_column_family(std::string_view input, size_t &pos,
                                        std::string &key) {
  const auto beg_pos = pos;
  auto end_pos = pos - 1;

  // Loop through the characters in the string until we see a '='.
  for (; pos < input.size() && input[pos] != '='; ++pos) {
    // If this is not a space, move the end position to the current position.
    if (input[pos] != ' ') end_pos = pos;
  }

  if (end_pos == beg_pos - 1) {
    // NO_LINT_DEBUG
    LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG,
                    "No column family found (options: %.*s)",
                    static_cast<int>(input.length()), input.data());
    return false;
  }

  key = input.substr(beg_pos, end_pos - beg_pos + 1);
  return true;
}

// Find a valid options portion.  Everything is deemed valid within the options
// portion until we hit as many close curly braces as we have seen open curly
// braces.
bool Rdb_cf_options::find_options(std::string_view input, size_t &pos,
                                  std::string &options) {
  // Make sure we have an open curly brace at the current position.
  if (pos < input.size() && input[pos] != '{') {
    // NO_LINT_DEBUG
    LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG,
                    "Invalid cf options, '{' expected (options: %.*s)",
                    static_cast<int>(input.length()), input.data());
    return false;
  }

  // Skip the open curly brace and any spaces.
  ++pos;
  skip_spaces(input, pos);

  // Set up our brace_count, the begin position and current end position.
  size_t brace_count = 1;
  const auto beg_pos = pos;

  // Loop through the characters in the string until we find the appropriate
  // number of closing curly braces.
  while (pos < input.size()) {
    switch (input[pos]) {
      case '}':
        // If this is a closing curly brace and we bring the count down to zero
        // we can exit the loop with a valid options string.
        if (--brace_count == 0) {
          options = input.substr(beg_pos, pos - beg_pos);
          ++pos;  // Move past the last closing curly brace
          return true;
        }

        break;

      case '{':
        // If this is an open curly brace increment the count.
        ++brace_count;
        break;

      default:
        break;
    }

    // Move to the next character.
    ++pos;
  }

  // We never found the correct number of closing curly braces.
  // Generate an error.
  // NO_LINT_DEBUG
  LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG,
                  "Mismatched cf options, '}' expected (options: %.*s)",
                  static_cast<int>(input.length()), input.data());
  return false;
}

bool Rdb_cf_options::find_cf_options_pair(std::string_view input, size_t &pos,
                                          std::string &cf,
                                          std::string &opt_str) {
  // Skip any spaces.
  skip_spaces(input, pos);

  // We should now have a column family name.
  if (!find_column_family(input, pos, cf)) return false;

  // If we are at the end of the input then we generate an error.
  if (pos == input.size()) {
    // NO_LINT_DEBUG
    LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG,
                    "Invalid cf options, '=' expected (options: %.*s)",
                    static_cast<int>(input.length()), input.data());
    return false;
  }

  // Skip equal sign and any spaces after it
  ++pos;
  skip_spaces(input, pos);

  // Find the options for this column family.  This should be in the format
  // {<options>} where <options> may contain embedded pairs of curly braces.
  if (!find_options(input, pos, opt_str)) return false;

  // Skip any trailing spaces after the option string.
  skip_spaces(input, pos);

  // We should either be at the end of the input string or at a semicolon.
  if (pos < input.size()) {
    if (input[pos] != ';') {
      // NO_LINT_DEBUG
      LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG,
                      "Invalid cf options, ';' expected (options: %.*s)",
                      static_cast<int>(input.length()), input.data());
      return false;
    }

    ++pos;
  }

  return true;
}

bool Rdb_cf_options::parse_cf_options(std::string_view cf_options,
                                      Name_to_config_t &option_map,
                                      std::stringstream *output) {
  std::string cf;
  std::string opt_str;
  std::stringstream ss;
  rocksdb::ColumnFamilyOptions options;
  rocksdb::ConfigOptions config_options;
  config_options.input_strings_escaped = false;
  config_options.ignore_unknown_options = false;

  // Only print warnings if the caller didn't pass an output stream
  bool print_warnings = (output == nullptr);
  if (output == nullptr) {
    output = &ss;
  }

  assert(option_map.empty());

  // Loop through the characters of the string until we reach the end.
  size_t pos = 0;

  while (pos < cf_options.size()) {
    // Attempt to find <cf>={<opt_str>}.
    if (!find_cf_options_pair(cf_options, pos, cf, opt_str)) {
      (*output) << "Failed to find options pair in override options (options: "
                << cf_options << ')';
      if (print_warnings) {
        // NO_LINT_DEBUG
        LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG, "%s",
                        output->str().c_str());
      }
      return false;
    }

    // Generate an error if we have already seen this column family.
    if (option_map.find(cf) != option_map.end()) {
      (*output) << "Duplicate entry for '" << cf.c_str()
                << "' in override options (options: " << cf_options << ')';
      if (print_warnings) {
        // NO_LINT_DEBUG
        LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG, "%s",
                        output->str().c_str());
      }
      return false;
    }

    // Generate an error if the <opt_str> is not valid according to RocksDB.
    rocksdb::Status s = rocksdb::GetColumnFamilyOptionsFromString(
        config_options, options, opt_str, &options);
    if (!s.ok()) {
      (*output) << "Invalid cf config for '" << cf
                << "' in override options: " << s.getState()
                << " (options: " << cf_options << ')';
      if (print_warnings) {
        // NO_LINT_DEBUG
        LogPluginErrMsg(WARNING_LEVEL, ER_LOG_PRINTF_MSG, "%s",
                        output->str().c_str());
      }
      return false;
    }

    // If everything is good, add this cf/opt_str pair to the map.
    option_map[cf] = opt_str;
  }

  return true;
}

bool Rdb_cf_options::set_override(std::string_view override_config) {
  Name_to_config_t configs;

  if (!parse_cf_options(override_config, configs)) {
    return false;
  }

  // Everything checked out - make the map live
  m_name_map = configs;

  return true;
}

bool Rdb_cf_options::is_timestamp_aware_comparator(
    const std::string_view cf_name) {
  return sysvars::enable_udt_in_mem && cf_name != DEFAULT_SYSTEM_CF_NAME &&
         cf_name != DEFAULT_TMP_SYSTEM_CF_NAME &&
         cf_name != DEFAULT_TMP_CF_NAME;
}

const rocksdb::Comparator *Rdb_cf_options::get_cf_comparator(
    std::string_view cf_name) {
  auto is_reverse_cf = Rdb_cf_manager::is_cf_name_reverse(cf_name);
  if (is_timestamp_aware_comparator(cf_name)) {
    if (is_reverse_cf) {
      return rocksdb::ReverseBytewiseComparatorWithU64Ts();
    } else {
      return rocksdb::BytewiseComparatorWithU64Ts();
    }
  } else {
    if (is_reverse_cf) {
      return rocksdb::ReverseBytewiseComparator();
    } else {
      return rocksdb::BytewiseComparator();
    }
  }
}

std::shared_ptr<rocksdb::MergeOperator> Rdb_cf_options::get_cf_merge_operator(
    std::string_view cf_name) {
  return (cf_name == DEFAULT_SYSTEM_CF_NAME ||
          cf_name == DEFAULT_TMP_SYSTEM_CF_NAME)
             ? std::make_shared<Rdb_system_merge_op>()
             : nullptr;
}

bool Rdb_cf_options::get_cf_options(const std::string &cf_name,
                                    rocksdb::ColumnFamilyOptions *const opts) {
  *opts = m_default_cf_opts;
  get(cf_name, opts);

  // Set the comparator according to 'rev:'
  opts->comparator = get_cf_comparator(cf_name);
  opts->merge_operator = get_cf_merge_operator(cf_name);

  // When enabling UDT in memory, we disable persisting UDT in SST files.
  if (is_timestamp_aware_comparator(cf_name)) {
    opts->persist_user_defined_timestamps = false;
  }
  // this sst partitioner is used in bulk load scenario, no need to set it for
  // non-data cfs.
  if (cf_name != DEFAULT_SYSTEM_CF_NAME && cf_name != DEFAULT_TMP_CF_NAME &&
      cf_name != DEFAULT_TMP_SYSTEM_CF_NAME) {
    if (opts->sst_partitioner_factory != nullptr) {
      // NO_LINT_DEBUG
      LogPluginErrMsg(
          WARNING_LEVEL, ER_LOG_PRINTF_MSG,
          "Invalid cf options for %s, sst_partitioner_factory should not be "
          "set.",
          cf_name.c_str());
      return false;
    }
    // This comparator is for bulk load scenario, use non-timestamp aware one
    // because we don't assign timestamp to bulk-loaded key.
    opts->sst_partitioner_factory =
        std::make_shared<Rdb_sst_partitioner_factory>(
            opts->comparator->GetRootComparator(), opts->num_levels,
            Rdb_cf_manager::is_cf_name_reverse(cf_name));
  }
  return true;
}

}  // namespace myrocks
