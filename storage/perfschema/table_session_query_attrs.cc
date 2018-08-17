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

#include "storage/perfschema/table_session_query_attrs.h"

#include "sql/field.h"
#include "sql/mysqld_thd_manager.h"
#include "sql/plugin_table.h"
#include "sql/sql_class.h"
#include "sql/table.h"
#include "storage/perfschema/pfs_buffer_container.h"
#include "storage/perfschema/table_helper.h"

THR_LOCK table_session_query_attrs::m_table_lock;

Plugin_table table_session_query_attrs::m_table_def(
    /* Schema name */
    "performance_schema",
    /* Name */
    "session_query_attrs",
    /* Definition */
    "  PROCESSLIST_ID BIGINT(21) unsigned NOT NULL DEFAULT '0',\n"
    "  ATTR_NAME VARCHAR(256) NOT NULL,\n"
    "  ATTR_VALUE VARCHAR(256) NOT NULL,\n"
    "  ORDINAL_POSITION BIGINT(21) unsigned NOT NULL\n",
    /* Options */
    " ENGINE=PERFORMANCE_SCHEMA",
    /* Tablespace */
    nullptr);

PFS_engine_table_share table_session_query_attrs::m_share = {
    &pfs_readonly_acl,
    table_session_query_attrs::create,
    NULL, /* write_row */
    NULL, /* delete_all_rows */
    table_session_query_attrs::get_row_count,
    sizeof(PFS_simple_index),
    &table_session_query_attrs::m_table_lock,
    &table_session_query_attrs::m_table_def,
    false, /* perpetual */
    PFS_engine_table_proxy(),
    {0},
    false /* m_in_purgatory */
};

class query_attribute_row {
 public:
  query_attribute_row(ulonglong processlist_id, const std::string &attr_name,
                      const std::string &attr_value, unsigned ordinal_position)
      : m_processlist_id(processlist_id),
        m_ordinal_position(ordinal_position),
        m_attr_name(attr_name),
        m_attr_value(attr_value) {}

  /* Disabled copy. */
  query_attribute_row(query_attribute_row &) = delete;
  query_attribute_row &operator=(query_attribute_row &) = delete;

  /* Allow std::move copies. */
  query_attribute_row(query_attribute_row &&) = default;
  query_attribute_row &operator=(query_attribute_row &&) = default;

 public:
  ulonglong processlist_id() const { return m_processlist_id; }
  const std::string &attr_name() const { return m_attr_name; }
  const std::string &attr_value() const { return m_attr_value; }
  unsigned ordinal_position() const { return m_ordinal_position; }

 private:
  ulonglong m_processlist_id;
  unsigned m_ordinal_position;
  std::string m_attr_name;
  std::string m_attr_value;
};

class All_THD_query_attrs_visitor : public Do_THD_Impl {
 public:
  virtual void operator()(THD *thd) override {
    mysql_mutex_lock(&thd->LOCK_thd_data);
    auto thread_id = (ulonglong)thd->thread_id();
    unsigned ordinal_position = 0;
    for (const auto &kvp : thd->query_attrs_list) {
      const auto &attr_name = kvp.first;
      const auto &attr_value = kvp.second;
      m_query_attributes.emplace_back(thread_id, attr_name, attr_value,
                                      ordinal_position++);
    }
    mysql_mutex_unlock(&thd->LOCK_thd_data);
  }

  std::vector<query_attribute_row> &query_attributes() {
    return m_query_attributes;
  }

 private:
  std::vector<query_attribute_row> m_query_attributes;
};

class table_session_query_attrs_impl {
 public:
  table_session_query_attrs_impl(PFS_simple_index *curr_pos);

  void reset_position(void);
  int rnd_next(void);
  int rnd_pos(void);
  int read_row_values(TABLE *table, Field **fields, bool read_all);

 public:
  std::vector<query_attribute_row> m_all_rows;
  /** Current position. */
  PFS_simple_index *m_curr_pos;
  const query_attribute_row *m_current_row;
};

enum query_attr_field_offset {
  FO_PROCESSLIST_ID,
  FO_ATTR_NAME,
  FO_ATTR_VALUE,
  FO_ORDINAL_POSITION,
};

void table_session_query_attrs_impl::reset_position(void) {
  m_curr_pos->set_at(0u);
}

int table_session_query_attrs_impl::rnd_next(void) {
  if (m_curr_pos->m_index >= m_all_rows.size()) {
    m_current_row = nullptr;
    return HA_ERR_END_OF_FILE;
  }
  m_current_row = &m_all_rows[m_curr_pos->m_index];
  m_curr_pos->next();
  return 0;
}

int table_session_query_attrs_impl::rnd_pos(void) {
  if (m_curr_pos->m_index >= m_all_rows.size()) {
    m_current_row = nullptr;
    return HA_ERR_RECORD_DELETED;
  }
  m_current_row = &m_all_rows[m_curr_pos->m_index];
  return 0;
}

table_session_query_attrs_impl::table_session_query_attrs_impl(
    PFS_simple_index *curr_pos)
    : m_curr_pos(curr_pos) {
  All_THD_query_attrs_visitor thd_query_attrs_visitor;
  Global_THD_manager::get_instance()->do_for_all_thd(&thd_query_attrs_visitor);
  m_all_rows.swap(thd_query_attrs_visitor.query_attributes());
  m_current_row = nullptr;
}

int table_session_query_attrs_impl::read_row_values(TABLE *table,
                                                    Field **fields,
                                                    bool read_all) {
  Field *f;
  const auto &curr_row = *m_current_row;

  /* Set the null bits */
  assert(table->s->null_bytes == 0);

  for (; (f = *fields); fields++) {
    if (read_all || bitmap_is_set(table->read_set, f->field_index())) {
      switch (f->field_index()) {
        case FO_PROCESSLIST_ID:
          set_field_ulonglong(f, curr_row.processlist_id());
          break;
        case FO_ATTR_NAME: {
          const auto &attr_name = curr_row.attr_name();
          set_field_varchar_utf8mb4(f, attr_name.c_str(), attr_name.length());
        } break;
        case FO_ATTR_VALUE: {
          const auto &attr_value = curr_row.attr_value();
          set_field_varchar_utf8mb4(f, attr_value.c_str(), attr_value.length());
        } break;
        case FO_ORDINAL_POSITION:
          set_field_ulonglong(f, curr_row.ordinal_position());
          break;
        default:
          assert(false);
      }
    }
  }

  return 0;
}

table_session_query_attrs::table_session_query_attrs()
    : PFS_engine_table(&m_share, &m_pos),
      m_pos(0),
      m_query_attrs_impl(new table_session_query_attrs_impl(&m_pos)) {}

PFS_engine_table *table_session_query_attrs::create(PFS_engine_table_share *) {
  return new table_session_query_attrs();
}

ha_rows table_session_query_attrs::get_row_count(void) {
  /*
    The real number of attributes per thread does not matter,
    we only need to hint the optimizer there are few (mostly one or less)
    per thread.
  */
  return global_thread_container.get_row_count();
}

void table_session_query_attrs::reset_position(void) {
  m_query_attrs_impl->reset_position();
}

int table_session_query_attrs::rnd_next(void) {
  return m_query_attrs_impl->rnd_next();
}

int table_session_query_attrs::rnd_pos(const void *pos) {
  set_position(pos);
  return m_query_attrs_impl->rnd_pos();
}

int table_session_query_attrs::read_row_values(TABLE *table, unsigned char *,
                                               Field **fields, bool read_all) {
  return m_query_attrs_impl->read_row_values(table, fields, read_all);
}
