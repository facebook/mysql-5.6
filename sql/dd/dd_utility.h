/* Copyright (c) 2018, 2021, Oracle and/or its affiliates.

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

#ifndef DD__UTILITY_INCLUDED
#define DD__UTILITY_INCLUDED

#include "sql/dd/string_type.h"  // dd::String_type
#include "sql/handler.h"         // enum_tx_isolation
#include "sql/mysqld.h"

struct CHARSET_INFO;
class THD;

namespace dd {

///////////////////////////////////////////////////////////////////////////

/**
  Normalize (or transform) the multibyte character set string.

  The normalized string contains the weight of the each character of the source
  string. The normalized strings are suitable for the comparisons (strings
  yields the correct collation order).

  @param      cs                          Character set.
  @param      src                         Source string.
  @param[out] normalized_str_buf          Buffer to store the normalized string.
  @param      normalized_str_buf_length   Size of the normalized_str_buf.

  @returns length of the normalized string. 0 is returned if buffer length is
  insufficient to store the normalized string.
*/
size_t normalize_string(const CHARSET_INFO *cs, const String_type &src,
                        char *normalized_str_buf,
                        size_t normalized_str_buf_length);

/**
  Check if DDSE (Data Dictionary Storage Engine) is in
  readonly mode.

  @param thd                 Thread
  @param schema_name         Abbreviation or name of schema (I_S, P_S, ndbinfo)
                             for use in warning message output

  @returns false on success, otherwise true.
*/
bool check_if_server_ddse_readonly(THD *thd, const char *schema_name);

/**
  Get the isolation level for a data dictionary transaction. InnoDB uses READ
  UNCOMMITTED to work correctly in the following cases:
  - when called in the middle of an atomic DDL statement;
  - wehn called during the server startup when the undo logs have not been
  initialized yet.
  @return isolation level
*/
[[nodiscard]] inline enum_tx_isolation get_dd_isolation_level() {
  assert(default_dd_storage_engine == DEFAULT_DD_ROCKSDB ||
         default_dd_storage_engine == DEFAULT_DD_INNODB);
  return default_dd_storage_engine == DEFAULT_DD_ROCKSDB ? ISO_READ_COMMITTED
                                                         : ISO_READ_UNCOMMITTED;
}

[[nodiscard]] inline legacy_db_type get_dd_engine_type() {
  assert(default_dd_storage_engine == DEFAULT_DD_ROCKSDB ||
         default_dd_storage_engine == DEFAULT_DD_INNODB);
  const auto db_type = default_dd_storage_engine == DEFAULT_DD_ROCKSDB
                           ? DB_TYPE_ROCKSDB
                           : DB_TYPE_INNODB;
  return db_type;
}

[[nodiscard]] inline handlerton *get_dd_engine(THD *thd) {
  const auto db_type = get_dd_engine_type();
  return ha_resolve_by_legacy_type(thd, db_type);
}

[[nodiscard]] inline const char *get_dd_engine_name() {
  assert(default_dd_storage_engine == DEFAULT_DD_ROCKSDB ||
         default_dd_storage_engine == DEFAULT_DD_INNODB);
  return default_dd_storage_engine == DEFAULT_DD_ROCKSDB ? "ROCKSDB" : "INNODB";
}

[[nodiscard]] inline const char *get_dd_engine_name(legacy_db_type db_type) {
  assert(db_type == DB_TYPE_INNODB || db_type == DB_TYPE_ROCKSDB);
  return db_type == DB_TYPE_ROCKSDB ? "ROCKSDB" : "INNODB";
}
///////////////////////////////////////////////////////////////////////////

}  // namespace dd

#endif
