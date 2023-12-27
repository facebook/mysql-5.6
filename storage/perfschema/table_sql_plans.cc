
/* Copyright (c) 2010, 2019, Oracle and/or its affiliates. All rights reserved.

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
  @file storage/perfschema/table_sql_plans.cc
  Table SQL_PLANS (implementation).
*/

#include "storage/perfschema/table_sql_plans.h"

#include <stddef.h>

#include "my_dbug.h"
#include "my_md5.h"
#include "my_thread.h"
#include "sql/field.h"
#include "sql/plugin_table.h"
#include "sql/sql_info.h"
#include "sql/table.h"
#include "storage/perfschema/pfs_buffer_container.h"
#include "storage/perfschema/pfs_global.h"
#include "storage/perfschema/table_helper.h"

THR_LOCK table_sql_plans::m_table_lock;

Plugin_table table_sql_plans::m_table_def(
    /* Schema name */
    "performance_schema",
    /* Name */
    "sql_plans",
    /* Definition */
    "PLAN_ID VARCHAR(32) NOT NULL,\n"
    "COUNT_OCCUR BIGINT UNSIGNED NOT NULL,\n"
    "LAST_RECORDED BIGINT UNSIGNED NOT NULL,\n"
    "PLAN_ROW TEXT\n",
    /* Options */
    "ENGINE=PERFORMANCE_SCHEMA",
    /* Tablespace */
    nullptr);

PFS_engine_table_share table_sql_plans::m_share = {
    &pfs_truncatable_acl,
    table_sql_plans::create,
    NULL, /* write_row */
    table_sql_plans::delete_all_rows,
    table_sql_plans::get_row_count,
    sizeof(PFS_simple_index),
    &m_table_lock,
    &m_table_def,
    false, /* perpetual */
    PFS_engine_table_proxy(),
    {0},
    false /* m_in_purgatory */
};

enum sql_plans_field_id : ushort {
  PLAN_ID,
  COUNT_OCCUR,
  LAST_RECORDED,
  PLAN_ROW
};

PFS_engine_table *table_sql_plans::create(PFS_engine_table_share *) {
  return new table_sql_plans();
}

int table_sql_plans::delete_all_rows(void) {
  // Temporarily commenting out
  // reset_sql_plans();
  return 0;
}

ha_rows table_sql_plans::get_row_count(void) {
  // Calculate this based on actual # of entries
  // Could also return the max because this table will fill up quickly
  // Temporarily commenting out
  // return get_captured_plan_count();
  return 0;
}

// Temporary dummy function
std::vector<sql_plan_row> get_all_sql_plans() {
  std::vector<sql_plan_row> sql_plans;
  return sql_plans;
}

table_sql_plans::table_sql_plans()
    : PFS_engine_table(&m_share, &m_pos), m_pos(0), m_next_pos(0) {
  m_all_rows = get_all_sql_plans();
  m_current_row = nullptr;
}

void table_sql_plans::reset_position(void) {
  m_pos = 0;
  m_next_pos = 0;
}

int table_sql_plans::rnd_next(void) {
  m_pos.set_at(&m_next_pos);
  if (m_pos.m_index >= m_all_rows.size()) {
    m_current_row = nullptr;
    return HA_ERR_END_OF_FILE;
  }
  m_current_row = &m_all_rows[m_pos.m_index];
  m_next_pos.set_after(&m_pos);
  return 0;
}

int table_sql_plans::rnd_pos(const void *pos) {
  set_position(pos);
  if (m_pos.m_index >= m_all_rows.size()) {
    m_current_row = nullptr;
    return HA_ERR_RECORD_DELETED;
  }
  m_current_row = &m_all_rows[m_pos.m_index];
  return 0;
}

int table_sql_plans::read_row_values(TABLE *table, unsigned char *buf,
                                     Field **fields, bool read_all) {
  Field *f;
  const auto &curr_row = *m_current_row;

  /*
    Set the null bits. It indicates how many fields could be null
    in the table.
  */
  assert(table->s->null_bytes == 1);
  buf[0] = 0;

  for (; (f = *fields); fields++) {
    if (read_all || bitmap_is_set(table->read_set, f->field_index())) {
      switch (f->field_index()) {
        case sql_plans_field_id::PLAN_ID: /* PLAN_ID VARCHAR(32) */
          set_field_varchar_utf8mb4(f, curr_row.plan_id().c_str(),
                                    curr_row.plan_id().length());
          break;
        case sql_plans_field_id::COUNT_OCCUR: /* COUNT_OCCUR BIGINT  */
          set_field_ulonglong(f, curr_row.count_occur());
          break;
        case sql_plans_field_id::LAST_RECORDED: /* LAST_RECORDED BIGINT UNSIGNED
                                                 */
          set_field_ulonglong(f, curr_row.last_recorded());
          break;
        case sql_plans_field_id::PLAN_ROW: /* PLAN_ROW VARCHAR(2048) */
          set_field_varchar_utf8mb4(f, curr_row.plan_row().c_str(),
                                    curr_row.plan_row().length());
          break;
        default:
          assert(false);
      }
    }
  }

  return 0;
}
