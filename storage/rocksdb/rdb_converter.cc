/*
   Copyright (c) 2015, Facebook, Inc.

   This program is f
   i the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

/* This C++ file's header file */
#include "./rdb_converter.h"

/* Standard C++ header files */
#include <algorithm>
#include <map>
#include <string>
#include <vector>

/* MySQL header files */
#include "./log.h"
#include "./my_stacktrace.h"
#include "./sql_array.h"

/* MyRocks header files */
#include "./ha_rocksdb.h"
#include "./rdb_datadic.h"
#include "./rdb_utils.h"

namespace myrocks {

void dbug_modify_key_varchar8(String *on_disk_rec) {
  std::string res;
  // The key starts with index number
  res.append(on_disk_rec->ptr(), Rdb_key_def::INDEX_NUMBER_SIZE);

  // Then, a mem-comparable form of a varchar(8) value.
  res.append("ABCDE\0\0\0\xFC", 9);
  on_disk_rec->length(0);
  on_disk_rec->append(res.data(), res.size());
}

/*
  Initialize Rdb_converter with table data
  @thd        IN      Thread context
  @tbl_def    IN      MyRocks table definition
  @table      IN      Current open table
*/
Rdb_converter::Rdb_converter(const THD *thd, const Rdb_tbl_def *tbl_def,
                             TABLE *table)
    : m_thd(thd), m_tbl_def(tbl_def), m_table(table) {
  DBUG_ASSERT(thd != nullptr);
  DBUG_ASSERT(tbl_def != nullptr);
  DBUG_ASSERT(table != nullptr);

  m_key_requested = false;
  m_verify_row_debug_checksums = false;
  m_maybe_unpack_info = false;
  m_row_checksums_checked = 0;

  setup_field_encoders();
}

Rdb_converter::~Rdb_converter() {
  my_free(m_encoder_arr);
  m_encoder_arr = nullptr;
}

/*
  Decide storage type for each encoder
*/
void Rdb_converter::get_storage_type(Rdb_field_encoder *const encoder,
                                     const uint kp) {
  auto pk_descr =
      m_tbl_def->m_key_descr_arr[ha_rocksdb::pk_index(m_table, m_tbl_def)];
  // STORE_SOME uses unpack_info.
  if (pk_descr->has_unpack_info(kp)) {
    DBUG_ASSERT(pk_descr->can_unpack(kp));
    encoder->m_storage_type = Rdb_field_encoder::STORE_SOME;
    m_maybe_unpack_info = true;
  } else if (pk_descr->can_unpack(kp)) {
    encoder->m_storage_type = Rdb_field_encoder::STORE_NONE;
  }
}

/*
  @brief
    Setup which fields will be unpacked when reading rows

  @detail
    Three special cases when we still unpack all fields:
    - When client requires decode_all_fields, such as this table is being
  updated (m_lock_rows==RDB_LOCK_WRITE).
    - When @@rocksdb_verify_row_debug_checksums is ON (In this mode, we need to
  read all fields to find whether there is a row checksum at the end. We could
  skip the fields instead of decoding them, but currently we do decoding.)
    - On index merge as bitmap is cleared during that operation

  @seealso
    Rdb_converter::setup_field_encoders()
    Rdb_converter::convert_record_from_storage_format()
*/
void Rdb_converter::setup_field_decoders(const MY_BITMAP *field_map,
                                         bool decode_all_fields) {
  m_key_requested = false;
  m_decoders_vect.clear();
  int last_useful = 0;
  int skip_size = 0;

  for (uint i = 0; i < m_table->s->fields; i++) {
    // bitmap is cleared on index merge, but it still needs to decode columns
    bool field_requested =
        decode_all_fields || m_verify_row_debug_checksums ||
        bitmap_is_clear_all(field_map) ||
        bitmap_is_set(field_map, m_table->field[i]->field_index);

    // We only need the decoder if the whole record is stored.
    if (m_encoder_arr[i].m_storage_type != Rdb_field_encoder::STORE_ALL) {
      // the field potentially needs unpacking
      if (field_requested) {
        // the field is in the read set
        m_key_requested = true;
      }
      continue;
    }

    if (field_requested) {
      // We will need to decode this field
      m_decoders_vect.push_back({&m_encoder_arr[i], true, skip_size});
      last_useful = m_decoders_vect.size();
      skip_size = 0;
    } else {
      if (m_encoder_arr[i].uses_variable_len_encoding() ||
          m_encoder_arr[i].maybe_null()) {
        // For variable-length field, we need to read the data and skip it
        m_decoders_vect.push_back({&m_encoder_arr[i], false, skip_size});
        skip_size = 0;
      } else {
        // Fixed-width field can be skipped without looking at it.
        // Add appropriate skip_size to the next field.
        skip_size += m_encoder_arr[i].m_pack_length_in_rec;
      }
    }
  }

  // It could be that the last few elements are varchars that just do
  // skipping. Remove them.
  m_decoders_vect.erase(m_decoders_vect.begin() + last_useful,
                        m_decoders_vect.end());
}

void Rdb_converter::setup_field_encoders() {
  uint null_bytes = 0;
  uchar cur_null_mask = 0x1;

  m_encoder_arr = static_cast<Rdb_field_encoder *>(
      my_malloc(m_table->s->fields * sizeof(Rdb_field_encoder), MYF(0)));
  if (m_encoder_arr == nullptr) {
    return;
  }

  for (uint i = 0; i < m_table->s->fields; i++) {
    Field *const field = m_table->field[i];
    m_encoder_arr[i].m_storage_type = Rdb_field_encoder::STORE_ALL;

    /*
      Check if this field is
      - a part of primary key, and
      - it can be decoded back from its key image.
      If both hold, we don't need to store this field in the value part of
      RocksDB's key-value pair.

      If hidden pk exists, we skip this check since the field will never be
      part of the hidden pk.
    */
    if (!Rdb_key_def::table_has_hidden_pk(m_table)) {
      KEY *const pk_info = &m_table->key_info[m_table->s->primary_key];
      for (uint kp = 0; kp < pk_info->user_defined_key_parts; kp++) {
        /* key_part->fieldnr is counted from 1 */
        if (field->field_index + 1 == pk_info->key_part[kp].fieldnr) {
          get_storage_type(&m_encoder_arr[i], kp);
          break;
        }
      }
    }

    m_encoder_arr[i].m_field_type = field->real_type();
    m_encoder_arr[i].m_field_index = i;
    m_encoder_arr[i].m_pack_length_in_rec = field->pack_length_in_rec();

    if (field->real_maybe_null()) {
      m_encoder_arr[i].m_null_mask = cur_null_mask;
      m_encoder_arr[i].m_null_offset = null_bytes;
      if (cur_null_mask == 0x80) {
        cur_null_mask = 0x1;
        null_bytes++;
      } else {
        cur_null_mask = cur_null_mask << 1;
      }
    } else {
      m_encoder_arr[i].m_null_mask = 0;
    }
  }

  /* Count the last, unfinished NULL-bits byte */
  if (cur_null_mask != 0x1) {
    null_bytes++;
  }

  m_null_bytes_in_record = null_bytes;
}

/*
  EntryPoint for Decode:
  Decode key slice(if requested) and value slice using built-in field
  decoders
  @key_def        IN          key definition to decode
  @dst            IN,OUT      Mysql buffer to fill decoded content
  @key_slice      IN          RocksDB key slice to decode
  @value_slice    IN          RocksDB value slice to decode
*/
int Rdb_converter::decode(const std::shared_ptr<Rdb_key_def> &key_def,
                          uchar *dst,  // address to fill data
                          const rocksdb::Slice *key_slice,
                          const rocksdb::Slice *value_slice) {
  // Currently only support decode primary key, Will add decode secondary later
  DBUG_ASSERT(key_def->m_index_type == Rdb_key_def::INDEX_TYPE_PRIMARY ||
              key_def->m_index_type == Rdb_key_def::INDEX_TYPE_HIDDEN_PRIMARY);

  const rocksdb::Slice *updated_key_slice = key_slice;
#ifndef NDEBUG
  String last_rowkey;
  last_rowkey.copy(key_slice->data(), key_slice->size(), &my_charset_bin);
  DBUG_EXECUTE_IF("myrocks_simulate_bad_pk_read1",
                  { dbug_modify_key_varchar8(&last_rowkey); });
  rocksdb::Slice rowkey_slice(last_rowkey.ptr(), last_rowkey.length());
  updated_key_slice = &rowkey_slice;
#endif
  return convert_record_from_storage_format(key_def, updated_key_slice,
                                            value_slice, dst);
}

/*
  Convert RocksDb key slice and value slice to Mysql format
  @key_def       IN        key definition to decode
  @key_slice      IN           RocksDB key slice
  @value_slice    IN           RocksDB value slice
  @dst            OUT          MySql format address
*/
int Rdb_converter::convert_record_from_storage_format(
    const std::shared_ptr<Rdb_key_def> &pk_def,
    const rocksdb::Slice *const key_slice,
    const rocksdb::Slice *const value_slice, uchar *const dst) {
  Rdb_string_reader reader(value_slice);

  const char *unpack_info = nullptr;
  uint16 unpack_info_len = 0;
  rocksdb::Slice unpack_slice;

  /* If it's a TTL record, skip the 8 byte TTL value */
  const char *ttl_bytes;
  if (pk_def->has_ttl()) {
    if ((ttl_bytes = reader.read(ROCKSDB_SIZEOF_TTL_RECORD))) {
      memcpy(m_ttl_bytes, ttl_bytes, ROCKSDB_SIZEOF_TTL_RECORD);
    } else {
      return HA_ERR_ROCKSDB_CORRUPT_DATA;
    }
  }

  /* Other fields are decoded from the value */
  const char *null_bytes = nullptr;
  if (m_null_bytes_in_record &&
      !(null_bytes = reader.read(m_null_bytes_in_record))) {
    return HA_ERR_ROCKSDB_CORRUPT_DATA;
  }

  if (m_maybe_unpack_info) {
    unpack_info = reader.get_current_ptr();
    if (!unpack_info || !Rdb_key_def::is_unpack_data_tag(unpack_info[0]) ||
        !reader.read(Rdb_key_def::get_unpack_header_size(unpack_info[0]))) {
      return HA_ERR_ROCKSDB_CORRUPT_DATA;
    }

    unpack_info_len =
        rdb_netbuf_to_uint16(reinterpret_cast<const uchar *>(unpack_info + 1));
    unpack_slice = rocksdb::Slice(unpack_info, unpack_info_len);

    reader.read(unpack_info_len -
                Rdb_key_def::get_unpack_header_size(unpack_info[0]));
  }

  int err = HA_EXIT_SUCCESS;
  if (m_key_requested) {
    /*
      Decode PK fields from the key
    */
    err = pk_def->unpack_record(m_table, dst, key_slice,
                                unpack_info ? &unpack_slice : nullptr,
                                false /* verify_checksum */);
  }

  if (err != HA_EXIT_SUCCESS) {
    return err;
  }

  err = convert_fields_from_storage_format(pk_def, &reader, dst, null_bytes);
  if (err != HA_EXIT_SUCCESS) {
    return err;
  }

  if (m_verify_row_debug_checksums) {
    return verify_row_debug_checksum(pk_def, &reader, key_slice, value_slice);
  }

  return HA_EXIT_SUCCESS;
}

/*
   Convert MyRocks Value slice back to Mysql format
   @pk_def        IN      Key definition
   @reader        IN      RocksDB value slice reader
   @key           IN      RocksDB key slice
   @value         IN      RocksDB value slice
   @dst           OUT     Mysql table record buffer address
   @null_bytes    IN      Null bits mark
 */
int Rdb_converter::convert_fields_from_storage_format(
    const std::shared_ptr<Rdb_key_def> &pk_def, Rdb_string_reader *reader,
    uchar *dst, const char *null_bytes) {
  int err = HA_EXIT_SUCCESS;
  for (auto it = m_decoders_vect.begin(); it != m_decoders_vect.end(); it++) {
    const Rdb_field_encoder *const field_dec = it->m_field_enc;
    bool decode = it->m_decode;
    bool isNull =
        field_dec->maybe_null() &&
        ((null_bytes[field_dec->m_null_offset] & field_dec->m_null_mask) != 0);

    Field *const field = m_table->field[field_dec->m_field_index];

    /* Skip the bytes we need to skip */
    if (it->m_skip && !reader->read(it->m_skip)) {
      return HA_ERR_ROCKSDB_CORRUPT_DATA;
    }

    uint field_offset = field->ptr - m_table->record[0];
    uint null_offset = field->null_offset();
    bool maybe_null = field->real_maybe_null();
    field->move_field(dst + field_offset,
                      maybe_null ? dst + null_offset : nullptr,
                      field->null_bit);
    // WARNING! - Don't return before restoring field->ptr and field->null_ptr!

    if (isNull) {
      if (decode) {
        /* This sets the NULL-bit of this record */
        field->set_null();
        /*
          Besides that, set the field value to default value. CHECKSUM TABLE
          depends on this.
        */
        memcpy(field->ptr, m_table->s->default_values + field_offset,
               field->pack_length());
      }
    } else {
      if (decode) {
        field->set_notnull();
      }

      if (field_dec->m_field_type == MYSQL_TYPE_BLOB) {
        err = convert_blob_from_storage_format((my_core::Field_blob *)field,
                                               reader, decode);
      } else if (field_dec->m_field_type == MYSQL_TYPE_VARCHAR) {
        err = convert_varchar_from_storage_format(
            (my_core::Field_varstring *)field, reader, decode);
      } else {
        err = convert_field_from_storage_format(
            field, reader, decode, field_dec->m_pack_length_in_rec);
      }
    }

    // Restore field->ptr and field->null_ptr
    field->move_field(m_table->record[0] + field_offset,
                      maybe_null ? m_table->record[0] + null_offset : nullptr,
                      field->null_bit);

    if (err != HA_EXIT_SUCCESS) {
      return err;
    }
  }

  return HA_EXIT_SUCCESS;
}

/*
   Convert RocksDB value slice into Mysql Record format
   @blob      IN,OUT        Mysql blob field
   @reader    IN            RocksDB value slice reader
   @decode    IN            Control whether decode current field
*/
int Rdb_converter::convert_blob_from_storage_format(
    my_core::Field_blob *const blob, Rdb_string_reader *const reader,
    bool decode) {
  /* Get the number of bytes needed to store length*/
  const uint length_bytes = blob->pack_length() - portable_sizeof_char_ptr;

  const char *data_len_str;
  if (!(data_len_str = reader->read(length_bytes))) {
    return HA_ERR_ROCKSDB_CORRUPT_DATA;
  }

  memcpy(blob->ptr, data_len_str, length_bytes);

  const uint32 data_len =
      blob->get_length(reinterpret_cast<const uchar *>(data_len_str),
                       length_bytes, m_table->s->db_low_byte_first);
  const char *blob_ptr;
  if (!(blob_ptr = reader->read(data_len))) {
    return HA_ERR_ROCKSDB_CORRUPT_DATA;
  }

  if (decode) {
    // set 8-byte pointer to 0, like innodb does (relevant for 32-bit
    // platforms)
    memset(blob->ptr + length_bytes, 0, 8);
    memcpy(blob->ptr + length_bytes, &blob_ptr, sizeof(uchar **));
  }

  return HA_EXIT_SUCCESS;
}

/*
   Convert varchar field in RocksDB storage format into Mysql Record format
   @field_var     IN,OUT    Mysql varchar field
   @reader        IN        RocksDB value slice reader
   @decode        IN        Control whether decode current field
*/
int Rdb_converter::convert_varchar_from_storage_format(
    my_core::Field_varstring *const field_var, Rdb_string_reader *const reader,
    bool decode) {
  const char *data_len_str;
  if (!(data_len_str = reader->read(field_var->length_bytes))) {
    return HA_ERR_ROCKSDB_CORRUPT_DATA;
  }

  uint data_len;
  /* field_var->length_bytes is 1 or 2 */
  if (field_var->length_bytes == 1) {
    data_len = (uchar)data_len_str[0];
  } else {
    DBUG_ASSERT(field_var->length_bytes == 2);
    data_len = uint2korr(data_len_str);
  }

  if (data_len > field_var->field_length) {
    /* The data on disk is longer than table DDL allows? */
    return HA_ERR_ROCKSDB_CORRUPT_DATA;
  }

  if (!reader->read(data_len)) {
    return HA_ERR_ROCKSDB_CORRUPT_DATA;
  }

  if (decode) {
    memcpy(field_var->ptr, data_len_str, field_var->length_bytes + data_len);
  }

  return HA_EXIT_SUCCESS;
}

/*
   Convert field in RocksDB storage format into Mysql Record format
   @field    IN,OUT        Mysql field
   @reader   IN            RocksDB value slice reader
   @decode   IN            Control whether decode current field
*/
int Rdb_converter::convert_field_from_storage_format(
    my_core::Field *const field, Rdb_string_reader *const reader, bool decode,
    uint len) {
  const char *data_bytes;
  if (len > 0) {
    if ((data_bytes = reader->read(len)) == nullptr) {
      return HA_ERR_ROCKSDB_CORRUPT_DATA;
    }

    if (decode) {
      memcpy(field->ptr, data_bytes, len);
    }
  }

  return HA_EXIT_SUCCESS;
}

int Rdb_converter::verify_row_debug_checksum(
    const std::shared_ptr<Rdb_key_def> &pk_def, Rdb_string_reader *reader,
    const rocksdb::Slice *key, const rocksdb::Slice *value) {
  if (reader->remaining_bytes() == RDB_CHECKSUM_CHUNK_SIZE &&
      reader->read(1)[0] == RDB_CHECKSUM_DATA_TAG) {
    uint32_t stored_key_chksum =
        rdb_netbuf_to_uint32((const uchar *)reader->read(RDB_CHECKSUM_SIZE));
    uint32_t stored_val_chksum =
        rdb_netbuf_to_uint32((const uchar *)reader->read(RDB_CHECKSUM_SIZE));

    const uint32_t computed_key_chksum =
        my_core::crc32(0, rdb_slice_to_uchar_ptr(key), key->size());
    const uint32_t computed_val_chksum =
        my_core::crc32(0, rdb_slice_to_uchar_ptr(value),
                       value->size() - RDB_CHECKSUM_CHUNK_SIZE);

    DBUG_EXECUTE_IF("myrocks_simulate_bad_pk_checksum1", stored_key_chksum++;);

    if (stored_key_chksum != computed_key_chksum) {
      pk_def->report_checksum_mismatch(true, key->data(), key->size());
      return HA_ERR_ROCKSDB_CHECKSUM_MISMATCH;
    }

    DBUG_EXECUTE_IF("myrocks_simulate_bad_pk_checksum2", stored_val_chksum++;);
    if (stored_val_chksum != computed_val_chksum) {
      pk_def->report_checksum_mismatch(false, value->data(), value->size());
      return HA_ERR_ROCKSDB_CHECKSUM_MISMATCH;
    }

    m_row_checksums_checked++;
  }
  if (reader->remaining_bytes()) {
    return HA_ERR_ROCKSDB_CORRUPT_DATA;
  }
  return HA_EXIT_SUCCESS;
}

}  // namespace myrocks
