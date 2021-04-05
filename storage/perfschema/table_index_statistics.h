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
  @file storage/perfschema/table_index_statistics.h
  Performance schema index_statistics table.
*/

#pragma once

#include <string>
#include <vector>

#include "storage/perfschema/pfs_engine_table.h"

class index_statistics_row {
 public:
  index_statistics_row(const std::string table_schema,
                       const std::string table_name,
                       const std::string index_name,
                       const ulonglong rows_requested)
      : m_table_schema(table_schema),
        m_table_name(table_name),
        m_index_name(index_name),
        m_rows_requested(rows_requested) {}

  /* Disabled copy. */
  index_statistics_row(index_statistics_row &) = delete;
  index_statistics_row &operator=(index_statistics_row &) = delete;

  /* Allow std::move copies. */
  index_statistics_row(index_statistics_row &&) = default;
  index_statistics_row &operator=(index_statistics_row &&) = default;

  /* Getters */
  std::string table_schema() const { return m_table_schema; }
  std::string table_name() const { return m_table_name; }
  std::string index_name() const { return m_index_name; }
  ulonglong rows_requested() const { return m_rows_requested; }

 private:
  std::string m_table_schema;
  std::string m_table_name;
  std::string m_index_name;
  ulonglong m_rows_requested;
};

class table_index_statistics : public PFS_engine_table {
 public:
  /** Table share */
  static PFS_engine_table_share m_share;
  static PFS_engine_table *create(PFS_engine_table_share *tshare);
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
  table_index_statistics();

 private:
  /** Current position. */
  PFS_simple_index m_pos;

  std::vector<index_statistics_row> m_all_rows;
  const index_statistics_row *m_current_row;

  /** Table share lock. */
  static THR_LOCK m_table_lock;

  /** Table definition. */
  static Plugin_table m_table_def;
};
