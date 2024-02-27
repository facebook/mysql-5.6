/* Copyright (c) 2024 Meta Platforms, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#pragma once

#include <assert.h>
#include <sys/types.h>

#include "include/lex_string.h"
#include "include/m_ctype.h"
#include "include/my_sqlcommand.h"
#include "include/work_queue.h"
#include "sql/current_thd.h"
#include "sql/handler.h"
#include "sql/mysqld.h"
#include "sql/sql_cmd.h" /* Sql_cmd */
#include "sql/sql_lex.h"
#include "sql/sql_list.h"
#include "sql_string.h"

class Item;
class READ_INFO;
class THD;
class Table_ref;

class Sql_cmd_dump_table final : public Sql_cmd {
 public:
  Sql_cmd_dump_table(Table_ident *table, const LEX_STRING &filename)
      : m_table(table), m_filename(filename) {}

  enum_sql_command sql_command_code() const override { return SQLCOM_DUMP; }

  bool execute(THD *thd) override;

  Table_ident *get_table() const { return m_table; }

  void set_thread_count(int n) {
    assert(n > 0);
    m_nthreads = n;
  }

  void set_chunk_size(int n) {
    assert(n > 0);
    m_chunk_size = n;
  }

  void set_consistent(bool consistent) { m_consistent = consistent; }

 private:
  struct Dump_work_item {
    /**
      Start key of the chunk (inclusive).
    */
    uchar *start_ref;
    /**
      End key of the chunk (inclusive). Used to sanity check the row count ends
      on this key.
    */
    uchar *end_ref;
    /**
      Zero-based chunk ID (monotonically increasing). Used for file naming.
    */
    int chunk_id;
    /**
      Number of rows to dump for this chunk.
    */
    int nrows;
  };

  /**
    Structure to save error context from worker threads.
  */
  struct Err_context {
    char m_message_text[MYSQL_ERRMSG_SIZE];
    uint m_errno;
  };

  /**
    Arguments passed to dump worker threads.
  */
  struct Dump_worker_args {
    /**
      [in] Pointer to command object.
    */
    Sql_cmd_dump_table *cmd;

    /**
      [in] Snapshot ID to use, if consistent read is enabled.
    */
    ulonglong snapshot_id;

    /**
      [in] Pointer to table share.
    */
    TABLE_SHARE *share;

    /**
      [in] Pointer to main THD.
    */
    THD *main_thd;

    /**
      [out] error flag.
    */
    bool is_err;

    /**
      [out] Error info copied from worker.
    */
    Err_context m_err;

    /**
      [out] Was the thread created?
    */
    bool created{false};

    /**
      [in] Work queue of chunks to process.
    */
    Work_queue<Dump_work_item> *queue;
  };

  static void *dump_worker(void *arg);

  bool start_threads(THD *thd, TABLE_SHARE *share, ulonglong snapshot_id,
                     int nthreads, my_thread_handle *handles,
                     Dump_worker_args *args);
  bool dump_chunk(Table_ref *tr, mem_root_deque<Item *> &list,
                  Dump_work_item *work);
  uchar *enqueue_chunk(THD *thd, TABLE *table, uchar *start_ref, uchar *end_row,
                       int chunk_id, int64_t chunk_rows);
  Table_ident *const m_table;
  LEX_STRING m_filename;
  Work_queue<Dump_work_item> m_work_queue;

  // Number of dump worker threads to create.
  int m_nthreads{1};

  // Chunk size. In rows for now, later can be other units such as MB.
  int m_chunk_size{128};

  // Should a consistent snapshot be used? Not all storage engines support it.
  bool m_consistent{false};
};
