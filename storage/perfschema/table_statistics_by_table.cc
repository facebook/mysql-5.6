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
  @file storage/perfschema/table_statistics_by_table.cc
  Table TABLE_LOCK_WAITS_SUMMARY_BY_TABLE (implementation).
*/

#include "storage/perfschema/table_statistics_by_table.h"

#include <stddef.h>

#include "my_dbug.h"
#include "my_thread.h"
#include "sql/field.h"
#include "sql/plugin_table.h"
#include "sql/table.h"
#include "storage/perfschema/pfs_buffer_container.h"
#include "storage/perfschema/pfs_column_types.h"
#include "storage/perfschema/pfs_column_values.h"
#include "storage/perfschema/pfs_global.h"
#include "storage/perfschema/pfs_instr_class.h"
#include "storage/perfschema/pfs_visitor.h"

THR_LOCK table_statistics_by_table::m_table_lock;

Plugin_table table_statistics_by_table::m_table_def(
    /* Schema name */
    "performance_schema",
    /* Name */
    "table_statistics_by_table",
    /* Definition */
    "  OBJECT_TYPE VARCHAR(64),\n"
    "  OBJECT_SCHEMA VARCHAR(64),\n"
    "  OBJECT_NAME VARCHAR(64),\n"
    "  QUERIES_USED BIGINT unsigned not null,\n"
    "  EMPTY_QUERIES BIGINT unsigned not null,\n"
    "  IO_WRITE_BYTES BIGINT unsigned not null, \n"
    "  IO_WRITE_REQUESTS BIGINT unsigned not null, \n"
    "  IO_READ_BYTES BIGINT unsigned not null, \n"
    "  IO_READ_REQUESTS BIGINT unsigned not null, \n"
    "  UNIQUE KEY `OBJECT` (OBJECT_TYPE, OBJECT_SCHEMA,\n"
    "                       OBJECT_NAME) USING HASH\n",
    /* Options */
    " ENGINE=PERFORMANCE_SCHEMA",
    /* Tablespace */
    nullptr);

PFS_engine_table_share table_statistics_by_table::m_share = {
    &pfs_truncatable_acl,
    table_statistics_by_table::create,
    NULL, /* write_row */
    table_statistics_by_table::delete_all_rows,
    table_statistics_by_table::get_row_count,
    sizeof(PFS_simple_index),
    &m_table_lock,
    &m_table_def,
    false, /* perpetual */
    PFS_engine_table_proxy(),
    {0},
    false /* m_in_purgatory */
};

bool PFS_index_statistics_by_table::match(const PFS_table_share *share) {
  if (m_fields >= 1) {
    if (!m_key_1.match(OBJECT_TYPE_TABLE)) {
      return false;
    }
  }

  if (m_fields >= 2) {
    if (!m_key_2.match(share)) {
      return false;
    }
  }

  if (m_fields >= 3) {
    if (!m_key_3.match(share)) {
      return false;
    }
  }

  return true;
}

PFS_engine_table *table_statistics_by_table::create(PFS_engine_table_share *) {
  return new table_statistics_by_table();
}

int table_statistics_by_table::delete_all_rows(void) {
  reset_table_statistics_by_table_handle();
  reset_table_statistics_by_table();
  return 0;
}

ha_rows table_statistics_by_table::get_row_count(void) {
  return global_table_share_container.get_row_count();
}

table_statistics_by_table::table_statistics_by_table()
    : PFS_engine_table(&m_share, &m_pos), m_pos(0), m_next_pos(0) {}

void table_statistics_by_table::reset_position(void) {
  m_pos.m_index = 0;
  m_next_pos.m_index = 0;
}

int table_statistics_by_table::rnd_init(bool) { return 0; }

int table_statistics_by_table::rnd_next(void) {
  PFS_table_share *pfs;

  m_pos.set_at(&m_next_pos);
  PFS_table_share_iterator it =
      global_table_share_container.iterate(m_pos.m_index);
  do {
    pfs = it.scan_next(&m_pos.m_index);
    if (pfs != NULL) {
      m_next_pos.set_after(&m_pos);
      return make_row(pfs);
    }
  } while (pfs != NULL);

  return HA_ERR_END_OF_FILE;
}

int table_statistics_by_table::rnd_pos(const void *pos) {
  PFS_table_share *pfs;

  set_position(pos);

  pfs = global_table_share_container.get(m_pos.m_index);
  if (pfs != NULL) {
    return make_row(pfs);
  }

  return HA_ERR_RECORD_DELETED;
}

int table_statistics_by_table::index_init(uint idx MY_ATTRIBUTE((unused)),
                                          bool) {
  PFS_index_statistics_by_table *result = NULL;
  assert(idx == 0);
  result = PFS_NEW(PFS_index_statistics_by_table);
  m_opened_index = result;
  m_index = result;
  return 0;
}

int table_statistics_by_table::index_next(void) {
  PFS_table_share *share;
  bool has_more_share = true;

  for (m_pos.set_at(&m_next_pos); has_more_share; m_pos.next()) {
    share = global_table_share_container.get(m_pos.m_index, &has_more_share);

    if (share != NULL) {
      if (m_opened_index->match(share)) {
        if (!make_row(share)) {
          m_next_pos.set_after(&m_pos);
          return 0;
        }
      }
    }
  }

  return HA_ERR_END_OF_FILE;
}

int table_statistics_by_table::make_row(PFS_table_share *share) {
  pfs_optimistic_state lock;
  PFS_table_query_stat query_stat;

  /*
    Evaluate aggregate stats when returning the first row
    of index and full table scan.
  */
  m_aggregate_stats.build_stats_if_needed();
  auto stat = m_aggregate_stats.get_stat(share);

  share->m_lock.begin_optimistic_lock(&lock);

  if (m_row.m_object.make_row(share)) {
    return HA_ERR_RECORD_DELETED;
  }

  /*
    First look if the table share is present in aggregate stats
    evaluated at beginning of the scan. If table share is absent,
    evaluate stats dynamically
   */
  if (stat == NULL) {
    PFS_table_query_stat_visitor visitor;
    PFS_object_iterator::visit_tables(share, &visitor);
    query_stat.aggregate(&visitor.m_stat);
  } else {
    query_stat.aggregate(stat);
  }

  if (!share->m_lock.end_optimistic_lock(&lock)) {
    return HA_ERR_RECORD_DELETED;
  }

  m_row.m_stat.set(&query_stat);

  return 0;
}

int table_statistics_by_table::read_row_values(TABLE *table, unsigned char *buf,
                                               Field **fields, bool read_all) {
  Field *f;

  /* Set the null bits */
  assert(table->s->null_bytes == 1);
  buf[0] = 0;

  for (; (f = *fields); fields++) {
    if (read_all || bitmap_is_set(table->read_set, f->field_index())) {
      switch (f->field_index()) {
        case 0: /* OBJECT_TYPE */
        case 1: /* SCHEMA_NAME */
        case 2: /* OBJECT_NAME */
          m_row.m_object.set_nullable_field(f->field_index(), f);
          break;
        case 3: /* QUERIES_USED */
          set_field_ulonglong(f, m_row.m_stat.queries_used);
          break;
        case 4: /* EMPTY_QUERIES */
          set_field_ulonglong(f, m_row.m_stat.empty_queries);
          break;
        case 5:
          set_field_ulonglong(f, m_row.m_stat.io_write_bytes);
          break;
        case 6:
          set_field_ulonglong(f, m_row.m_stat.io_write_requests);
          break;
        case 7:
          set_field_ulonglong(f, m_row.m_stat.io_read_bytes);
          break;
        case 8:
          set_field_ulonglong(f, m_row.m_stat.io_read_requests);
          break;
        default:
          assert(false);
      }
    }
  }

  return 0;
}
