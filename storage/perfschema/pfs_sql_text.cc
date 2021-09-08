/* Copyright (c) 2013, 2019, Oracle and/or its affiliates. All rights reserved.

  This sql_text is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2.0,
  as published by the Free Software Foundation.

  This sql_text is also distributed with certain software (including
  but not limited to OpenSSL) that is licensed under separate terms,
  as designated in a particular file or component or in included license
  documentation.  The authors of MySQL hereby grant you an additional
  permission to link the sql_text and your derivative works with the
  separately licensed software that they have included with MySQL.

  This sql_text is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License, version 2.0, for more details.

  You should have received a copy of the GNU General Public License
  along with this sql_text; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/**
  @file storage/perfschema/pfs_sql_text.cc
  Statement Digest data structures (implementation).
*/

#include "storage/perfschema/pfs_sql_text.h"

#include <string.h>

#include "storage/perfschema/pfs_buffer_container.h"
#include "storage/perfschema/pfs_builtin_memory.h"
#include "storage/perfschema/pfs_global.h"

size_t sql_text_size = 0;
LF_HASH sql_text_hash;
static bool sql_text_hash_inited = false;
static PFS_cacheline_atomic_uint32 sql_text_monotonic_index;
static unsigned char *statements_digest_token_array = nullptr;

/**
  Initialize table SQL_TEXT
  @param param performance schema sizing
*/
int init_sql_text(const PFS_global_param *param) {
  sql_text_size = param->m_sql_text_sizing;
  if (sql_text_size == 0) return 0;

  if (global_sql_text_container.init(sql_text_size)) {
    return 1;
  }
  sql_text_monotonic_index.m_u32.store(0);
  if (pfs_max_digest_length > 0) {
    /* Size of each digest array. */
    size_t digest_memory_size = pfs_max_digest_length * sizeof(unsigned char);

    statements_digest_token_array =
        PFS_MALLOC_ARRAY(&builtin_memory_digest_tokens, sql_text_size,
                         digest_memory_size, unsigned char, MYF(MY_ZEROFILL));

    if (unlikely(statements_digest_token_array == nullptr)) {
      cleanup_sql_text();
      return 1;
    }
  }
  reset_sql_text();
  return 0;
}

/** Cleanup table SQL_TEXT. */
void cleanup_sql_text(void) {
  if (sql_text_size == 0) return;
  if (pfs_max_digest_length > 0)
    PFS_FREE_ARRAY(&builtin_memory_digest_tokens, sql_text_size,
                   (pfs_max_digest_length * sizeof(unsigned char)),
                   statements_digest_token_array);
  global_sql_text_container.cleanup();
  sql_text_monotonic_index.m_u32.store(0);
}

static const uchar *sql_text_hash_get_key(const uchar *entry, size_t *length) {
  const PFS_sql_text *const *typed_entry;
  const PFS_sql_text *sql_text;
  const void *result;
  typed_entry = reinterpret_cast<const PFS_sql_text *const *>(entry);
  assert(typed_entry != nullptr);
  sql_text = *typed_entry;
  assert(sql_text != nullptr);
  *length = sizeof(sql_text->m_key.m_hash_key);
  result = sql_text->m_key.m_hash_key;
  return reinterpret_cast<const uchar *>(result);
}

/**
  Initialize the sql_text hash.
  @return 0 on success
*/
int init_sql_text_hash(const PFS_global_param *param) {
  if ((!sql_text_hash_inited) && (param->m_sql_text_sizing != 0)) {
    lf_hash_init(&sql_text_hash, sizeof(PFS_sql_text *), LF_HASH_UNIQUE, 0, 0,
                 sql_text_hash_get_key, &my_charset_bin);
    sql_text_hash_inited = true;
  }
  return 0;
}

/** Cleanup the sql_text hash. */
void cleanup_sql_text_hash(void) {
  if (sql_text_hash_inited) {
    lf_hash_destroy(&sql_text_hash);
    sql_text_hash_inited = false;
  }
}

static LF_PINS *get_sql_text_hash_pins(PFS_thread *thread) {
  if (unlikely(thread->m_sql_text_hash_pins == nullptr)) {
    if (!sql_text_hash_inited) {
      return nullptr;
    }
    thread->m_sql_text_hash_pins = lf_hash_get_pins(&sql_text_hash);
  }
  return thread->m_sql_text_hash_pins;
}

PFS_sql_text *find_or_create_sql_text(
    PFS_thread *thread, const sql_digest_storage *digest_storage) {
  LF_PINS *pins = get_sql_text_hash_pins(thread);
  if (unlikely(pins == nullptr)) {
    global_sql_text_container.m_lost++;
    return nullptr;
  }

  /* Prepare sql_text key */
  PFS_sql_text_key key;
  memcpy(&key.m_hash_key, digest_storage->m_hash, DIGEST_HASH_SIZE);

  PFS_sql_text **entry;
  PFS_sql_text *pfs = nullptr;
  uint retry_count = 0;
  const uint retry_max = 3;
  pfs_dirty_state dirty_state;

search:
  entry = reinterpret_cast<PFS_sql_text **>(lf_hash_search(
      &sql_text_hash, pins, key.m_hash_key, sizeof(key.m_hash_key)));

  if (entry && (entry != MY_LF_ERRPTR)) {
    /* If record already exists then return its pointer. */
    pfs = *entry;
    lf_hash_search_unpin(pins);
    return pfs;
  }

  lf_hash_search_unpin(pins);

  auto safe_index = sql_text_monotonic_index.m_u32++;
  if (safe_index >= sql_text_size) {
    global_sql_text_container.m_lost++;
    return nullptr;
  }

  /* Else create a new record in sql_text stat array. */
  pfs = global_sql_text_container.allocate(&dirty_state);
  if (pfs != nullptr) {
    memcpy(&pfs->m_key, &key, sizeof(PFS_sql_text_key));
    // copy digest_storage
    pfs->m_digest_storage.reset(
        statements_digest_token_array + safe_index * pfs_max_digest_length,
        pfs_max_digest_length);
    pfs->m_digest_storage.copy(digest_storage);
    /* Insert this record. */
    pfs->m_lock.dirty_to_allocated(&dirty_state);
    int res = lf_hash_insert(&sql_text_hash, pins, &pfs);

    if (likely(res == 0)) {
      return pfs;
    }
    global_sql_text_container.deallocate(pfs);

    if (res > 0) {
      /* Duplicate insert by another thread */
      if (++retry_count > retry_max) {
        /* Avoid infinite loops */
        global_sql_text_container.m_lost++;
        return nullptr;
      }
      goto search;
    }
    /* OOM in lf_hash_insert */
    global_sql_text_container.m_lost++;
    return nullptr;
  }

  return nullptr;
}

static void purge_sql_text(PFS_thread *thread, PFS_sql_text *pfs) {
  LF_PINS *pins = get_sql_text_hash_pins(thread);
  if (unlikely(pins == nullptr)) {
    return;
  }

  PFS_sql_text **entry;
  entry = reinterpret_cast<PFS_sql_text **>(
      lf_hash_search(&sql_text_hash, pins, pfs->m_key.m_hash_key,
                     sizeof(pfs->m_key.m_hash_key)));
  if (entry && (entry != MY_LF_ERRPTR)) {
    assert(*entry == pfs);
    lf_hash_delete(&sql_text_hash, pins, pfs->m_key.m_hash_key,
                   sizeof(pfs->m_key.m_hash_key));
    global_sql_text_container.deallocate(pfs);
  }

  lf_hash_search_unpin(pins);
}

class Proc_purge_sql_text : public PFS_buffer_processor<PFS_sql_text> {
 public:
  explicit Proc_purge_sql_text(PFS_thread *thread) : m_thread(thread) {}

  virtual void operator()(PFS_sql_text *pfs) override {
    purge_sql_text(m_thread, pfs);
  }

 private:
  PFS_thread *m_thread;
};

void reset_sql_text() {
  PFS_thread *thread = PFS_thread::get_current_thread();
  if (unlikely(thread == nullptr)) {
    return;
  }

  Proc_purge_sql_text proc(thread);
  global_sql_text_container.apply(proc);
  sql_text_monotonic_index.m_u32.store(0);
}
