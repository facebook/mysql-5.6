/* Copyright (c) 2017, Facebook. All rights reserved.
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

#ifndef _native_procedures_h
#define _native_procedures_h

#include <my_base.h>
#include <mysql_com.h>

#include <map>
#include <string>
#include <vector>

enum execution_status_enum {
  EC_OK = 0,
  EC_NP_WRONG_ARGUMENTS,
  EC_FATAL,
  EC_NET_ERR,
  EC_UNINIT,
  EC_REINIT,
  EC_NOT_FOUND,
  EC_INVAL,
  EC_UNKNOWN,
  EC_MAX = EC_UNKNOWN,
};

struct ExecutionContext {
  struct Value {
    enum class Type { NONE, STRING, INTEGER, DOUBLE };

    Type type;
    union {
      struct {
        const char *val;
        size_t len;
      } str;
      struct {
        longlong val;
        bool unsigned_val;
      } num;
      struct {
        double val;
      } real;
    };

    Value() { type = Type::NONE; }
    Value(const char *val, size_t len) {
      type = Type::STRING;
      str = {val, len};
    }
    Value(longlong l, bool unsign) {
      type = Type::INTEGER;
      num = {l, unsign};
    }
    Value(double d) {
      type = Type::DOUBLE;
      real = {d};
    }
  };

  virtual int open_table(const char *db, const char *table) = 0;
  virtual int set_read_columns(std::vector<const char *> columns) = 0;

  virtual int send_metadata() = 0;
  virtual int send_row() = 0;

  virtual int index_init(const char *index_name) = 0;
  virtual int index_read_map(uint idx, enum ha_rkey_function flag) = 0;

  virtual int key_write(uint idx,
                        const std::map<const char *, Value> &keyparts) = 0;
  virtual int key_part_count(uint *out) = 0;
  virtual int key_part_name(uint index, const char **out) = 0;

  virtual int read_range_first(uint range_flag, bool asc) = 0;
  virtual int read_range_next() = 0;

  virtual int field_is_null(uint index, bool *out) = 0;
  virtual int field_val_int(uint index, longlong *out) = 0;
  virtual int field_val_real(uint index, double *out) = 0;
  virtual int field_val_str(uint index, std::string *out) = 0;

  char m_error_message[MYSQL_ERRMSG_SIZE];
};

#endif
