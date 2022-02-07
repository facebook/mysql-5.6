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
  @file storage/perfschema/table_sql_text.cc
  Table SQL_TEXT (implementation).
*/

#include "storage/perfschema/table_sql_text.h"

#include <stddef.h>

#include "my_dbug.h"
#include "my_md5.h"
#include "my_thread.h"
#include "sql/field.h"
#include "sql/plugin_table.h"
#include "sql/table.h"
#include "storage/perfschema/pfs_buffer_container.h"
#include "storage/perfschema/pfs_global.h"

THR_LOCK table_sql_text::m_table_lock;

Plugin_table table_sql_text::m_table_def(
    /* Schema name */
    "performance_schema",
    /* Name */
    "sql_text",
    /* Definition */
    "DIGEST VARCHAR(64),\n"
    "DIGEST_TEXT LONGTEXT,\n"
    "UNIQUE KEY (DIGEST) USING HASH\n",
    /* Options */
    "ENGINE=PERFORMANCE_SCHEMA",
    /* Tablespace */
    nullptr);

PFS_engine_table_share table_sql_text::m_share = {
    &pfs_truncatable_acl,
    table_sql_text::create,
    NULL, /* write_row */
    table_sql_text::delete_all_rows,
    table_sql_text::get_row_count,
    sizeof(PFS_simple_index),
    &m_table_lock,
    &m_table_def,
    false, /* perpetual */
    PFS_engine_table_proxy(),
    {0},
    false /* m_in_purgatory */
};

bool PFS_index_sql_text::match(PFS_sql_text *pfs) {
  if (m_fields >= 1) {
    if (!m_key_1.match(pfs)) {
      return false;
    }
  }

  return true;
}

PFS_engine_table *table_sql_text::create(PFS_engine_table_share *) {
  return new table_sql_text();
}

int table_sql_text::delete_all_rows(void) {
  reset_sql_text();
  return 0;
}

ha_rows table_sql_text::get_row_count(void) { return sql_text_size; }

table_sql_text::table_sql_text()
    : PFS_engine_table(&m_share, &m_pos), m_pos(0), m_next_pos(0) {}

void table_sql_text::reset_position(void) {
  m_pos = 0;
  m_next_pos = 0;
}

int table_sql_text::rnd_next(void) {
  PFS_sql_text *pfs;

  m_pos.set_at(&m_next_pos);
  PFS_sql_text_iterator it = global_sql_text_container.iterate(m_pos.m_index);
  pfs = it.scan_next(&m_pos.m_index);
  if (pfs != NULL) {
    m_next_pos.set_after(&m_pos);
    return make_row(pfs);
  }

  return HA_ERR_END_OF_FILE;
}

int table_sql_text::rnd_pos(const void *pos) {
  PFS_sql_text *pfs;

  set_position(pos);

  pfs = global_sql_text_container.get(m_pos.m_index);
  if (pfs != NULL) {
    return make_row(pfs);
  }

  return HA_ERR_RECORD_DELETED;
}

int table_sql_text::index_init(uint idx MY_ATTRIBUTE((unused)), bool) {
  PFS_index_sql_text *result = NULL;
  assert(idx == 0);
  result = PFS_NEW(PFS_index_sql_text);
  m_opened_index = result;
  m_index = result;
  return 0;
}

int table_sql_text::index_next(void) {
  PFS_sql_text *pfs;
  bool has_more = true;

  for (m_pos.set_at(&m_next_pos); has_more; m_pos.next()) {
    pfs = global_sql_text_container.get(m_pos.m_index, &has_more);

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

int table_sql_text::make_row(PFS_sql_text *attrs) {
  if (check_pre_make_row("SQL_TEXT")) return 1;

  m_row.digest_text.length(0);
  m_row.digest_id_null = true;
  size_t safe_byte_count = attrs->m_digest_storage.m_byte_count;
  if (safe_byte_count > pfs_max_digest_length) {
    safe_byte_count = 0;
  }
  if (safe_byte_count > 0) {
    compute_digest_text(&attrs->m_digest_storage, &m_row.digest_text);
    DIGEST_HASH_TO_STRING(attrs->m_key.m_hash_key, m_row.digest_id);
    m_row.digest_id_null = false;
  }
  return 0;
}

int table_sql_text::read_row_values(TABLE *table, unsigned char *buf,
                                    Field **fields, bool read_all) {
  Field *f;

  /*
    Set the null bits. It indicates how many fields could be null
    in the table.
  */
  assert(table->s->null_bytes == 1);
  buf[0] = 0;

  for (; (f = *fields); fields++) {
    if (read_all || bitmap_is_set(table->read_set, f->field_index())) {
      switch (f->field_index()) {
        case 0: /* DIGEST */
          if (!m_row.digest_id_null) {
            set_field_varchar_utf8mb4(f, m_row.digest_id,
                                      DIGEST_HASH_TO_STRING_LENGTH);
          } else {
            f->set_null();
          }
          break;
        case 1: /* DIGEST_TEXT */
          if (m_row.digest_text.length())
            set_field_text(f, m_row.digest_text.ptr(),
                           m_row.digest_text.length(),
                           m_row.digest_text.charset());
          else {
            f->set_null();
          }
          break;
        default:
          assert(false);
      }
    }
  }

  return 0;
}
