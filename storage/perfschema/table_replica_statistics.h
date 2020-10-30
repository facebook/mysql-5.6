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
  @file storage/perfschema/table_replica_statistics.h
  Performance schema replica_statistics table.
*/

#ifndef TABLE_REPLICA_STATISTICS_H
#define TABLE_REPLICA_STATISTICS_H

#include <vector>

#include "storage/perfschema/pfs_engine_table.h"

class replica_statistics_row {
 public:
  replica_statistics_row(const ulonglong server_id, const ulonglong timestamp,
                         const ulonglong milli_sec_behind_master)
      : m_server_id(server_id),
        m_timestamp(timestamp),
        m_milli_sec_behind_master(milli_sec_behind_master) {}

  /* Disabled copy. */
  replica_statistics_row(replica_statistics_row &) = delete;
  replica_statistics_row &operator=(replica_statistics_row &) = delete;

  /* Allow std::move copies. */
  replica_statistics_row(replica_statistics_row &&) = default;
  replica_statistics_row &operator=(replica_statistics_row &&) = default;

 public:
  ulonglong server_id() const { return m_server_id; }
  ulonglong timestamp() const { return m_timestamp; }
  ulonglong milli_sec_behind_master() const {
    return m_milli_sec_behind_master;
  }

 private:
  ulonglong m_server_id;
  ulonglong m_timestamp;
  ulonglong m_milli_sec_behind_master;
};

class table_replica_statistics : public PFS_engine_table {
 public:
  /** Table share */
  static PFS_engine_table_share m_share;
  static PFS_engine_table *create(PFS_engine_table_share *);
  static ha_rows get_row_count();

  void reset_position(void) override;
  int rnd_next() override;
  int rnd_pos(const void *pos) override;

  /** Captures the most recent size of table in static context.
   * To be used as the estimated return value for get_row_count **/
  static std::atomic<int> m_most_recent_size;

 protected:
  int read_row_values(TABLE *table, unsigned char *buf, Field **fields,
                      bool read_all) override;

 private:
  table_replica_statistics();

 private:
  /** Current position. */
  PFS_simple_index m_pos;

  std::vector<replica_statistics_row> m_all_rows;
  const replica_statistics_row *m_current_row;

  /** Table share lock. */
  static THR_LOCK m_table_lock;

  /** Table definition. */
  static Plugin_table m_table_def;
};

#endif
