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

#ifndef TABLE_CLIENT_ATTRS_H
#define TABLE_CLIENT_ATTRS_H

/**
  @file storage/perfschema/table_client_attrs.h
  Table CLIENT_ATTRIBUTES (declarations).
*/

#include <sys/types.h>

#include "my_inttypes.h"
#include "storage/perfschema/pfs_client_attrs.h"
#include "storage/perfschema/table_helper.h"

/**
  @addtogroup performance_schema_tables
  @{
*/

class PFS_index_client_attrs : public PFS_engine_index {
 public:
  PFS_index_client_attrs() : PFS_engine_index(&m_key_1), m_key_1("CLIENT_ID") {}

  ~PFS_index_client_attrs() {}

  virtual bool match(PFS_client_attrs *pfs);

 private:
  PFS_key_client_id m_key_1;
};

/**
  A row of table
  PERFORMANCE_SCHEMA.CLIENT_ATTRIBUTES.
*/
struct row_client_attrs {
  char client_id[MD5_HASH_SIZE * 2 + 1];
  String client_attrs;
};

/** Table PERFORMANCE_SCHEMA.CLIENT_ATTRIBUTES . */
class table_client_attrs : public PFS_engine_table {
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

  table_client_attrs();

 public:
  ~table_client_attrs() override {}

 protected:
  int make_row(PFS_client_attrs *);

 private:
  /** Table share lock. */
  static THR_LOCK m_table_lock;
  /** Table definition. */
  static Plugin_table m_table_def;

  /** Current row. */
  row_client_attrs m_row;
  /** Current position. */
  PFS_simple_index m_pos;
  /** Next position. */
  PFS_simple_index m_next_pos;

  PFS_index_client_attrs *m_opened_index;
};

/** @} */
#endif
