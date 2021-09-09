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

#ifndef TABLE_SQL_TEXT_H
#define TABLE_SQL_TEXT_H

/**
  @file storage/perfschema/table_sql_text.h
  Table SQL_TEXT (declarations).
*/

#include <sys/types.h>

#include "my_inttypes.h"
#include "storage/perfschema/pfs_sql_text.h"
#include "storage/perfschema/table_helper.h"

/**
  @addtogroup performance_schema_tables
  @{
*/

class PFS_index_sql_text : public PFS_engine_index {
 public:
  PFS_index_sql_text() : PFS_engine_index(&m_key_1), m_key_1("DIGEST") {}

  ~PFS_index_sql_text() {}

  virtual bool match(PFS_sql_text *pfs);

 private:
  PFS_key_digest m_key_1;
};

/**
  A row of table
  PERFORMANCE_SCHEMA.SQL_TEXT
*/
struct row_sql_text {
  char digest_id[DIGEST_HASH_TO_STRING_LENGTH + 1];
  bool digest_id_null;
  String digest_text;
};

/** Table PERFORMANCE_SCHEMA.SQL_TEXT . */
class table_sql_text : public PFS_engine_table {
 public:
  /** Table share */
  static PFS_engine_table_share m_share;
  static PFS_engine_table *create(PFS_engine_table_share *);
  static int delete_all_rows();
  static ha_rows get_row_count();

  void reset_position() override;

  int rnd_next() override;
  int rnd_pos(const void *pos) override;

  int index_init(uint idx, bool sorted) override;
  int index_next() override;

 protected:
  int read_row_values(TABLE *table, unsigned char *buf, Field **fields,
                      bool read_all) override;

  table_sql_text();

 public:
  ~table_sql_text() override {}

 protected:
  int make_row(PFS_sql_text *);

 private:
  /** Table share lock. */
  static THR_LOCK m_table_lock;
  /** Table definition. */
  static Plugin_table m_table_def;

  /** Current row. */
  row_sql_text m_row;
  /** Current position. */
  PFS_simple_index m_pos;
  /** Next position. */
  PFS_simple_index m_next_pos;

  PFS_index_sql_text *m_opened_index;
};

/** @} */
#endif
