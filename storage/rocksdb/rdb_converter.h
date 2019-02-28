/*
   Copyright (c) 2018, Facebook, Inc.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#pragma once

/* C++ standard header files */
#include <string>
#include <vector>

/* MySQL header files */
#include "./handler.h"   /* handler */
#include "./my_global.h" /* ulonglong */
#include "./sql_string.h"
#include "./ut0counter.h"

/* MyRocks header files */
#include "./ha_rocksdb.h"
#include "./rdb_datadic.h"

namespace myrocks {
class Rdb_field_encoder;

/* Describes instructions on how to decode the field */
class READ_FIELD {
 public:
  /* Points to Rdb_field_encoder describing the field */
  Rdb_field_encoder *m_field_enc;
  /* if true, decode the field, otherwise skip it */
  bool m_decode;
  /* Skip this many bytes before reading (or skipping) this field */
  int m_skip;
};

/*
  Class to convert Mysql format to rocksdb storage format, and vice versa.
*/
class Rdb_converter {
 public:
  /*
    Initialize converter with table data
   */
  Rdb_converter(const THD *thd, const Rdb_tbl_def *tbl_def, TABLE *table);
  Rdb_converter(const Rdb_converter &decoder) = delete;
  Rdb_converter &operator=(const Rdb_converter &decoder) = delete;
  ~Rdb_converter();

  void setup_field_decoders(const MY_BITMAP *field_map,
                            bool decode_all_fields = false);

  int decode(const std::shared_ptr<Rdb_key_def> &key_def, uchar *dst,
             const rocksdb::Slice *key_slice,
             const rocksdb::Slice *value_slice);

  my_core::ha_rows get_row_checksums_checked() const {
    return m_row_checksums_checked;
  }
  bool get_verify_row_debug_checksums() const {
    return m_verify_row_debug_checksums;
  }
  void set_verify_row_debug_checksums(bool verify_row_debug_checksums) {
    m_verify_row_debug_checksums = verify_row_debug_checksums;
  }
  const Rdb_field_encoder *get_encoder_arr() const { return m_encoder_arr; }
  int get_null_bytes_in_record() { return m_null_bytes_in_record; }

  void set_is_key_requested(bool key_requested) {
    m_key_requested = key_requested;
  }
  bool get_maybe_unpack_info() const { return m_maybe_unpack_info; }

  char *get_ttl_bytes_buffer() { return m_ttl_bytes; }

 private:
  void setup_field_encoders();

  void get_storage_type(Rdb_field_encoder *const encoder, const uint kp);

  int convert_record_from_storage_format(
      const std::shared_ptr<Rdb_key_def> &pk_def,
      const rocksdb::Slice *const key, const rocksdb::Slice *const value,
      uchar *const buf);

  int convert_fields_from_storage_format(
      const std::shared_ptr<Rdb_key_def> &pk_def, Rdb_string_reader *reader,
      uchar *buf, const char *null_bytes);

  int convert_field_from_storage_format(my_core::Field *const field,
                                        Rdb_string_reader *const reader,
                                        bool decode, uint len);
  int convert_varchar_from_storage_format(
      my_core::Field_varstring *const field_var,
      Rdb_string_reader *const reader, bool decode);

  int convert_blob_from_storage_format(my_core::Field_blob *const blob,
                                       Rdb_string_reader *const reader,
                                       bool decode);

  int verify_row_debug_checksum(const std::shared_ptr<Rdb_key_def> &pk_def,
                                Rdb_string_reader *reader,
                                const rocksdb::Slice *key,
                                const rocksdb::Slice *value);

 private:
  /*
    This tells if any field which is part of the key needs to be unpacked and
    decoded.
   */
  bool m_key_requested;
  /* Controls whether verifying checksums during reading, This is updated from
  the session variable at the start of each query. */
  bool m_verify_row_debug_checksums;
  /* Thread handle*/
  const THD *m_thd;
  /* MyRocks table definition*/
  const Rdb_tbl_def *m_tbl_def;
  /* The current open table */
  TABLE *m_table;
  /*
    Number of bytes in on-disk (storage) record format that are used for
    storing SQL NULL flags.
  */
  int m_null_bytes_in_record;
  /*
   TRUE <=> Some fields in the PK may require unpack_info.
  */
  bool m_maybe_unpack_info;
  /*
    Pointer to the original TTL timestamp value (8 bytes) during UPDATE.
  */
  char m_ttl_bytes[ROCKSDB_SIZEOF_TTL_RECORD];
  /*
    Array of table->s->fields elements telling how to store fields in the
    record.
  */
  Rdb_field_encoder *m_encoder_arr;
  /*
    Array of request fields telling how to decode data in RocksDB format
  */
  std::vector<READ_FIELD> m_decoders_vect;
  /*
    A counter of how many row checksums were checked for this table. Note that
    this does not include checksums for secondary index entries.
  */
  my_core::ha_rows m_row_checksums_checked;
};
}  // namespace myrocks
