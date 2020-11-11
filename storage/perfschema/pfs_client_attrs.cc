/* Copyright (c) 2013, 2019, Oracle and/or its affiliates. All rights reserved.

  This client_attrs is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2.0,
  as published by the Free Software Foundation.

  This client_attrs is also distributed with certain software (including
  but not limited to OpenSSL) that is licensed under separate terms,
  as designated in a particular file or component or in included license
  documentation.  The authors of MySQL hereby grant you an additional
  permission to link the client_attrs and your derivative works with the
  separately licensed software that they have included with MySQL.

  This client_attrs is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License, version 2.0, for more details.

  You should have received a copy of the GNU General Public License
  along with this client_attrs; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/**
  @file storage/perfschema/pfs_client_attrs.cc
  Statement Digest data structures (implementation).
*/

#include "storage/perfschema/pfs_client_attrs.h"

#include <string.h>

#include "my_compiler.h"
#include "my_dbug.h"
#include "my_inttypes.h"
#include "my_sys.h"
#include "sql/mysqld.h"  //system_charset_info
#include "sql_string.h"
#include "storage/perfschema/pfs_buffer_container.h"
#include "storage/perfschema/pfs_global.h"
#include "storage/perfschema/pfs_instr.h"
#include "storage/perfschema/pfs_setup_object.h"

LF_HASH client_attrs_hash;
static bool client_attrs_hash_inited = false;

/**
  Initialize table CLIENT_ATTRIBUTES.
  @param param performance schema sizing
*/
int init_client_attrs(const PFS_global_param *param) {
  if (global_client_attrs_container.init(param->m_client_attrs_sizing)) {
    return 1;
  }

  reset_client_attrs();
  return 0;
}

/** Cleanup table CLIENT_ATTRIBUTES. */
void cleanup_client_attrs(void) { global_client_attrs_container.cleanup(); }

static const uchar *client_attrs_hash_get_key(const uchar *entry,
                                              size_t *length) {
  const PFS_client_attrs *const *typed_entry;
  const PFS_client_attrs *client_attrs;
  const void *result;
  typed_entry = reinterpret_cast<const PFS_client_attrs *const *>(entry);
  assert(typed_entry != nullptr);
  client_attrs = *typed_entry;
  assert(client_attrs != nullptr);
  *length = sizeof(client_attrs->m_key.m_hash_key);
  result = client_attrs->m_key.m_hash_key;
  return reinterpret_cast<const uchar *>(result);
}

/**
  Initialize the client_attrs hash.
  @return 0 on success
*/
int init_client_attrs_hash(const PFS_global_param *param) {
  if ((!client_attrs_hash_inited) && (param->m_client_attrs_sizing != 0)) {
    lf_hash_init(&client_attrs_hash, sizeof(PFS_client_attrs *), LF_HASH_UNIQUE,
                 0, 0, client_attrs_hash_get_key, &my_charset_bin);
    client_attrs_hash_inited = true;
  }
  return 0;
}

/** Cleanup the client_attrs hash. */
void cleanup_client_attrs_hash(void) {
  if (client_attrs_hash_inited) {
    lf_hash_destroy(&client_attrs_hash);
    client_attrs_hash_inited = false;
  }
}

static LF_PINS *get_client_attrs_hash_pins(PFS_thread *thread) {
  if (unlikely(thread->m_client_attrs_hash_pins == nullptr)) {
    if (!client_attrs_hash_inited) {
      return nullptr;
    }
    thread->m_client_attrs_hash_pins = lf_hash_get_pins(&client_attrs_hash);
  }
  return thread->m_client_attrs_hash_pins;
}

PFS_client_attrs *find_or_create_client_attrs(PFS_thread *thread,
                                              const uchar *client_id,
                                              const char *client_attrs,
                                              uint client_attrs_length) {
  LF_PINS *pins = get_client_attrs_hash_pins(thread);
  if (unlikely(pins == nullptr)) {
    global_client_attrs_container.m_lost++;
    return nullptr;
  }

  /* Prepare client_attrs key */
  PFS_client_attrs_key key;
  memcpy(&key.m_hash_key, client_id, MD5_HASH_SIZE);

  PFS_client_attrs **entry;
  PFS_client_attrs *pfs = nullptr;
  uint retry_count = 0;
  const uint retry_max = 3;
  pfs_dirty_state dirty_state;

search:
  entry = reinterpret_cast<PFS_client_attrs **>(lf_hash_search(
      &client_attrs_hash, pins, key.m_hash_key, sizeof(key.m_hash_key)));

  if (entry && (entry != MY_LF_ERRPTR)) {
    /* If record already exists then return its pointer. */
    pfs = *entry;
    lf_hash_search_unpin(pins);
    return pfs;
  }

  lf_hash_search_unpin(pins);

  /* Else create a new record in client_attrs stat array. */
  pfs = global_client_attrs_container.allocate(&dirty_state);
  if (pfs != nullptr) {
    memcpy(&pfs->m_key, &key, sizeof(PFS_client_attrs_key));
    memcpy(pfs->m_client_attrs, client_attrs, client_attrs_length);
    pfs->m_client_attrs_length = client_attrs_length;

    /* Insert this record. */
    pfs->m_lock.dirty_to_allocated(&dirty_state);
    int res = lf_hash_insert(&client_attrs_hash, pins, &pfs);

    if (likely(res == 0)) {
      return pfs;
    }

    global_client_attrs_container.deallocate(pfs);

    if (res > 0) {
      /* Duplicate insert by another thread */
      if (++retry_count > retry_max) {
        /* Avoid infinite loops */
        global_client_attrs_container.m_lost++;
        return nullptr;
      }
      goto search;
    }
    /* OOM in lf_hash_insert */
    global_client_attrs_container.m_lost++;
    return nullptr;
  }

  return nullptr;
}

static void purge_client_attrs(PFS_thread *thread, PFS_client_attrs *pfs) {
  LF_PINS *pins = get_client_attrs_hash_pins(thread);
  if (unlikely(pins == nullptr)) {
    return;
  }

  PFS_client_attrs **entry;
  entry = reinterpret_cast<PFS_client_attrs **>(
      lf_hash_search(&client_attrs_hash, pins, pfs->m_key.m_hash_key,
                     sizeof(pfs->m_key.m_hash_key)));
  if (entry && (entry != MY_LF_ERRPTR)) {
    assert(*entry == pfs);
    lf_hash_delete(&client_attrs_hash, pins, pfs->m_key.m_hash_key,
                   sizeof(pfs->m_key.m_hash_key));
    global_client_attrs_container.deallocate(pfs);
  }

  lf_hash_search_unpin(pins);
}

class Proc_purge_client_attrs : public PFS_buffer_processor<PFS_client_attrs> {
 public:
  explicit Proc_purge_client_attrs(PFS_thread *thread) : m_thread(thread) {}

  virtual void operator()(PFS_client_attrs *pfs) override {
    purge_client_attrs(m_thread, pfs);
  }

 private:
  PFS_thread *m_thread;
};

void reset_client_attrs() {
  PFS_thread *thread = PFS_thread::get_current_thread();
  if (unlikely(thread == nullptr)) {
    return;
  }

  Proc_purge_client_attrs proc(thread);
  global_client_attrs_container.apply(proc);
}
