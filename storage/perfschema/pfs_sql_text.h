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

#ifndef PFS_SQL_TEXT_H
#define PFS_SQL_TEXT_H

/**
  @file storage/perfschema/pfs_sql_text.h
  Stored Program data structures (declarations).
*/

#include <sys/types.h>
#include "storage/perfschema/pfs_global.h"
#include "storage/perfschema/pfs_instr.h"

extern LF_HASH sql_text_hash;
extern size_t sql_text_size;

/**
  Hash key for a sql text.
*/
struct PFS_sql_text_key {
  uchar m_hash_key[DIGEST_HASH_SIZE];
};

struct PFS_ALIGNED PFS_sql_text : public PFS_instr {
  /** Hash key */
  PFS_sql_text_key m_key;
  /** Digest storage */
  sql_digest_storage m_digest_storage;
};

int init_sql_text(const PFS_global_param *param);
void cleanup_sql_text();
int init_sql_text_hash(const PFS_global_param *param);
void cleanup_sql_text_hash();

void reset_sql_text();

PFS_sql_text *find_or_create_sql_text(PFS_thread *thread,
                                      const sql_digest_storage *digest_storage);

#endif
