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
  @file storage/perfschema/table_sql_plans.h
  Performance_schema.sql_plans table.
*/

#ifndef TABLE_SQL_PLANS_H
#define TABLE_SQL_PLANS_H

#include <string>
#include <vector>

#include "storage/perfschema/pfs_engine_table.h"

class sql_plan_row {
 public:
  sql_plan_row(const std::string plan_id, const uint count_occur,
               const ulonglong last_recorded, const std::string plan_row)
      : m_plan_id(plan_id),
        m_count_occur(count_occur),
        m_last_recorded(last_recorded),
        m_plan_row(plan_row) {}
  /* Disabled copy. */
  sql_plan_row(sql_plan_row &) = delete;
  sql_plan_row &operator=(sql_plan_row &) = delete;

  /* Allow std::move copies. */
  sql_plan_row(sql_plan_row &&) = default;
  sql_plan_row &operator=(sql_plan_row &&) = default;

 public:
  std::string plan_id() const { return m_plan_id; }
  uint count_occur() const { return m_count_occur; }
  ulonglong last_recorded() const { return m_last_recorded; }
  std::string plan_row() const { return m_plan_row; }

 private:
  std::string m_plan_id;
  uint m_count_occur;
  ulonglong m_last_recorded;
  std::string m_plan_row;
};

class table_sql_plans : public PFS_engine_table {
 public:
  /** Table share */
  static PFS_engine_table_share m_share;
  static PFS_engine_table *create(PFS_engine_table_share *);
  static ha_rows get_row_count();
  static int delete_all_rows(void);

  void reset_position(void) override;
  int rnd_next() override;
  int rnd_pos(const void *pos) override;

 protected:
  int read_row_values(TABLE *table, unsigned char *buf, Field **fields,
                      bool read_all) override;

 private:
  table_sql_plans();

 private:
  /** Current position. */
  PFS_simple_index m_pos;

  /** Next position */
  PFS_simple_index m_next_pos;

  std::vector<sql_plan_row> m_all_rows;
  const sql_plan_row *m_current_row;

  /** Table share lock. */
  static THR_LOCK m_table_lock;

  /** Table definition. */
  static Plugin_table m_table_def;
};

#endif
