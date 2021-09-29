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

#ifndef TABLE_ESMS_BY_ALL_H
#define TABLE_ESMS_BY_ALL_H

/**
  @file storage/perfschema/table_esms_by_all.h
  Table EVENTS_STATEMENTS_SUMMARY_BY_ALL (declarations).
*/

#include <sys/types.h>

#include "include/my_md5_size.h"
#include "my_inttypes.h"
#include "storage/perfschema/pfs_digest.h"
#include "storage/perfschema/table_helper.h"

/**
  @addtogroup performance_schema_tables
  @{
*/

class PFS_index_esms_by_all : public PFS_engine_index {
 public:
  PFS_index_esms_by_all()
      : PFS_engine_index(&m_key_1, &m_key_2, &m_key_3, &m_key_4),
        m_key_1("SCHEMA_NAME"),
        m_key_2("DIGEST"),
        m_key_3("USER"),
        m_key_4("CLIENT_ID") {}

  ~PFS_index_esms_by_all() {}

  virtual bool match(PFS_statements_digest_stat *pfs, const char *schema_name,
                     const char *user_name);

 private:
  PFS_key_schema m_key_1;
  PFS_key_digest m_key_2;
  PFS_key_user m_key_3;
  PFS_key_client_id m_key_4;
};

/**
  A row of table
  PERFORMANCE_SCHEMA.EVENTS_STATEMENTS_SUMMARY_BY_ALL.
*/
struct row_esms_by_all {
  /** Columns DIGEST/DIGEST_TEXT. */
  PFS_digest_row m_digest;

  char m_user_name[NAME_LEN];
  uint m_user_name_length;

  char client_id[MD5_HASH_TO_STRING_LENGTH + 1];
  char plan_id[MD5_HASH_TO_STRING_LENGTH + 1];

  /** Columns COUNT_STAR, SUM/MIN/AVG/MAX TIMER_WAIT. */
  PFS_statement_stat_row m_stat;

  /** Column FIRST_SEEN. */
  ulonglong m_first_seen;
  /** Column LAST_SEEN. */
  ulonglong m_last_seen;

  /** Column QUANTILE_95. */
  ulonglong m_p95;
  /** Column QUANTILE_99. */
  ulonglong m_p99;
  /** Column QUANTILE_999. */
  ulonglong m_p999;

  /** Column QUERY_SAMPLE_TEXT. */
  String m_query_sample;
  /** Column QUERY_SAMPLE_SEEN. */
  ulonglong m_query_sample_seen;
  /** Column QUERY_SAMPLE_TIMER_WAIT. */
  ulonglong m_query_sample_timer_wait;
};

/** Table PERFORMANCE_SCHEMA.EVENTS_STATEMENTS_SUMMARY_BY_ALL. */
class table_esms_by_all : public PFS_engine_table {
 public:
  /** Table share */
  static PFS_engine_table_share m_share;
  static PFS_engine_table *create(PFS_engine_table_share *);
  static int delete_all_rows();
  static ha_rows get_row_count();

  virtual void reset_position(void) override;

  virtual int rnd_next() override;
  virtual int rnd_pos(const void *pos) override;

  virtual int index_init(uint idx, bool sorted) override;
  virtual int index_next() override;

 protected:
  virtual int read_row_values(TABLE *table, unsigned char *buf, Field **fields,
                              bool read_all) override;

  table_esms_by_all();

 public:
  ~table_esms_by_all() override {}

 protected:
  int make_row(PFS_statements_digest_stat *);

 private:
  /** Table share lock. */
  static THR_LOCK m_table_lock;
  /** Table definition. */
  static Plugin_table m_table_def;

  /** Current row. */
  row_esms_by_all m_row;
  /** Current position. */
  PFS_simple_index m_pos;
  /** Next position. */
  PFS_simple_index m_next_pos;

  PFS_index_esms_by_all *m_opened_index;

  ID_NAME_WITHOUT_LOCK_MAP m_db_map;
  ID_NAME_WITHOUT_LOCK_MAP m_user_map;
  ID_NAME_WITHOUT_LOCK_MAP m_query_text_map;
};

/** @} */
#endif
