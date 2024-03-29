/* Copyright (c) 2024 Meta Platforms, Inc. All rights reserved.

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
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/* Dump data from a table into a text file */

#include "sql/sql_dump.h"

#include "auth/auth_acls.h"
#include "mysql/components/services/log_builtins.h"
#include "sql/debug_sync.h"
#include "sql/item.h"
#include "sql/log.h"
#include "sql/mysqld.h"
#include "sql/mysqld_thd_manager.h"
#include "sql/parse_tree_helpers.h"
#include "sql/protocol_classic.h"
#include "sql/query_result.h"
#include "sql/snapshot.h"
#include "sql/sql_base.h"
#include "sql/sql_class.h"
#include "sql/sql_error.h"
#include "sql/table.h"
#include "sql/transaction.h"

/**
  Dump one chunk into a unique file.

  @param tr Table ref to dump
  @param list List of expressions/fields to include in the dump.
  @param work Work item containing the chunk context.

  @return true on error, false otherwise.
*/
bool Sql_cmd_dump_table::dump_chunk(Table_ref *tr, mem_root_deque<Item *> *list,
                                    Dump_work_item *work) {
  DBUG_TRACE;

  DBUG_PRINT("dump", ("Dumping chunk %" PRId64, work->chunk_id));
  bool is_err = false;
  // Was handler initialized?
  bool ha_init = false;
  int error = 0;
  TABLE *table = tr->table;
  THD *thd = table->in_use;
  assert(thd);
  handler *ha = table->file;
  uchar *rowbuf = table->record[0];
  int numrows = 0;
  char filename[FN_REFLEN];

#ifdef HAVE_PSI_THREAD_INTERFACE
  // Set in pfs threads table / SHOW PROCESSLIST /
  // INFORMATION_SCHEMA.PROCESSLIST
  char pfs_info_msg[256];
  snprintf(pfs_info_msg, sizeof(pfs_info_msg), "Dumping chunk %" PRId64,
           work->chunk_id);
  PSI_THREAD_CALL(set_thread_info)
  (pfs_info_msg, sizeof(pfs_info_msg));
#endif

  // Create a filename with the chunk suffix.
  snprintf(filename, sizeof(filename), "%s.%" PRId64, m_filename.str,
           work->chunk_id);
  sql_exchange exchange(filename, false /* dumpfile */, FILETYPE_CSV);

  // Create a result_export with the filename above and default escape options.
  // TODO: add grammar to customize field/line sep options.
  Query_result_export result(&exchange);

  if (result.prepare(thd, *list, nullptr /* query expression */)) {
    is_err = true;
    goto exit;
  }

  if (result.start_execution(thd)) {
    is_err = true;
    goto exit;
  }

  // Start a scan from the range given.

  error = ha->ha_index_init(0, true /* sorted */);
  if (error) {
    ha->print_error(error, MYF(0));
    is_err = true;
    goto exit;
  }

  ha_init = true;

  // Position the iterator and scan the first row in the chunk.
  if (!work->start_ref) {
    // This is the first chunk in the table, just get the first row overall.
    error = ha->ha_index_first(rowbuf);
  } else {
    error = ha->ha_index_read_map(rowbuf, work->start_ref, HA_WHOLE_KEY,
                                  HA_READ_AFTER_KEY);
  }
  if (error && error != HA_ERR_END_OF_FILE && error != HA_ERR_KEY_NOT_FOUND) {
    LogErr(ERROR_LEVEL, ER_SQL_HA_READ_FAILED, error, tr->table_name);
    ha->print_error(error, MYF(0));
    is_err = true;
    goto exit;
  }

  while (!error) {
    thd->check_yield();

    ++numrows;
    DBUG_PRINT("verbose",
               ("read row %d in chunk %" PRId64, numrows, work->chunk_id));

    // send rowbuf to result (which will be the chunk file).
    result.send_data(thd, *list);

    // See if we've dumped all the rows for this chunk.
    if (numrows == work->nrows) {
      // As a sanity check, check rowbuf and see if it matches the end key
      // (work->end_ref). We do want to read the end key, but not beyond. The
      // following implicitly checks table->record[0]. NOTE: key_cmp_if_same
      // returns FALSE if they match.
      assert(!key_cmp_if_same(
          table, work->end_ref, 0,
          table->key_info[0].key_length /* check whole key */));
      DBUG_PRINT("dump", ("found end key in chunk %" PRId64 ". Breaking out.",
                          work->chunk_id));

      break;
    }

    // Try to fetch the next row along the index.
    error = ha->ha_index_next(rowbuf);
    if (error && error != HA_ERR_END_OF_FILE && error != HA_ERR_KEY_NOT_FOUND) {
      LogErr(ERROR_LEVEL, ER_SQL_HA_READ_FAILED, error, tr->table_name);
      ha->print_error(error, MYF(0));
      is_err = true;
      goto exit;
    }
  }

exit:
  result.cleanup();

  if (ha_init) {
    ha->ha_index_end();
  }

  if (thd->is_error()) {
    Diagnostics_area *da = thd->get_stmt_da();
    // NO_LINT_DEBUG
    sql_print_error("Error during dumping chunk %" PRId64 ": %d: %s",
                    work->chunk_id, da->mysql_errno(), da->message_text());
  } else {
    DBUG_PRINT("dump", ("done writing chunk %" PRId64 ". %d rows",
                        work->chunk_id, numrows));
  }

  return is_err;
}

/**
  Helper to set the PK fields into the table's read_set.
*/
static void set_pk_fields(TABLE *table) {
  KEY *pk = &table->key_info[table->s->primary_key];
  for (unsigned int i = 0; i < pk->actual_key_parts; ++i) {
    KEY_PART_INFO *key_part = &pk->key_part[i];
    bitmap_set_bit(table->read_set, key_part->fieldnr - 1);
  }
}

/**
  Dump worker entry point. Processes chunk work items from a queue and writes
  them to storage.
*/
/* static */ void *Sql_cmd_dump_table::dump_worker(void *arg) {
  THD new_thd;
  THD *thd = &new_thd;
  Query_block *query_block = nullptr;
  Dump_worker_args *args = static_cast<Dump_worker_args *>(arg);
  Global_THD_manager *thd_manager = Global_THD_manager::get_instance();
  bool thd_inited = false;
  // List of fields (or expressions) to dump.
  mem_root_deque<Item *> field_list(nullptr);
  TABLE *table = nullptr;
  Table_ref *tr = nullptr;
  handlerton *hton = nullptr;
  bool snapshot_attached = false;

  thd->system_thread = SYSTEM_THREAD_BACKGROUND;

  thd->thread_stack = (char *)&thd;
  // my_thread_init() must be called before DBUG_TRACE or DBUG_ENTER since it
  // sets up the THD in TLS. See set_mysys_thread_var().
  if (my_thread_init()) {
    args->is_err = true;
    return nullptr;
  }

  // Initialize NET so that SHOW PROCESSLIST works properly. This sets up, for
  // example, the rw_status so that thread_state_info(), used by
  // List_process_list, doesn't get confused.
  thd->get_protocol_classic()->init_net(nullptr);

  // TODO: Consider Auto_THD.
  thd->set_new_thread_id();
  // store_globals must be called after an ID has been assigned.
  thd->store_globals();
  // Propagate the DB from the main THD.
  thd->set_db(args->main_thd->db());
  thd_manager->add_thd(thd);
  mysql_thread_set_psi_id(thd->thread_id());
  thd->set_command(COM_TABLE_DUMP);
  thd->security_context()->set_host_or_ip_ptr(my_localhost,
                                              strlen(my_localhost));
#ifdef HAVE_PSI_THREAD_INTERFACE
  /* Update the thread instrumentation. */
  PSI_THREAD_CALL(set_thread_account)
  (thd->security_context()->user().str, thd->security_context()->user().length,
   thd->security_context()->host_or_ip().str,
   thd->security_context()->host_or_ip().length);
  PSI_THREAD_CALL(set_thread_command)(thd->get_command());
  PSI_THREAD_CALL(set_thread_start_time)(thd->query_start_in_secs());
#endif /* HAVE_PSI_THREAD_INTERFACE */

  // Set this after the thread ID has been set.
  DBUG_ENTER("dump_worker");
  THD_STAGE_INFO(thd, stage_dumping_chunk);

  lex_start(thd);
  thd_inited = true;
  DBUG_PRINT("dump", ("Dump worker started"));

  // Now that we have a thd, set the mem root for the list.
  field_list.set_mem_root(thd->mem_root);

  query_block = thd->lex->query_block;

  tr = query_block->add_table_to_list(thd, args->cmd->get_table(), nullptr, 0);

  if (!tr) {
    args->is_err = true;
    goto exit;
  }

  // This is needed for insert_fields() below to iterate the fields of the
  // table.
  query_block->context.resolve_in_table_list_only(tr);

  // Grant SELECT_ACL on the table ref so that insert_fields or setup_fields is
  // able to access the columns. The access check has already been performed in
  // the main thread (see the call to check_table_access()). This is an internal
  // thread spawned on behalf of the main one, so we do not need to re-validate
  // permissions.
  tr->set_privileges(SELECT_ACL);

  if (open_and_lock_tables(thd, tr, 0)) {
    args->is_err = true;
    goto exit;
  }

  table = tr->table;
  hton = table->s->db_type();

  // Start a consistent snapshot if needed.
  if (args->snapshot_id) {
    snapshot_info_st snapshot_info;
    snapshot_info.op = snapshot_operation::SNAPSHOT_ATTACH;
    snapshot_info.snapshot_id = args->snapshot_id;
    if (ha_explicit_snapshot(thd, hton, &snapshot_info)) {
      args->is_err = true;
      goto exit;
    }
    DBUG_PRINT("dump", ("Attached snapshot %llu", args->snapshot_id));
    snapshot_attached = true;
  }

  if (!args->cmd->m_item_list) {
    // If no item list provided, add a select-all "*" expression as the item
    // list and expand it to all the fields in the table ref.
    Item_field star_field(&query_block->context, nullptr, nullptr, "*");
    field_list.push_back(&star_field);
    auto list_it = field_list.begin();

    // Expand the "*" select expression into fields.
    if (insert_fields(thd, query_block, tr->db, tr->alias, &field_list,
                      &list_it, false /* any_privileges */)) {
      args->is_err = true;
      goto exit;
    }
  } else {
    // Build a list of fields based on those captured in the parse and
    // resolve/bind them to this thread's TABLE.
    // Note that we can't just use args->cmd->m_item_list as is because it is
    // already bound to the main thread's TABLE object. We need to create our
    // own list.
    for (Item *item : args->cmd->m_item_list->value) {
      Item_field *field = new (thd->mem_root) Item_field(
          &query_block->context, nullptr, nullptr, item->item_name.ptr());
      if (!field) {
        my_error(ER_OUTOFMEMORY, MYF(ME_FATALERROR), sizeof(Item_field));
        args->is_err = true;
        goto exit;
      }
      field_list.push_back(field);
    }

    // Resolve the columns in field_list and add them to the read_set.
    if (setup_fields(thd, /*want_privilege=*/SELECT_ACL,
                     /*allow_sum_func=*/false, /*split_sum_funcs=*/false,
                     /*column_update=*/false, /*typed_items=*/nullptr,
                     &field_list, Ref_item_array())) {
      args->is_err = true;
      goto exit;
    }

    // Ensure all PK columns are retrieved since we want precise positioning
    // when we iterate chunks.
    set_pk_fields(table);
  }

  // Dequeue work items until killed.
  while (!thd->is_killed() && !args->main_thd->is_killed()) {
    thd->check_yield();

    // dequeue() will also check for thd->is_killed() internally and return
    // nullptr in that case.
    auto work = args->queue->dequeue(thd);
    if (!work) {
      // No more work and THD killed.
      break;
    }
    if (args->cmd->dump_chunk(tr, &field_list, work)) {
      args->is_err = true;
      goto exit;
    }
  }

  if (thd->is_killed()) {
    DBUG_PRINT("dump", ("Dump worker killed. Aborting."));
  } else if (args->main_thd->is_killed()) {
    DBUG_PRINT("dump", ("Main dump thread killed. Aborting."));
  }

exit:

  if (thd_inited) {
    if (thd->is_error()) {
      Diagnostics_area *da = thd->get_stmt_da();
      args->m_err.m_errno = da->mysql_errno();
      strcpy(args->m_err.m_message_text, da->message_text());
    }

    if (snapshot_attached) {
      snapshot_info_st snapshot_info;
      snapshot_info.op = snapshot_operation::SNAPSHOT_RELEASE;
      if (ha_explicit_snapshot(thd, hton, &snapshot_info)) {
        my_printf_error(ER_UNKNOWN_ERROR, "failed to release snapshot %llu",
                        MYF(0), snapshot_info.snapshot_id);
        args->is_err = true;
      }
    }

    trans_rollback_stmt(thd);
    trans_rollback(thd);
    close_thread_tables(thd);

    lex_end(thd->lex);

    thd->get_protocol_classic()->end_net();

    // Must be called before thd_manager->remove_thd.
    thd->release_resources();
    thd_manager->remove_thd(thd);
    my_thread_end();

    thd = nullptr;
  }

  DBUG_RETURN(nullptr);
}

/**
  Create all the worker threads to process chunks.

  @return true on error. false otherwise.
*/
bool Sql_cmd_dump_table::start_threads(THD *thd, TABLE_SHARE *share,
                                       ulonglong snapshot_id, int nthreads,
                                       my_thread_handle *handles,
                                       Dump_worker_args *args) {
  DBUG_TRACE;
  bool is_err = false;

  my_thread_attr_t thr_attr;
  my_thread_attr_init(&thr_attr);
  my_thread_attr_setdetachstate(&thr_attr, MY_THREAD_CREATE_JOINABLE);

  for (int i = 0; i < nthreads; ++i) {
    Dump_worker_args *arg = args + i;
    arg->cmd = this;
    arg->main_thd = thd;
    arg->share = share;
    arg->queue = &m_work_queue;
    arg->snapshot_id = snapshot_id;

    int error = mysql_thread_create_seq(key_thread_dump_worker, i, handles + i,
                                        &thr_attr, dump_worker, arg);
    if (error) {
      LogErr(ERROR_LEVEL, ER_CANT_CREATE_DUMP_THREAD, errno).os_errno(errno);
      arg->is_err = true;
      is_err = true;
      goto exit;
    }

    arg->created = true;
  }

exit:
  return is_err;
}

/**
  Helper to enqueue chunks.

  @return the next start key (exclusive) or nullptr if there was an error.
*/
uchar *Sql_cmd_dump_table::enqueue_chunk(THD *thd, TABLE *table,
                                         uchar *start_ref, uchar *end_row,
                                         int64_t chunk_id, int64_t chunk_rows) {
  uchar *new_start_ref = nullptr;
  // TODO: should we use some other allocator so that the memory doesn't
  // keep growing (since you can't free from a memroot individually)? Or
  // perhaps a lookaside list where we can reuse work items?

  // TODO T177975594: myrocks implements ref_length incorrectly, using its
  // own internal format instead of MySQL's KeyTupleFormat.
  // assert(ha->ref_length);
  KEY *pk = &table->key_info[table->s->primary_key];

  auto work_item = new (thd->mem_root) Dump_work_item;
  if (!work_item) {
    goto exit;
  }

  if (start_ref) {
    work_item->start_ref = new (thd->mem_root) uchar[pk->key_length];
    if (!work_item->start_ref) {
      goto exit;
    }
    memcpy(work_item->start_ref, start_ref, pk->key_length);
  } else {
    work_item->start_ref = nullptr;
  }

  work_item->end_ref = new (thd->mem_root) uchar[pk->key_length];
  if (!work_item->end_ref) {
    goto exit;
  }
  // Get the position of the current PK into ha->ref.
  // ha->position(rowbuf);
  // NOTE T177975594: position() is broken on myrocks. It fills ha->ref and
  // ref_length based on its internal memcmp'arable rocksdb format, instead
  // of MySQL's KeyTupleFormat. I.e. it doesn't use key_copy() like other
  // engines. Use key_copy ourselves to produce a correct key for the
  // handler APIs.
  key_copy(work_item->end_ref, end_row, pk, pk->key_length);

  work_item->chunk_id = chunk_id;
  work_item->nrows = chunk_rows;

  m_work_queue.enqueue(work_item);
  DBUG_PRINT("dump", ("enqueued work item %" PRId64 " for %d rows",
                      work_item->chunk_id, work_item->nrows));

  // Next range will start *after* this point.
  new_start_ref = work_item->end_ref;

exit:
  return new_start_ref;
}

/**
  Helper to get the actual (not max) size of a row that will be output to the
  chunk file, taking into account variable length fields and actual column
  list if provided, or all the fields in `table` otherwise.
*/
static int get_row_actual_size(TABLE *table,
                               const mem_root_deque<Item *> *field_list) {
  int sum = 0;
  if (field_list) {
    for (Item *item : *field_list) {
      assert(item->type() == Item::FIELD_ITEM);
      Item_field *item_field = down_cast<Item_field *>(item);
      Field *field = item_field->field;
      sum += field->data_length();
    }
  } else {
    // No field list given, assume all the fields in the table.
    for (uint i = 0; i < table->s->fields; i++) {
      assert(bitmap_is_set(table->read_set, i));
      Field *field = table->field[i];
      sum += field->data_length();
    }
  }
  return sum;
}

static const char *chunk_unit_names[] = {
    "unset",  // invalid
    "rows",  "kb", "mb", "gb",
};
static_assert(std::size(chunk_unit_names) ==
              static_cast<size_t>(Chunk_unit::LAST));

/**
  Helper to send result metadata for client to consume. Includes info about
  the dump like number of chunks.

  @return true on error. false otherwise.
*/
static bool send_dump_result_info(THD *thd, longlong nchunks, longlong nrows) {
  DBUG_TRACE;
  bool is_err = false;
  Protocol *protocol = thd->get_protocol();

  mem_root_deque<Item *> field_list(thd->mem_root);
  field_list.push_back(
      new Item_return_int("num_chunks", 10, MYSQL_TYPE_LONGLONG));
  field_list.push_back(
      new Item_return_int("rows_dumped", 10, MYSQL_TYPE_LONGLONG));

  if (thd->send_result_metadata(field_list,
                                Protocol::SEND_NUM_ROWS | Protocol::SEND_EOF)) {
    is_err = true;
    goto exit;
  }

  protocol->start_row();

  // Fill the fields.
  protocol->store(nchunks);
  protocol->store(nrows);

  // Send the row.
  if (protocol->end_row()) {
    is_err = true;
    goto exit;
  }

exit:
  if (!is_err) {
    my_eof(thd);
  }

  return is_err;
}

/**
  Main command entry point for DUMP TABLE statement.
*/
bool Sql_cmd_dump_table::execute(THD *thd) {
  DBUG_TRACE;
  bool is_err = false;
  Table_ref *const table_ref = thd->lex->query_tables;
  int error = 0;
  TABLE *table = nullptr;
  handler *ha = nullptr;
  uchar *rowbuf = nullptr;
  handlerton *hton = nullptr;
  mem_root_deque<Item *> *item_list =
      m_item_list ? &m_item_list->value : nullptr;
  assert(m_nthreads > 0);
  DBUG_PRINT("dump", ("Dumping table '%s' with %d threads. Chunk size: %d %s.",
                      table_ref->table_name, m_nthreads, m_chunk_size,
                      chunk_unit_names[(int)m_chunk_unit]));
  // Track the start of each range scan. Initial one will be from start of
  // table.
  uchar *start_ref = nullptr;
  int64_t rownum = 0;

  // Number of bytes examined so far from the scan, reset after each chunk is
  // created.
  uint64_t bytes_so_far = 0;

  // Used to count the number of rows in each chunk. With row count based
  // chunking, this is a constant. But with size-based, we will need to know
  // how many we scanned before creating the chunk work item, to avoid
  // checking the end key each time.
  int64_t last_rownum = 0;
  int64_t chunk_id = 0;
  bool snapshot_created = false;
  bool scan_started = false;

  // Row based chunking variables.
  int nrows = m_chunk_size;  // number of rows per chunk if row-based chunking
                             // is used.

  // Size-based chunking variables.
  uint64_t chunk_size_bytes = 0;  // if byte-based chunking is used.
  // The number of bits to shift m_chunk_size (left) to convert it to bytes,
  // and bytes_so_far (right) to convert it to `m_chunk_unit`s.
  int chunk_unit_shift_bits = 0;

  std::vector<my_thread_handle> handles(m_nthreads);
  std::vector<Dump_worker_args> worker_args(m_nthreads);
  snapshot_info_st snapshot_info;

  if (check_table_access(thd, SELECT_ACL, table_ref, false, UINT_MAX, false)) {
    is_err = true;
    goto exit;
  }

  if (open_and_lock_tables(thd, table_ref, 0)) {
    is_err = true;
    goto exit;
  }

  table = table_ref->table;
  ha = table->file;
  rowbuf = table->record[0];
  hton = table->s->db_type();

  if (table->s->is_missing_primary_key()) {
    // Table must have a primary key to use DUMP TABLE since we need to position
    // cursors efficiently to arbitrary points.
    my_error(ER_REQUIRES_PRIMARY_KEY, MYF(0));
    is_err = true;
    goto exit;
  }

  if (m_consistent) {
    // Create a shared snapshot of the data for all worker threads to use.
    snapshot_info.op = snapshot_operation::SNAPSHOT_CREATE;
    if (ha_explicit_snapshot(thd, hton, &snapshot_info)) {
      is_err = true;
      goto exit;
    }
    DBUG_PRINT("dump", ("Created snapshot %llu", snapshot_info.snapshot_id));
    snapshot_created = true;
    DEBUG_SYNC(thd, "dump_snapshot_created");
  }

  if (item_list) {
    // Resolve the columns in m_item_list and add them to the read_set.
    if (setup_fields(thd, /*want_privilege=*/SELECT_ACL,
                     /*allow_sum_func=*/false, /*split_sum_funcs=*/false,
                     /*column_update=*/false, /*typed_items=*/nullptr,
                     item_list, Ref_item_array())) {
      is_err = true;
      goto exit;
    }

    // Ensure all PK columns are retrieved since we want precise positioning
    // when we iterate chunks.
    set_pk_fields(table);
  } else {
    // Add all columns to the read_set.
    table->use_all_columns();
  }

  // Start worker threads.
  if (start_threads(thd, table->s, m_consistent ? snapshot_info.snapshot_id : 0,
                    m_nthreads, handles.data(), worker_args.data())) {
    is_err = true;
    goto exit;
  }

  // Start main scan over base table, looking for chunk boundaries.
  error = ha->ha_rnd_init(true);
  if (error) {
    LogErr(ERROR_LEVEL, ER_SQL_HA_READ_FAILED, error, table_ref->table_name);
    ha->print_error(error, MYF(0));
    goto exit;
  }
  scan_started = true;

  THD_STAGE_INFO(thd, stage_dumping_table);

  // Compute some size-based chunking constants if needed.
  if (m_chunk_unit != Chunk_unit::ROWS) {
    // Row count based chunking.
    switch (m_chunk_unit) {
      // Size-based chunks.
      // IMPORTANT: ensure the size units are in descending order in size to
      // ensure the correct chunk_unit_shift_bits calculation.
      case Chunk_unit::GB:
        chunk_unit_shift_bits += 10;
        [[fallthrough]];
      case Chunk_unit::MB:
        chunk_unit_shift_bits += 10;
        [[fallthrough]];
      case Chunk_unit::KB: {
        chunk_unit_shift_bits += 10;

        // Calculate how much space we want to consume per chunk in bytes.
        chunk_size_bytes = m_chunk_size << chunk_unit_shift_bits;
        break;
      }
      default:
        is_err = true;
        assert(!"invalid chunk unit");
        my_error(ER_INTERNAL_ERROR, MYF(0), "Invalid chunk unit");
        goto exit;
    }
  }

  // Scan the base table and create work items every N {bytes,rows}
  while (!thd->is_killed()) {
    thd->check_yield();

    DEBUG_SYNC(thd, "dump_table_loop");

    // Check if any of the workers have failed before continuing.
    for (size_t i = 0; i < worker_args.size(); ++i) {
      Dump_worker_args *args = worker_args.data() + i;
      if (args->is_err) {
        if (args->m_err.m_errno) {
          my_printf_error(args->m_err.m_errno, "worker thread %zu failed: %s",
                          MYF(0), i, args->m_err.m_message_text);

        } else {
          my_printf_error(ER_UNKNOWN_ERROR, "worker thread %zu failed", MYF(0),
                          i);
        }
        is_err = true;
        goto exit;
      }
    }
    error = ha->ha_rnd_next(rowbuf);
    if (error) {
      if (error != HA_ERR_END_OF_FILE) {
        LogErr(ERROR_LEVEL, ER_SQL_HA_READ_FAILED, error,
               table_ref->table_name);
        ha->print_error(error, MYF(0));
        is_err = true;
      } else {
        // Not a real error.
        error = 0;
      }

      // Finished the scan: error or otherwise.
      break;
    }

    rownum++;

    // Track bytes if needed.
    if (m_chunk_unit != Chunk_unit::ROWS) {
      bytes_so_far += get_row_actual_size(table, item_list);
    }

    DBUG_EXECUTE_IF("verbose", {
      char buf[128];
      std::string tuple;

      for (uint i = 0; i < table->s->fields; i++) {
        if (bitmap_is_set(table->read_set, i)) {
          Field *field = table->field[i];
          String tmp(buf, thd->charset());
          String *str = field->val_str(&tmp);
          tuple += field->field_name;
          tuple += ": ";
          tuple.append(str->c_ptr());
          tuple += ", ";
        }
      }
      DBUG_PRINT("verbose", ("tuple read: %s: ", tuple.c_str()));
    });

    // Should we make a new chunk?
    bool new_chunk = false;

    if (m_chunk_unit == Chunk_unit::ROWS) {
      new_chunk = rownum % nrows == 0;
    } else {
      if (bytes_so_far >= chunk_size_bytes) {
        new_chunk = true;
        bytes_so_far -= chunk_size_bytes;
      }
    }

    if (new_chunk) {
      const int64_t chunk_rows = rownum - last_rownum;
      last_rownum = rownum;
      assert(chunk_rows > 0);

      // make new chunk work item.
      // TODO: check hdl for HA_PRIMARY_KEY_REQUIRED_FOR_POSITION
      start_ref = enqueue_chunk(thd, table, start_ref, rowbuf /* end_row */,
                                chunk_id++, chunk_rows);
      if (!start_ref) {
        is_err = true;
        goto exit;
      }
    }
  }

  // Enqueue the last chunk in case it didn't evenly divide.
  if (rownum > last_rownum) {
    int64_t chunk_rows = rownum - last_rownum;
    last_rownum = rownum;
    assert(chunk_rows > 0);

    start_ref = enqueue_chunk(thd, table, start_ref, rowbuf /* end_row */,
                              chunk_id++, chunk_rows);
    if (!start_ref) {
      is_err = true;
      goto exit;
    }
  }

exit:
  if (scan_started) {
    // End the scan.
    ha->ha_rnd_end();
  }

  m_work_queue.shutdown();

  // Wait for all worker threads to complete.
  for (int i = 0; i < m_nthreads; ++i) {
    Dump_worker_args *args = worker_args.data() + i;
    if (!args->created) {
      continue;
    }

    thd_wait_begin(thd, THD_WAIT_SLEEP);

    /* Wait for thread to die */
    my_thread_join(&handles[i], nullptr);

    thd_wait_end(thd);

    if (!is_err && args->is_err) {
      if (args->m_err.m_errno) {
        my_printf_error(args->m_err.m_errno, "worker thread %d failed: %s",
                        MYF(0), i, args->m_err.m_message_text);

      } else {
        my_printf_error(ER_UNKNOWN_ERROR, "worker thread %d failed", MYF(0), i);
      }
      is_err = true;
    }
  }

  if (snapshot_created) {
    // Release the snapshot we created.
    snapshot_info.op = snapshot_operation::SNAPSHOT_RELEASE;
    if (ha_explicit_snapshot(thd, hton, &snapshot_info)) {
      my_printf_error(ER_UNKNOWN_ERROR, "failed to release snapshot %llu",
                      MYF(0), snapshot_info.snapshot_id);
    }
  }

  trans_rollback_stmt(thd);
  trans_rollback(thd);
  close_thread_tables(thd);

  if (!is_err) {
    DBUG_PRINT("dump", ("Finished dumping table '%s' with %" PRId64 " rows",
                        table_ref->table_name, rownum));

    char msg[256];
    snprintf(msg, sizeof(msg),
             "dump table complete: %" PRId64 " rows, %" PRId64 " chunks",
             rownum, chunk_id);
    if (send_dump_result_info(thd, chunk_id, rownum)) {
      is_err = true;
    }
  }

  return is_err;
}
