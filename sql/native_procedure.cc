#include "key.h"
#include "lock.h"
#include "records.h"
#include "sql_base.h"
#include "sql_show.h"
#include "sql_table.h"
#include "transaction.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <unordered_map>
#include <vector>

#include <native_procedure_priv.h>

static mysql_rwlock_t THR_LOCK_np;
#ifdef HAVE_PSI_INTERFACE
static PSI_rwlock_key key_THR_LOCK_np;

static PSI_rwlock_info all_rwlocks[] = {
    {&key_THR_LOCK_np, "THR_LOCK_np", PSI_FLAG_GLOBAL}};

static void init_psi_keys() {
  mysql_rwlock_register("sql", all_rwlocks, array_elements(all_rwlocks));
}
#endif

static std::unordered_map<std::string, native_proc> proc_map;

static const char *ec_errmsg[] = {
    "OK",
    "Fatal",
    "Invalid procedure arguments",
    "Network error",
    "Uninitialized",
    "Cannot reinitialize",
    "Not found",
    "Invalid arguments",
    "Unknown",
};
static_assert(array_elements(ec_errmsg) == EC_MAX + 1,
              "ec_errmsg must match EC_MAX.");

static const char system_db[] = "mysql";
static const char system_table[] = "native_proc";

static void *search_dlhandle(const std::string &dlname) {
  for (const auto &p : proc_map) {
    if (dlname == p.second.dl) {
      return p.second.dlhandle;
    }
  }
  return nullptr;
}

static int add_native_procedure(native_proc *np) {
  void *dl = nullptr;
  bool dlclose_needed = false;

  if (proc_map.count(np->name) > 0) {
    my_error(ER_NP_PROCEDURE_EXISTS, MYF(0), np->name.c_str());
    return 1;
  }

  dl = search_dlhandle(np->dl);

  // If dlhandle not present, dlopen the shared object.
  if (dl == nullptr) {
    char dlpath[FN_REFLEN];
    strxnmov(dlpath, sizeof(dlpath) - 1, opt_plugin_dir, "/", np->dl.c_str(),
             NullS);
    unpack_filename(dlpath, dlpath);

    if (!(dl = dlopen(dlpath, RTLD_NOW))) {
      const char *errmsg;
      int error_number = dlopen_errno;
      DLERROR_GENERATE(errmsg, error_number);

      my_error(ER_CANT_OPEN_LIBRARY, MYF(0), np->dl.c_str(), error_number,
               errmsg);
      return 1;
    }
    dlclose_needed = true;
  }
  np->dlhandle = dl;

  // Lookup function pointer with dlsym.
  if (!(np->proc = (proc_t *)dlsym(np->dlhandle, np->name.c_str()))) {
    my_error(ER_CANT_FIND_DL_ENTRY, MYF(0), np->name.c_str());
    if (dlclose_needed) {
      DBUG_ASSERT(dl);
      dlclose(dl);
    }
    return 1;
  }

  // Insert into proc_map.
  proc_map[np->name] = *np;

  return 0;
}

void native_procedure_init() {
  TABLE_LIST tables;
  MEM_ROOT mem;
  READ_RECORD read_record_info;
  TABLE *table;
  int error;
  DBUG_ENTER("native_procedure_init");

#ifdef HAVE_PSI_INTERFACE
  init_psi_keys();
#endif
  mysql_rwlock_init(key_THR_LOCK_np, &THR_LOCK_np);
  init_sql_alloc(&mem, UDF_ALLOC_BLOCK_SIZE, 0);

  // Initialize THD (we don't have THD during server startup).
  THD *new_thd = new THD;
  if (!new_thd) {
    // NO_LINT_DEBUG
    sql_print_error("Can't allocate memory for native procedures");
    free_root(&mem, MYF(0));
    DBUG_VOID_RETURN;
  }

  new_thd->thread_stack = (char *)&new_thd;
  new_thd->store_globals();
  new_thd->set_db(system_db, sizeof(system_db) - 1);

  // Open mysql.native_proc table.
  tables.init_one_table(system_db, sizeof(system_db), system_table,
                        sizeof(system_table), system_table, TL_READ);
  if (open_and_lock_tables(new_thd, &tables, FALSE,
                           MYSQL_LOCK_IGNORE_TIMEOUT)) {
    // NO_LINT_DEBUG
    sql_print_error("Can't open the mysql.native_proc table. "
                    "Please run mysql_upgrade to create it.");
    goto exit;
  }

  // Read records in table.
  table = tables.table;
  if (init_read_record(&read_record_info, new_thd, table, NULL, 1, 1, FALSE))
    goto exit;
  table->use_all_columns();
  while (!(error = read_record_info.read_record(&read_record_info))) {
    native_proc np;
    np.name = get_field(&mem, table->field[0]);
    np.dl = get_field(&mem, table->field[2]);
    DBUG_ASSERT(strcmp("native", get_field(&mem, table->field[1])) == 0);

    if (add_native_procedure(&np)) {
      // NO_LINT_DEBUG
      sql_print_error("Error adding native procedure: '%.64s'",
                      np.name.c_str());
    } else {
      DBUG_ASSERT(proc_map.count(np.name) > 0);
      proc_map[np.name].enabled = true;
    }
  }

  if (error > 0) {
    // NO_LINT_DEBUG
    sql_print_error("Got unknown error: %d", my_errno);
  }

  end_read_record(&read_record_info);
  // Force close to free memory.
  table->m_needs_reopen = TRUE;

exit:
  close_mysql_tables(new_thd);
  free_root(&mem, MYF(0));
  delete new_thd;
  my_pthread_setspecific_ptr(THR_THD, 0);
  DBUG_VOID_RETURN;
}

void native_procedure_destroy() {
  mysql_rwlock_destroy(&THR_LOCK_np);
  proc_map.clear();
}

void native_procedure(THD *thd, const char *packet, size_t length) {
  uint pos = 0;
  proc_t *fn = nullptr;
  const char *packet_copy;
  char proc[NAME_CHAR_LEN];

  if (thd->ec == nullptr) {
    thd->ec = new ExecutionContextImpl;
  }

  auto &ec = thd->ec;
  ec->set_thd(thd);
  packet_copy = ec->set_query(packet, length);

  thd->reset_for_next_command();

  // Length check before reading.
  sscanf(packet, "%*s %n", &pos);
  DBUG_ASSERT(pos <= length);
  if (pos >= sizeof(proc)) {
    my_printf_error(ER_NP_UNKNOWN_PROCEDURE, ER(ER_NP_UNKNOWN_PROCEDURE),
                    MYF(0), packet);
    goto exit;
  }
  sscanf(packet, "%s", proc);

  mysql_rwlock_rdlock(&THR_LOCK_np);
  if (proc_map.count(proc) > 0 && proc_map[proc].enabled) {
    proc_map[proc].count++;
    fn = proc_map[proc].proc;
  }
  mysql_rwlock_unlock(&THR_LOCK_np);
  if (fn != nullptr) {
    int ret = fn(ec, packet_copy + pos, length - pos);

    if (thd->killed_errno()) {
      thd->send_kill_message();
      thd->killed = THD::NOT_KILLED;
      thd->mysys_var->abort = 0;
    } else if (ret == EC_NP_WRONG_ARGUMENTS) {
      my_printf_error(ER_NP_WRONG_ARGUMENTS, ER(ER_NP_WRONG_ARGUMENTS), MYF(0),
                      thd->query());
    } else if (ret) {
      if (ret > 0 && ret <= EC_MAX) {
        char buf[MYSQL_ERRMSG_SIZE];
        my_snprintf(buf, sizeof(buf), "%s: %s", ec_errmsg[ret],
                    ec->m_error_message);
        my_printf_error(ER_NP_FAILED, ER(ER_NP_FAILED), MYF(0), ret, buf,
                        thd->query());
      } else {
        my_printf_error(ER_NP_FAILED, ER(ER_NP_FAILED), MYF(0), ret,
                        ec->m_error_message, thd->query());
      }
    }
    ec->index_end();
  } else {
    my_printf_error(ER_NP_UNKNOWN_PROCEDURE, ER(ER_NP_UNKNOWN_PROCEDURE),
                    MYF(0), proc);
  }

  if (!thd->is_error()) {
    my_eof(thd);
  }

exit:
  if (!thd->in_sub_stmt)
    trans_commit_stmt(thd);

  close_thread_tables(thd);

  if (thd->in_multi_stmt_transaction_mode()) {
    thd->mdl_context.release_statement_locks();
  } else {
    thd->mdl_context.release_transactional_locks();
  }

  ec->reset();

  thd->reset_query();
  if (thd->get_user_connect()) {
    thd->increment_questions_counter();
  }
}

int mysql_create_native_procedure(THD *thd, native_proc *np) {
  int error = 0;
  int rc = 1;
  TABLE *table = nullptr;
  TABLE_LIST tables;
  bool save_binlog_row_based = false;

  DBUG_ENTER("mysql_create_native_procedure");

  if (check_valid_path(np->dl.c_str(), np->dl.size())) {
    my_message(ER_UDF_NO_PATHS, ER(ER_UDF_NO_PATHS), MYF(0));
    DBUG_RETURN(1);
  }

  if (np->name.size() > NAME_CHAR_LEN) {
    my_error(ER_TOO_LONG_IDENT, MYF(0), np->name.c_str());
    DBUG_RETURN(1);
  }

  tables.init_one_table(system_db, sizeof(system_db), system_table,
                        sizeof(system_table), system_table, TL_WRITE);
  if (!(table =
            open_ltable(thd, &tables, TL_WRITE, MYSQL_LOCK_IGNORE_TIMEOUT))) {
    DBUG_RETURN(1);
  }

  // Turn off row binlogging of this statement and use statement-based so that
  // all supporting tables are updated for CREATE FUNCTION command.
  if ((save_binlog_row_based = thd->is_current_stmt_binlog_format_row()))
    thd->clear_current_stmt_binlog_format_row();

  mysql_rwlock_wrlock(&THR_LOCK_np);
  if (add_native_procedure(np)) {
    mysql_rwlock_unlock(&THR_LOCK_np);
    goto exit;
  }

  // Create entry in mysql.native_proc table
  table->use_all_columns();
  restore_record(table, s->default_values);
  table->field[0]->store(np->name.c_str(), np->name.size(),
                         system_charset_info);
  table->field[1]->store((longlong)1, TRUE); // type
  table->field[2]->store(np->dl.c_str(), np->dl.size(), system_charset_info);
  table->field[3]->store("", 0, system_charset_info); // lua script
  error = table->file->ha_write_row(table->record[0]);

  if (error) {
    char errbuf[MYSYS_STRERROR_SIZE];
    my_error(ER_ERROR_ON_WRITE, MYF(0), "mysql.native_proc", error,
             my_strerror(errbuf, sizeof(errbuf), error));

    DBUG_ASSERT(proc_map.count(np->name) > 0);
    proc_map.erase(np->name);

    DBUG_ASSERT(np->dlhandle != nullptr);

    // dlclose the handle if we are the last owners of it.
    if (!search_dlhandle(np->dl)) {
      dlclose(np->dlhandle);
    }

    mysql_rwlock_unlock(&THR_LOCK_np);
    goto exit;
  }

  // Binlog the create function.
  if (write_bin_log(thd, TRUE, thd->query(), thd->query_length())) {
    mysql_rwlock_unlock(&THR_LOCK_np);
    goto exit;
  }
  rc = 0;

  // After things are successful, enable the procedure.
  proc_map[np->name].enabled = true;
  mysql_rwlock_unlock(&THR_LOCK_np);

exit:
  DBUG_ASSERT(!thd->is_current_stmt_binlog_format_row());
  if (save_binlog_row_based)
    thd->set_current_stmt_binlog_format_row();
  DBUG_RETURN(rc);
}

int mysql_drop_native_procedure(THD *thd, native_proc *np) {
  int rc = 1;
  TABLE *table = nullptr;
  TABLE_LIST tables;
  bool save_binlog_row_based = false;

  DBUG_ENTER("mysql_drop_native_procedure");

  tables.init_one_table(system_db, sizeof(system_db), system_table,
                        sizeof(system_table), system_table, TL_WRITE);
  if (!(table = open_ltable(thd, &tables, TL_WRITE, MYSQL_LOCK_IGNORE_TIMEOUT)))
    DBUG_RETURN(1);

  if ((save_binlog_row_based = thd->is_current_stmt_binlog_format_row()))
    thd->clear_current_stmt_binlog_format_row();

  mysql_rwlock_wrlock(&THR_LOCK_np);
  if (proc_map.count(np->name) == 0 || !proc_map[np->name].enabled) {
    mysql_rwlock_unlock(&THR_LOCK_np);
    my_error(ER_NP_UNKNOWN_PROCEDURE, MYF(0), np->name.c_str());
    goto exit;
  }

  // We don't call dlclose on the handle by design. It is generally hard to
  // ensure safety in this case.
  proc_map[np->name].enabled = false;
  mysql_rwlock_unlock(&THR_LOCK_np);

  table->use_all_columns();
  table->field[0]->store(np->name.c_str(), np->name.size(), &my_charset_bin);
  if (!table->file->ha_index_read_idx_map(table->record[0], 0,
                                          (uchar *)table->field[0]->ptr,
                                          HA_WHOLE_KEY, HA_READ_KEY_EXACT)) {
    int delete_err;
    if ((delete_err = table->file->ha_delete_row(table->record[0]))) {
      table->file->print_error(delete_err, MYF(0));
      goto exit;
    }
  }

  if (!write_bin_log(thd, TRUE, thd->query(), thd->query_length())) {
    rc = 0;
  }

exit:
  /* Restore the state of binlog format */
  DBUG_ASSERT(!thd->is_current_stmt_binlog_format_row());
  if (save_binlog_row_based)
    thd->set_current_stmt_binlog_format_row();
  DBUG_RETURN(rc);
}

void ExecutionContextImpl::set_thd(THD *thd) { m_thd = thd; }

const char *ExecutionContextImpl::set_query(const char *packet, size_t length) {
  if (m_query_buffer.size() < length + 1) {
    m_query_buffer.resize(length + 1);
  }

  memcpy(&m_query_buffer[0], packet, length);
  m_query_buffer[length] = '\0';
  m_thd->set_query((char *)&m_query_buffer[0], length, m_thd->charset());

  return (const char *)&m_query_buffer[0];
}

void ExecutionContextImpl::index_end() {
  if (m_key) {
    m_table->set_keyread(false);
    m_table->file->ha_index_end();
  }
}

bool ExecutionContextImpl::field_covered(const char *name) {
  for (uint i = 0; i < m_key->actual_key_parts; i++) {
    if (strcmp(m_key->key_part[i].field->field_name, name) == 0) {
      return m_table->file->index_flags(m_keynr, i, false) & HA_KEYREAD_ONLY;
    }
  }

  return false;
}

bool ExecutionContextImpl::check_keyread() {
  for (auto field : m_fields) {
    if (!field_covered(field->field_name)) {
      return false;
    }
  }

  return true;
}

/////////////////////////////////////////
// Public API implementation begins here.
/////////////////////////////////////////

int ExecutionContextImpl::open_table(const char *db, const char *table) {
  if (m_fatal) {
    return EC_FATAL;
  }

  if (m_table != nullptr) {
    return EC_REINIT;
  }

  m_tl.init_one_table(C_STRING_WITH_LEN(db ? db : m_thd->db),
                      C_STRING_WITH_LEN(table), table, TL_READ);
  m_tl.mdl_request.init(MDL_key::TABLE, db ? db : m_thd->db, table,
                        MDL_SHARED_READ, MDL_TRANSACTION);
  Open_table_context ot_act(m_thd, 0);
  if (::open_table(m_thd, &m_tl, &ot_act)) {
    return EC_UNKNOWN;
  }

  m_table = m_tl.table;
  m_table->clear_column_bitmaps();
  m_table->reginfo.lock_type = TL_READ;
  m_thd->lex->sql_command = SQLCOM_SELECT;
  m_thd->lock = mysql_lock_tables(m_thd, &m_table, 1, 0);
  if (m_thd->lock == nullptr) {
    m_fatal = true;
    return EC_FATAL;
  }

  return EC_OK;
}

int ExecutionContextImpl::set_read_columns(std::vector<const char *> columns) {
  if (m_fatal) {
    return EC_FATAL;
  }

  if (m_table == nullptr) {
    return EC_UNINIT;
  }

  if (m_fields.size() > 0 || m_columns.size() > 0) {
    return EC_REINIT;
  }

  if (columns.size() == 0) {
    return EC_INVAL;
  }

  for (const char *column : columns) {
    Field *field = nullptr;
    for (Field **f = m_table->field; *f != nullptr; f++) {
      if (strcmp((*f)->field_name, column) == 0) {
        field = *f;
        break;
      }
    }

    if (field == nullptr) {
      return EC_NOT_FOUND;
    }

    bitmap_set_bit(m_table->read_set, field->field_index);
    m_fields.push_back(field);
    m_columns.push_back(column);
  }

  return EC_OK;
}

int ExecutionContextImpl::send_metadata() {
  if (m_fatal) {
    return EC_FATAL;
  }

  THD *thd = m_thd;
  thd->protocol->reset();
  uchar buff[8];
  std::pair<const char *, size_t> *metadata_fields;
  char metadata[12] = {0};

  if (m_metadata_sent) {
    return EC_REINIT;
  }
  m_metadata_sent = true;

  DBUG_ASSERT(m_fields.size() == m_columns.size());
  if (m_table == nullptr || m_columns.size() == 0) {
    return EC_UNINIT;
  }

  // Write column count.
  uchar *pos = net_store_length(buff, m_columns.size());
  if (my_net_write(thd->get_net(), buff, (size_t)(pos - buff))) {
    return EC_NET_ERR;
  }

  std::array<std::pair<const char *, size_t>, 7> standard_metadata = {
      {{"def", 3},
       {m_table->s->db.str, strlen(m_table->s->db.str)},
       {m_table->s->table_name.str, strlen(m_table->s->table_name.str)},
       {m_table->s->table_name.str, strlen(m_table->s->table_name.str)},
       {nullptr, 0},
       {nullptr, 0},
       {metadata, sizeof(metadata)}}};

  std::array<std::pair<const char *, size_t>, 7> minimal_metadata = {
      {{"def", 3},
       {nullptr, 0},
       {nullptr, 0},
       {nullptr, 0},
       {nullptr, 0},
       {nullptr, 0},
       {metadata, sizeof(metadata)}}};

  if (thd->variables.protocol_mode == PROTO_MODE_MINIMAL_OBJECT_NAMES_IN_RSMD)
    metadata_fields = minimal_metadata.data();
  else
    metadata_fields = standard_metadata.data();
  int2store(metadata, my_charset_bin.number);

  // Send column metadata.
  for (uint i = 0; i < m_columns.size(); i++) {
    bool res = false;
    thd->protocol->prepare_for_resend();

    metadata_fields[4].first = m_columns[i];
    metadata_fields[4].second = strlen(m_columns[i]);
    standard_metadata[5].first = m_columns[i];
    standard_metadata[5].second = strlen(m_columns[i]);

    int4store(metadata + 2, m_fields[i]->max_display_length());
    metadata[6] = m_fields[i]->type();
    uint32 flags = m_fields[i]->flags;
    flags = m_fields[i]->table->maybe_null ? (flags & ~NOT_NULL_FLAG) : flags;
    int2store(metadata + 7, flags);
    metadata[9] = m_fields[i]->decimals();

    for (uint j = 0; j < array_elements(standard_metadata); j++) {
      res |= thd->protocol->store(
          metadata_fields[j].first, metadata_fields[j].second,
          system_charset_info, thd->variables.character_set_results);
    }

    thd->protocol->update_checksum();
    if (res || thd->protocol->write()) {
      m_fatal = true;
      return EC_NET_ERR;
    }
  }

  // Send eof packet if necessary.
  if (!(thd->client_capabilities & CLIENT_DEPRECATE_EOF)) {
    if (net_send_eof(thd, thd->server_status,
                     thd->get_stmt_da()->current_statement_warn_count()))
      return EC_NET_ERR;
  }

  return EC_OK;
}

int ExecutionContextImpl::send_row() {
  if (m_fatal) {
    return EC_FATAL;
  }

  THD *thd = m_thd;
  uchar buff[MAX_FIELD_WIDTH];
  String tmp((char *)buff, sizeof(buff), &my_charset_bin);

  thd->protocol->prepare_for_resend();
  thd->protocol->prepare_for_send(m_columns.size());

  DBUG_ASSERT(m_fields.size() == m_columns.size());

  for (auto field : m_fields) {
    if (field->is_real_null()) {
      thd->protocol->store_null();
    } else {
      String *v = field->val_str(&tmp);
      thd->protocol->store(v->ptr(), v->length(), v->charset());
    }
  }

  thd->protocol->update_checksum();
  if (thd->protocol->write()) {
    m_fatal = true;
    return EC_NET_ERR;
  }

  thd->inc_sent_row_count(1);
  thd->status_var.rows_sent++;
  return EC_OK;
}

int ExecutionContextImpl::index_init(const char *index_name) {
  if (m_fatal) {
    return EC_FATAL;
  }

  if (m_table == nullptr || index_name == nullptr) {
    return EC_UNINIT;
  }

  if (m_key != nullptr) {
    return EC_REINIT;
  }

  int ret;
  for (uint keynr = 0; keynr < m_table->s->keynames.count; ++keynr) {
    if (strcmp(m_table->s->keynames.type_names[keynr], index_name) == 0) {
      if ((ret = m_table->file->ha_index_init(keynr, true /* sorted */))) {
        return ret;
      }
      m_key = m_table->key_info + keynr;
      m_keynr = keynr;
      return EC_OK;
    }
  }
  return EC_NOT_FOUND;
}

int ExecutionContextImpl::index_read_map(uint idx, enum ha_rkey_function flag) {
  if (m_fatal) {
    return EC_FATAL;
  }

  if (m_table == nullptr || m_key == nullptr) {
    return EC_UNINIT;
  }

  if (idx > 1) {
    return EC_INVAL;
  }

  m_table->set_keyread(check_keyread());

  return m_table->file->ha_index_read_map(
      m_table->record[0], &m_key_buffer[idx][0], m_keypart_map[idx], flag);
}

int ExecutionContextImpl::key_write(
    uint idx, const std::map<const char *, Value> &keyparts) {
  int ret = EC_OK;
  key_part_map full_map = 0;
  uint key_length = 0;

  if (m_fatal) {
    return EC_FATAL;
  }

  DBUG_ASSERT(m_key->actual_key_parts <= MAX_REF_PARTS);
  if (m_key == nullptr) {
    return EC_UNINIT;
  }

  if (idx > 1) {
    return EC_INVAL;
  }

  if (keyparts.size() > MAX_REF_PARTS || keyparts.size() == 0) {
    return EC_INVAL;
  }

  m_keypart_map[idx] = 0;
  auto save_write_set = m_table->write_set;
  m_table->write_set = nullptr;
  auto save_count_cuted_fields = m_thd->count_cuted_fields;
  m_thd->count_cuted_fields = CHECK_FIELD_WARN;
  for (const auto &kp : keyparts) {
    bool found = false;
    for (uint i = 0; i < m_key->actual_key_parts; i++) {
      if (strcmp(m_key->key_part[i].field->field_name, kp.first) == 0) {
        type_conversion_status s = TYPE_OK;
        auto &v = kp.second;

        switch (v.type) {
        case Value::Type::NONE:
          m_key->key_part[i].field->set_null();
          break;
        case Value::Type::STRING:
          s = m_key->key_part[i].field->store(v.str.val, v.str.len,
                                              &my_charset_bin);
          break;
        case Value::Type::INTEGER:
          s = m_key->key_part[i].field->store(v.num.val, v.num.unsigned_val);
          break;
        case Value::Type::DOUBLE:
          s = m_key->key_part[i].field->store(v.real.val);
          break;
        default:
          DBUG_ASSERT(false);
        }

        if (s != TYPE_OK) {
          my_snprintf(m_error_message, sizeof(m_error_message),
                      "Conversion failed for field %s.", kp.first);
          ret = EC_INVAL;
          goto exit;
        }

        m_keypart_map[idx] |= (1 << i);
        key_length += m_key->key_part[i].store_length;
        found = true;
        break;
      }
    }

    if (!found) {
      ret = EC_NOT_FOUND;
      goto exit;
    }
  }

  // Check that all keyparts were initialized via key_write.
  full_map = (1 << keyparts.size()) - 1;
  if (full_map != m_keypart_map[idx]) {
    m_keypart_map[idx] = 0;
    my_snprintf(m_error_message, sizeof(m_error_message),
                "Not all keyparts are initialized");
    ret = EC_INVAL;
    goto exit;
  }

  m_key_buffer[idx].resize(key_length);
  key_copy(&m_key_buffer[idx][0], m_table->record[0], m_key, key_length);

exit:
  m_table->write_set = save_write_set;
  m_thd->count_cuted_fields = save_count_cuted_fields;
  return ret;
}

int ExecutionContextImpl::key_part_count(uint *out) {
  if (m_fatal) {
    return EC_FATAL;
  }

  if (out == nullptr) {
    return EC_INVAL;
  }

  if (m_key == nullptr) {
    return EC_UNINIT;
  }

  *out = m_key->actual_key_parts;
  return EC_OK;
}

int ExecutionContextImpl::key_part_name(uint index, const char **out) {
  if (m_fatal) {
    return EC_FATAL;
  }

  if (m_key == nullptr) {
    return EC_UNINIT;
  }

  if (out == nullptr || index >= m_key->actual_key_parts) {
    return EC_INVAL;
  }

  *out = m_key->key_part[index].field->field_name;
  return EC_OK;
}

bool ExecutionContextImpl::range_ended() {
  if (m_end_key.keypart_map) {
    int ret = m_table->file->compare_key(&m_end_key);
    if (m_asc < 0) {
      ret = -ret;
    }

    if (ret > 0) {
      return true;
    }
  }
  return false;
}

int ExecutionContextImpl::read_range_first(uint range_flag, bool asc) {
  int ret = 0;
  if (m_fatal) {
    return EC_FATAL;
  }

  if (m_key == nullptr) {
    return EC_UNINIT;
  }

  uint idx;
  enum handler::enum_range_scan_direction dir;
  enum ha_rkey_function start_flag;
  enum ha_rkey_function end_flag;
  decltype(&handler::ha_index_first) fn = nullptr;
  key_range start_key;

  if (asc) {
    m_asc = 1;
    idx = 0;
    dir = handler::RANGE_SCAN_ASC;
    // Flags inspired by quick_range_seq_next.
    start_flag =
        (range_flag & NEAR_MIN) ? HA_READ_AFTER_KEY : HA_READ_KEY_OR_NEXT;
    end_flag = (range_flag & NEAR_MAX) ? HA_READ_BEFORE_KEY : HA_READ_AFTER_KEY;
    fn = &handler::ha_index_first;
  } else {
    m_asc = -1;
    idx = 1;
    dir = handler::RANGE_SCAN_DESC;
    // Flags inspired by QUICK_RANGE::make_min_endpoint and
    // QUICK_SELECT_DESC::get_next.
    start_flag = (range_flag & NEAR_MAX) ? HA_READ_BEFORE_KEY
                                         : HA_READ_PREFIX_LAST_OR_PREV;
    end_flag =
        (range_flag & NEAR_MIN) ? HA_READ_AFTER_KEY : HA_READ_KEY_OR_NEXT;
    fn = &handler::ha_index_last;
  }

  start_key.key = &m_key_buffer[idx][0];
  start_key.length = m_key_buffer[idx].size();
  start_key.keypart_map = m_keypart_map[idx];

  m_end_key.key = &m_key_buffer[1 - idx][0];
  m_end_key.length = m_key_buffer[1 - idx].size();
  m_end_key.keypart_map = m_keypart_map[1 - idx];

  m_end_key.flag = end_flag;

  if (m_end_key.keypart_map) {
    m_table->file->set_end_range(&m_end_key, dir);
  }

  m_table->set_keyread(check_keyread());

  if (start_key.keypart_map == 0) {
    ret = (m_table->file->*fn)(m_table->record[0]);
  } else {
    ret = m_table->file->ha_index_read_map(m_table->record[0], start_key.key,
                                           start_key.keypart_map, start_flag);
  }

  if (ret) {
    return ret;
  }

  if (range_ended()) {
    return HA_ERR_END_OF_FILE;
  }

  return EC_OK;
}

int ExecutionContextImpl::read_range_next() {
  if (m_fatal) {
    return EC_FATAL;
  }

  if (m_asc == 0) {
    return EC_UNINIT;
  }

  if (m_thd->killed) {
    return EC_UNKNOWN;
  }

  int res = 0;
  if (m_asc > 0) {
    res = m_table->file->ha_index_next(m_table->record[0]);
  } else {
    res = m_table->file->ha_index_prev(m_table->record[0]);
  }

  if (res) {
    return res;
  }

  if (range_ended()) {
    return HA_ERR_END_OF_FILE;
  }

  return EC_OK;
}

int ExecutionContextImpl::field_is_null(uint index, bool *out) {
  if (m_fatal) {
    return EC_FATAL;
  }

  if (out == nullptr || index >= m_fields.size()) {
    return EC_INVAL;
  }

  *out = m_fields[index]->is_real_null();
  return EC_OK;
}

int ExecutionContextImpl::field_val_int(uint index, longlong *out) {
  if (m_fatal) {
    return EC_FATAL;
  }

  if (out == nullptr || index >= m_fields.size()) {
    return EC_INVAL;
  }

  *out = m_fields[index]->val_int();
  return EC_OK;
}

int ExecutionContextImpl::field_val_real(uint index, double *out) {
  if (m_fatal) {
    return EC_FATAL;
  }

  if (out == nullptr || index >= m_fields.size()) {
    return EC_INVAL;
  }

  *out = m_fields[index]->val_real();
  return EC_OK;
}

int ExecutionContextImpl::field_val_str(uint index, std::string *out) {
  uchar buff[MAX_FIELD_WIDTH];
  String tmp((char *)buff, sizeof(buff), &my_charset_bin);

  if (m_fatal) {
    return EC_FATAL;
  }

  if (out == nullptr || index >= m_fields.size()) {
    return EC_INVAL;
  }

  String *v = m_fields[index]->val_str(&tmp);
  *out = {v->ptr(), v->length()};
  return EC_OK;
}

ST_FIELD_INFO native_procs_fields_info[] = {
    {"NAME", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
    {"STATUS", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
    {"LIBRARY", NAME_LEN, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE},
    {"CALLS", 10, MYSQL_TYPE_LONGLONG, 0, MY_I_S_UNSIGNED, 0, SKIP_OPEN_TABLE},
    {0, 0, MYSQL_TYPE_STRING, 0, 0, 0, SKIP_OPEN_TABLE}};

int fill_native_procs(THD *thd, TABLE_LIST *tables, Item *cond) {
  DBUG_ENTER("fill_native_procs");
  int ret = 0;
  TABLE *table = tables->table;
  Field **fields = table->field;

  mysql_rwlock_rdlock(&THR_LOCK_np);
  for (const auto &v : proc_map) {
    fields[0]->store(v.second.name.c_str(), v.second.name.size(),
                     system_charset_info);
    if (v.second.enabled) {
      fields[1]->store("ENABLED", 7, system_charset_info);
    } else {
      fields[1]->store("DISABLED", 8, system_charset_info);
    }
    fields[2]->store(v.second.dl.c_str(), v.second.dl.size(),
                     system_charset_info);
    fields[3]->store(v.second.count.load(), TRUE);

    if (schema_table_store_record(thd, table)) {
      ret = -1;
      break;
    }
  }
  mysql_rwlock_unlock(&THR_LOCK_np);

  DBUG_RETURN(ret);
}
