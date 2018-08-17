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
  @file storage/perfschema/table_session_query_attrs.h
  Performance schema query_attributes table.
*/

#include "storage/perfschema/pfs_engine_table.h"

class table_session_query_attrs_impl;

class table_session_query_attrs : public PFS_engine_table {
 public:
  /** Table share */
  static PFS_engine_table_share m_share;
  static PFS_engine_table *create(PFS_engine_table_share *);
  static ha_rows get_row_count();

  virtual void reset_position(void) override;
  virtual int rnd_next() override;
  virtual int rnd_pos(const void *pos) override;

 protected:
  virtual int read_row_values(TABLE *table, unsigned char *buf, Field **fields,
                              bool read_all) override;

 private:
  table_session_query_attrs();

 private:
  /** Current position. */
  PFS_simple_index m_pos;

  std::unique_ptr<table_session_query_attrs_impl> m_query_attrs_impl;

  /** Table share lock. */
  static THR_LOCK m_table_lock;

  /** Table definition. */
  static Plugin_table m_table_def;
};
