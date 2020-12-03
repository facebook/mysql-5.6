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
  @file storage/perfschema/table_sql_findings.h
  Performance schema sql_findings table.
*/

#ifndef TABLE_SQL_FINDINGS_H
#define TABLE_SQL_FINDINGS_H

#include <string>
#include <vector>

#include "storage/perfschema/pfs_engine_table.h"

class sql_findings_row {
 public:
  sql_findings_row(const std::string sql_id, const ulonglong code,
                   const std::string level, const std::string message,
                   const std::string query_text, const ulonglong count,
                   const ulonglong last_recorded)
      : m_sql_id(sql_id),
        m_code(code),
        m_level(level),
        m_message(message),
        m_query_text(query_text),
        m_count(count),
        m_last_recorded(last_recorded) {}

  /* Disabled copy. */
  sql_findings_row(sql_findings_row &) = delete;
  sql_findings_row &operator=(sql_findings_row &) = delete;

  /* Allow std::move copies. */
  sql_findings_row(sql_findings_row &&) = default;
  sql_findings_row &operator=(sql_findings_row &&) = default;

 public:
  std::string sql_id() const { return m_sql_id; }
  ulonglong code() const { return m_code; }
  std::string level() const { return m_level; }
  std::string message() const { return m_message; }
  std::string query_text() const { return m_query_text; }
  ulonglong count() const { return m_count; }
  ulonglong last_recorded() const { return m_last_recorded; }

 private:
  std::string m_sql_id;
  ulonglong m_code;
  std::string m_level;
  std::string m_message;
  std::string m_query_text;
  ulonglong m_count;
  ulonglong m_last_recorded;
};

class table_sql_findings : public PFS_engine_table {
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
  table_sql_findings();

 private:
  /** Current position. */
  PFS_simple_index m_pos;

  std::vector<sql_findings_row> m_all_rows;
  const sql_findings_row *m_current_row;

  /** Table share lock. */
  static THR_LOCK m_table_lock;

  /** Table definition. */
  static Plugin_table m_table_def;
};

#endif
