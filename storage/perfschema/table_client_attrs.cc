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
  @file storage/perfschema/table_client_attrs.cc
  Table EVENTS_STATEMENTS_SUMMARY_GLOBAL_BY_ALL (implementation).
*/

#include "storage/perfschema/table_client_attrs.h"

#include <stddef.h>

#include "my_dbug.h"
#include "my_md5.h"
#include "my_thread.h"
#include "sql/field.h"
#include "sql/plugin_table.h"
#include "sql/table.h"
#include "storage/perfschema/pfs_buffer_container.h"
#include "storage/perfschema/pfs_client_attrs.h"
#include "storage/perfschema/pfs_global.h"
#include "storage/perfschema/table_helper.h"

THR_LOCK table_client_attrs::m_table_lock;

Plugin_table table_client_attrs::m_table_def(
    /* Schema name */
    "performance_schema",
    /* Name */
    "client_attributes",
    /* Definition */
    "  CLIENT_ID VARCHAR(65),\n"
    "  CLIENT_ATTRIBUTES LONGTEXT,\n"
    "  UNIQUE KEY (CLIENT_ID) USING HASH\n",
    /* Options */
    " ENGINE=PERFORMANCE_SCHEMA",
    /* Tablespace */
    nullptr);

PFS_engine_table_share table_client_attrs::m_share = {
    &pfs_truncatable_acl,
    table_client_attrs::create,
    NULL, /* write_row */
    table_client_attrs::delete_all_rows,
    table_client_attrs::get_row_count,
    sizeof(PFS_simple_index),
    &m_table_lock,
    &m_table_def,
    false, /* perpetual */
    PFS_engine_table_proxy(),
    {0},
    false /* m_in_purgatory */
};

bool PFS_index_client_attrs::match(PFS_client_attrs *pfs) {
  if (m_fields >= 1) {
    if (!m_key_1.match(pfs)) {
      return false;
    }
  }

  return true;
}

PFS_engine_table *table_client_attrs::create(PFS_engine_table_share *) {
  return new table_client_attrs();
}

int table_client_attrs::delete_all_rows(void) {
  reset_client_attrs();
  return 0;
}

ha_rows table_client_attrs::get_row_count(void) { return digest_max; }

table_client_attrs::table_client_attrs()
    : PFS_engine_table(&m_share, &m_pos), m_pos(0), m_next_pos(0) {}

void table_client_attrs::reset_position(void) {
  m_pos = 0;
  m_next_pos = 0;
}

int table_client_attrs::rnd_next(void) {
  PFS_client_attrs *pfs;

  m_pos.set_at(&m_next_pos);
  PFS_client_attrs_iterator it =
      global_client_attrs_container.iterate(m_pos.m_index);
  pfs = it.scan_next(&m_pos.m_index);
  if (pfs != NULL) {
    m_next_pos.set_after(&m_pos);
    return make_row(pfs);
  }

  return HA_ERR_END_OF_FILE;
}

int table_client_attrs::rnd_pos(const void *pos) {
  PFS_client_attrs *pfs;

  set_position(pos);

  pfs = global_client_attrs_container.get(m_pos.m_index);
  if (pfs != NULL) {
    return make_row(pfs);
  }

  return HA_ERR_RECORD_DELETED;
}

int table_client_attrs::index_init(uint idx MY_ATTRIBUTE((unused)), bool) {
  PFS_index_client_attrs *result = NULL;
  DBUG_ASSERT(idx == 0);
  result = PFS_NEW(PFS_index_client_attrs);
  m_opened_index = result;
  m_index = result;
  return 0;
}

int table_client_attrs::index_next(void) {
  PFS_client_attrs *pfs;
  bool has_more = true;

  for (m_pos.set_at(&m_next_pos); has_more; m_pos.next()) {
    pfs = global_client_attrs_container.get(m_pos.m_index, &has_more);

    if (pfs != NULL) {
      if (m_opened_index->match(pfs)) {
        if (!make_row(pfs)) {
          m_next_pos.set_after(&m_pos);
          return 0;
        }
      }
    }
  }

  return HA_ERR_END_OF_FILE;
}

int table_client_attrs::make_row(PFS_client_attrs *attrs) {
  array_to_hex(m_row.client_id, attrs->m_key.m_hash_key, MD5_HASH_SIZE);
  m_row.client_id[MD5_HASH_SIZE * 2] = '\0';

  m_row.client_attrs.length(0);
  m_row.client_attrs.append(attrs->m_client_attrs, attrs->m_client_attrs_length,
                            &my_charset_bin);
  return 0;
}

int table_client_attrs::read_row_values(TABLE *table, unsigned char *buf,
                                        Field **fields, bool read_all) {
  Field *f;

  /*
    Set the null bits. It indicates how many fields could be null
    in the table.
  */
  DBUG_ASSERT(table->s->null_bytes == 1);
  buf[0] = 0;

  for (; (f = *fields); fields++) {
    if (read_all || bitmap_is_set(table->read_set, f->field_index)) {
      switch (f->field_index) {
        case 0: /* CLIENT_ID */
          if (strlen(m_row.client_id) > 0) {
            set_field_varchar_utf8(f, m_row.client_id, strlen(m_row.client_id));
          } else {
            f->set_null();
          }
          break;
        case 1: /* CLIENT_ATTRIBUTES */
          if (m_row.client_attrs.length())
            set_field_text(f, m_row.client_attrs.ptr(),
                           m_row.client_attrs.length(),
                           m_row.client_attrs.charset());
          else {
            f->set_null();
          }
          break;
      }
    }
  }

  return 0;
}
