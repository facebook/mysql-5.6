/* Copyright (c) 2010, 2018, Oracle and/or its affiliates. All rights reserved.

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
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA
  */

/**
  @file storage/perfschema/table_write_throttling_rules.h
  Performance schema write_throttling_rules table.
*/

#ifndef TABLE_WRITE_THROTTLING_RULES_H
#define TABLE_WRITE_THROTTLING_RULES_H

#include <vector>

#include "storage/perfschema/pfs_engine_table.h"

class write_throttling_rules_row {
 public:
  write_throttling_rules_row(const std::string mode,
                             const ulonglong creation_time,
                             const std::string type, const std::string value)
      : m_mode(mode),
        m_creation_time(creation_time),
        m_type(type),
        m_value(value) {}

  /* Disabled copy. */
  write_throttling_rules_row(write_throttling_rules_row &) = delete;
  write_throttling_rules_row &operator=(write_throttling_rules_row &) = delete;

  /* Allow std::move copies. */
  write_throttling_rules_row(write_throttling_rules_row &&) = default;
  write_throttling_rules_row &operator=(write_throttling_rules_row &&) =
      default;

 public:
  std::string mode() const { return m_mode; }
  ulonglong creation_time() const { return m_creation_time; }
  std::string type() const { return m_type; }
  std::string value() const { return m_value; }

 private:
  std::string m_mode;
  ulonglong m_creation_time;
  std::string m_type;
  std::string m_value;
};

class table_write_throttling_rules : public PFS_engine_table {
 public:
  /** Table share */
  static PFS_engine_table_share m_share;
  static PFS_engine_table *create(PFS_engine_table_share *);
  static ha_rows get_row_count();

  void reset_position(void) override;
  int rnd_next() override;
  int rnd_pos(const void *pos) override;

  /** Captures the most recent size of table in static context.
   * To be used as the estimated return value for get_row_count **/
  static std::atomic<int> m_most_recent_size;

 protected:
  int read_row_values(TABLE *table, unsigned char *buf, Field **fields,
                      bool read_all) override;

 private:
  table_write_throttling_rules();

 private:
  /** Current position. */
  PFS_simple_index m_pos;

  std::vector<write_throttling_rules_row> m_all_rows;
  const write_throttling_rules_row *m_current_row;

  /** Table share lock. */
  static THR_LOCK m_table_lock;

  /** Table definition. */
  static Plugin_table m_table_def;
};

#endif
