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

#ifndef PFS_CLIENT_ATTRS_H
#define PFS_CLIENT_ATTRS_H

/**
  @file storage/perfschema/pfs_client_attrs.h
  Stored Program data structures (declarations).
*/

#include <sys/types.h>

#include "storage/perfschema/pfs_column_types.h"
#include "storage/perfschema/pfs_global.h"
#include "storage/perfschema/pfs_instr.h"
#include "storage/perfschema/pfs_stat.h"

extern LF_HASH client_attrs_hash;

/**
  Hash key for a client attribute.
*/
struct PFS_client_attrs_key {
  uchar m_hash_key[MD5_HASH_SIZE];
};

struct PFS_ALIGNED PFS_client_attrs : public PFS_instr {
  /** Hash key */
  PFS_client_attrs_key m_key;

  char m_client_attrs[1024];
  int m_client_attrs_length;
};

int init_client_attrs(const PFS_global_param *param);
void cleanup_client_attrs();
int init_client_attrs_hash(const PFS_global_param *param);
void cleanup_client_attrs_hash();

void reset_client_attrs();

PFS_client_attrs *find_or_create_client_attrs(PFS_thread *thread,
                                              const uchar *client_id,
                                              const char *client_attrs,
                                              uint client_attrs_length);

#endif
