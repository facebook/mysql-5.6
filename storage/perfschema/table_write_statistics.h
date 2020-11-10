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
  @file storage/perfschema/table_write_statistics.h
  Performance schema write_statistics table.
*/

#ifndef TABLE_WRITE_STATISTICS_H
#define TABLE_WRITE_STATISTICS_H

#include <string>
#include <vector>

#include "storage/perfschema/pfs_engine_table.h"

class write_statistics_row {
 public:
  write_statistics_row(const ulonglong timestamp, const std::string type,
                       const std::string value,
                       const ulonglong write_data_bytes,
                       const ulonglong cpu_write_time_ms)
      : m_timestamp(timestamp),
        m_type(type),
        m_value(value),
        m_write_data_bytes(write_data_bytes),
        m_cpu_write_time_ms(cpu_write_time_ms) {}

  /* Disabled copy. */
  write_statistics_row(write_statistics_row &) = delete;
  write_statistics_row &operator=(write_statistics_row &) = delete;

  /* Allow std::move copies. */
  write_statistics_row(write_statistics_row &&) = default;
  write_statistics_row &operator=(write_statistics_row &&) = default;

 public:
  ulonglong timestamp() const { return m_timestamp; }
  std::string type() const { return m_type; }
  std::string value() const { return m_value; }
  ulonglong write_data_bytes() const { return m_write_data_bytes; }
  ulonglong cpu_write_time_ms() const { return m_cpu_write_time_ms; }

 private:
  ulonglong m_timestamp;
  std::string m_type;
  std::string m_value;
  ulonglong m_write_data_bytes;
  ulonglong m_cpu_write_time_ms;
};

class table_write_statistics : public PFS_engine_table {
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
  table_write_statistics();

 private:
  /** Current position. */
  PFS_simple_index m_pos;

  std::vector<write_statistics_row> m_all_rows;
  const write_statistics_row *m_current_row;

  /** Table share lock. */
  static THR_LOCK m_table_lock;

  /** Table definition. */
  static Plugin_table m_table_def;
};

#endif
