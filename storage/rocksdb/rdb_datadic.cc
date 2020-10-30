/*
   Copyright (c) 2012,2013 Monty Program Ab

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

#include <openssl/ssl.h>
#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation  // gcc: Class implementation
#endif

/* This C++ file's header file */
#include "./rdb_datadic.h"

/* C++ standard header files */
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

/* MySQL header files */
#include "./m_ctype.h"
#include "./my_bit.h"
#include "./my_bitmap.h"
#include "./my_byteorder.h"
#include "my_compare.h"  // get_rec_bits
#include "my_dir.h"
#include "myisampack.h"  // mi_int2store
#include "mysql/thread_pool_priv.h"
#include "sql/dd/cache/dictionary_client.h"  // dd::cache::Dictionary_client
#include "sql/field.h"
#include "sql/key.h"
#include "sql/sql_table.h"

/* MyRocks header files */
#include "./ha_rocksdb.h"
#include "./ha_rocksdb_proto.h"
#include "./my_stacktrace.h"
#include "./rdb_cf_manager.h"
#include "./rdb_psi.h"
#include "./rdb_utils.h"

extern CHARSET_INFO my_charset_utf16_bin;
extern CHARSET_INFO my_charset_utf16le_bin;
extern CHARSET_INFO my_charset_utf32_bin;

namespace myrocks {

void get_mem_comparable_space(const CHARSET_INFO *cs,
                              const std::vector<uchar> **xfrm, size_t *xfrm_len,
                              size_t *mb_len);

/*
  Decode  current key field
  @param  fpi               IN      data structure contains field metadata
  @param  field             IN      current field
  @param  reader            IN      key slice reader
  @param  unp_reader        IN      unpack information reader
  @return
    HA_EXIT_SUCCESS    OK
    other              HA_ERR error code
*/
int Rdb_convert_to_record_key_decoder::decode_field(
    Rdb_field_packing *fpi, Field *field, Rdb_string_reader *reader,
    const uchar *const default_value, Rdb_string_reader *unpack_reader) {
  if (fpi->m_maybe_null) {
    const char *nullp;
    if (!(nullp = reader->read(1))) {
      return HA_EXIT_FAILURE;
    }

    if (*nullp == 0) {
      /* Set the NULL-bit of this field */
      field->set_null();
      /* Also set the field to its default value */
      memcpy(field->field_ptr(), default_value, field->pack_length());
      return HA_EXIT_SUCCESS;
    } else if (*nullp == 1) {
      field->set_notnull();
    } else {
      return HA_EXIT_FAILURE;
    }
  }

  return (fpi->m_unpack_func)(fpi, field, field->field_ptr(), reader,
                              unpack_reader);
}

/*
  Decode  current key field

  @param  buf               OUT     the buf starting address
  @param  offset            OUT     the bytes offset when data is written
  @param  fpi               IN      data structure contains field metadata
  @param  table             IN      current table
  @param  field             IN      current field
  @param  has_unpack_inf    IN      whether contains unpack inf
  @param  reader            IN      key slice reader
  @param  unp_reader        IN      unpack information reader
  @return
    HA_EXIT_SUCCESS    OK
    other              HA_ERR error code
*/
int Rdb_convert_to_record_key_decoder::decode(
    uchar *const buf, uint *offset, Rdb_field_packing *fpi, TABLE *table,
    Field *field, bool has_unpack_info, Rdb_string_reader *reader,
    Rdb_string_reader *unpack_reader) {
  assert(buf != nullptr);
  assert(offset != nullptr);

  uint field_offset = field->field_ptr() - table->record[0];
  *offset = field_offset;
  uint null_offset = field->null_offset();
  bool maybe_null = field->is_nullable();

  field->move_field(buf + field_offset,
                    maybe_null ? buf + null_offset : nullptr, field->null_bit);

  // If we need unpack info, but there is none, tell the unpack function
  // this by passing unp_reader as nullptr. If we never read unpack_info
  // during unpacking anyway, then there won't an error.
  bool maybe_missing_unpack = !has_unpack_info && fpi->uses_unpack_info();

  int res =
      decode_field(fpi, field, reader, table->s->default_values + field_offset,
                   maybe_missing_unpack ? nullptr : unpack_reader);

  // Restore field->field_ptr() and field->null_ptr
  field->move_field(table->record[0] + field_offset,
                    maybe_null ? table->record[0] + null_offset : nullptr,
                    field->null_bit);
  if (res != UNPACK_SUCCESS) {
    return HA_ERR_ROCKSDB_CORRUPT_DATA;
  }
  return HA_EXIT_SUCCESS;
}

/*
  Skip current key field

  @param  fpi          IN    data structure contains field metadata
  @param  field        IN    current field
  @param  reader       IN    key slice reader
  @param  unp_reader   IN    unpack information reader
  @return
    HA_EXIT_SUCCESS    OK
    other              HA_ERR error code
*/
int Rdb_convert_to_record_key_decoder::skip(
    const Rdb_field_packing *fpi, const Field *field, Rdb_string_reader *reader,
    Rdb_string_reader *unp_reader, bool covered_bitmap_format_enabled) {
  /* It is impossible to unpack the column. Skip it. */
  if (fpi->m_maybe_null) {
    const char *nullp;
    if (!(nullp = reader->read(1))) {
      return HA_ERR_ROCKSDB_CORRUPT_DATA;
    }
    if (*nullp == 0) {
      /* This is a NULL value */
      return HA_EXIT_SUCCESS;
    }
    /* If NULL marker is not '0', it can be only '1'  */
    if (*nullp != 1) {
      return HA_ERR_ROCKSDB_CORRUPT_DATA;
    }
  }
  if ((fpi->m_skip_func)(fpi, field, reader)) {
    return HA_ERR_ROCKSDB_CORRUPT_DATA;
  }

  // If this is a space padded varchar, we need to skip the indicator
  // bytes for trailing bytes. They're useless since we can't restore the
  // field anyway.
  //
  // There is a special case for prefixed varchars where we do not
  // generate unpack info, because we know prefixed varchars cannot be
  // unpacked. In this case, it is not necessary to skip.
  if (fpi->m_skip_func == &Rdb_key_def::skip_variable_space_pad &&
      !fpi->m_unpack_info_stores_value && !covered_bitmap_format_enabled) {
    unp_reader->read(fpi->m_unpack_info_uses_two_bytes ? 2 : 1);
  }
  return HA_EXIT_SUCCESS;
}

Rdb_key_field_iterator::Rdb_key_field_iterator(
    const Rdb_key_def *key_def, Rdb_field_packing *pack_info,
    Rdb_string_reader *reader, Rdb_string_reader *unp_reader, TABLE *table,
    bool has_unpack_info, const MY_BITMAP *covered_bitmap, uchar *const buf) {
  m_key_def = key_def;
  m_pack_info = pack_info;
  m_iter_index = 0;
  m_iter_end = key_def->get_key_parts();
  m_reader = reader;
  m_unp_reader = unp_reader;
  m_table = table;
  m_has_unpack_info = has_unpack_info;
  m_covered_bitmap = covered_bitmap;
  m_buf = buf;
  m_secondary_key =
      (key_def->m_index_type == Rdb_key_def::INDEX_TYPE_SECONDARY);
  m_hidden_pk_exists = Rdb_key_def::table_has_hidden_pk(table);
  m_is_hidden_pk =
      (key_def->m_index_type == Rdb_key_def::INDEX_TYPE_HIDDEN_PRIMARY);
  m_curr_bitmap_pos = 0;
  m_offset = 0;
}

void *Rdb_key_field_iterator::get_dst() const { return m_buf + m_offset; }

int Rdb_key_field_iterator::get_field_index() const {
  assert(m_field != nullptr);
  return m_field->field_index();
}

bool Rdb_key_field_iterator::get_is_null() const { return m_is_null; }
Field *Rdb_key_field_iterator::get_field() const {
  assert(m_field != nullptr);
  return m_field;
}

bool Rdb_key_field_iterator::has_next() { return m_iter_index < m_iter_end; }

/**
 Iterate each field in the key and decode/skip one by one
*/
int Rdb_key_field_iterator::next() {
  int status = HA_EXIT_SUCCESS;
  while (m_iter_index < m_iter_end) {
    int curr_index = m_iter_index++;
    m_fpi = &m_pack_info[curr_index];
    /*
      Hidden pk field is packed at the end of the secondary keys, but the SQL
      layer does not know about it. Skip retrieving field if hidden pk.
    */
    if ((m_secondary_key && m_hidden_pk_exists &&
         curr_index + 1 == m_iter_end) ||
        m_is_hidden_pk) {
      assert(m_fpi->m_unpack_func);
      if ((m_fpi->m_skip_func)(m_fpi, nullptr, m_reader)) {
        return HA_ERR_ROCKSDB_CORRUPT_DATA;
      }
      return HA_EXIT_SUCCESS;
    }

    m_field = m_fpi->get_field_in_table(m_table);

    bool covered_column = (m_fpi->m_covered == Rdb_key_def::KEY_COVERED);
    if (m_covered_bitmap != nullptr &&
        Rdb_key_def::is_variable_length_field(m_field->real_type()) &&
        m_fpi->m_covered == Rdb_key_def::KEY_MAY_BE_COVERED) {
      covered_column = m_curr_bitmap_pos < MAX_REF_PARTS &&
                       bitmap_is_set(m_covered_bitmap, m_curr_bitmap_pos++);
    }

    if (m_fpi->m_unpack_func && covered_column) {
      /* It is possible to unpack this column. Do it. */
      status = Rdb_convert_to_record_key_decoder::decode(
          m_buf, &m_offset, m_fpi, m_table, m_field, m_has_unpack_info,
          m_reader, m_unp_reader);
      if (status) {
        return status;
      }
      break;
    } else {
      status = Rdb_convert_to_record_key_decoder::skip(
          m_fpi, m_field, m_reader, m_unp_reader, m_secondary_key);
      if (status) {
        return status;
      }
    }
  }
  return HA_EXIT_SUCCESS;
}

/*
  Rdb_key_def class implementation
*/
Rdb_key_def::Rdb_key_def(
    uint indexnr_arg, uint keyno_arg,
    std::shared_ptr<rocksdb::ColumnFamilyHandle> cf_handle_arg,
    uint16_t index_dict_version_arg, uchar index_type_arg,
    uint16_t kv_format_version_arg, bool is_reverse_cf_arg,
    bool is_per_partition_cf_arg, const char *_name, Rdb_index_stats _stats,
    uint32 index_flags_bitmap, uint32 ttl_rec_offset, uint64 ttl_duration)
    : m_index_number(indexnr_arg),
      m_cf_handle(cf_handle_arg),
      m_index_dict_version(index_dict_version_arg),
      m_index_type(index_type_arg),
      m_kv_format_version(kv_format_version_arg),
      m_is_reverse_cf(is_reverse_cf_arg),
      m_is_per_partition_cf(is_per_partition_cf_arg),
      m_name(_name),
      m_stats(_stats),
      m_index_flags_bitmap(index_flags_bitmap),
      m_ttl_rec_offset(ttl_rec_offset),
      m_ttl_duration(ttl_duration),
      m_ttl_column(""),
      m_pk_part_no(nullptr),
      m_pack_info(nullptr),
      m_keyno(keyno_arg),
      m_key_parts(0),
      m_ttl_pk_key_part_offset(UINT_MAX),
      m_ttl_field_index(UINT_MAX),
      m_prefix_extractor(nullptr),
      m_maxlength(0)  // means 'not intialized'
{
  mysql_mutex_init(0, &m_mutex, MY_MUTEX_INIT_FAST);
  rdb_netbuf_store_index(m_index_number_storage_form, m_index_number);
  m_total_index_flags_length =
      calculate_index_flag_offset(m_index_flags_bitmap, MAX_FLAG);
  assert(m_cf_handle);
}

Rdb_key_def::Rdb_key_def(const Rdb_key_def &k)
    : m_index_number(k.m_index_number),
      m_cf_handle(k.m_cf_handle),
      m_is_reverse_cf(k.m_is_reverse_cf),
      m_is_per_partition_cf(k.m_is_per_partition_cf),
      m_name(k.m_name),
      m_stats(k.m_stats),
      m_index_flags_bitmap(k.m_index_flags_bitmap),
      m_ttl_rec_offset(k.m_ttl_rec_offset),
      m_ttl_duration(k.m_ttl_duration),
      m_ttl_column(k.m_ttl_column),
      m_pk_part_no(k.m_pk_part_no),
      m_pack_info(k.m_pack_info),
      m_keyno(k.m_keyno),
      m_key_parts(k.m_key_parts),
      m_ttl_pk_key_part_offset(k.m_ttl_pk_key_part_offset),
      m_ttl_field_index(UINT_MAX),
      m_prefix_extractor(k.m_prefix_extractor),
      m_maxlength(k.m_maxlength) {
  mysql_mutex_init(0, &m_mutex, MY_MUTEX_INIT_FAST);
  rdb_netbuf_store_index(m_index_number_storage_form, m_index_number);
  m_total_index_flags_length =
      calculate_index_flag_offset(m_index_flags_bitmap, MAX_FLAG);
  if (k.m_pack_info) {
    const size_t size = sizeof(Rdb_field_packing) * k.m_key_parts;
    void *buf = my_malloc(PSI_NOT_INSTRUMENTED, size, MYF(0));
    m_pack_info = new (buf) Rdb_field_packing(*k.m_pack_info);
  }

  if (k.m_pk_part_no) {
    const size_t size = sizeof(uint) * m_key_parts;
    m_pk_part_no =
        reinterpret_cast<uint *>(my_malloc(PSI_NOT_INSTRUMENTED, size, MYF(0)));
    memcpy(m_pk_part_no, k.m_pk_part_no, size);
  }
}

Rdb_key_def::~Rdb_key_def() {
  mysql_mutex_destroy(&m_mutex);

  my_free(m_pk_part_no);
  m_pk_part_no = nullptr;

  my_free(m_pack_info);
  m_pack_info = nullptr;
}

void Rdb_key_def::setup(const TABLE *const tbl,
                        const Rdb_tbl_def *const tbl_def) {
  assert(tbl != nullptr);
  assert(tbl_def != nullptr);

  /*
    Set max_length based on the table.  This can be called concurrently from
    multiple threads, so there is a mutex to protect this code.
  */
  const bool is_hidden_pk = (m_index_type == INDEX_TYPE_HIDDEN_PRIMARY);
  const bool hidden_pk_exists = table_has_hidden_pk(tbl);
  const bool secondary_key = (m_index_type == INDEX_TYPE_SECONDARY);
  if (!m_maxlength) {
    RDB_MUTEX_LOCK_CHECK(m_mutex);
    if (m_maxlength != 0) {
      RDB_MUTEX_UNLOCK_CHECK(m_mutex);
      return;
    }

    KEY *key_info = nullptr;
    KEY *pk_info = nullptr;
    if (!is_hidden_pk) {
      key_info = &tbl->key_info[m_keyno];
      if (!hidden_pk_exists) pk_info = &tbl->key_info[tbl->s->primary_key];
      m_name = std::string(key_info->name);
    } else {
      m_name = HIDDEN_PK_NAME;
    }

    if (secondary_key) {
      m_pk_key_parts = hidden_pk_exists ? 1 : pk_info->actual_key_parts;
    } else {
      pk_info = nullptr;
      m_pk_key_parts = 0;
    }

    // "unique" secondary keys support:
    m_key_parts = is_hidden_pk ? 1 : key_info->actual_key_parts;

    if (secondary_key) {
      /*
        In most cases, SQL layer puts PK columns as invisible suffix at the
        end of secondary key. There are cases where this doesn't happen:
        - unique secondary indexes.
        - partitioned tables.

        Internally, we always need PK columns as suffix (and InnoDB does,
        too, if you were wondering).

        The loop below will attempt to put all PK columns at the end of key
        definition.  Columns that are already included in the index (either
        by the user or by "extended keys" feature) are not included for the
        second time.
      */
      m_key_parts += m_pk_key_parts;
    }

    if (secondary_key) {
      m_pk_part_no = reinterpret_cast<uint *>(
          my_malloc(PSI_NOT_INSTRUMENTED, sizeof(uint) * m_key_parts, MYF(0)));
    } else {
      m_pk_part_no = nullptr;
    }

    const size_t size = sizeof(Rdb_field_packing) * m_key_parts;
    m_pack_info = reinterpret_cast<Rdb_field_packing *>(
        my_malloc(PSI_NOT_INSTRUMENTED, size, MYF(0)));

    /*
      Guaranteed not to error here as checks have been made already during
      table creation.
    */
    Rdb_key_def::extract_ttl_col(tbl, tbl_def, &m_ttl_column,
                                 &m_ttl_field_index, true);

    size_t max_len = INDEX_NUMBER_SIZE;
    int unpack_len = 0;
    int max_part_len = 0;
    bool simulating_extkey = false;
    bool store_covered_bitmap = false;
    uint dst_i = 0;
    uint max_blob_length = 0;
    uint keyno_to_set = m_keyno;
    uint keypart_to_set = 0;

    if (is_hidden_pk) {
      Field *field = nullptr;
      m_pack_info[dst_i].setup(this, field, keyno_to_set, 0, 0);
      m_pack_info[dst_i].m_unpack_data_offset = unpack_len;
      max_len += m_pack_info[dst_i].m_max_image_len;
      max_part_len = std::max(max_part_len, m_pack_info[dst_i].m_max_image_len);
      dst_i++;
    } else {
      KEY_PART_INFO *key_part = key_info->key_part;

      /* this loop also loops over the 'extended key' tail */
      for (uint src_i = 0; src_i < m_key_parts; src_i++, keypart_to_set++) {
        Field *const field = key_part ? key_part->field : nullptr;

        if (simulating_extkey && !hidden_pk_exists) {
          assert(secondary_key);
          /* Check if this field is already present in the key definition */
          bool found = false;
          for (uint j = 0; j < key_info->actual_key_parts; j++) {
            if (field->field_index() ==
                    key_info->key_part[j].field->field_index() &&
                key_part->length == key_info->key_part[j].length) {
              found = true;
              break;
            }
          }

          if (found) {
            key_part++;
            continue;
          }
        }

        if (field && field->is_nullable()) max_len += 1;  // NULL-byte

        m_pack_info[dst_i].setup(this, field, keyno_to_set, keypart_to_set,
                                 key_part ? key_part->length : 0);
        m_pack_info[dst_i].m_unpack_data_offset = unpack_len;

        if (pk_info) {
          m_pk_part_no[dst_i] = -1;
          for (uint j = 0; j < m_pk_key_parts; j++) {
            if (field->field_index() ==
                pk_info->key_part[j].field->field_index()) {
              m_pk_part_no[dst_i] = j;
              break;
            }
          }
        } else if (secondary_key && hidden_pk_exists) {
          /*
            The hidden pk can never be part of the sk.  So it is always
            appended to the end of the sk.
          */
          m_pk_part_no[dst_i] = -1;
          if (simulating_extkey) m_pk_part_no[dst_i] = 0;
        }

        max_len += m_pack_info[dst_i].m_max_image_len;
        store_covered_bitmap |=
            (m_pack_info[dst_i].m_covered == KEY_MAY_BE_COVERED);
        max_part_len =
            std::max(max_part_len, m_pack_info[dst_i].m_max_image_len);

        /*
          Update the max_blob_length for secondary keys.
          This will be used later to allocate the blob buffer while
          unpacking secondary keys.
        */
        if (field && secondary_key && is_blob(field->real_type())) {
          max_blob_length += key_part->length;
        }
        /*
          Check key part name here, if it matches the TTL column then we store
          the offset of the TTL key part here.
        */
        if (!m_ttl_column.empty() &&
            !my_strcasecmp(system_charset_info, field->field_name,
                           m_ttl_column.c_str())) {
          assert(field->real_type() == MYSQL_TYPE_LONGLONG);
          assert(field->key_type() == HA_KEYTYPE_ULONGLONG);
          assert(!field->is_nullable());
          m_ttl_pk_key_part_offset = dst_i;
        }

        key_part++;
        /*
          For "unique" secondary indexes, pretend they have
          "index extensions"
         */
        if (secondary_key && src_i + 1 == key_info->actual_key_parts) {
          simulating_extkey = true;
          if (!hidden_pk_exists) {
            keyno_to_set = tbl->s->primary_key;
            key_part = pk_info->key_part;
            keypart_to_set = (uint)-1;
          } else {
            keyno_to_set = tbl_def->m_key_count - 1;
            key_part = nullptr;
            keypart_to_set = 0;
          }
        }

        dst_i++;
      }
    }

    m_key_parts = dst_i;
    m_max_blob_length = max_blob_length;
    m_store_covered_bitmap = store_covered_bitmap && secondary_key;
    /* Initialize the memory needed by the stats structure */
    m_stats.m_distinct_keys_per_prefix.resize(get_key_parts());

    /* Cache prefix extractor for bloom filter usage later */
    rocksdb::Options opt = rdb_get_rocksdb_db()->GetOptions(get_cf());
    m_prefix_extractor = opt.prefix_extractor;

    /*
      This should be the last member variable set before releasing the mutex
      so that other threads can't see the object partially set up.
     */
    m_maxlength = max_len;

    RDB_MUTEX_UNLOCK_CHECK(m_mutex);
  }
}

/*
  Determine if the table has TTL enabled by parsing the table comment.

  @param[IN]  table_arg
  @param[IN]  tbl_def_arg
  @param[OUT] ttl_duration        Default TTL value parsed from table comment
*/
uint Rdb_key_def::extract_ttl_duration(const TABLE *const table_arg,
                                       const Rdb_tbl_def *const tbl_def_arg,
                                       uint64 *ttl_duration) {
  assert(table_arg != nullptr);
  assert(tbl_def_arg != nullptr);
  assert(ttl_duration != nullptr);
  std::string table_comment(table_arg->s->comment.str,
                            table_arg->s->comment.length);

  bool ttl_duration_per_part_match_found = false;
  std::string ttl_duration_str = Rdb_key_def::parse_comment_for_qualifier(
      table_comment, table_arg, tbl_def_arg, &ttl_duration_per_part_match_found,
      RDB_TTL_DURATION_QUALIFIER);

  /* If we don't have a ttl duration, nothing to do here. */
  if (ttl_duration_str.empty()) {
    return HA_EXIT_SUCCESS;
  }

  /*
    Catch errors where a non-integral value was used as ttl duration, strtoull
    will return 0.
  */
  *ttl_duration = std::strtoull(ttl_duration_str.c_str(), nullptr, 0);
  if (!*ttl_duration) {
    my_error(ER_RDB_TTL_DURATION_FORMAT, MYF(0), ttl_duration_str.c_str());
    return HA_EXIT_FAILURE;
  }

  return HA_EXIT_SUCCESS;
}

/*
  Determine if the table has TTL enabled by parsing the table comment.

  @param[IN]  table_arg
  @param[IN]  tbl_def_arg
  @param[OUT] ttl_column          TTL column in the table
  @param[IN]  skip_checks         Skip validation checks (when called in
                                  setup())
*/
uint Rdb_key_def::extract_ttl_col(const TABLE *const table_arg,
                                  const Rdb_tbl_def *const tbl_def_arg,
                                  std::string *ttl_column,
                                  uint *ttl_field_index, bool skip_checks) {
  std::string table_comment(table_arg->s->comment.str,
                            table_arg->s->comment.length);
  /*
    Check if there is a TTL column specified. Note that this is not required
    and if omitted, an 8-byte ttl field will be prepended to each record
    implicitly.
  */
  bool ttl_col_per_part_match_found = false;
  std::string ttl_col_str = Rdb_key_def::parse_comment_for_qualifier(
      table_comment, table_arg, tbl_def_arg, &ttl_col_per_part_match_found,
      RDB_TTL_COL_QUALIFIER);

  if (skip_checks) {
    for (uint i = 0; i < table_arg->s->fields; i++) {
      Field *const field = table_arg->field[i];
      if (!my_strcasecmp(system_charset_info, field->field_name,
                         ttl_col_str.c_str())) {
        *ttl_column = ttl_col_str;
        *ttl_field_index = i;
      }
    }
    return HA_EXIT_SUCCESS;
  }

  /* Check if TTL column exists in table */
  if (!ttl_col_str.empty()) {
    bool found = false;
    for (uint i = 0; i < table_arg->s->fields; i++) {
      Field *const field = table_arg->field[i];
      if (!my_strcasecmp(system_charset_info, field->field_name,
                         ttl_col_str.c_str()) &&
          field->real_type() == MYSQL_TYPE_LONGLONG &&
          field->key_type() == HA_KEYTYPE_ULONGLONG && !field->is_nullable()) {
        *ttl_column = ttl_col_str;
        *ttl_field_index = i;
        found = true;
        break;
      }
    }

    if (!found) {
      my_error(ER_RDB_TTL_COL_FORMAT, MYF(0), ttl_col_str.c_str());
      return HA_EXIT_FAILURE;
    }
  }

  return HA_EXIT_SUCCESS;
}

const std::string Rdb_key_def::gen_qualifier_for_table(
    const char *const qualifier, const std::string &partition_name) {
  bool has_partition = !partition_name.empty();
  std::string qualifier_str = "";

  if (!strcmp(qualifier, RDB_CF_NAME_QUALIFIER)) {
    return has_partition ? gen_cf_name_qualifier_for_partition(partition_name)
                         : qualifier_str + RDB_CF_NAME_QUALIFIER +
                               RDB_QUALIFIER_VALUE_SEP;
  } else if (!strcmp(qualifier, RDB_TTL_DURATION_QUALIFIER)) {
    return has_partition
               ? gen_ttl_duration_qualifier_for_partition(partition_name)
               : qualifier_str + RDB_TTL_DURATION_QUALIFIER +
                     RDB_QUALIFIER_VALUE_SEP;
  } else if (!strcmp(qualifier, RDB_TTL_COL_QUALIFIER)) {
    return has_partition ? gen_ttl_col_qualifier_for_partition(partition_name)
                         : qualifier_str + RDB_TTL_COL_QUALIFIER +
                               RDB_QUALIFIER_VALUE_SEP;
  } else {
    assert(0);
  }

  return qualifier_str;
}

/*
  Formats the string and returns the column family name assignment part for a
  specific partition.
*/
const std::string Rdb_key_def::gen_cf_name_qualifier_for_partition(
    const std::string &prefix) {
  assert(!prefix.empty());

  return prefix + RDB_PER_PARTITION_QUALIFIER_NAME_SEP + RDB_CF_NAME_QUALIFIER +
         RDB_QUALIFIER_VALUE_SEP;
}

const std::string Rdb_key_def::gen_ttl_duration_qualifier_for_partition(
    const std::string &prefix) {
  assert(!prefix.empty());

  return prefix + RDB_PER_PARTITION_QUALIFIER_NAME_SEP +
         RDB_TTL_DURATION_QUALIFIER + RDB_QUALIFIER_VALUE_SEP;
}

const std::string Rdb_key_def::gen_ttl_col_qualifier_for_partition(
    const std::string &prefix) {
  assert(!prefix.empty());

  return prefix + RDB_PER_PARTITION_QUALIFIER_NAME_SEP + RDB_TTL_COL_QUALIFIER +
         RDB_QUALIFIER_VALUE_SEP;
}

const std::string Rdb_key_def::parse_comment_for_qualifier(
    const std::string &comment, const TABLE *const table_arg,
    const Rdb_tbl_def *const tbl_def_arg, bool *per_part_match_found,
    const char *const qualifier) {
  assert(table_arg != nullptr);
  assert(tbl_def_arg != nullptr);
  assert(per_part_match_found != nullptr);
  assert(qualifier != nullptr);

  std::string empty_result;

  // Flag which marks if partition specific options were found.
  *per_part_match_found = false;

  if (comment.empty()) {
    return empty_result;
  }

  // Let's fetch the comment for a index and check if there's a custom key
  // name specified for a partition we are handling.
  std::vector<std::string> v =
      myrocks::parse_into_tokens(comment, RDB_QUALIFIER_SEP);

  std::string search_str = gen_qualifier_for_table(qualifier);

  // If table has partitions then we need to check if user has requested
  // qualifiers on a per partition basis.
  //
  // NOTE: this means if you specify a qualifier for a specific partition it
  // will take precedence the 'table level' qualifier if one exists.
  std::string search_str_part;
  if (table_arg->part_info != nullptr) {
    std::string partition_name = tbl_def_arg->base_partition();
    assert(!partition_name.empty());
    search_str_part = gen_qualifier_for_table(qualifier, partition_name);
  }

  assert(!search_str.empty());

  // Basic O(N) search for a matching assignment. At most we expect maybe
  // ten or so elements here.
  if (!search_str_part.empty()) {
    for (const auto &it : v) {
      if (it.substr(0, search_str_part.length()) == search_str_part) {
        // We found a prefix match. Try to parse it as an assignment.
        std::vector<std::string> tokens =
            myrocks::parse_into_tokens(it, RDB_QUALIFIER_VALUE_SEP);

        // We found a custom qualifier, it was in the form we expected it to be.
        // Return that instead of whatever we initially wanted to return. In
        // a case below the `foo` part will be returned to the caller.
        //
        // p3_cfname=foo
        //
        // If no value was specified then we'll return an empty string which
        // later gets translated into using a default CF.
        if (tokens.size() == 2) {
          *per_part_match_found = true;
          return tokens[1];
        } else {
          return empty_result;
        }
      }
    }
  }

  // Do this loop again, this time searching for 'table level' qualifiers if we
  // didn't find any partition level qualifiers above.
  for (const auto &it : v) {
    if (it.substr(0, search_str.length()) == search_str) {
      std::vector<std::string> tokens =
          myrocks::parse_into_tokens(it, RDB_QUALIFIER_VALUE_SEP);
      if (tokens.size() == 2) {
        return tokens[1];
      } else {
        return empty_result;
      }
    }
  }

  // If we didn't find any partitioned/non-partitioned qualifiers, return an
  // empty string.
  return empty_result;
}

/**
  Read a memcmp key part from a slice using the passed in reader.

  Returns -1 if field was null, 1 if error, 0 otherwise.
*/
int Rdb_key_def::read_memcmp_key_part(const TABLE *table_arg,
                                      Rdb_string_reader *reader,
                                      const uint part_num) const {
  /* It is impossible to unpack the column. Skip it. */
  if (m_pack_info[part_num].m_maybe_null) {
    const char *nullp;
    if (!(nullp = reader->read(1))) return 1;
    if (*nullp == 0) {
      /* This is a NULL value */
      return -1;
    } else {
      /* If NULL marker is not '0', it can be only '1'  */
      if (*nullp != 1) return 1;
    }
  }

  Rdb_field_packing *fpi = &m_pack_info[part_num];
  assert(table_arg->s != nullptr);

  bool is_hidden_pk_part = (part_num + 1 == m_key_parts) &&
                           (table_arg->s->primary_key == MAX_INDEXES);
  Field *field = nullptr;
  if (!is_hidden_pk_part) {
    field = fpi->get_field_in_table(table_arg);
  }
  if ((fpi->m_skip_func)(fpi, field, reader)) {
    return 1;
  }
  return 0;
}

/**
  Get a mem-comparable form of Primary Key from mem-comparable form of this key

  @param
    pk_descr        Primary Key descriptor
    key             Index tuple from this key in mem-comparable form
    pk_buffer  OUT  Put here mem-comparable form of the Primary Key.

  @note
    It may or may not be possible to restore primary key columns to their
    mem-comparable form.  To handle all cases, this function copies mem-
    comparable forms directly.

    RocksDB SE supports "Extended keys". This means that PK columns are present
    at the end of every key.  If the key already includes PK columns, then
    these columns are not present at the end of the key.

    Because of the above, we copy each primary key column.

  @todo
    If we checked crc32 checksums in this function, we would catch some CRC
    violations that we currently don't. On the other hand, there is a broader
    set of queries for which we would check the checksum twice.
*/

uint Rdb_key_def::get_primary_key_tuple(const TABLE *const table,
                                        const Rdb_key_def &pk_descr,
                                        const rocksdb::Slice *const key,
                                        uchar *const pk_buffer) const {
  assert(table != nullptr);
  assert(key != nullptr);
  assert(m_index_type == Rdb_key_def::INDEX_TYPE_SECONDARY);
  assert(pk_buffer);

  uint size = 0;
  uchar *buf = pk_buffer;
  assert(m_pk_key_parts);

  /* Put the PK number */
  rdb_netbuf_store_index(buf, pk_descr.m_index_number);
  buf += INDEX_NUMBER_SIZE;
  size += INDEX_NUMBER_SIZE;

  const char *start_offs[MAX_REF_PARTS];
  const char *end_offs[MAX_REF_PARTS];
  int pk_key_part;
  uint i;
  Rdb_string_reader reader(key);

  // Skip the index number
  if ((!reader.read(INDEX_NUMBER_SIZE))) return RDB_INVALID_KEY_LEN;

  for (i = 0; i < m_key_parts; i++) {
    if ((pk_key_part = m_pk_part_no[i]) != -1) {
      start_offs[pk_key_part] = reader.get_current_ptr();
    }

    if (read_memcmp_key_part(table, &reader, i) > 0) {
      return RDB_INVALID_KEY_LEN;
    }

    if (pk_key_part != -1) {
      end_offs[pk_key_part] = reader.get_current_ptr();
    }
  }

  for (i = 0; i < m_pk_key_parts; i++) {
    const uint part_size = end_offs[i] - start_offs[i];
    memcpy(buf, start_offs[i], end_offs[i] - start_offs[i]);
    buf += part_size;
    size += part_size;
  }

  return size;
}

/**
  Get a mem-comparable form of Secondary Key from mem-comparable form of this
  key, without the extended primary key tail.

  @param
    key                Index tuple from this key in mem-comparable form
    sk_buffer     OUT  Put here mem-comparable form of the Secondary Key.
    n_null_fields OUT  Put number of null fields contained within sk entry
*/
uint Rdb_key_def::get_memcmp_sk_parts(const TABLE *table,
                                      const rocksdb::Slice &key,
                                      uchar *sk_buffer,
                                      uint *n_null_fields) const {
  assert(table != nullptr);
  assert(sk_buffer != nullptr);
  assert(n_null_fields != nullptr);
  assert(m_keyno != table->s->primary_key && !table_has_hidden_pk(table));

  uchar *buf = sk_buffer;

  int res;
  Rdb_string_reader reader(&key);
  const char *start = reader.get_current_ptr();

  // Skip the index number
  if ((!reader.read(INDEX_NUMBER_SIZE))) return RDB_INVALID_KEY_LEN;

  for (uint i = 0; i < table->key_info[m_keyno].user_defined_key_parts; i++) {
    if ((res = read_memcmp_key_part(table, &reader, i)) > 0) {
      return RDB_INVALID_KEY_LEN;
    } else if (res == -1) {
      (*n_null_fields)++;
    }
  }

  uint sk_memcmp_len = reader.get_current_ptr() - start;
  memcpy(buf, start, sk_memcmp_len);
  return sk_memcmp_len;
}

/**
  Convert index tuple into storage (i.e. mem-comparable) format

  @detail
    Currently this is done by unpacking into table->record[0] and then
    packing index columns into storage format.

  @param pack_buffer Temporary area for packing varchar columns. Its
                     size is at least max_storage_fmt_length() bytes.
*/

uint Rdb_key_def::pack_index_tuple(TABLE *const tbl, uchar *const pack_buffer,
                                   uchar *const packed_tuple,
                                   const uchar *const key_tuple,
                                   const key_part_map &keypart_map) const {
  assert(tbl != nullptr);
  assert(pack_buffer != nullptr);
  assert(packed_tuple != nullptr);
  assert(key_tuple != nullptr);

  /* We were given a record in KeyTupleFormat. First, save it to record */
  const uint key_len = calculate_key_len(tbl, m_keyno, keypart_map);
  key_restore(tbl->record[0], const_cast<uchar *>(key_tuple),
              &tbl->key_info[m_keyno], key_len);

  uint n_used_parts = my_count_bits(keypart_map);
  if (keypart_map == HA_WHOLE_KEY) n_used_parts = 0;  // Full key is used

  /* Then, convert the record into a mem-comparable form */
  return pack_record(tbl, pack_buffer, tbl->record[0], packed_tuple, nullptr,
                     false, 0, n_used_parts);
}

/*
  @return Number of bytes that were changed
*/
int Rdb_key_def::successor(uchar *const packed_tuple, const uint len) {
  assert(packed_tuple != nullptr);

  int changed = 0;
  uchar *p = packed_tuple + len - 1;
  for (; p > packed_tuple; p--) {
    changed++;
    if (*p != uchar(0xFF)) {
      *p = *p + 1;
      break;
    }
    *p = '\0';
  }
  return changed;
}

/*
  @return Number of bytes that were changed
*/
int Rdb_key_def::predecessor(uchar *const packed_tuple, const uint len) {
  assert(packed_tuple != nullptr);

  int changed = 0;
  uchar *p = packed_tuple + len - 1;
  for (; p > packed_tuple; p--) {
    changed++;
    if (*p != uchar(0x00)) {
      *p = *p - 1;
      break;
    }
    *p = 0xFF;
  }
  return changed;
}

static const std::map<char, size_t> UNPACK_HEADER_SIZES = {
    {RDB_UNPACK_DATA_TAG, RDB_UNPACK_HEADER_SIZE},
    {RDB_UNPACK_COVERED_DATA_TAG, RDB_UNPACK_COVERED_HEADER_SIZE},
    {RDB_UNPACK_DATA_WITHOUT_LEN_TAG, RDB_UNPACK_DATA_WITHOUT_LEN_HEADER_SIZE}};

/*
  @return The length in bytes of the header specified by the given tag
*/
size_t Rdb_key_def::get_unpack_header_size(char tag) {
  assert(is_unpack_data_tag(tag));
  return UNPACK_HEADER_SIZES.at(tag);
}

/*
  Get a bitmap indicating which varchar columns must be covered for this
  lookup to be covered. If the bitmap is a subset of the covered bitmap, then
  the lookup is covered. If it can already be determined that the lookup is
  not covered, map->bitmap will be set to null.
 */
void Rdb_key_def::get_lookup_bitmap(const TABLE *table, MY_BITMAP *map) const {
  assert(map->bitmap == nullptr);
  bitmap_init(map, nullptr, MAX_REF_PARTS);
  uint curr_bitmap_pos = 0;

  // Indicates which columns in the read set might be covered.
  MY_BITMAP maybe_covered_bitmap;
  bitmap_init(&maybe_covered_bitmap, nullptr, table->read_set->n_bits);

  for (uint i = 0; i < m_key_parts; i++) {
    if (table_has_hidden_pk(table) && i + 1 == m_key_parts) {
      continue;
    }

    Field *const field = m_pack_info[i].get_field_in_table(table);

    // Columns which are always covered are not stored in the covered bitmap so
    // we can ignore them here too.
    if (m_pack_info[i].m_covered == KEY_COVERED &&
        bitmap_is_set(table->read_set, field->field_index())) {
      bitmap_set_bit(&maybe_covered_bitmap, field->field_index());
      continue;
    }

    switch (field->real_type()) {
      // This type may be covered depending on the record. If it was requested,
      // we require the covered bitmap to have this bit set.
      case MYSQL_TYPE_VARCHAR:
      case MYSQL_TYPE_TINY_BLOB:
      case MYSQL_TYPE_MEDIUM_BLOB:
      case MYSQL_TYPE_LONG_BLOB:
      case MYSQL_TYPE_BLOB:
        if (curr_bitmap_pos < MAX_REF_PARTS) {
          if (bitmap_is_set(table->read_set, field->field_index())) {
            bitmap_set_bit(map, curr_bitmap_pos);
            bitmap_set_bit(&maybe_covered_bitmap, field->field_index());
          }
          curr_bitmap_pos++;
        } else {
          bitmap_free(&maybe_covered_bitmap);
          bitmap_free(map);
          return;
        }
        break;
      // This column is a type which is never covered. If it was requested, we
      // know this lookup will never be covered.
      default:
        if (bitmap_is_set(table->read_set, field->field_index())) {
          bitmap_free(&maybe_covered_bitmap);
          bitmap_free(map);
          return;
        }
        break;
    }
  }

  // If there are columns which are not covered in the read set, the lookup
  // can't be covered.
  if (!bitmap_cmp(table->read_set, &maybe_covered_bitmap)) {
    bitmap_free(map);
  }
  bitmap_free(&maybe_covered_bitmap);
}

/*
  Return true if for this secondary index
  - All of the requested columns are in the index
  - All values for columns that are prefix-only indexes are shorter or equal
    in length to the prefix
 */
bool Rdb_key_def::covers_lookup(const rocksdb::Slice *const unpack_info,
                                const MY_BITMAP *const lookup_bitmap) const {
  assert(lookup_bitmap != nullptr);
  if (m_index_type != INDEX_TYPE_SECONDARY ||
      lookup_bitmap->bitmap == nullptr) {
    return false;
  }

  Rdb_string_reader unp_reader = Rdb_string_reader::read_or_empty(unpack_info);

  // Check if this unpack_info has a covered_bitmap
  const char *unpack_header = unp_reader.get_current_ptr();
  const bool has_covered_unpack_info =
      unp_reader.remaining_bytes() &&
      unpack_header[0] == RDB_UNPACK_COVERED_DATA_TAG;
  if (!has_covered_unpack_info ||
      !unp_reader.read(RDB_UNPACK_COVERED_HEADER_SIZE)) {
    return false;
  }

  MY_BITMAP covered_bitmap;
  my_bitmap_map covered_bits;
  bitmap_init(&covered_bitmap, &covered_bits, MAX_REF_PARTS);
  covered_bits = rdb_netbuf_to_uint16((const uchar *)unpack_header +
                                      sizeof(RDB_UNPACK_COVERED_DATA_TAG));

  return bitmap_is_subset(lookup_bitmap, &covered_bitmap);
}

/* Indicates that all key parts can be unpacked to cover a secondary lookup */
bool Rdb_key_def::can_cover_lookup() const {
  for (uint i = 0; i < m_key_parts; i++) {
    if (m_pack_info[i].m_covered != KEY_COVERED) return false;
  }
  return true;
}

uchar *Rdb_key_def::pack_field(
    Field *const field, Rdb_field_packing *pack_info, uchar *tuple,
    uchar *const packed_tuple MY_ATTRIBUTE((__unused__)),
    uchar *const pack_buffer, Rdb_string_writer *const unpack_info,
    uint *const n_null_fields) const {
  if (field->is_nullable()) {
    assert(is_storage_available(tuple - packed_tuple, 1));
    if (field->is_real_null()) {
      /* NULL value. store '\0' so that it sorts before non-NULL values */
      *tuple++ = 0;
      /* That's it, don't store anything else */
      if (n_null_fields) (*n_null_fields)++;
      return tuple;
    } else {
      /* Not a NULL value. Store '1' */
      *tuple++ = 1;
    }
  }

  const bool create_unpack_info =
      (unpack_info &&  // we were requested to generate unpack_info
       pack_info->uses_unpack_info());  // and this keypart uses it
  Rdb_pack_field_context pack_ctx(unpack_info);

  // Set the offset for methods which do not take an offset as an argument
  assert(
      is_storage_available(tuple - packed_tuple, pack_info->m_max_image_len));

  (pack_info->m_pack_func)(pack_info, field, pack_buffer, &tuple, &pack_ctx);

  /* Make "unpack info" to be stored in the value */
  if (create_unpack_info) {
    (pack_info->m_make_unpack_info_func)(pack_info, field, &pack_ctx);
  }

  return tuple;
}

/**
  Get index columns from the record and pack them into mem-comparable form.

  @param
    tbl                   Table we're working on
    record           IN   Record buffer with fields in table->record format
    pack_buffer      IN   Temporary area for packing varchars. The size is
                          at least max_storage_fmt_length() bytes.
    packed_tuple     OUT  Key in the mem-comparable form
    unpack_info      OUT  Unpack data
    unpack_info_len  OUT  Unpack data length
    n_key_parts           Number of keyparts to process. 0 means all of them.
    n_null_fields    OUT  Number of key fields with NULL value.
    ttl_bytes        IN   Previous ttl bytes from old record for update case or
                          current ttl bytes from just packed primary key/value
  @detail
    Some callers do not need the unpack information, they can pass
    unpack_info=nullptr, unpack_info_len=nullptr.

  @return
    Length of the packed tuple
*/

uint Rdb_key_def::pack_record(const TABLE *const tbl, uchar *const pack_buffer,
                              const uchar *const record,
                              uchar *const packed_tuple,
                              Rdb_string_writer *const unpack_info,
                              const bool should_store_row_debug_checksums,
                              const longlong hidden_pk_id, uint n_key_parts,
                              uint *const n_null_fields,
                              const char *const ttl_bytes) const {
  assert(tbl != nullptr);
  assert(pack_buffer != nullptr);
  assert(record != nullptr);
  assert(packed_tuple != nullptr);
  // Checksums for PKs are made when record is packed.
  // We should never attempt to make checksum just from PK values
  assert_IMP(should_store_row_debug_checksums,
                  (m_index_type == INDEX_TYPE_SECONDARY));

  uchar *tuple = packed_tuple;
  size_t unpack_start_pos = size_t(-1);
  size_t unpack_len_pos = size_t(-1);
  size_t covered_bitmap_pos = size_t(-1);
  const bool hidden_pk_exists = table_has_hidden_pk(tbl);

  rdb_netbuf_store_index(tuple, m_index_number);
  tuple += INDEX_NUMBER_SIZE;

  // If n_key_parts is 0, it means all columns.
  // The following includes the 'extended key' tail.
  // The 'extended key' includes primary key. This is done to 'uniqify'
  // non-unique indexes
  const bool use_all_columns = n_key_parts == 0 || n_key_parts == MAX_REF_PARTS;

  // If hidden pk exists, but hidden pk wasnt passed in, we can't pack the
  // hidden key part.  So we skip it (its always 1 part).
  if (hidden_pk_exists && !hidden_pk_id && use_all_columns) {
    n_key_parts = m_key_parts - 1;
  } else if (use_all_columns) {
    n_key_parts = m_key_parts;
  }

  if (n_null_fields) *n_null_fields = 0;

  char tag = RDB_UNPACK_DATA_TAG;
  if (m_index_type == INDEX_TYPE_SECONDARY) {
    tag = m_store_covered_bitmap ? RDB_UNPACK_COVERED_DATA_TAG
                                 : RDB_UNPACK_DATA_WITHOUT_LEN_TAG;
  }

  if (unpack_info) {
    unpack_info->clear();

    if (m_index_type == INDEX_TYPE_SECONDARY &&
        m_total_index_flags_length > 0) {
      // Reserve space for index flag fields
      unpack_info->alloc_init(m_total_index_flags_length);

      // Insert TTL timestamp
      if (has_ttl() && ttl_bytes) {
        write_index_flag_field(unpack_info,
                               reinterpret_cast<const uchar *>(ttl_bytes),
                               Rdb_key_def::TTL_FLAG);
      }
    }

    unpack_start_pos = unpack_info->get_current_pos();
    unpack_info->write_uint8(tag);

    if (tag == RDB_UNPACK_DATA_TAG) {
      unpack_len_pos = unpack_info->get_current_pos();
      // we don't know the total length yet, so write a zero
      unpack_info->write_uint16(0);
    }

    if (m_store_covered_bitmap) {
      // Reserve two bytes for the covered bitmap. This will store, for key
      // parts which are not always covering, whether or not it is covering
      // for this record.
      covered_bitmap_pos = unpack_info->get_current_pos();
      unpack_info->write_uint16(0);
    }
  }

  MY_BITMAP covered_bitmap;
  my_bitmap_map covered_bits;
  uint curr_bitmap_pos = 0;
  bitmap_init(&covered_bitmap, &covered_bits, MAX_REF_PARTS);

  for (uint i = 0; i < n_key_parts; i++) {
    // Fill hidden pk id into the last key part for secondary keys for tables
    // with no pk
    if (hidden_pk_exists && hidden_pk_id && i + 1 == n_key_parts) {
      m_pack_info[i].fill_hidden_pk_val(&tuple, hidden_pk_id);
      break;
    }

    Field *const field = m_pack_info[i].get_field_in_table(tbl);
    assert(field != nullptr);

    uint field_offset = field->field_ptr() - tbl->record[0];
    uint null_offset = field->null_offset(tbl->record[0]);
    bool maybe_null = field->is_nullable();

    field->move_field(
        const_cast<uchar *>(record) + field_offset,
        maybe_null ? const_cast<uchar *>(record) + null_offset : nullptr,
        field->null_bit);
    // WARNING! Don't return without restoring field->field_ptr() and
    // field->null_ptr
    tuple = pack_field(field, &m_pack_info[i], tuple, packed_tuple, pack_buffer,
                       unpack_info, n_null_fields);

    // If this key part is a prefix of a VARCHAR field, check if it's covered.
    if (m_store_covered_bitmap &&
        Rdb_key_def::is_variable_length_field(field->real_type()) &&
        m_pack_info[i].m_covered == KEY_MAY_BE_COVERED &&
        curr_bitmap_pos < MAX_REF_PARTS) {
      if (m_pack_info[i].m_unpack_func != nullptr &&
          is_varlength_prefix_covering(field, &m_pack_info[i])) {
        bitmap_set_bit(&covered_bitmap, curr_bitmap_pos);
      }
      curr_bitmap_pos++;
    }

    // Restore field->field_ptr() and field->null_ptr
    field->move_field(tbl->record[0] + field_offset,
                      maybe_null ? tbl->record[0] + null_offset : nullptr,
                      field->null_bit);
  }

  if (unpack_info) {
    const size_t len = unpack_info->get_current_pos() - unpack_start_pos;
    assert(len <= std::numeric_limits<uint16_t>::max());

    // Don't store the unpack_info if it has only the header (that is, there's
    // no meaningful content).
    // Primary Keys are special: for them, store the unpack_info even if it's
    // empty (provided m_maybe_unpack_info==true, see
    // ha_rocksdb::convert_record_to_storage_format)
    if (m_index_type == Rdb_key_def::INDEX_TYPE_SECONDARY) {
      if (len == get_unpack_header_size(tag) && !covered_bits) {
        unpack_info->truncate(unpack_start_pos);
      } else if (m_store_covered_bitmap) {
        unpack_info->write_uint16_at(covered_bitmap_pos, covered_bits);
      }
    } else {
      unpack_info->write_uint16_at(unpack_len_pos, len);
    }

    //
    // Secondary keys have key and value checksums in the value part
    // Primary key is a special case (the value part has non-indexed columns),
    // so the checksums are computed and stored by
    // ha_rocksdb::convert_record_to_storage_format
    //
    if (should_store_row_debug_checksums) {
      const ha_checksum key_crc32 =
          my_core::my_checksum(0, packed_tuple, tuple - packed_tuple);
      const ha_checksum val_crc32 = my_core::my_checksum(
          0, unpack_info->ptr(), unpack_info->get_current_pos());

      unpack_info->write_uint8(RDB_CHECKSUM_DATA_TAG);
      unpack_info->write_uint32(key_crc32);
      unpack_info->write_uint32(val_crc32);
    }
  }

  assert(is_storage_available(tuple - packed_tuple, 0));

  return tuple - packed_tuple;
}

/**
  Pack the hidden primary key into mem-comparable form.

  @param
    tbl                   Table we're working on
    hidden_pk_id     IN   New value to be packed into key
    packed_tuple     OUT  Key in the mem-comparable form

  @return
    Length of the packed tuple
*/

uint Rdb_key_def::pack_hidden_pk(const longlong hidden_pk_id,
                                 uchar *const packed_tuple) const {
  assert(packed_tuple != nullptr);

  uchar *tuple = packed_tuple;
  rdb_netbuf_store_index(tuple, m_index_number);
  tuple += INDEX_NUMBER_SIZE;
  assert(m_key_parts == 1);
  assert(is_storage_available(tuple - packed_tuple,
                                   m_pack_info[0].m_max_image_len));

  m_pack_info[0].fill_hidden_pk_val(&tuple, hidden_pk_id);

  assert(is_storage_available(tuple - packed_tuple, 0));
  return tuple - packed_tuple;
}

/*
  Compares two keys without unpacking

  @detail
  @return
    0 - Ok. column_index is the index of the first column which is different.
          -1 if two kes are equal
    1 - Data format error.
*/
int Rdb_key_def::compare_keys(const rocksdb::Slice *key1,
                              const rocksdb::Slice *key2,
                              std::size_t *const column_index) const {
  assert(key1 != nullptr);
  assert(key2 != nullptr);
  assert(column_index != nullptr);

  // the caller should check the return value and
  // not rely on column_index being valid
  *column_index = 0xbadf00d;

  Rdb_string_reader reader1(key1);
  Rdb_string_reader reader2(key2);

  // Skip the index number
  auto indexp1 = reader1.read(INDEX_NUMBER_SIZE);
  if (!indexp1) return HA_EXIT_FAILURE;

  auto indexp2 = reader2.read(INDEX_NUMBER_SIZE);
  if (!indexp2) return HA_EXIT_FAILURE;

  // shouldn't compare with other index
  assert(memcmp(indexp1, indexp2, INDEX_NUMBER_SIZE) == 0);

  for (uint i = 0; i < m_key_parts; i++) {
    const Rdb_field_packing *const fpi = &m_pack_info[i];
    if (fpi->m_maybe_null) {
      const auto nullp1 = reader1.read(1);
      const auto nullp2 = reader2.read(1);

      if (nullp1 == nullptr || nullp2 == nullptr) {
        return HA_EXIT_FAILURE;
      }

      if (*nullp1 != *nullp2) {
        *column_index = i;
        return HA_EXIT_SUCCESS;
      }

      if (*nullp1 == 0) {
        /* This is a NULL value */
        continue;
      }
    }

    const auto before_skip1 = reader1.get_current_ptr();
    const auto before_skip2 = reader2.get_current_ptr();
    assert(fpi->m_skip_func);
    if ((fpi->m_skip_func)(fpi, nullptr, &reader1)) {
      return HA_EXIT_FAILURE;
    }
    if ((fpi->m_skip_func)(fpi, nullptr, &reader2)) {
      return HA_EXIT_FAILURE;
    }
    const auto size1 = reader1.get_current_ptr() - before_skip1;
    const auto size2 = reader2.get_current_ptr() - before_skip2;
    if (size1 != size2) {
      *column_index = i;
      return HA_EXIT_SUCCESS;
    }

    if (memcmp(before_skip1, before_skip2, size1) != 0) {
      *column_index = i;
      return HA_EXIT_SUCCESS;
    }
  }

  *column_index = m_key_parts;
  return HA_EXIT_SUCCESS;
}

/*
  @brief
    Given a zero-padded key, determine its real key length

  @detail
    Fixed-size skip functions just read.
*/

size_t Rdb_key_def::key_length(const TABLE *const table,
                               const rocksdb::Slice &key) const {
  assert(table != nullptr);

  Rdb_string_reader reader(&key);

  if ((!reader.read(INDEX_NUMBER_SIZE))) {
    return size_t(-1);
  }
  for (uint i = 0; i < m_key_parts; i++) {
    const Rdb_field_packing *fpi = &m_pack_info[i];
    const Field *field = nullptr;
    if (m_index_type != INDEX_TYPE_HIDDEN_PRIMARY) {
      field = fpi->get_field_in_table(table);
    }
    if ((fpi->m_skip_func)(fpi, field, &reader)) {
      return size_t(-1);
    }
  }
  return key.size() - reader.remaining_bytes();
}

/*
  Decode unpack info in value

  @return
    HA_EXIT_SUCCESS    OK
    other              HA_ERR error code
 */
int Rdb_key_def::decode_unpack_info(Rdb_string_reader *unp_reader,
                                    bool *has_unpack_info,
                                    const char **unpack_header) const {
  // For secondary keys, we expect the value field to contain index flags,
  // unpack data, and checksum data in that order. One or all can be missing,
  // but they cannot be reordered.
  if (unp_reader->remaining_bytes()) {
    if (m_index_type == INDEX_TYPE_SECONDARY &&
        m_total_index_flags_length > 0 &&
        !unp_reader->read(m_total_index_flags_length)) {
      return HA_ERR_ROCKSDB_CORRUPT_DATA;
    }
  }

  *unpack_header = unp_reader->get_current_ptr();
  *has_unpack_info =
      unp_reader->remaining_bytes() && is_unpack_data_tag(*unpack_header[0]);
  if (*has_unpack_info) {
    if (!unp_reader->read(get_unpack_header_size(*unpack_header[0]))) {
      return HA_ERR_ROCKSDB_CORRUPT_DATA;
    }
  }

  return HA_EXIT_SUCCESS;
}

/*
  Take mem-comparable form and unpack_info and unpack it to Table->record

  @detail
    not all indexes support this

  @return
    HA_EXIT_SUCCESS    OK
    other              HA_ERR error code
*/

int Rdb_key_def::unpack_record(TABLE *const table, uchar *const buf,
                               const rocksdb::Slice *const packed_key,
                               const rocksdb::Slice *const unpack_info,
                               const bool verify_row_debug_checksums) const {
  Rdb_string_reader reader(packed_key);
  Rdb_string_reader unp_reader = Rdb_string_reader::read_or_empty(unpack_info);

  // There is no checksuming data after unpack_info for primary keys, because
  // the layout there is different. The checksum is verified in
  // ha_rocksdb::convert_record_from_storage_format instead.
  assert_IMP(!(m_index_type == INDEX_TYPE_SECONDARY),
                  !verify_row_debug_checksums);

  // Skip the index number
  if ((!reader.read(INDEX_NUMBER_SIZE))) {
    return HA_ERR_ROCKSDB_CORRUPT_DATA;
  }

  const char *unpack_header;
  bool has_unpack_info;
  int err = HA_EXIT_SUCCESS;
  err = decode_unpack_info(&unp_reader, &has_unpack_info, &unpack_header);
  if (err) {
    return err;
  }

  // Reset the blob buffer required for unpacking.
  auto handler = (ha_rocksdb *)table->file;
  if (this->m_max_blob_length) {
    auto handler = (ha_rocksdb *)table->file;
    if (handler->reset_blob_buffer(this->m_max_blob_length)) {
      return HA_ERR_OUT_OF_MEM;
    }
  }

  // Read the covered bitmap
  MY_BITMAP covered_bitmap;
  my_bitmap_map covered_bits;
  bool has_covered_bitmap =
      has_unpack_info && (unpack_header[0] == RDB_UNPACK_COVERED_DATA_TAG);
  if (has_covered_bitmap) {
    bitmap_init(&covered_bitmap, &covered_bits, MAX_REF_PARTS);
    covered_bits = rdb_netbuf_to_uint16((const uchar *)unpack_header +
                                        sizeof(RDB_UNPACK_COVERED_DATA_TAG));
  }

  Rdb_key_field_iterator iter(
      this, m_pack_info, &reader, &unp_reader, table, has_unpack_info,
      has_covered_bitmap ? &covered_bitmap : nullptr, buf);
  while (iter.has_next()) {
    err = iter.next();
    if (unlikely(err)) {
      return err;
    }
  }

  /*
    Check checksum values if present
  */
  const char *ptr;
  if ((ptr = unp_reader.read(1)) && *ptr == RDB_CHECKSUM_DATA_TAG) {
    if (verify_row_debug_checksums) {
      uint32_t stored_key_chksum = rdb_netbuf_to_uint32(
          (const uchar *)unp_reader.read(RDB_CHECKSUM_SIZE));
      const uint32_t stored_val_chksum = rdb_netbuf_to_uint32(
          (const uchar *)unp_reader.read(RDB_CHECKSUM_SIZE));

      const ha_checksum computed_key_chksum = my_core::my_checksum(
          0, (const uchar *)packed_key->data(), packed_key->size());
      const ha_checksum computed_val_chksum =
          my_core::my_checksum(0, (const uchar *)unpack_info->data(),
                               unpack_info->size() - RDB_CHECKSUM_CHUNK_SIZE);

      DBUG_EXECUTE_IF("myrocks_simulate_bad_key_checksum1",
                      stored_key_chksum++;);

      if (stored_key_chksum != computed_key_chksum) {
        report_checksum_mismatch(true, packed_key->data(), packed_key->size());
        return HA_ERR_ROCKSDB_CHECKSUM_MISMATCH;
      }

      if (stored_val_chksum != computed_val_chksum) {
        report_checksum_mismatch(false, unpack_info->data(),
                                 unpack_info->size() - RDB_CHECKSUM_CHUNK_SIZE);
        return HA_ERR_ROCKSDB_CHECKSUM_MISMATCH;
      }
    } else {
      /* The checksums are present but we are not checking checksums */
    }
    handler->m_validated_checksums++;
  }

  if (reader.remaining_bytes()) return HA_ERR_ROCKSDB_CORRUPT_DATA;

  return HA_EXIT_SUCCESS;
}

bool Rdb_key_def::table_has_hidden_pk(const TABLE *const table) {
  return table->s->primary_key == MAX_INDEXES;
}

void Rdb_key_def::report_checksum_mismatch(const bool is_key,
                                           const char *const data,
                                           const size_t data_size) const {
  // NO_LINT_DEBUG
  sql_print_error("Checksum mismatch in %s of key-value pair for index 0x%x",
                  is_key ? "key" : "value", get_index_number());

  const std::string buf = rdb_hexdump(data, data_size, RDB_MAX_HEXDUMP_LEN);
  // NO_LINT_DEBUG
  sql_print_error("Data with incorrect checksum (%" PRIu64 " bytes): %s",
                  (uint64_t)data_size, buf.c_str());

  my_error(ER_INTERNAL_ERROR, MYF(0), "Record checksum mismatch");
}

bool Rdb_key_def::index_format_min_check(const int pk_min,
                                         const int sk_min) const {
  switch (m_index_type) {
    case INDEX_TYPE_PRIMARY:
    case INDEX_TYPE_HIDDEN_PRIMARY:
      return (m_kv_format_version >= pk_min);
    case INDEX_TYPE_SECONDARY:
      return (m_kv_format_version >= sk_min);
    default:
      assert(0);
      return false;
  }
}

///////////////////////////////////////////////////////////////////////////////////////////
// Rdb_field_packing
///////////////////////////////////////////////////////////////////////////////////////////

/*
  Function of type rdb_index_field_skip_t
*/

int Rdb_key_def::skip_max_length(const Rdb_field_packing *const fpi,
                                 const Field *const field
                                     MY_ATTRIBUTE((__unused__)),
                                 Rdb_string_reader *const reader) {
  if (!reader->read(fpi->m_max_image_len)) return HA_EXIT_FAILURE;
  return HA_EXIT_SUCCESS;
}

/*
  (RDB_ESCAPE_LENGTH-1) must be an even number so that pieces of lines are not
  split in the middle of an UTF-8 character. See the implementation of
  unpack_binary_or_utf8_varlength.
*/
#define RDB_ESCAPE_LENGTH 9

#define RDB_ENCODED_SIZE(len)                                   \
  ((len + (RDB_ESCAPE_LENGTH - 2)) / (RDB_ESCAPE_LENGTH - 1)) * \
      RDB_ESCAPE_LENGTH

/*
  Function of type rdb_index_field_skip_t
*/

int Rdb_key_def::skip_variable_length_encoding(
    const Rdb_field_packing *const fpi MY_ATTRIBUTE((__unused__)),
    const Field *const field, Rdb_string_reader *const reader) {
  const uchar *ptr;
  bool finished = false;

  size_t dst_len; /* How much data can be there */
  if (field) {
    dst_len = fpi->m_max_field_bytes;
  } else {
    dst_len = UINT_MAX;
  }

  /* Decode the length-emitted encoding here */
  while ((ptr = (const uchar *)reader->read(RDB_ESCAPE_LENGTH))) {
    uint used_bytes;

    used_bytes =
        calc_unpack_variable_format(ptr[RDB_ESCAPE_LENGTH - 1], &finished);

    if (used_bytes == (uint)-1 || dst_len < used_bytes) {
      return HA_EXIT_FAILURE;  // Corruption in the data
    }

    if (finished) {
      break;
    }

    dst_len -= used_bytes;
  }

  if (!finished) {
    return HA_EXIT_FAILURE;
  }

  return HA_EXIT_SUCCESS;
}

const int VARCHAR_CMP_LESS_THAN_SPACES = 1;
const int VARCHAR_CMP_EQUAL_TO_SPACES = 2;
const int VARCHAR_CMP_GREATER_THAN_SPACES = 3;

/*
  Skip a keypart that uses Variable-Length Space-Padded encoding
*/

int Rdb_key_def::skip_variable_space_pad(const Rdb_field_packing *const fpi,
                                         const Field *const field,
                                         Rdb_string_reader *const reader) {
  const uchar *ptr;
  bool finished = false;

  size_t dst_len = UINT_MAX; /* How much data can be there */

  if (field) {
    dst_len = fpi->m_max_field_bytes;
  }

  uchar encoded_byte = *(const uchar *)reader->read(1);
  // Check if lead segment byte is VARCHAR_CMP_EQUAL_TO_SPACES.
  // This indicates empty content and we can return prematurely.
  if (encoded_byte == VARCHAR_CMP_EQUAL_TO_SPACES) return HA_EXIT_SUCCESS;

  /* Decode the length-emitted encoding here */
  while ((ptr = (const uchar *)reader->read(fpi->m_segment_size))) {
    // See pack_with_varlength_space_pad
    const uchar c = ptr[fpi->m_segment_size - 1];
    if (c == VARCHAR_CMP_EQUAL_TO_SPACES) {
      // This is the last segment
      finished = true;
      break;
    } else if (c == VARCHAR_CMP_LESS_THAN_SPACES ||
               c == VARCHAR_CMP_GREATER_THAN_SPACES) {
      // This is not the last segment
      if ((fpi->m_segment_size - 1) > dst_len) {
        // The segment is full of data but the table field can't hold that
        // much! This must be data corruption.
        return HA_EXIT_FAILURE;
      }
      dst_len -= (fpi->m_segment_size - 1);
    } else {
      // Encountered a value that's none of the VARCHAR_CMP* constants
      // It's data corruption.
      return HA_EXIT_FAILURE;
    }
  }
  return finished ? HA_EXIT_SUCCESS : HA_EXIT_FAILURE;
}

/*
  Function of type rdb_index_field_unpack_t

  Packs an integer by converting them to big endian. After that,
  unsigned integers are naturally in the right order. For signed
  integer, reverse the sign bit so that positive numbers are
  bigger than negative numbers, byte-wise.
*/
template <int length>
void Rdb_key_def::pack_integer(
    Rdb_field_packing *const fpi MY_ATTRIBUTE((__unused__)), Field *const field,
    uchar *buf MY_ATTRIBUTE((__unused__)), uchar **dst,
    Rdb_pack_field_context *const pack_ctx MY_ATTRIBUTE((__unused__))) {
  assert(fpi != nullptr);
  assert(field != nullptr);
  assert(dst != nullptr);
  assert(*dst != nullptr);
  assert(length == fpi->m_max_image_len);
  const uchar *ptr = field->field_ptr();
  const bool unsigned_flag = static_cast<Field_num *>(field)->is_unsigned();
  uchar *to = *dst;

#ifdef WORDS_BIGENDIAN
  {
    if (unsigned_flag) {
      to[0] = ptr[0];
    } else {
      to[0] = static_cast<char>(ptr[0] ^ 128);  // Reverse the sign bit.
    }

    /* Parameterized length should enable loop unrolling */
    for (int i = 1; i < length; i++) to[i] = ptr[i];
  }
#else
  {
    const int sign_byte = ptr[length - 1];
    if (unsigned_flag) {
      to[0] = sign_byte;
    } else {
      to[0] = static_cast<char>(sign_byte ^ 128);  // Reverse the sign bit.
    }

    /* Parameterized length should enable loop unrolling */
    for (int i = 1, j = length - 2; i < length; ++i, --j) to[i] = ptr[j];
  }
#endif

  *dst += length;
}

template <int length>
void Rdb_key_def::pack_unsigned(
    Rdb_field_packing *const fpi MY_ATTRIBUTE((__unused__)), Field *const field,
    uchar *buf MY_ATTRIBUTE((__unused__)), uchar **dst,
    Rdb_pack_field_context *const pack_ctx MY_ATTRIBUTE((__unused__))) {
  assert(fpi != nullptr);
  assert(field != nullptr);
  assert(dst != nullptr);
  assert(*dst != nullptr);
  assert(length == fpi->m_max_image_len);

  const uchar *ptr = field->field_ptr();
  uchar *to = *dst;

#ifdef WORDS_BIGENDIAN
  /* Parameterized length should enable loop unrolling */
  for (int i = 0; i < length; i++) to[i] = ptr[i];
#else
  /* Parameterized length should enable loop unrolling */
  for (int i = 0, j = length - 1; i < length; ++i, --j) to[i] = ptr[j];
#endif

  *dst += length;
}

template <int length>
int Rdb_key_def::unpack_integer(
    Rdb_field_packing *const fpi MY_ATTRIBUTE((__unused__)), Field *const field,
    uchar *const to, Rdb_string_reader *const reader,
    Rdb_string_reader *const unp_reader MY_ATTRIBUTE((__unused__))) {
  assert(length == fpi->m_max_image_len);

  const uchar *from;
  if (!(from = (const uchar *)reader->read(length))) {
    return UNPACK_FAILURE; /* Mem-comparable image doesn't have enough bytes */
  }

#ifdef WORDS_BIGENDIAN
  {
    if (static_cast<Field_num *>(field)->is_unsigned()) {
      to[0] = from[0];
    } else {
      to[0] = static_cast<char>(from[0] ^ 128);  // Reverse the sign bit.
    }
    /* Parameterized length should enable loop unrolling */
    for (int i = 1; i < length; i++) to[i] = from[i];
  }
#else
  {
    const int sign_byte = from[0];
    if (static_cast<Field_num *>(field)->is_unsigned()) {
      to[length - 1] = sign_byte;
    } else {
      to[length - 1] =
          static_cast<char>(sign_byte ^ 128);  // Reverse the sign bit.
    }

    /* Parameterized length should enable loop unrolling */
    for (int i = 0, j = length - 1; i < length - 1; ++i, --j) to[i] = from[j];
  }
#endif
  return UNPACK_SUCCESS;
}

template <int length>
int Rdb_key_def::unpack_unsigned(
    Rdb_field_packing *const fpi MY_ATTRIBUTE((__unused__)),
    Field *const field MY_ATTRIBUTE((__unused__)), uchar *const to,
    Rdb_string_reader *const reader,
    Rdb_string_reader *const unp_reader MY_ATTRIBUTE((__unused__))) {
  assert(length == fpi->m_max_image_len);

  const uchar *from;
  if (!(from = (const uchar *)reader->read(length))) {
    return UNPACK_FAILURE; /* Mem-comparable image doesn't have enough bytes */
  }

#ifdef WORDS_BIGENDIAN
  /* Parameterized length should enable loop unrolling */
  for (int i = 0; i < length; i++) to[i] = from[i];
#else
  /* Parameterized length should enable loop unrolling */
  for (int i = 0, j = length - 1; i < length; ++i, --j) to[i] = from[j];
#endif

  return UNPACK_SUCCESS;
}

#if !defined(WORDS_BIGENDIAN)
static void rdb_swap_double_bytes(uchar *const dst, const uchar *const src) {
#if defined(__FLOAT_WORD_ORDER) && (__FLOAT_WORD_ORDER == __BIG_ENDIAN)
  // A few systems store the most-significant _word_ first on little-endian
  dst[0] = src[3];
  dst[1] = src[2];
  dst[2] = src[1];
  dst[3] = src[0];
  dst[4] = src[7];
  dst[5] = src[6];
  dst[6] = src[5];
  dst[7] = src[4];
#else
  dst[0] = src[7];
  dst[1] = src[6];
  dst[2] = src[5];
  dst[3] = src[4];
  dst[4] = src[3];
  dst[5] = src[2];
  dst[6] = src[1];
  dst[7] = src[0];
#endif
}

static void rdb_swap_float_bytes(uchar *const dst, const uchar *const src) {
  dst[0] = src[3];
  dst[1] = src[2];
  dst[2] = src[1];
  dst[3] = src[0];
}
#else
#define rdb_swap_double_bytes nullptr
#define rdb_swap_float_bytes nullptr
#endif

int Rdb_key_def::unpack_floating_point(
    uchar *const dst, Rdb_string_reader *const reader, const size_t size,
    const int exp_digit, const uchar *const zero_pattern,
    const uchar *const zero_val, void (*swap_func)(uchar *, const uchar *)) {
  const uchar *const from = (const uchar *)reader->read(size);
  if (from == nullptr) {
    /* Mem-comparable image doesn't have enough bytes */
    return UNPACK_FAILURE;
  }

  /* Check to see if the value is zero */
  if (memcmp(from, zero_pattern, size) == 0) {
    memcpy(dst, zero_val, size);
    return UNPACK_SUCCESS;
  }

#if defined(WORDS_BIGENDIAN)
  // On big-endian, output can go directly into result
  uchar *const tmp = dst;
#else
  // Otherwise use a temporary buffer to make byte-swapping easier later
  uchar tmp[8];
#endif

  memcpy(tmp, from, size);

  if (tmp[0] & 0x80) {
    // If the high bit is set the original value was positive so
    // remove the high bit and subtract one from the exponent.
    ushort exp_part = ((ushort)tmp[0] << 8) | (ushort)tmp[1];
    exp_part &= 0x7FFF;                             // clear high bit;
    exp_part -= (ushort)1 << (16 - 1 - exp_digit);  // subtract from exponent
    tmp[0] = (uchar)(exp_part >> 8);
    tmp[1] = (uchar)exp_part;
  } else {
    // Otherwise the original value was negative and all bytes have been
    // negated.
    for (size_t ii = 0; ii < size; ii++) tmp[ii] ^= 0xFF;
  }

#if !defined(WORDS_BIGENDIAN)
  // On little-endian, swap the bytes around
  swap_func(dst, tmp);
#else
  assert(swap_func == nullptr);
#endif

  return UNPACK_SUCCESS;
}

#if !defined(DBL_EXP_DIG)
#define DBL_EXP_DIG (sizeof(double) * 8 - DBL_MANT_DIG)
#endif

void Rdb_key_def::pack_double_internal(double nr, uchar *to) {
  uchar *tmp = to;
  if (nr == 0.0) { /* Change to zero string */
    tmp[0] = 0x80;
    memset(tmp + 1, 0, sizeof(nr) - 1);
  } else {
#ifdef WORDS_BIGENDIAN
    memcpy(tmp, &nr, sizeof(nr));
#else
    {
      uchar *ptr = reinterpret_cast<uchar *>(&nr);
#if defined(__FLOAT_WORD_ORDER) && (__FLOAT_WORD_ORDER == __BIG_ENDIAN)
      tmp[0] = ptr[3];
      tmp[1] = ptr[2];
      tmp[2] = ptr[1];
      tmp[3] = ptr[0];
      tmp[4] = ptr[7];
      tmp[5] = ptr[6];
      tmp[6] = ptr[5];
      tmp[7] = ptr[4];
#else
      tmp[0] = ptr[7];
      tmp[1] = ptr[6];
      tmp[2] = ptr[5];
      tmp[3] = ptr[4];
      tmp[4] = ptr[3];
      tmp[5] = ptr[2];
      tmp[6] = ptr[1];
      tmp[7] = ptr[0];
#endif
    }
#endif
    /* Negative */
    if (tmp[0] & 0x80) {
      /* make complement */
      uint i;
      for (i = 0; i < sizeof(nr); i++) tmp[i] = tmp[i] ^ (uchar)255;
    } else {
      /* Set high and move exponent one up */
      ushort exp_part =
          (((ushort)tmp[0] << 8) | (ushort)tmp[1] | (ushort)32768);
      exp_part += (ushort)1 << (16 - 1 - DBL_EXP_DIG);
      tmp[0] = (uchar)(exp_part >> 8);
      tmp[1] = (uchar)exp_part;
    }
  }
}

/*
  Function of type rdb_index_field_unpack_t

  Pack a double in the same way as float
*/
void Rdb_key_def::pack_double(
    Rdb_field_packing *const fpi, Field *const field,
    uchar *const buf MY_ATTRIBUTE((__unused__)), uchar **dst,
    Rdb_pack_field_context *const pack_ctx MY_ATTRIBUTE((__unused__))) {
  assert(fpi != nullptr);
  assert(field != nullptr);
  assert(dst != nullptr);
  assert(*dst != nullptr);
  assert(field->real_type() == MYSQL_TYPE_DOUBLE);

  const int length = fpi->m_max_image_len;
  const uchar *ptr = field->field_ptr();
  uchar *to = *dst;

  double nr;
#ifdef WORDS_BIGENDIAN
  if (field->table->s->db_low_byte_first) {
    float8get(&nr, ptr);
  } else {
#endif
    nr = doubleget(ptr);
#ifdef WORDS_BIGENDIAN
  }
#endif

  if (length < 8) {
    std::array<uchar, 8> buff;
    pack_double_internal(nr, buff.data());
    memcpy(to, buff.data(), length);
  } else {
    pack_double_internal(nr, to);
  }

  *dst += length;
}

/*
  Function of type rdb_index_field_unpack_t

  Unpack a double by doing the reverse action of change_double_for_sort
  (sql/filesort.cc).  Note that this only works on IEEE values.
  Note also that this code assumes that NaN and +/-Infinity are never
  allowed in the database.
*/
int Rdb_key_def::unpack_double(
    Rdb_field_packing *const fpi MY_ATTRIBUTE((__unused__)),
    Field *const field MY_ATTRIBUTE((__unused__)), uchar *const field_ptr,
    Rdb_string_reader *const reader,
    Rdb_string_reader *const unp_reader MY_ATTRIBUTE((__unused__))) {
  static double zero_val = 0.0;
  static const uchar zero_pattern[8] = {128, 0, 0, 0, 0, 0, 0, 0};

  return unpack_floating_point(field_ptr, reader, sizeof(double), DBL_EXP_DIG,
                               zero_pattern, (const uchar *)&zero_val,
                               rdb_swap_double_bytes);
}

#if !defined(FLT_EXP_DIG)
#define FLT_EXP_DIG (sizeof(float) * 8 - FLT_MANT_DIG)
#endif

/*
  Function of type rdb_index_field_unpack_t

  Pack a float in memcmp key format:
  1) If positive, add 1 to exponent and make sign bit to be 1 (so that
  positive floats are bigger than negative floats, byte-wise). The +1
  part wasn't strictly necessary but need to be preserved for compat
  2) If negative, negate all bits to preserve the ordering (note
  negative floats are stored as-is without compliments)
  3) If 0 / -0, convert to positive 0 with sign bit 1 in order to be
  bigger than negative floats, byte-wise

  Note that this only works on IEEE values.

  Note also that this code assumes that NaN and +/-Infinity are never
  allowed in the database.
*/
void Rdb_key_def::pack_float(
    Rdb_field_packing *const fpi, Field *const field,
    uchar *const buf MY_ATTRIBUTE((__unused__)), uchar **dst,
    Rdb_pack_field_context *const pack_ctx MY_ATTRIBUTE((__unused__))) {
  assert(fpi != nullptr);
  assert(field != nullptr);
  assert(dst != nullptr);
  assert(*dst != nullptr);
  assert(field->real_type() == MYSQL_TYPE_FLOAT);

  const int length = fpi->m_max_image_len;
  const uchar *ptr = field->field_ptr();
  uchar *to = *dst;

  assert(length == sizeof(float));
  float nr;
#ifdef WORDS_BIGENDIAN
  if (field->table->s->db_low_byte_first) {
    float4get(nr, ptr);
  } else {
#endif
    memcpy(&nr, ptr, std::min<uint>(length, sizeof(float)));
#ifdef WORDS_BIGENDIAN
  }
#endif

  uchar *tmp = to;
  if (nr == 0.0f) { /* Change to zero string */
    tmp[0] = (uchar)128;
    memset(tmp + 1, 0, std::min<uint>(length, sizeof(nr) - 1));
  } else {
#ifdef WORDS_BIGENDIAN
    memcpy(tmp, &nr, sizeof(nr));
#else
    tmp[0] = ptr[3];
    tmp[1] = ptr[2];
    tmp[2] = ptr[1];
    tmp[3] = ptr[0];
#endif
    /* Negative */
    if (tmp[0] & 128) {
      /* make complement */
      uint i;
      for (i = 0; i < sizeof(nr); i++) tmp[i] = (uchar)(tmp[i] ^ (uchar)255);
    } else {
      /* exponent + 1 */
      ushort exp_part =
          (((ushort)tmp[0] << 8) | (ushort)tmp[1] | (ushort)32768);
      exp_part += (ushort)1 << (16 - 1 - FLT_EXP_DIG);
      tmp[0] = (uchar)(exp_part >> 8);
      tmp[1] = (uchar)exp_part;
    }
  }

  *dst += length;
}

/*
  Function of type rdb_index_field_unpack_t

  Unpack a float by doing the reverse action of pack_float

  Note that this only works on IEEE values.

  Unpack a float by doing the reverse action of Field_float::make_sort_key
  (sql/field.cc).  Note that this only works on IEEE values.
  Note also that this code assumes that NaN and +/-Infinity are never
  allowed in the database.
*/
int Rdb_key_def::unpack_float(
    Rdb_field_packing *const fpi MY_ATTRIBUTE((__unused__)),
    Field *const field MY_ATTRIBUTE((__unused__)), uchar *const field_ptr,
    Rdb_string_reader *const reader,
    Rdb_string_reader *const unp_reader MY_ATTRIBUTE((__unused__))) {
  static float zero_val = 0.0;
  static const uchar zero_pattern[4] = {128, 0, 0, 0};

  return unpack_floating_point(field_ptr, reader, sizeof(float), FLT_EXP_DIG,
                               zero_pattern, (const uchar *)&zero_val,
                               rdb_swap_float_bytes);
}

/*
  Function of type rdb_index_field_unpack_t

  Pack a newdate with byte 0 and byte 2 swapped
 */
void Rdb_key_def::pack_newdate(
    Rdb_field_packing *const fpi, Field *const field,
    uchar *const buf MY_ATTRIBUTE((__unused__)), uchar **dst,
    Rdb_pack_field_context *const pack_ctx MY_ATTRIBUTE((__unused__))) {
  assert(fpi != nullptr);
  assert(field != nullptr);
  assert(dst != nullptr);
  assert(*dst != nullptr);
  assert(field->real_type() == MYSQL_TYPE_NEWDATE);

  const int length = fpi->m_max_image_len;
  const uchar *ptr = field->field_ptr();
  uchar *to = *dst;

  assert(length == 3);
  to[0] = ptr[2];
  to[1] = ptr[1];
  to[2] = ptr[0];

  *dst += length;
}

/*
  Function of type rdb_index_field_unpack_t used to
  Unpack by doing the reverse action to pack_newdate
*/

int Rdb_key_def::unpack_newdate(
    Rdb_field_packing *const fpi MY_ATTRIBUTE((__unused__)),
    Field *const field MY_ATTRIBUTE((__unused__)), uchar *const field_ptr,
    Rdb_string_reader *const reader,
    Rdb_string_reader *const unp_reader MY_ATTRIBUTE((__unused__))) {
  const char *from;
  assert(fpi->m_max_image_len == 3);

  if (!(from = reader->read(3))) {
    /* Mem-comparable image doesn't have enough bytes */
    return UNPACK_FAILURE;
  }

  field_ptr[0] = from[2];
  field_ptr[1] = from[1];
  field_ptr[2] = from[0];
  return UNPACK_SUCCESS;
}

/*
  Function of type rdb_index_field_unpack_t

  Pack bit type with uneven hights bits (mod of 8) first and then
  the rest. See Field_bit::Field_bit for more details.
 */
void Rdb_key_def::pack_bit(
    Rdb_field_packing *const fpi, Field *const field,
    uchar *const buf MY_ATTRIBUTE((__unused__)), uchar **dst,
    Rdb_pack_field_context *const pack_ctx MY_ATTRIBUTE((__unused__))) {
  assert(fpi != nullptr);
  assert(field != nullptr);
  assert(dst != nullptr);
  assert(*dst != nullptr);
  assert(field->real_type() == MYSQL_TYPE_BIT);

  uint length = fpi->m_max_image_len;
  const uchar *ptr = field->field_ptr();
  uchar *to = *dst;
  auto *field_bit = static_cast<Field_bit *>(field);

  if (field_bit->bit_len) {
    /* uneven high bits */
    uchar bits = get_rec_bits(field_bit->bit_ptr, field_bit->bit_ofs,
                              field_bit->bit_len);
    *to++ = bits;
    length--;
  }

  /* copy the rest */
  uint data_length = std::min(length, field_bit->bytes_in_rec);
  memcpy(to, ptr, data_length);

  *dst += fpi->m_max_image_len;
}

/*
  Function of type rdb_index_field_unpack_t, used to
  Unpack the bit by copying it over.
  See Field_bit::unpack_bit for more details.
*/
int Rdb_key_def::unpack_bit(Rdb_field_packing *const fpi, Field *const field,
                            uchar *const to, Rdb_string_reader *const reader,
                            Rdb_string_reader *const unp_reader
                                MY_ATTRIBUTE((__unused__))) {
  assert(fpi != nullptr);
  assert(field != nullptr);
  const char *from;
  if (!(from = reader->read(fpi->m_max_image_len))) {
    /* Mem-comparable image doesn't have enough bytes */
    return UNPACK_FAILURE;
  }
  auto *field_bit = static_cast<Field_bit *>(field);
  if (field_bit->bit_len > 0) {
    /* uneven high bits */
    set_rec_bits(*from, field_bit->bit_ptr, field_bit->bit_ofs,
                 field_bit->bit_len);
    from++;
  }
  /* copy the rest */
  uint data_length =
      std::min((uint)fpi->m_max_image_len, field_bit->bytes_in_rec);
  memcpy(to, from, data_length);
  return UNPACK_SUCCESS;
}

/*
  Function of type rdb_index_field_unpack_t, used to
  unpack the string by copying it over.
  This is for any data type that is a simply memcpy, such as
  BINARY(n) where the value occupies the whole length.
*/

void Rdb_key_def::pack_binary_str(
    Rdb_field_packing *const fpi, Field *const field,
    uchar *const buf MY_ATTRIBUTE((__unused__)), uchar **dst,
    Rdb_pack_field_context *const pack_ctx MY_ATTRIBUTE((__unused__))) {
  assert(fpi != nullptr);
  assert(field != nullptr);
  assert(dst != nullptr);
  assert(*dst != nullptr);

  memcpy(*dst, field->field_ptr(), fpi->m_max_image_len);

  *dst += fpi->m_max_image_len;
}

/*
  Function of type rdb_index_field_unpack_t, used to
  Unpack the string by copying it over.
  This is for BINARY(n) where the value occupies the whole length.
*/

int Rdb_key_def::unpack_binary_str(
    Rdb_field_packing *const fpi, Field *const field MY_ATTRIBUTE((__unused__)),
    uchar *const to, Rdb_string_reader *const reader,
    Rdb_string_reader *const unp_reader MY_ATTRIBUTE((__unused__))) {
  const char *from;
  if (!(from = reader->read(fpi->m_max_image_len))) {
    /* Mem-comparable image doesn't have enough bytes */
    return UNPACK_FAILURE;
  }

  memcpy(to, from, fpi->m_max_image_len);
  return UNPACK_SUCCESS;
}

/*
  Function of type rdb_index_field_unpack_t.

  Pack string with strnxfrm which converts a given string to memcmp format

  See CHARSET_INFO.txt for more details.
*/
void Rdb_key_def::pack_string(
    Rdb_field_packing *const fpi, Field *const field,
    uchar *const buf MY_ATTRIBUTE((__unused__)), uchar **dst,
    Rdb_pack_field_context *const pack_ctx MY_ATTRIBUTE((__unused__))) {
  assert(fpi != nullptr);
  assert(field != nullptr);
  assert(dst != nullptr);
  assert(*dst != nullptr);

  auto field_charset = field->charset();
  auto field_length = field->field_length;
  auto char_length = field->char_length();

  /*
    We don't store explicitly how many bytes long this string is.
    Find out by calling charpos, since just using field_length
    could give strnxfrm a buffer with more than char_length() code
    points, which is not allowed.

    The min() is because charpos() is allowed to return a value past
    the end of the string for “end of string”.
  */
  size_t input_length = std::min<size_t>(
      field_length,
      field_charset->cset->charpos(
          field_charset, pointer_cast<const char *>(field->field_ptr()),
          pointer_cast<const char *>(field->field_ptr()) + field_length,
          field->char_length()));

  if (field_charset->pad_attribute == NO_PAD) {
    /*
      Our CHAR default behavior is to strip spaces upon retrieval.
      For PAD SPACE collations, we'll pad with spaces later so it doesn't
      matter anyway. But for NO PAD collations, we need to strip spaces and
      pad with correct pad char later.
    */
    input_length = field_charset->cset->lengthsp(
        field_charset, (const char *)field->field_ptr(), input_length);
  }

  size_t tmp MY_ATTRIBUTE((unused)) = field_charset->coll->strnxfrm(
      field_charset, *dst, fpi->m_max_image_len, char_length,
      field->field_ptr(), input_length, MY_STRXFRM_PAD_TO_MAXLEN);
  assert((int)tmp == fpi->m_max_image_len);

  *dst += fpi->m_max_image_len;
}

/*
  Function of type rdb_index_field_unpack_t.
  For UTF-8, we need to convert 2- or 3-byte wide-character entities back into
  UTF8 sequences.
*/

template <const int bytes>
int unpack_utf8_str_templ(Rdb_field_packing *const fpi, Field *const field,
                          uchar *dst, Rdb_string_reader *const reader,
                          Rdb_string_reader *const unp_reader
                              MY_ATTRIBUTE((__unused__))) {
  my_core::CHARSET_INFO *const cset = (my_core::CHARSET_INFO *)field->charset();
  const uchar *src;
  if (!(src = (const uchar *)reader->read(fpi->m_max_image_len))) {
    /* Mem-comparable image doesn't have enough bytes */
    return UNPACK_FAILURE;
  }

  const uchar *const src_end = src + fpi->m_max_image_len;
  uchar *const dst_end = dst + field->pack_length();

  while (src < src_end) {
    my_wc_t wc = (bytes == 3) ? (src[0] << 16) | (src[1] << 8) | src[2]
                              : (src[0] << 8) | src[1];
    src += bytes;
    int res = cset->cset->wc_mb(cset, wc, dst, dst_end);
    assert(res > 0 && res <= bytes + 1);
    if (res < 0) return UNPACK_FAILURE;
    dst += res;
  }

  cset->cset->fill(cset, reinterpret_cast<char *>(dst), dst_end - dst,
                   cset->pad_char);
  return UNPACK_SUCCESS;
}

rdb_index_field_unpack_t Rdb_key_def::unpack_utf8mb4_str =
    unpack_utf8_str_templ<3>;
rdb_index_field_unpack_t Rdb_key_def::unpack_utf8_str =
    unpack_utf8_str_templ<2>;

/*
  This is the new algorithm.  Similarly to the legacy format the input
  is split up into N-1 bytes and a flag byte is used as the Nth byte
  in the output.

  - If the previous segment needed any padding the flag is set to the
    number of bytes used (0..N-2).  0 is possible in the first segment
    if the input is 0 bytes long.
  - If no padding was used and there is no more data left in the input
    the flag is set to N-1
  - If no padding was used and there is still data left in the input the
    flag is set to N.

  For N=9, the following input values encode to the specified
  outout (where 'X' indicates a byte of the original input):
  - 0 bytes  is encoded as 0 0 0 0 0 0 0 0 0
  - 1 byte   is encoded as X 0 0 0 0 0 0 0 1
  - 2 bytes  is encoded as X X 0 0 0 0 0 0 2
  - 7 bytes  is encoded as X X X X X X X 0 7
  - 8 bytes  is encoded as X X X X X X X X 8
  - 9 bytes  is encoded as X X X X X X X X 9 X 0 0 0 0 0 0 0 1
  - 10 bytes is encoded as X X X X X X X X 9 X X 0 0 0 0 0 0 2
*/
void Rdb_key_def::pack_variable_format(
    const uchar *src,  // The data to encode
    size_t src_len,    // The length of the data to encode
    uchar **dst)       // The location to encode the data
{
  uchar *ptr = *dst;

  for (;;) {
    // Figure out how many bytes to copy, copy them and adjust pointers
    const size_t copy_len = std::min((size_t)RDB_ESCAPE_LENGTH - 1, src_len);
    memcpy(ptr, src, copy_len);
    ptr += copy_len;
    src += copy_len;
    src_len -= copy_len;

    // Are we at the end of the input?
    if (src_len == 0) {
      // pad with zeros if necessary;
      const size_t padding_bytes = RDB_ESCAPE_LENGTH - 1 - copy_len;
      if (padding_bytes > 0) {
        memset(ptr, 0, padding_bytes);
        ptr += padding_bytes;
      }

      // Put the flag byte (0 - N-1) in the output
      *(ptr++) = (uchar)copy_len;
      break;
    }

    // We have more data - put the flag byte (N) in and continue
    *(ptr++) = RDB_ESCAPE_LENGTH;
  }

  *dst = ptr;
}

/*
  Function of type rdb_index_field_pack_t
*/

void Rdb_key_def::pack_with_varlength_encoding(
    Rdb_field_packing *const fpi, Field *const field, uchar *buf, uchar **dst,
    Rdb_pack_field_context *const pack_ctx MY_ATTRIBUTE((__unused__))) {
  const CHARSET_INFO *const charset = field->charset();
  size_t value_length = field->data_length();
  const char *src = get_data_value(field);

  // Max memcmp byte length with char_length(), in case we need to truncate
  const size_t max_xfrm_len = charset->cset->charpos(
      charset, src, src + value_length,
      fpi->m_max_field_bytes / fpi->m_varlength_charset->mbmaxlen);

  // Trimmed length in code points - this is needed to avoid the padding
  // behavior in strnxfrm for padding collations otherwise strnxfrm would
  // pad to max length which defeats the trimming earlier
  const size_t trimmed_codepoints =
      charset->cset->numchars(charset, src, src + value_length);

  const size_t xfrm_len = charset->coll->strnxfrm(
      charset, buf, fpi->m_max_image_len_before_encoding,
      std::min<size_t>(
          trimmed_codepoints,
          fpi->m_max_field_bytes / fpi->m_varlength_charset->mbmaxlen),
      reinterpret_cast<const uchar *>(src),
      std::min<size_t>(value_length, max_xfrm_len), 0);

  /* Got a mem-comparable image in 'buf'. Now, produce varlength encoding */
  pack_variable_format(buf, xfrm_len, dst);
}

/*
  Compare the string suffix with a hypothetical infinite string of
  spaces. It could be that the first difference is beyond the end of
  current chunk.

  @return last byte value for the segment chunk which acts as terminator.
*/
static int rdb_get_segment_terminator(
    const uchar *buf, const uchar *const buf_end,
    const std::vector<uchar> *const space_xfrm) {
  int cmp = 0;
  while (buf < buf_end) {
    size_t bytes = std::min((size_t)(buf_end - buf), space_xfrm->size());
    if ((cmp = memcmp(buf, space_xfrm->data(), bytes)) != 0) break;
    buf += bytes;
  }

  int val;
  if (cmp < 0) {
    val = VARCHAR_CMP_LESS_THAN_SPACES;
  } else if (cmp > 0) {
    val = VARCHAR_CMP_GREATER_THAN_SPACES;
  } else {
    // It turns out all the rest are spaces.
    val = VARCHAR_CMP_EQUAL_TO_SPACES;
  }

  return val;
}

static const int RDB_TRIMMED_CHARS_OFFSET = 8;
/* clang-format off */
/*
  Pack the data with Variable-Length Space-Padded Encoding.

  The encoding is there to meet two goals:

  Goal#1. Comparison. The SQL standard says

    " If the collation for the comparison has the PAD SPACE characteristic,
    for the purposes of the comparison, the shorter value is effectively
    extended to the length of the longer by concatenation of <space>s on the
    right.

  At the moment, all MySQL collations except one have the PAD SPACE
  characteristic.  The exception is the "binary" collation that is used by
  [VAR]BINARY columns. (Note that binary collations for specific charsets,
  like utf8_bin or latin1_bin are not the same as "binary" collation, they have
  the PAD SPACE characteristic).

  Goal#2 is to preserve the number of trailing spaces in the original value.

  This is achieved by using the following encoding:
  The key part:
  - Stores mem-comparable image of the column
  - It is stored in chunks of fpi->m_segment_size bytes (*)
    = If the remainder of the chunk is not occupied, it is padded with mem-
      comparable image of the space character (cs->pad_char to be precise).
  - The last byte of the chunk shows how the rest of column's mem-comparable
    image would compare to mem-comparable image of the column extended with
    spaces. There are three possible values.
     - VARCHAR_CMP_LESS_THAN_SPACES,
     - VARCHAR_CMP_EQUAL_TO_SPACES
     - VARCHAR_CMP_GREATER_THAN_SPACES
  - First byte of the encoding is also encoded using above three values. This
    helps to optimize for empty content in which first byte will be encoded
    as VARCHAR_CMP_EQUAL_TO_SPACES.

  VARCHAR_CMP_EQUAL_TO_SPACES means that this chunk is the last one (the rest
  is spaces, or something that sorts as spaces, so there is no reason to store
  it).

  Example: if fpi->m_segment_size=5, and the collation is latin1_bin:

  'abcd\0'   => [<VARCHAR_CMP_GREATER>][ 'abcd' <VARCHAR_CMP_LESS> ]['\0    ' <VARCHAR_CMP_EQUAL> ]
  'abcd'     => [<VARCHAR_CMP_GREATER>][ 'abcd' <VARCHAR_CMP_EQUAL>]
  'abcd   '  => [<VARCHAR_CMP_GREATER>][ 'abcd' <VARCHAR_CMP_EQUAL>]
  'abcdZZZZ' => [<VARCHAR_CMP_GREATER>][ 'abcd' <VARCHAR_CMP_GREATER>][ 'ZZZZ' <VARCHAR_CMP_EQUAL>]
  ''         => [<VARCHAR_CMP_EQUAL>]

  As mentioned above, the last chunk is padded with mem-comparable images of
  cs->pad_char. It can be 1-byte long (latin1), 2 (utf8_bin), 3 (utf8mb4), etc.

  fpi->m_segment_size depends on the used collation. It is chosen to be such
  that no mem-comparable image of space will ever stretch across the segments
  (see get_segment_size_from_collation).

  == The value part (aka unpack_info) ==
  The value part stores the number of space characters that one needs to add
  when unpacking the string.
  - If the number is positive, it means add this many spaces at the end
  - If the number is negative, it means padding has added extra spaces which
    must be removed.

  Storage considerations
  - depending on column's max size, the number may occupy 1 or 2 bytes
  - the number of spaces that need to be removed is not more than
    RDB_TRIMMED_CHARS_OFFSET=8, so we offset the number by that value and
    then store it as unsigned.

  @seealso
    unpack_binary_or_utf8_varlength_space_pad
    unpack_simple_varlength_space_pad
    dummy_make_unpack_info
    skip_variable_space_pad
*/
/* clang-format on */

void Rdb_key_def::pack_with_varlength_space_pad(
    Rdb_field_packing *const fpi, Field *const field, uchar *buf, uchar **dst,
    Rdb_pack_field_context *const pack_ctx) {
  Rdb_string_writer *const unpack_info = pack_ctx->writer;
  const CHARSET_INFO *const charset = field->charset();
  size_t value_length = field->data_length();
  const char *src = get_data_value(field);

  // We only store the trimmed contents but encode the missing char with
  // removed_chars later to save space
  const size_t trimmed_len =
      charset->cset->lengthsp(charset, src, value_length);

  // Max memcmp byte length with char_length() including space characters, in
  // case we need to truncate for prefix keys
  const size_t max_allowed_len = charset->cset->charpos(
      charset, src, src + value_length,
      fpi->m_max_field_bytes / fpi->m_varlength_charset->mbmaxlen);

  // Max memcmp byte length with char_length(), in case we need to truncate
  // for prefix keys
  const size_t max_xfrm_len = charset->cset->charpos(
      charset, src, src + trimmed_len,
      fpi->m_max_field_bytes / fpi->m_varlength_charset->mbmaxlen);

  // Trimmed length in code points - this is needed to avoid the padding
  // behavior in strnxfrm for padding collations otherwise strnxfrm would
  // pad to max length which defeats the trimming earlier
  const size_t trimmed_codepoints =
      charset->cset->numchars(charset, src, src + trimmed_len);

  const size_t xfrm_len = charset->coll->strnxfrm(
      charset, buf, fpi->m_max_image_len_before_encoding,
      std::min<size_t>(
          trimmed_codepoints,
          fpi->m_max_field_bytes / fpi->m_varlength_charset->mbmaxlen),
      reinterpret_cast<const uchar *>(src),
      std::min<size_t>(trimmed_len, max_xfrm_len), 0);

  /* Got a mem-comparable image in 'buf'. Now, produce varlength encoding */
  uchar *const buf_end = buf + xfrm_len;

  size_t encoded_size = 0;
  uchar *ptr = *dst;
  size_t padding_bytes = 0;

  // In new varchar format, we encode the lead segment byte. In
  // can we reach end of buffer, we are done.
  // This can happen in case of empty data or data containing
  // only spaces.
  *ptr = rdb_get_segment_terminator(buf, buf_end, fpi->space_xfrm);
  encoded_size++;
  if (*(ptr++) == VARCHAR_CMP_EQUAL_TO_SPACES) goto unpack_info;

  while (true) {
    const size_t copy_len =
        std::min<size_t>(fpi->m_segment_size - 1, buf_end - buf);
    padding_bytes = fpi->m_segment_size - 1 - copy_len;
    memcpy(ptr, buf, copy_len);
    ptr += copy_len;
    buf += copy_len;

    if (padding_bytes) {
      memcpy(ptr, fpi->space_xfrm->data(), padding_bytes);
      ptr += padding_bytes;
      *ptr = VARCHAR_CMP_EQUAL_TO_SPACES;  // last segment
    } else {
      // Compare the string suffix with a hypothetical infinite string of
      // spaces. It could be that the first difference is beyond the end of
      // current chunk.
      *ptr = rdb_get_segment_terminator(buf, buf_end, fpi->space_xfrm);
    }
    encoded_size += fpi->m_segment_size;

    if (*(ptr++) == VARCHAR_CMP_EQUAL_TO_SPACES) break;
  }

unpack_info:
  /*
   There is no need to store the number of trimmed/padded endspaces in below
   cases : 1) m_unpack_info_stores_value means unpack_info stores the whole
   original value 2) max_allowed_len < value_length i.e prefix key is not
   covered and this is only used if m_use_covered_bitmap_format.
  */
  if (unpack_info && !fpi->m_unpack_info_stores_value &&
      (max_allowed_len >= value_length || !fpi->m_is_secondary_key)) {
    // (value_length - trimmed_len) is the number of trimmed space *characters*
    // then, padding_bytes is the number of *bytes* added as padding
    // then, we add 8, because we don't store negative values.
    assert(padding_bytes % fpi->space_xfrm_len == 0);
    assert((value_length - trimmed_len) % fpi->space_mb_len == 0);
    const size_t removed_chars =
        RDB_TRIMMED_CHARS_OFFSET +
        (value_length - trimmed_len) / fpi->space_mb_len -
        padding_bytes / fpi->space_xfrm_len;

    if (fpi->m_unpack_info_uses_two_bytes) {
      unpack_info->write_uint16(removed_chars);
    } else {
      assert(removed_chars < 0x100);
      unpack_info->write_uint8(removed_chars);
    }
  }

  *dst += encoded_size;
}

/*
  Calculate the number of used bytes in the chunk and whether this is the
  last chunk in the input.  This is based on the new format - see
  pack_variable_format.
 */
uint Rdb_key_def::calc_unpack_variable_format(uchar flag, bool *done) {
  // Check for invalid flag values
  if (flag > RDB_ESCAPE_LENGTH) {
    return (uint)-1;
  }

  // Values from 1 to N-1 indicate this is the last chunk and that is how
  // many bytes were used
  if (flag < RDB_ESCAPE_LENGTH) {
    *done = true;
    return flag;
  }

  // A value of N means we used N-1 bytes and had more to go
  *done = false;
  return RDB_ESCAPE_LENGTH - 1;
}

void Rdb_key_def::store_field(const uchar *data, const size_t length,
                              Field *field) {
  if (field->real_type() == MYSQL_TYPE_VARCHAR) {
    auto field_var = (Field_varstring *)field;
    auto length_bytes = field_var->get_length_bytes();
    if (length_bytes == 1) {
      field_var->field_ptr()[0] = length;
    } else {
      assert(length_bytes == 2);
      int2store(field_var->field_ptr(), length);
    }
    // data is not used for varchar as field->field_ptr() + length_bytes
    // already contains required data.
  } else if (is_blob(field->real_type())) {
    auto field_blob = (Field_blob *)field;
    auto length_bytes = field_blob->pack_length_no_ptr();
    field_blob->store_length(length);
    auto blob_data = (char *)(data);
    memset(field_blob->field_ptr() + length_bytes, 0, 8);
    memcpy(field_blob->field_ptr() + length_bytes, &blob_data,
           sizeof(uchar **));
  } else {
    assert(false);
  }
}

const char *Rdb_key_def::get_data_value(const Field *field) {
  if (field->real_type() == MYSQL_TYPE_VARCHAR) {
    const auto field_var = static_cast<const Field_varstring *>(field);
    auto length_bytes = field_var->get_length_bytes();
    return reinterpret_cast<const char *>(field_var->field_ptr() +
                                          length_bytes);
  } else if (is_blob(field->real_type())) {
    const auto field_blob = static_cast<const Field_blob *>(field);
    auto length_bytes = field_blob->pack_length_no_ptr();
    uchar *blob;
    memcpy(&blob, field_blob->field_ptr() + length_bytes, sizeof(char *));
    return reinterpret_cast<const char *>(blob);
  } else {
    assert(false);
    return nullptr;
  }
}

uchar *Rdb_key_def::get_data_start_ptr(const Field *field,
                                       const size_t max_field_length) {
  uchar *data_start = nullptr;
  if (field->real_type() == MYSQL_TYPE_VARCHAR) {
    const auto field_var = static_cast<const Field_varstring *>(field);
    data_start = const_cast<uchar *>(field_var->field_ptr()) +
                 field_var->get_length_bytes();
  } else if (is_blob(field->real_type())) {
    auto handler = (ha_rocksdb *)field->table->file;
    data_start = handler->get_blob_buffer(max_field_length);
  } else {
    assert(false);
  }
  return data_start;
}

uint16 Rdb_key_def::get_length_bytes(const Field *field) {
  uint16 length_bytes = 0;
  if (field->real_type() == MYSQL_TYPE_VARCHAR) {
    const auto field_var = static_cast<const Field_varstring *>(field);
    length_bytes = field_var->get_length_bytes();
  } else if (is_blob(field->real_type())) {
    const auto field_blob = static_cast<const Field_blob *>(field);
    length_bytes = field_blob->pack_length_no_ptr();
  } else {
    assert(false);
  }
  return length_bytes;
}

bool Rdb_key_def::is_varlength_prefix_covering(
    const Field *field, const Rdb_field_packing *const fpi) {
  /* Similar implementation from ha_protoypes.h
  innobase_get_at_most_n_mbchars */
  auto charset = field->charset();
  size_t data_len = field->data_length();
  auto src = get_data_value(field);
  auto n_chars = fpi->m_max_field_bytes / charset->mbmaxlen;

  /* If the charset is multi-byte, then we must find the length of the
  first at most n_chars in the string. If the string contains no more than
  n characters, then we return true */
  if (charset->mbmaxlen > 1) {
    /* my_charpos() returns the byte length of the first n_chars
    characters, or a value bigger than the length of str, if
    there were not enough full characters in str.

    Why does the code below work:
    Suppose that we are looking for n UTF-8 characters.

    1) If the string is long enough, then the prefix contains at
    least n complete UTF-8 characters + maybe some extra
    characters + an incomplete UTF-8 character. No problem in
    this case. The function returns the pointer to the
    end of the nth character.

    2) If the string is not long enough, then the string contains
    the complete value of a column, that is, only complete UTF-8
    characters, and we can store in the column prefix index the
    whole string. */

    auto char_length = my_charpos(charset, src, src + data_len, (int)n_chars);
    if (char_length < data_len) {
      return false;
    }
  } else if (data_len > fpi->m_max_field_bytes) {
    return false;
  }
  return true;
}

bool Rdb_key_def::is_variable_length_field(const enum_field_types type) {
  return type == MYSQL_TYPE_VARCHAR || is_blob(type);
}

template <const int bytes>
bool check_src_len(uint src_len);

template <>
bool check_src_len<2>(uint src_len) {
  if (src_len & 1) {
    /*
      utf8mb3 characters are encoded into two-byte entities. There is no way
      we can have an odd number of bytes after encoding.
    */
    return false;
  }
  return true;
}

template <>
bool check_src_len<3>(uint src_len) {
  if (src_len % 3) {
    /*
      utf8mb4 characters are encoded into three-byte entities. There is no way
      we can have 1 or 2 bytes after encoding.
    */
    return false;
  }
  return true;
}

/*
  Function of type rdb_index_field_unpack_t
*/
int Rdb_key_def::unpack_binary_varlength(
    Rdb_field_packing *const fpi, Field *const field,
    uchar *dst MY_ATTRIBUTE((__unused__)), Rdb_string_reader *const reader,
    Rdb_string_reader *const unp_reader MY_ATTRIBUTE((__unused__))) {
  assert(field->field_ptr() == dst);
  assert(fpi->m_varlength_charset == &my_charset_bin);
  const uchar *ptr;
  size_t len = 0;
  bool finished = false;
  uchar *data_start = get_data_start_ptr(field, fpi->m_max_field_bytes);
  uchar *data = data_start;
  // How much we can unpack
  size_t data_len = fpi->m_max_field_bytes;

  /* Decode the length-emitted encoding here */
  while ((ptr = (const uchar *)reader->read(RDB_ESCAPE_LENGTH))) {
    uint used_bytes;

    used_bytes =
        calc_unpack_variable_format(ptr[RDB_ESCAPE_LENGTH - 1], &finished);

    if (used_bytes == (uint)-1 || data_len < used_bytes) {
      return UNPACK_FAILURE;  // Corruption in the data
    }

    /*
      Now, we need to decode used_bytes of data and append them to the value.
    */
    memcpy(data, ptr, used_bytes);

    data += used_bytes;
    data_len -= used_bytes;
    len += used_bytes;

    if (finished) {
      break;
    }
  }

  if (!finished) {
    return UNPACK_FAILURE;
  }
  store_field(data_start, len, field);
  return UNPACK_SUCCESS;
}

/*
  @seealso
    pack_with_varlength_space_pad - packing function
    unpack_simple_varlength_space_pad - unpacking function for 'simple'
    charsets.
    skip_variable_space_pad - skip function
*/

template <const int bytes>
int Rdb_key_def::unpack_binary_or_utf8_varlength_space_pad(
    Rdb_field_packing *const fpi, Field *const field,
    uchar *dst MY_ATTRIBUTE((__unused__)), Rdb_string_reader *const reader,
    Rdb_string_reader *const unp_reader) {
  assert(field->field_ptr() == dst);
  const uchar *ptr;
  size_t len = 0;
  bool finished = false;
  uchar *data_start = get_data_start_ptr(field, fpi->m_max_field_bytes);
  uchar *data = data_start;
  uchar *data_end = data + fpi->m_max_field_bytes;

  uint space_padding_bytes = 0;
  uint extra_spaces;
  if ((fpi->m_unpack_info_uses_two_bytes
           ? unp_reader->read_uint16(&extra_spaces)
           : unp_reader->read_uint8(&extra_spaces))) {
    return UNPACK_FAILURE;
  }

  if (extra_spaces <= RDB_TRIMMED_CHARS_OFFSET) {
    space_padding_bytes =
        -(static_cast<int>(extra_spaces) - RDB_TRIMMED_CHARS_OFFSET);
    extra_spaces = 0;
  } else {
    extra_spaces -= RDB_TRIMMED_CHARS_OFFSET;
  }

  space_padding_bytes *= fpi->space_xfrm_len;

  // Check if lead segment byte is VARCHAR_CMP_EQUAL_TO_SPACES.
  // This indicates empty content or just spaces. We can bypass
  // the main loop and check for spaces to be appended.
  uchar encoded_byte = *(const uchar *)reader->read(1);
  if (encoded_byte == VARCHAR_CMP_EQUAL_TO_SPACES) goto finished;

  /* Decode the length-emitted encoding here */
  while ((ptr = (const uchar *)reader->read(fpi->m_segment_size))) {
    const char last_byte = ptr[fpi->m_segment_size - 1];
    size_t used_bytes;
    if (last_byte == VARCHAR_CMP_EQUAL_TO_SPACES)  // this is the last segment
    {
      if (space_padding_bytes > (fpi->m_segment_size - 1)) {
        return UNPACK_FAILURE;  // Cannot happen, corrupted data
      }
      used_bytes = (fpi->m_segment_size - 1) - space_padding_bytes;
      finished = true;
    } else {
      if (last_byte != VARCHAR_CMP_LESS_THAN_SPACES &&
          last_byte != VARCHAR_CMP_GREATER_THAN_SPACES) {
        return UNPACK_FAILURE;  // Invalid value
      }
      used_bytes = fpi->m_segment_size - 1;
    }

    // Now, need to decode used_bytes of data and append them to the value.
    if (bytes > 1) {
      if (!check_src_len<bytes>(used_bytes)) return UNPACK_FAILURE;

      const uchar *src = ptr;
      const uchar *const src_end = ptr + used_bytes;
      while (src < src_end) {
        my_wc_t wc = (bytes == 3) ? (src[0] << 16) | (src[1] << 8) | src[2]
                                  : (src[0] << 8) | src[1];
        src += bytes;
        const CHARSET_INFO *cset = fpi->m_varlength_charset;
        int res = cset->cset->wc_mb(cset, wc, data, data_end);
        assert(res <= bytes + 1);
        if (res <= 0) return UNPACK_FAILURE;
        data += res;
        len += res;
      }
    } else {
      if (data + used_bytes > data_end) return UNPACK_FAILURE;
      memcpy(data, ptr, used_bytes);
      data += used_bytes;
      len += used_bytes;
    }

    if (finished) break;
  }

  if (!finished) return UNPACK_FAILURE;

finished:

  if (extra_spaces) {
    // Both binary and UTF-8 charset store space as ' ',
    // so the following is ok:
    if (data + extra_spaces > data_end) return UNPACK_FAILURE;
    memset(data, fpi->m_varlength_charset->pad_char, extra_spaces);
    len += extra_spaces;
  }
  store_field(data_start, len, field);
  return UNPACK_SUCCESS;
}

rdb_index_field_unpack_t Rdb_key_def::unpack_binary_varlength_space_pad =
    unpack_binary_or_utf8_varlength_space_pad<1>;
rdb_index_field_unpack_t Rdb_key_def::unpack_utf8_varlength_space_pad =
    unpack_binary_or_utf8_varlength_space_pad<2>;
rdb_index_field_unpack_t Rdb_key_def::unpack_utf8mb4_varlength_space_pad =
    unpack_binary_or_utf8_varlength_space_pad<3>;

/////////////////////////////////////////////////////////////////////////

/*
  Function of type rdb_make_unpack_info_t
*/

void Rdb_key_def::make_unpack_unknown(
    const Rdb_field_packing *fpi MY_ATTRIBUTE((__unused__)),
    const Field *const field, Rdb_pack_field_context *const pack_ctx) {
  pack_ctx->writer->write(field->field_ptr(), field->pack_length());
}

/*
  This point of this function is only to indicate that unpack_info is
  available.

  The actual unpack_info data is produced by the function that packs the key,
  that is, pack_with_varlength_space_pad.
*/

void Rdb_key_def::dummy_make_unpack_info(
    const Rdb_field_packing *fpi MY_ATTRIBUTE((__unused__)),
    const Field *field MY_ATTRIBUTE((__unused__)),
    Rdb_pack_field_context *pack_ctx MY_ATTRIBUTE((__unused__))) {
  // Do nothing
}

/*
  Function of type rdb_index_field_unpack_t
*/

int Rdb_key_def::unpack_unknown(Rdb_field_packing *const fpi,
                                Field *const field, uchar *const dst,
                                Rdb_string_reader *const reader,
                                Rdb_string_reader *const unp_reader) {
  const uchar *ptr;
  const uint len = fpi->m_unpack_data_len;
  // We don't use anything from the key, so skip over it.
  if (skip_max_length(fpi, field, reader)) {
    return UNPACK_FAILURE;
  }

  assert_IMP(len > 0, unp_reader != nullptr);

  if ((ptr = (const uchar *)unp_reader->read(len))) {
    memcpy(dst, ptr, len);
    return UNPACK_SUCCESS;
  }
  return UNPACK_FAILURE;
}

/*
  Function of type rdb_make_unpack_info_t
*/

void Rdb_key_def::make_unpack_unknown_varlength(
    const Rdb_field_packing *const fpi MY_ATTRIBUTE((__unused__)),
    const Field *const field, Rdb_pack_field_context *const pack_ctx) {
  uint len = field->data_length();
  uint length_bytes = get_length_bytes(field);
  pack_ctx->writer->write(field->field_ptr(), length_bytes);
  if (field->real_type() == MYSQL_TYPE_VARCHAR) {
    pack_ctx->writer->write(field->field_ptr() + length_bytes, len);
  } else {
    uchar *blob;
    memcpy(&blob, field->field_ptr() + length_bytes, sizeof(char *));
    pack_ctx->writer->write(blob, len);
  }
}

/*
  Function of type rdb_index_field_unpack_t

  @detail
  Unpack a key part in an "unknown" collation from its
  (mem_comparable_form, unpack_info) form.

  "Unknown" means we have no clue about how mem_comparable_form is made from
  the original string, so we keep the whole original string in the unpack_info.

  @seealso
    make_unpack_unknown, unpack_unknown
*/

int Rdb_key_def::unpack_unknown_varlength(Rdb_field_packing *const fpi,
                                          Field *const field,
                                          uchar *dst MY_ATTRIBUTE((__unused__)),
                                          Rdb_string_reader *const reader,
                                          Rdb_string_reader *const unp_reader) {
  assert(field->field_ptr() == dst);
  const uchar *ptr;

  uchar *data_start = get_data_start_ptr(field, fpi->m_max_field_bytes);
  uchar *data = data_start;
  const uint len_bytes = get_length_bytes(field);
  // We don't use anything from the key, so skip over it.
  if ((fpi->m_skip_func)(fpi, field, reader)) {
    return UNPACK_FAILURE;
  }

  assert(len_bytes > 0);
  assert(unp_reader != nullptr);

  if ((ptr = (uchar *)unp_reader->read(len_bytes))) {
    uint len = 0;
    if (field->real_type() == MYSQL_TYPE_VARCHAR) {
      len = len_bytes == 1 ? (uint)*ptr : uint2korr(ptr);
    } else {
      const auto field_blob = static_cast<const Field_blob *>(field);
      len = field_blob->get_length(ptr);
    }
    if ((ptr = (const uchar *)unp_reader->read(len))) {
      memcpy(data, ptr, len);
      store_field(data_start, len, field);
      return UNPACK_SUCCESS;
    }
  }
  return UNPACK_FAILURE;
}

/*
  Write unpack_data for a "simple" collation
*/
static void rdb_write_unpack_simple(Rdb_bit_writer *const writer,
                                    const Rdb_collation_codec *const codec,
                                    const uchar *const src,
                                    const size_t src_len) {
  for (uint i = 0; i < src_len; i++) {
    writer->write(codec->m_enc_size[src[i]], codec->m_enc_idx[src[i]]);
  }
}

static uint rdb_read_unpack_simple(Rdb_bit_reader *const reader,
                                   const Rdb_collation_codec *const codec,
                                   const uchar *const src, const size_t src_len,
                                   uchar *const dst) {
  for (uint i = 0; i < src_len; i++) {
    if (codec->m_dec_size[src[i]] > 0) {
      uint *ret;
      assert(reader != nullptr);

      if ((ret = reader->read(codec->m_dec_size[src[i]])) == nullptr) {
        return UNPACK_FAILURE;
      }
      dst[i] = codec->m_dec_idx[*ret][src[i]];
    } else {
      dst[i] = codec->m_dec_idx[0][src[i]];
    }
  }

  return UNPACK_SUCCESS;
}

/*
  Function of type rdb_make_unpack_info_t

  @detail
    Make unpack_data for VARCHAR(n) in a "simple" charset.
*/

void Rdb_key_def::make_unpack_simple_varlength(
    const Rdb_field_packing *const fpi, const Field *const field,
    Rdb_pack_field_context *const pack_ctx) {
  size_t value_length = field->data_length();
  const uchar *src = reinterpret_cast<const uchar *>(get_data_value(field));
  Rdb_bit_writer bit_writer(pack_ctx->writer);
  // The std::min compares characters with bytes, but for simple collations,
  // mbmaxlen = 1.
  rdb_write_unpack_simple(
      &bit_writer, fpi->m_charset_codec, src,
      std::min(fpi->m_max_field_bytes / fpi->m_varlength_charset->mbmaxlen,
               value_length));
}

/*
  Function of type rdb_index_field_unpack_t

  @seealso
    pack_with_varlength_space_pad - packing function
    unpack_binary_or_utf8_varlength_space_pad - a similar unpacking function
*/

int Rdb_key_def::unpack_simple_varlength_space_pad(
    Rdb_field_packing *const fpi, Field *const field,
    uchar *dst MY_ATTRIBUTE((__unused__)), Rdb_string_reader *const reader,
    Rdb_string_reader *const unp_reader) {
  assert(field->field_ptr() == dst);
  const uchar *ptr;
  size_t len = 0;
  bool finished = false;
  uchar *data_start = get_data_start_ptr(field, fpi->m_max_field_bytes);
  uchar *data = data_start;

  // For simple collations, char_length is also number of bytes.
  assert((size_t)fpi->m_max_image_len >=
              (fpi->m_max_field_bytes / fpi->m_varlength_charset->mbmaxlen));
  uchar *data_end = data + fpi->m_max_field_bytes;
  Rdb_bit_reader bit_reader(unp_reader);

  uint space_padding_bytes = 0;
  uint extra_spaces;
  assert(unp_reader != nullptr);

  if ((fpi->m_unpack_info_uses_two_bytes
           ? unp_reader->read_uint16(&extra_spaces)
           : unp_reader->read_uint8(&extra_spaces))) {
    return UNPACK_FAILURE;
  }

  if (extra_spaces <= 8) {
    space_padding_bytes = -(static_cast<int>(extra_spaces) - 8);
    extra_spaces = 0;
  } else {
    extra_spaces -= 8;
  }

  space_padding_bytes *= fpi->space_xfrm_len;

  // Check if lead segment byte is VARCHAR_CMP_EQUAL_TO_SPACES.
  // This indicates empty content or just spaces. We can bypass
  // the main loop and check for spaces to be appended.
  uchar encoded_byte = *(const uchar *)reader->read(1);
  if (encoded_byte == VARCHAR_CMP_EQUAL_TO_SPACES) goto finished;

  /* Decode the length-emitted encoding here */
  while ((ptr = (const uchar *)reader->read(fpi->m_segment_size))) {
    const char last_byte =
        ptr[fpi->m_segment_size - 1];  // number of padding bytes
    size_t used_bytes;
    if (last_byte == VARCHAR_CMP_EQUAL_TO_SPACES) {
      // this is the last one
      if (space_padding_bytes > (fpi->m_segment_size - 1)) {
        return UNPACK_FAILURE;  // Cannot happen, corrupted data
      }
      used_bytes = (fpi->m_segment_size - 1) - space_padding_bytes;
      finished = true;
    } else {
      if (last_byte != VARCHAR_CMP_LESS_THAN_SPACES &&
          last_byte != VARCHAR_CMP_GREATER_THAN_SPACES) {
        return UNPACK_FAILURE;
      }
      used_bytes = fpi->m_segment_size - 1;
    }

    if (data + used_bytes > data_end) {
      // The value on disk is longer than the field definition allows?
      return UNPACK_FAILURE;
    }

    uint ret;
    if ((ret = rdb_read_unpack_simple(&bit_reader, fpi->m_charset_codec, ptr,
                                      used_bytes, data)) != UNPACK_SUCCESS) {
      return ret;
    }

    data += used_bytes;
    len += used_bytes;

    if (finished) break;
  }

  if (!finished) return UNPACK_FAILURE;

finished:

  if (extra_spaces) {
    if (data + extra_spaces > data_end) return UNPACK_FAILURE;
    // pad_char has a 1-byte form in all charsets that
    // are handled by rdb_init_collation_mapping.
    memset(data, field->charset()->pad_char, extra_spaces);
    len += extra_spaces;
  }
  store_field(data_start, len, field);
  return UNPACK_SUCCESS;
}

/*
  Function of type rdb_make_unpack_info_t

  @detail
    Make unpack_data for CHAR(n) value in a "simple" charset.
    It is CHAR(N), so SQL layer has padded the value with spaces up to N chars.

  @seealso
    The VARCHAR variant is in make_unpack_simple_varlength
*/

void Rdb_key_def::make_unpack_simple(const Rdb_field_packing *const fpi,
                                     const Field *const field,
                                     Rdb_pack_field_context *const pack_ctx) {
  const uchar *const src = field->field_ptr();
  Rdb_bit_writer bit_writer(pack_ctx->writer);
  rdb_write_unpack_simple(&bit_writer, fpi->m_charset_codec, src,
                          field->pack_length());
}

/*
  Function of type rdb_index_field_unpack_t
*/

int Rdb_key_def::unpack_simple(Rdb_field_packing *const fpi,
                               Field *const field MY_ATTRIBUTE((__unused__)),
                               uchar *const dst,
                               Rdb_string_reader *const reader,
                               Rdb_string_reader *const unp_reader) {
  const uchar *ptr;
  const uint len = fpi->m_max_image_len;
  Rdb_bit_reader bit_reader(unp_reader);

  if (!(ptr = (const uchar *)reader->read(len))) {
    return UNPACK_FAILURE;
  }

  return rdb_read_unpack_simple(unp_reader ? &bit_reader : nullptr,
                                fpi->m_charset_codec, ptr, len, dst);
}

Rdb_field_packing *Rdb_key_def::get_pack_info(uint pack_no) {
  return &m_pack_info[pack_no];
}

// See Rdb_charset_space_info::spaces_xfrm
const int RDB_SPACE_XFRM_SIZE = 32;

namespace {

// A class holding information about how space character is represented in a
// charset.
class Rdb_charset_space_info {
 public:
  Rdb_charset_space_info(const Rdb_charset_space_info &) = delete;
  Rdb_charset_space_info &operator=(const Rdb_charset_space_info &) = delete;
  Rdb_charset_space_info() = default;

  // A few strxfrm'ed space characters, at least RDB_SPACE_XFRM_SIZE bytes
  std::vector<uchar> spaces_xfrm;

  // length(strxfrm(' '))
  size_t space_xfrm_len;

  // length of the space character itself
  // Typically space is just 0x20 (length=1) but in ucs2 it is 0x00 0x20
  // (length=2)
  size_t space_mb_len;
};

}  // namespace

static std::array<std::unique_ptr<Rdb_charset_space_info>, MY_ALL_CHARSETS_SIZE>
    rdb_mem_comparable_space;

/*
  @brief
  For a given charset, get
   - strxfrm('    '), a sample that is at least RDB_SPACE_XFRM_SIZE bytes long.
   - length of strxfrm(charset, ' ')
   - length of the space character in the charset

  @param cs  IN    Charset to get the space for
  @param ptr OUT   A few space characters
  @param len OUT   Return length of the space (in bytes)

  @detail
    It is tempting to pre-generate mem-comparable form of space character for
    every charset on server startup.
    One can't do that: some charsets are not initialized until somebody
    attempts to use them (e.g. create or open a table that has a field that
    uses the charset).
*/

static void rdb_get_mem_comparable_space(const CHARSET_INFO *const cs,
                                         const std::vector<uchar> **xfrm,
                                         size_t *const xfrm_len,
                                         size_t *const mb_len) {
  assert(cs->number < MY_ALL_CHARSETS_SIZE);
  if (!rdb_mem_comparable_space[cs->number].get()) {
    RDB_MUTEX_LOCK_CHECK(rdb_mem_cmp_space_mutex);
    if (!rdb_mem_comparable_space[cs->number].get()) {
      // Upper bound of how many bytes can be occupied by multi-byte form of a
      // character in any charset.
      const int MAX_MULTI_BYTE_CHAR_SIZE = 4;
      assert(cs->mbmaxlen <= MAX_MULTI_BYTE_CHAR_SIZE);

      // multi-byte form of the ' ' (space) character
      uchar space_mb[MAX_MULTI_BYTE_CHAR_SIZE];

      const size_t space_mb_len = cs->cset->wc_mb(
          cs, (my_wc_t)cs->pad_char, space_mb, space_mb + sizeof(space_mb));

      // mem-comparable image of the space character
      std::array<uchar, 20> space;

      const size_t space_len = cs->coll->strnxfrm(
          cs, space.data(), sizeof(space), 1, space_mb, space_mb_len, 0);
      Rdb_charset_space_info *const info = new Rdb_charset_space_info;
      info->space_xfrm_len = space_len;
      info->space_mb_len = space_mb_len;
      while (info->spaces_xfrm.size() < RDB_SPACE_XFRM_SIZE) {
        info->spaces_xfrm.insert(info->spaces_xfrm.end(), space.data(),
                                 space.data() + space_len);
      }
      rdb_mem_comparable_space[cs->number].reset(info);
    }
    RDB_MUTEX_UNLOCK_CHECK(rdb_mem_cmp_space_mutex);
  }

  *xfrm = &rdb_mem_comparable_space[cs->number]->spaces_xfrm;
  *xfrm_len = rdb_mem_comparable_space[cs->number]->space_xfrm_len;
  *mb_len = rdb_mem_comparable_space[cs->number]->space_mb_len;
}

Rds_mysql_mutex rdb_mem_cmp_space_mutex;
Rds_mysql_mutex rdb_collation_data_mutex;
std::array<const Rdb_collation_codec *, MY_ALL_CHARSETS_SIZE>
    rdb_collation_data;

static bool rdb_is_simple_collation(const my_core::CHARSET_INFO *const cs) {
  return (cs->coll == &my_collation_8bit_simple_ci_handler);
}

static bool rdb_is_binary_collation(const my_core::CHARSET_INFO *const cs) {
  return (cs->coll == &my_collation_8bit_bin_handler) ||
         (cs == &my_charset_utf8mb4_bin) || (cs == &my_charset_utf8mb3_bin);
}

static const Rdb_collation_codec *rdb_init_collation_mapping(
    const my_core::CHARSET_INFO *const cs) {
  assert(cs && cs->state & MY_CS_AVAILABLE);
  const Rdb_collation_codec *codec = rdb_collation_data[cs->number];

  if (codec == nullptr && rdb_is_simple_collation(cs)) {
    RDB_MUTEX_LOCK_CHECK(rdb_collation_data_mutex);

    codec = rdb_collation_data[cs->number];
    if (codec == nullptr) {
      Rdb_collation_codec *cur = nullptr;

      // Compute reverse mapping for simple collations.
      cur = new Rdb_collation_codec;
      std::map<uchar, std::vector<uchar>> rev_map;
      size_t max_conflict_size = 0;
      for (int src = 0; src < 256; src++) {
        uchar dst = cs->sort_order[src];
        rev_map[dst].push_back(src);
        max_conflict_size = std::max(max_conflict_size, rev_map[dst].size());
      }
      cur->m_dec_idx.resize(max_conflict_size);

      for (auto const &p : rev_map) {
        uchar dst = p.first;
        for (uint idx = 0; idx < p.second.size(); idx++) {
          uchar src = p.second[idx];
          uchar bits = my_bit_log2(my_round_up_to_next_power(p.second.size()));
          cur->m_enc_idx[src] = idx;
          cur->m_enc_size[src] = bits;
          cur->m_dec_size[dst] = bits;
          cur->m_dec_idx[idx][dst] = src;
        }
      }

      cur->m_make_unpack_info_func = {Rdb_key_def::make_unpack_simple_varlength,
                                      Rdb_key_def::make_unpack_simple};
      cur->m_unpack_func = {Rdb_key_def::unpack_simple_varlength_space_pad,
                            Rdb_key_def::unpack_simple};

      if (cur != nullptr) {
        codec = cur;
        cur->m_cs = cs;
        rdb_collation_data[cs->number] = cur;
      }
    }

    RDB_MUTEX_UNLOCK_CHECK(rdb_collation_data_mutex);
  }

  return codec;
}

static int get_segment_size_from_collation(const CHARSET_INFO *const cs) {
  int ret;
  if (cs == &my_charset_utf8mb4_bin || cs == &my_charset_utf16_bin ||
      cs == &my_charset_utf16le_bin || cs == &my_charset_utf32_bin) {
    /*
      In these collations, a character produces one weight, which is 3 bytes.
      Segment has 3 characters, add one byte for VARCHAR_CMP_* marker, and we
      get 3*3+1=10
    */
    ret = 10;
  } else {
    /*
      All other collations. There are two classes:
      - Unicode-based, except for collations mentioned in the if-condition.
        For these all weights are 2 bytes long, a character may produce 0..8
        weights.
        in any case, 8 bytes of payload in the segment guarantee that the last
        space character won't span across segments.

      - Collations not based on unicode. These have length(strxfrm(' '))=1,
        there nothing to worry about.

      In both cases, take 8 bytes payload + 1 byte for VARCHAR_CMP* marker.
    */
    ret = 9;
  }
  assert(ret < RDB_SPACE_XFRM_SIZE);
  return ret;
}

/*
  @brief
    Setup packing of index field into its mem-comparable form

  @detail
    - It is possible produce mem-comparable form for any datatype.
    - Some datatypes also allow to unpack the original value from its
      mem-comparable form.
      = Some of these require extra information to be stored in "unpack_info".
        unpack_info is not a part of mem-comparable form, it is only used to
        restore the original value

  @param
    field  IN  field to be packed/un-packed

  @return
    true  -  Field can be read with index-only reads
    false -  Otherwise
*/

bool Rdb_field_packing::setup(const Rdb_key_def *const key_descr,
                              const Field *const field, const uint keynr_arg,
                              const uint key_part_arg,
                              const uint16 key_length) {
  assert(key_descr != nullptr);

  enum_field_types type = field ? field->real_type() : MYSQL_TYPE_LONGLONG;

  m_keynr = keynr_arg;
  m_key_part = key_part_arg;

  m_maybe_null = field ? field->is_nullable() : false;
  m_unpack_func = nullptr;
  m_make_unpack_info_func = nullptr;
  m_unpack_data_len = 0;
  space_xfrm = nullptr;  // safety

  m_is_secondary_key =
      key_descr->m_index_type == Rdb_key_def::INDEX_TYPE_SECONDARY;

  /* Calculate image length. By default, is is pack_length() */
  m_max_image_len =
      field ? field->pack_length() : ROCKSDB_SIZEOF_HIDDEN_PK_COLUMN;
  m_max_image_len_before_encoding = 0;
  m_max_field_bytes = key_length;

  m_skip_func = Rdb_key_def::skip_max_length;
  m_pack_func = nullptr;

  m_covered = Rdb_key_def::KEY_NOT_COVERED;

  switch (type) {
    case MYSQL_TYPE_LONGLONG:
      m_pack_func = Rdb_key_def::pack_integer<8>;
      m_unpack_func = Rdb_key_def::unpack_integer<8>;
      m_covered = Rdb_key_def::KEY_COVERED;
      return true;

    case MYSQL_TYPE_LONG:
      m_pack_func = Rdb_key_def::pack_integer<4>;
      m_unpack_func = Rdb_key_def::unpack_integer<4>;
      m_covered = Rdb_key_def::KEY_COVERED;
      return true;

    case MYSQL_TYPE_INT24:
      m_pack_func = Rdb_key_def::pack_integer<3>;
      m_unpack_func = Rdb_key_def::unpack_integer<3>;
      m_covered = Rdb_key_def::KEY_COVERED;
      return true;

    case MYSQL_TYPE_SHORT:
      m_pack_func = Rdb_key_def::pack_integer<2>;
      m_unpack_func = Rdb_key_def::unpack_integer<2>;
      m_covered = Rdb_key_def::KEY_COVERED;
      return true;

    case MYSQL_TYPE_TINY:
      m_pack_func = Rdb_key_def::pack_integer<1>;
      m_unpack_func = Rdb_key_def::unpack_integer<1>;
      m_covered = Rdb_key_def::KEY_COVERED;
      return true;

    case MYSQL_TYPE_DOUBLE:
      m_pack_func = Rdb_key_def::pack_double;
      m_unpack_func = Rdb_key_def::unpack_double;
      m_covered = Rdb_key_def::KEY_COVERED;
      return true;

    case MYSQL_TYPE_FLOAT:
      m_pack_func = Rdb_key_def::pack_float;
      m_unpack_func = Rdb_key_def::unpack_float;
      m_covered = Rdb_key_def::KEY_COVERED;
      return true;

    case MYSQL_TYPE_NEWDECIMAL:
    /*
      Unpacking decimal values was supported only after fix for issue#253,
      because of that ha_rocksdb::get_storage_type() handles decimal values
      in a special way.
    */
    case MYSQL_TYPE_DATETIME2:
    case MYSQL_TYPE_TIMESTAMP2:
    case MYSQL_TYPE_TIME2:
    case MYSQL_TYPE_YEAR:
      /* Everything that comes here is packed with just a memcpy(). */
      m_pack_func = Rdb_key_def::pack_binary_str;
      m_unpack_func = Rdb_key_def::unpack_binary_str;
      m_covered = Rdb_key_def::KEY_COVERED;
      return true;

    case MYSQL_TYPE_NEWDATE:
      /*
        It assumes the data is 3 bytes, and packing is done by swapping
        the byte order (for both big- and little-endian)
      */
      m_pack_func = Rdb_key_def::pack_newdate;
      m_unpack_func = Rdb_key_def::unpack_newdate;
      m_covered = Rdb_key_def::KEY_COVERED;
      return true;

    case MYSQL_TYPE_TINY_BLOB:
    case MYSQL_TYPE_MEDIUM_BLOB:
    case MYSQL_TYPE_LONG_BLOB:
    case MYSQL_TYPE_BLOB: {
      m_pack_func = Rdb_key_def::pack_with_varlength_encoding;
      break;  // handling below
    }
    case MYSQL_TYPE_SET:
    case MYSQL_TYPE_ENUM: {
      const auto field_enum = static_cast<const Field_enum *>(field);
      switch (field_enum->pack_length()) {
        case 1:
          m_pack_func = Rdb_key_def::pack_unsigned<1>;
          m_unpack_func = Rdb_key_def::unpack_unsigned<1>;
          break;

        case 2:
          m_pack_func = Rdb_key_def::pack_unsigned<2>;
          m_unpack_func = Rdb_key_def::unpack_unsigned<2>;
          break;

        case 3:
          m_pack_func = Rdb_key_def::pack_unsigned<3>;
          m_unpack_func = Rdb_key_def::unpack_unsigned<3>;
          break;

        case 4:
          m_pack_func = Rdb_key_def::pack_unsigned<4>;
          m_unpack_func = Rdb_key_def::unpack_unsigned<4>;
          break;

        case 8:
          m_pack_func = Rdb_key_def::pack_unsigned<8>;
          m_unpack_func = Rdb_key_def::unpack_unsigned<8>;
          break;

        default:
          assert(false);
          break;
      }
      m_covered = Rdb_key_def::KEY_COVERED;
      return true;
    }

    case MYSQL_TYPE_BIT:
      m_pack_func = Rdb_key_def::pack_bit;
      m_unpack_func = Rdb_key_def::unpack_bit;
      m_covered = Rdb_key_def::KEY_COVERED;
      return true;

    case MYSQL_TYPE_VARCHAR:
      m_pack_func = Rdb_key_def::pack_with_varlength_encoding;
      break;  // handling below

    case MYSQL_TYPE_STRING:
      m_pack_func = Rdb_key_def::pack_string;
      break;  // handling below

    default:
      // MYSQL_TYPE_DECIMAL, MYSQL_TYPE_TIMESTAMP,
      // MYSQL_TYPE_TIME, MYSQL_TYPE_DATETIME,
      // MYSQL_TYPE_VAR_STRING are obsolete
      // MYSQL_TYPE_JSON is not supported as index
      // MYSQL_TYPE_GEOMETRY is not supported by MyRocks yet
      SHIP_ASSERT(!"Unexpected MYSQL_TYPE_* seen in packing");
      return false;
  }

  m_unpack_info_stores_value = false;
  /* Handle [VAR](CHAR|BINARY) */
  const bool is_varlength = Rdb_key_def::is_variable_length_field(type);
  if (is_varlength || type == MYSQL_TYPE_STRING) {
    /*
      For CHAR-based columns, check how strxfrm image will take.
    */
    const CHARSET_INFO *cs = field->charset();
    m_max_image_len = cs->coll->strnxfrmlen(cs, m_max_field_bytes);

    /* Remember the original length before encoding - we'll use it in
      packing / padding calculations later */
    m_max_image_len_before_encoding = m_max_image_len;
  }

  const CHARSET_INFO *cs = field->charset();
  // max_image_len before chunking is taken into account
  const int max_image_len_before_chunks = m_max_image_len;

  if (is_varlength) {
    // The default for varchar is variable-length, without space-padding for
    // comparisons
    m_varlength_charset = cs;
    m_skip_func = Rdb_key_def::skip_variable_length_encoding;

    // Calculate the maximum size of the short section plus the
    // maximum size of the long section
    m_max_image_len = RDB_ENCODED_SIZE(m_max_image_len);

    m_unpack_info_uses_two_bytes = (m_max_field_bytes + 8 >= 0x100);
  }

  if (is_varlength || type == MYSQL_TYPE_STRING) {
    // See http://dev.mysql.com/doc/refman/5.7/en/string-types.html for
    // information about character-based datatypes are compared.

    if (cs == &my_charset_bin) {
      // - SQL layer pads BINARY(N) so that it always is N bytes long.
      // - For VARBINARY(N), values may have different lengths, so we're using
      //   variable-length encoding. This is also the only charset where the
      //   values are not space-padded for comparison.
      assert((is_varlength &&
              m_pack_func == Rdb_key_def::pack_with_varlength_encoding) ||
             m_pack_func == Rdb_key_def::pack_string);
      assert(m_make_unpack_info_func == nullptr);
      m_unpack_func = is_varlength ? Rdb_key_def::unpack_binary_varlength
                                   : Rdb_key_def::unpack_binary_str;
      m_covered = Rdb_key_def::KEY_COVERED;
    } else if (rdb_is_binary_collation(cs)) {
      // For _bin collations, mem-comparable form of the string is the string
      // itself.

      if (is_varlength) {
        // VARCHARs - are compared as if they were space-padded - but are
        // not actually space-padded (reading the value back produces the
        // original value, without the padding)
        m_unpack_func = (cs == &my_charset_utf8mb4_bin)
                            ? Rdb_key_def::unpack_utf8mb4_varlength_space_pad
                        : (cs == &my_charset_utf8mb3_bin)
                            ? Rdb_key_def::unpack_utf8_varlength_space_pad
                            : Rdb_key_def::unpack_binary_varlength_space_pad;

        m_skip_func = Rdb_key_def::skip_variable_space_pad;
        m_pack_func = Rdb_key_def::pack_with_varlength_space_pad;
        m_make_unpack_info_func = Rdb_key_def::dummy_make_unpack_info;
        m_segment_size = get_segment_size_from_collation(cs);
        m_max_image_len =
            (max_image_len_before_chunks / (m_segment_size - 1) + 1) *
                m_segment_size +
            1;
        rdb_get_mem_comparable_space(cs, &space_xfrm, &space_xfrm_len,
                                     &space_mb_len);
      } else {
        // SQL layer pads CHAR(N) values to their maximum length.
        // We just store that and restore it back.
        assert(m_pack_func == Rdb_key_def::pack_string);
        assert(m_make_unpack_info_func == nullptr);
        m_unpack_func =
            (cs == &my_charset_utf8mb4_bin)   ? Rdb_key_def::unpack_utf8mb4_str
            : (cs == &my_charset_utf8mb3_bin) ? Rdb_key_def::unpack_utf8_str
                                              : Rdb_key_def::unpack_binary_str;
      }
      m_covered = Rdb_key_def::KEY_COVERED;
    } else {
      // This is [VAR]CHAR(n) and the collation is not $(charset_name)_bin

      m_covered = Rdb_key_def::KEY_COVERED;  // index-only scans are possible
      m_unpack_data_len = is_varlength ? 0 : field->field_length;
      const uint idx = is_varlength ? 0 : 1;
      const Rdb_collation_codec *codec = nullptr;

      if (is_varlength) {
        // VARCHAR requires space-padding for doing comparisons
        //
        // The check for cs->levels_for_order is to catch
        // latin2_czech_cs and cp1250_czech_cs - multi-level collations
        // that Variable-Length Space Padded Encoding can't handle.
        // It is not expected to work for any other multi-level collations,
        // either.
        // Currently we handle these collations as NO_PAD, even if they have
        // PAD_SPACE attribute.
        if (cs->levels_for_compare == 1) {
          if (cs->pad_attribute == NO_PAD) {
            m_pack_func = Rdb_key_def::pack_with_varlength_encoding;
            m_skip_func = Rdb_key_def::skip_variable_length_encoding;
          } else {
            m_pack_func = Rdb_key_def::pack_with_varlength_space_pad;
            m_skip_func = Rdb_key_def::skip_variable_space_pad;
          }
          m_segment_size = get_segment_size_from_collation(cs);
          m_max_image_len =
              (max_image_len_before_chunks / (m_segment_size - 1) + 1) *
                  m_segment_size +
              1;
          rdb_get_mem_comparable_space(cs, &space_xfrm, &space_xfrm_len,
                                       &space_mb_len);
        } else {
          //  NO_LINT_DEBUG
          sql_print_warning(
              "RocksDB: you're trying to create an index "
              "with a multi-level collation %s",
              cs->m_coll_name);
          //  NO_LINT_DEBUG
          sql_print_warning(
              "MyRocks will handle this collation internally "
              "as if it had a NO_PAD attribute.");
        }
      }

      if ((codec = rdb_init_collation_mapping(cs)) != nullptr) {
        // The collation allows to store extra information in the unpack_info
        // which can be used to restore the original value from the
        // mem-comparable form.
        m_make_unpack_info_func = codec->m_make_unpack_info_func[idx];
        m_unpack_func = codec->m_unpack_func[idx];
        m_charset_codec = codec;
      } else {
        // We have no clue about how this collation produces mem-comparable
        // form. Our way of restoring the original value is to keep a copy of
        // the original value in unpack_info.
        m_unpack_info_stores_value = true;
        m_make_unpack_info_func =
            is_varlength ? Rdb_key_def::make_unpack_unknown_varlength
                         : Rdb_key_def::make_unpack_unknown;
        m_unpack_func = is_varlength ? Rdb_key_def::unpack_unknown_varlength
                                     : Rdb_key_def::unpack_unknown;
      }
    }

    // Make an adjustment: if this column is partially covered, tell the SQL
    // layer we can't do index-only scans. Later when we perform an index read,
    // we'll check on a record-by-record basis if we can do an index-only scan
    // or not.
    uint field_length;
    if (field->table) {
      field_length = field->table->field[field->field_index()]->field_length;
    } else {
      field_length = field->field_length;
    }

    if (field_length != key_length) {
      m_covered = Rdb_key_def::KEY_MAY_BE_COVERED;
      // If this index doesn't support covered bitmaps, then we won't know
      // during a read if the column is actually covered or not. If so, we need
      // to assume the column isn't covered and skip it during unpacking.
      //
      // If key_descr == NULL, then this is a dummy field and we probably don't
      // need to perform this step. However, to preserve the behavior before
      // this change, we'll only skip this step if we have an index which
      // supports covered bitmaps.
      if (!m_is_secondary_key) {
        m_unpack_func = nullptr;
        m_make_unpack_info_func = nullptr;
        m_unpack_info_stores_value = true;
        m_covered = Rdb_key_def::KEY_NOT_COVERED;
      }
    }
  }

  return m_covered == Rdb_key_def::KEY_COVERED;
}

Field *Rdb_field_packing::get_field_in_table(const TABLE *const tbl) const {
  return tbl->key_info[m_keynr].key_part[m_key_part].field;
}

void Rdb_field_packing::fill_hidden_pk_val(uchar **dst,
                                           const longlong hidden_pk_id) const {
  assert(m_max_image_len == 8);

  String to;
  rdb_netstr_append_uint64(&to, hidden_pk_id);
  memcpy(*dst, to.ptr(), m_max_image_len);

  *dst += m_max_image_len;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Rdb_ddl_manager
///////////////////////////////////////////////////////////////////////////////////////////

Rdb_tbl_def::~Rdb_tbl_def() {
  auto ddl_manager = rdb_get_ddl_manager();
  /* Don't free key definitions */
  if (m_key_descr_arr) {
    for (uint i = 0; i < m_key_count; i++) {
      if (ddl_manager && m_key_descr_arr[i]) {
        ddl_manager->erase_index_num(m_key_descr_arr[i]->get_gl_index_id());
      }

      m_key_descr_arr[i] = nullptr;
    }

    delete[] m_key_descr_arr;
    m_key_descr_arr = nullptr;
  }
}

/*
  Put table definition DDL entry. Actual write is done at
  Rdb_dict_manager::commit.

  We write
    dbname.tablename -> version + {key_entry, key_entry, key_entry, ... }

  Where key entries are a tuple of
    ( cf_id, index_nr )
*/

bool Rdb_tbl_def::put_dict(Rdb_dict_manager *const dict,
                           Rdb_cf_manager *cf_manager,
                           rocksdb::WriteBatch *const batch,
                           const rocksdb::Slice &key) {
  StringBuffer<8 * Rdb_key_def::PACKED_SIZE> indexes;
  indexes.alloc(Rdb_key_def::VERSION_SIZE +
                m_key_count * Rdb_key_def::PACKED_SIZE * 2);
  rdb_netstr_append_uint16(&indexes, Rdb_key_def::DDL_ENTRY_INDEX_VERSION);

  for (uint i = 0; i < m_key_count; i++) {
    const Rdb_key_def &kd = *m_key_descr_arr[i];

    const uint cf_id = kd.get_cf()->GetID();
    /*
      If cf_id already exists, cf_flags must be the same.
      To prevent race condition, reading/modifying/committing CF flags
      need to be protected by mutex (dict_manager->lock()).
      When RocksDB supports transaction with pessimistic concurrency
      control, we can switch to use it and removing mutex.
    */
    const std::string cf_name = kd.get_cf()->GetName();

    std::shared_ptr<rocksdb::ColumnFamilyHandle> cfh =
        cf_manager->get_cf(cf_name);

    if (!cfh || cfh != kd.get_shared_cf() || dict->get_dropped_cf(cf_id)) {
      // The CF has been dropped, i.e., cf_manager.remove_dropped_cf() has been
      // called; or the CF is being dropped, i.e., cf_manager.drop_cf() has
      // been called.
      my_error(ER_CF_DROPPED, MYF(0), cf_name.c_str());
      return true;
    }

    rdb_netstr_append_uint32(&indexes, cf_id);

    uint32 index_number = kd.get_index_number();
    rdb_netstr_append_uint32(&indexes, index_number);

    struct Rdb_index_info index_info;
    index_info.m_gl_index_id = {cf_id, index_number};
    index_info.m_index_dict_version = Rdb_key_def::INDEX_INFO_VERSION_LATEST;
    index_info.m_index_type = kd.m_index_type;
    index_info.m_kv_version = kd.m_kv_format_version;
    index_info.m_index_flags = kd.m_index_flags_bitmap;
    index_info.m_ttl_duration = kd.m_ttl_duration;

    dict->add_or_update_index_cf_mapping(batch, &index_info);
  }

  const rocksdb::Slice svalue(indexes.c_ptr(), indexes.length());

  dict->put_key(batch, key, svalue);
  return false;
}

// Length that each index flag takes inside the record.
// Each index in the array maps to the enum INDEX_FLAG
static const std::array<uint, 1> index_flag_lengths = {
    {ROCKSDB_SIZEOF_TTL_RECORD}};

bool Rdb_key_def::has_index_flag(uint32 index_flags, enum INDEX_FLAG flag) {
  return flag & index_flags;
}

uint32 Rdb_key_def::calculate_index_flag_offset(uint32 index_flags,
                                                enum INDEX_FLAG flag,
                                                uint *const length) {
  assert_IMP(flag != MAX_FLAG,
                  Rdb_key_def::has_index_flag(index_flags, flag));

  uint offset = 0;
  for (size_t bit = 0; bit < sizeof(index_flags) * CHAR_BIT; ++bit) {
    int mask = 1 << bit;

    /* Exit once we've reached the proper flag */
    if (flag & mask) {
      if (length != nullptr) {
        *length = index_flag_lengths[bit];
      }
      break;
    }

    if (index_flags & mask) {
      offset += index_flag_lengths[bit];
    }
  }

  return offset;
}

void Rdb_key_def::write_index_flag_field(Rdb_string_writer *const buf,
                                         const uchar *const val,
                                         enum INDEX_FLAG flag) const {
  uint len;
  uint offset = calculate_index_flag_offset(m_index_flags_bitmap, flag, &len);
  assert(offset + len <= buf->get_current_pos());
  memcpy(buf->ptr() + offset, val, len);
}

void Rdb_tbl_def::check_if_is_mysql_system_table() {
  static const char *const system_dbs[] = {
      "mysql",
      "performance_schema",
      "information_schema",
  };

  m_is_mysql_system_table = false;
  for (uint ii = 0; ii < array_elements(system_dbs); ii++) {
    if (strcmp(m_dbname.c_str(), system_dbs[ii]) == 0) {
      m_is_mysql_system_table = true;
      break;
    }
  }
}

void Rdb_tbl_def::check_and_set_read_free_rpl_table() {
  m_is_read_free_rpl_table =
      rdb_read_free_regex_handler.matches(base_tablename());
}

void Rdb_tbl_def::set_name(const std::string &name) {
  int err MY_ATTRIBUTE((__unused__));

  m_dbname_tablename = name;
  err = rdb_split_normalized_tablename(name, &m_dbname, &m_tablename,
                                       &m_partition);
  assert(err == 0);

  check_if_is_mysql_system_table();
}

GL_INDEX_ID Rdb_tbl_def::get_autoincr_gl_index_id() {
  for (uint i = 0; i < m_key_count; i++) {
    auto &k = m_key_descr_arr[i];
    if (k->m_index_type == Rdb_key_def::INDEX_TYPE_PRIMARY ||
        k->m_index_type == Rdb_key_def::INDEX_TYPE_HIDDEN_PRIMARY) {
      return k->get_gl_index_id();
    }
  }

  // Every table must have a primary key, even if it's hidden.
  abort();
  return GL_INDEX_ID();
}

void Rdb_ddl_manager::erase_index_num(const GL_INDEX_ID &gl_index_id) {
  m_index_num_to_keydef.erase(gl_index_id);
}

void Rdb_ddl_manager::add_uncommitted_keydefs(
    const std::unordered_set<std::shared_ptr<Rdb_key_def>> &indexes) {
  mysql_rwlock_wrlock(&m_rwlock);
  for (const auto &index : indexes) {
    m_index_num_to_uncommitted_keydef[index->get_gl_index_id()] = index;
  }
  mysql_rwlock_unlock(&m_rwlock);
}

void Rdb_ddl_manager::remove_uncommitted_keydefs(
    const std::unordered_set<std::shared_ptr<Rdb_key_def>> &indexes) {
  mysql_rwlock_wrlock(&m_rwlock);
  for (const auto &index : indexes) {
    m_index_num_to_uncommitted_keydef.erase(index->get_gl_index_id());
  }
  mysql_rwlock_unlock(&m_rwlock);
}

int Rdb_ddl_manager::find_in_uncommitted_keydef(const uint32_t &cf_id) {
  mysql_rwlock_rdlock(&m_rwlock);
  for (const auto &pr : m_index_num_to_uncommitted_keydef) {
    const auto &kd = pr.second;

    if (kd->get_cf()->GetID() == cf_id) {
      mysql_rwlock_unlock(&m_rwlock);
      return HA_EXIT_FAILURE;
    }
  }

  mysql_rwlock_unlock(&m_rwlock);
  return HA_EXIT_SUCCESS;
}

namespace  // anonymous namespace = not visible outside this source file
{
struct Rdb_validate_tbls : public Rdb_tables_scanner {
  using tbl_list_t = std::map<std::string, std::set<std::string>>;

  tbl_list_t m_list;

  int add_table(Rdb_tbl_def *tdef) override;

  bool validate(void);
};
}  // anonymous namespace

/*
  Get a list of tables that we expect to have DD tables for.  This will use the
  information just read from the RocksDB data dictionary.
*/
int Rdb_validate_tbls::add_table(Rdb_tbl_def *tdef) {
  assert(tdef != nullptr);

  /*
    Add the database/table into the list that are not temp table.
    Also skip over truncate temp table.
  */
  if (tdef->base_tablename().find(tmp_file_prefix) == std::string::npos &&
      tdef->base_tablename().find(TRUNCATE_TABLE_PREFIX) == std::string::npos) {
    m_list[tdef->base_dbname()].insert(tdef->base_tablename());
  }

  return HA_EXIT_SUCCESS;
}

/*
  Get the list of RocksDB tables from the MySQL data dictionary
  and compare them to the list of tables from the RocksDB database dictionary
*/
bool Rdb_validate_tbls::validate(void) {
  THD *const thd = my_core::thd_get_current_thd();
  bool result = true;

  dd::cache::Dictionary_client::Auto_releaser releaser(thd->dd_client());

  /* Compare that to the list of RocksDB tables in the MySQL data dictionary */
  std::vector<const dd::Schema *> schema_vector;
  if (thd->dd_client()->fetch_global_components(&schema_vector)) return false;

  for (const dd::Schema *schema : schema_vector) {
    std::vector<dd::String_type> tables;
    if (thd->dd_client()->fetch_schema_table_names_by_engine(
            schema, rocksdb_hton_name, &tables))
      return false;

    if (tables.size() == 0) continue;

    const std::string dbname = schema->name().c_str();
    // convert DD db/table name to my_charset_filename CHARSET_INFO
    // db/table name in MyRocks DD use my_charset_filename CHARSET_INFO
    // db/table name in DD use system_charset_info CHARSET_INFO
    char dbbuff[FN_REFLEN];
    tablename_to_filename(dbname.c_str(), dbbuff, sizeof(dbbuff));
    std::string expected_db_name(dbbuff);
    for (const dd::String_type &table_name : tables) {
      const std::string tablename = table_name.c_str();

      // convert DD db/table name to my_charset_filename CHARSET_INFO
      // db/table name in MyRocks DD use my_charset_filename CHARSET_INFO
      // db/table name in DD use system_charset_info CHARSET_INFO
      char tbbuff[FN_REFLEN];
      tablename_to_filename(tablename.c_str(), tbbuff, sizeof(tbbuff));
      std::string expected_tbl_name(tbbuff);
      /*
        Attempt to remove the table entry from the list of tables.  If this
        fails then we know we had a DD table that wasn't registered in RocksDB.
      */
      if (m_list.count(expected_db_name) == 0 ||
          m_list[expected_db_name].erase(expected_tbl_name) == 0) {
        sql_print_warning(
            "RocksDB: Schema mismatch - "
            "A DD table exists for table %s.%s, "
            "but that table is not registered in RocksDB",
            dbname.c_str(), tablename.c_str());
        result = false;
      }
    }

    /* Remove any databases which have no more tables listed */
    if (m_list.count(expected_db_name) == 1 &&
        m_list[expected_db_name].size() == 0) {
      m_list.erase(expected_db_name);
    }
  }

  /*
    Any tables left in the tables list are ones that are registered in RocksDB
    but don't have a corresponding DD table.
  */
  for (const auto &db : m_list) {
    for (const auto &table : db.second) {
      sql_print_warning(
          "Schema mismatch - Table %s.%s is registered in RocksDB "
          "but does not have a corresponding DD table",
          db.first.c_str(), table.c_str());
      result = false;
    }
  }
  return result;
}

/*
  Validate that all the tables in the RocksDB database dictionary match the
  MySQL data dictionary
*/
bool Rdb_ddl_manager::validate_schemas(void) {
  Rdb_validate_tbls table_list;

  /* Get the list of tables from the RocksDB database dictionary */
  if (scan_for_tables(&table_list) != 0) {
    return false;
  }

  return table_list.validate();
}

/*
  Validate that all auto increment values in the data dictionary are on a
  supported version.
*/
bool Rdb_ddl_manager::validate_auto_incr() {
  std::unique_ptr<rocksdb::Iterator> it(m_dict->new_iterator());

  uchar auto_incr_entry[Rdb_key_def::INDEX_NUMBER_SIZE];
  rdb_netbuf_store_index(auto_incr_entry, Rdb_key_def::AUTO_INC);
  const rocksdb::Slice auto_incr_entry_slice(
      reinterpret_cast<char *>(auto_incr_entry),
      Rdb_key_def::INDEX_NUMBER_SIZE);
  for (it->Seek(auto_incr_entry_slice); it->Valid(); it->Next()) {
    const rocksdb::Slice key = it->key();
    const rocksdb::Slice val = it->value();
    GL_INDEX_ID gl_index_id;

    if (key.size() >= Rdb_key_def::INDEX_NUMBER_SIZE &&
        memcmp(key.data(), auto_incr_entry, Rdb_key_def::INDEX_NUMBER_SIZE)) {
      break;
    }

    if (key.size() != Rdb_key_def::INDEX_NUMBER_SIZE * 3) {
      return false;
    }

    if (val.size() <= Rdb_key_def::VERSION_SIZE) {
      return false;
    }

    // Check if we have orphaned entries for whatever reason by cross
    // referencing ddl entries.
    auto ptr = reinterpret_cast<const uchar *>(key.data());
    ptr += Rdb_key_def::INDEX_NUMBER_SIZE;
    rdb_netbuf_read_gl_index(&ptr, &gl_index_id);
    if (!m_dict->get_index_info(gl_index_id, nullptr)) {
      // NO_LINT_DEBUG
      sql_print_warning(
          "RocksDB: AUTOINC mismatch - "
          "Index number (%u, %u) found in AUTOINC "
          "but does not exist as a DDL entry for table %s",
          gl_index_id.cf_id, gl_index_id.index_id,
          safe_get_table_name(gl_index_id).c_str());
      return false;
    }

    ptr = reinterpret_cast<const uchar *>(val.data());
    const int version = rdb_netbuf_read_uint16(&ptr);
    if (version > Rdb_key_def::AUTO_INCREMENT_VERSION) {
      // NO_LINT_DEBUG
      sql_print_warning(
          "RocksDB: AUTOINC mismatch - "
          "Index number (%u, %u) found in AUTOINC "
          "is on unsupported version %d for table %s",
          gl_index_id.cf_id, gl_index_id.index_id, version,
          safe_get_table_name(gl_index_id).c_str());
      return false;
    }
  }

  if (!it->status().ok()) {
    return false;
  }

  return true;
}

bool Rdb_ddl_manager::init(Rdb_dict_manager *const dict_arg,
                           Rdb_cf_manager *const cf_manager,
                           const uint32_t validate_tables) {
  m_dict = dict_arg;
  m_cf_manager = cf_manager;
  mysql_rwlock_init(0, &m_rwlock);

  /* Read the data dictionary and populate the hash */
  uchar ddl_entry[Rdb_key_def::INDEX_NUMBER_SIZE];
  rdb_netbuf_store_index(ddl_entry, Rdb_key_def::DDL_ENTRY_INDEX_START_NUMBER);
  const rocksdb::Slice ddl_entry_slice((char *)ddl_entry,
                                       Rdb_key_def::INDEX_NUMBER_SIZE);

  /* Reading data dictionary should always skip bloom filter */
  rocksdb::Iterator *it = m_dict->new_iterator();
  int i = 0;

  uint max_index_id_in_dict = 0;
  m_dict->get_max_index_id(&max_index_id_in_dict);

  for (it->Seek(ddl_entry_slice); it->Valid(); it->Next()) {
    const uchar *ptr;
    const uchar *ptr_end;
    const rocksdb::Slice key = it->key();
    const rocksdb::Slice val = it->value();

    if (key.size() >= Rdb_key_def::INDEX_NUMBER_SIZE &&
        memcmp(key.data(), ddl_entry, Rdb_key_def::INDEX_NUMBER_SIZE)) {
      break;
    }

    if (key.size() <= Rdb_key_def::INDEX_NUMBER_SIZE) {
      // NO_LINT_DEBUG
      sql_print_error("RocksDB: Table_store: key has length %d (corruption?)",
                      (int)key.size());
      return true;
    }

    Rdb_tbl_def *const tdef =
        new Rdb_tbl_def(key, Rdb_key_def::INDEX_NUMBER_SIZE);

    // Now, read the DDLs.
    const int real_val_size = val.size() - Rdb_key_def::VERSION_SIZE;
    if (real_val_size % Rdb_key_def::PACKED_SIZE * 2 > 0) {
      // NO_LINT_DEBUG
      sql_print_error("RocksDB: Table_store: invalid keylist for table %s",
                      tdef->full_tablename().c_str());
      return true;
    }
    tdef->m_key_count = real_val_size / (Rdb_key_def::PACKED_SIZE * 2);
    tdef->m_key_descr_arr = new std::shared_ptr<Rdb_key_def>[tdef->m_key_count];

    ptr = reinterpret_cast<const uchar *>(val.data());
    const int version = rdb_netbuf_read_uint16(&ptr);
    if (version != Rdb_key_def::DDL_ENTRY_INDEX_VERSION) {
      // NO_LINT_DEBUG
      sql_print_error(
          "RocksDB: DDL ENTRY Version was not expected."
          "Expected: %d, Actual: %d",
          Rdb_key_def::DDL_ENTRY_INDEX_VERSION, version);
      return true;
    }
    ptr_end = ptr + real_val_size;
    for (uint keyno = 0; ptr < ptr_end; keyno++) {
      GL_INDEX_ID gl_index_id;
      rdb_netbuf_read_gl_index(&ptr, &gl_index_id);
      uint flags = 0;
      struct Rdb_index_info index_info;
      if (!m_dict->get_index_info(gl_index_id, &index_info)) {
        // NO_LINT_DEBUG
        sql_print_error(
            "RocksDB: Could not get index information "
            "for Index Number (%u,%u), table %s",
            gl_index_id.cf_id, gl_index_id.index_id,
            tdef->full_tablename().c_str());
        return true;
      }
      if (max_index_id_in_dict < gl_index_id.index_id) {
        // NO_LINT_DEBUG
        sql_print_error(
            "RocksDB: Found max index id %u from data dictionary "
            "but also found larger index id %u from dictionary. "
            "This should never happen and possibly a bug.",
            max_index_id_in_dict, gl_index_id.index_id);
        return true;
      }
      if (!m_dict->get_cf_flags(gl_index_id.cf_id, &flags)) {
        // NO_LINT_DEBUG
        sql_print_error(
            "RocksDB: Could not get Column Family Flags "
            "for CF Number %d, table %s",
            gl_index_id.cf_id, tdef->full_tablename().c_str());
        return true;
      }

      if ((flags & Rdb_key_def::AUTO_CF_FLAG) != 0) {
        // The per-index cf option is deprecated.  Make sure we don't have the
        // flag set in any existing database.   NO_LINT_DEBUG
        // NO_LINT_DEBUG
        sql_print_error(
            "RocksDB: The defunct AUTO_CF_FLAG is enabled for CF "
            "number %d, table %s",
            gl_index_id.cf_id, tdef->full_tablename().c_str());
      }

      std::shared_ptr<rocksdb::ColumnFamilyHandle> cfh =
          cf_manager->get_cf(gl_index_id.cf_id);
      assert(cfh);

      uint32 ttl_rec_offset =
          Rdb_key_def::has_index_flag(index_info.m_index_flags,
                                      Rdb_key_def::TTL_FLAG)
              ? Rdb_key_def::calculate_index_flag_offset(
                    index_info.m_index_flags, Rdb_key_def::TTL_FLAG)
              : UINT_MAX;

      /*
        We can't fully initialize Rdb_key_def object here, because full
        initialization requires that there is an open TABLE* where we could
        look at Field* objects and set max_length and other attributes
      */
      tdef->m_key_descr_arr[keyno] = std::make_shared<Rdb_key_def>(
          gl_index_id.index_id, keyno, cfh, index_info.m_index_dict_version,
          index_info.m_index_type, index_info.m_kv_version,
          flags & Rdb_key_def::REVERSE_CF_FLAG,
          flags & Rdb_key_def::PER_PARTITION_CF_FLAG, "",
          m_dict->get_stats(gl_index_id), index_info.m_index_flags,
          ttl_rec_offset, index_info.m_ttl_duration);
    }

    assert(tdef->m_key_count > 0);
    tdef->m_tbl_stats.set(
        tdef->m_key_count > 0 ? tdef->m_key_descr_arr[0]->m_stats.m_rows : 0, 0,
        0);

    put(tdef);
    i++;
  }

  /*
    If validate_tables is greater than 0 run the validation.  Only fail the
    initialzation if the setting is 1.  If the setting is 2 we continue.
  */
  if (validate_tables > 0) {
    std::string msg;
    if (!validate_schemas()) {
      msg =
          "RocksDB: Problems validating data dictionary "
          "against DD tables, exiting";
    } else if (!validate_auto_incr()) {
      msg =
          "RocksDB: Problems validating auto increment values in "
          "data dictionary, exiting";
    }
    if (validate_tables == 1 && !msg.empty()) {
      // NO_LINT_DEBUG
      sql_print_error(
          "%s. Use \"rocksdb_validate_tables=2\" to ignore this error.",
          msg.c_str());
      return true;
    }
  }

  // index ids used by applications should not conflict with
  // data dictionary index ids
  if (max_index_id_in_dict < Rdb_key_def::END_DICT_INDEX_ID) {
    max_index_id_in_dict = Rdb_key_def::END_DICT_INDEX_ID;
  }

  m_sequence.init(max_index_id_in_dict + 1);

  if (!it->status().ok()) {
    rdb_log_status_error(it->status(), "Table_store load error");
    return true;
  }
  delete it;
  // NO_LINT_DEBUG
  sql_print_information("RocksDB: Table_store: loaded DDL data for %d tables",
                        i);

  initialized = true;
  return false;
}

Rdb_tbl_def *Rdb_ddl_manager::find(const std::string &table_name,
                                   const bool lock) {
  if (lock) {
    mysql_rwlock_rdlock(&m_rwlock);
  }

  Rdb_tbl_def *rec = nullptr;
  const auto it = m_ddl_map.find(table_name);
  if (it != m_ddl_map.end()) {
    rec = it->second;
  }

  if (lock) {
    mysql_rwlock_unlock(&m_rwlock);
  }

  return rec;
}

int Rdb_ddl_manager::find_indexes(const std::string &table_name,
                                  std::vector<GL_INDEX_ID> *indexes) {
  mysql_rwlock_rdlock(&m_rwlock);

  Rdb_tbl_def *tdef = nullptr;
  const auto it = m_ddl_map.find(table_name);
  if (it != m_ddl_map.end()) {
    tdef = it->second;
  }

  if (!tdef) {
    mysql_rwlock_unlock(&m_rwlock);
    return HA_EXIT_FAILURE;
  }

  for (uint i = 0; i < tdef->m_key_count; i++) {
    indexes->push_back(tdef->m_key_descr_arr[i]->get_gl_index_id());
  }

  mysql_rwlock_unlock(&m_rwlock);

  return HA_EXIT_SUCCESS;
}

int Rdb_ddl_manager::find_table_stats(const std::string &table_name,
                                      Rdb_table_stats *tbl_stats) {
  mysql_rwlock_rdlock(&m_rwlock);

  Rdb_tbl_def *tdef = nullptr;
  const auto it = m_ddl_map.find(table_name);
  if (it != m_ddl_map.end()) {
    tdef = it->second;
  }

  if (!tdef) {
    mysql_rwlock_unlock(&m_rwlock);
    return HA_EXIT_FAILURE;
  }

  *tbl_stats = tdef->m_tbl_stats;

  mysql_rwlock_unlock(&m_rwlock);

  return HA_EXIT_SUCCESS;
}

// this is a safe version of the find() function below.  It acquires a read
// lock on m_rwlock to make sure the Rdb_key_def is not discarded while we
// are finding it.  Copying it into 'ret' increments the count making sure
// that the object will not be discarded until we are finished with it.
std::shared_ptr<const Rdb_key_def> Rdb_ddl_manager::safe_find(
    GL_INDEX_ID gl_index_id) {
  std::shared_ptr<const Rdb_key_def> ret(nullptr);

  mysql_rwlock_rdlock(&m_rwlock);

  const auto it = m_index_num_to_keydef.find(gl_index_id);
  if (it != m_index_num_to_keydef.end()) {
    const auto table_def = find(it->second.first, false);
    if (table_def && it->second.second < table_def->m_key_count) {
      const auto &kd = table_def->m_key_descr_arr[it->second.second];
      if (kd->max_storage_fmt_length() != 0) {
        ret = kd;
      }
    }
  } else {
    const auto uncommitted_it =
        m_index_num_to_uncommitted_keydef.find(gl_index_id);
    if (uncommitted_it != m_index_num_to_uncommitted_keydef.end()) {
      const auto &kd = uncommitted_it->second;
      if (kd->max_storage_fmt_length() != 0) {
        ret = kd;
      }
    }
  }

  mysql_rwlock_unlock(&m_rwlock);

  return ret;
}

// this method assumes at least read-only lock on m_rwlock
const std::shared_ptr<Rdb_key_def> &Rdb_ddl_manager::find(
    GL_INDEX_ID gl_index_id) {
  const auto it = m_index_num_to_keydef.find(gl_index_id);
  if (it != m_index_num_to_keydef.end()) {
    const auto table_def = find(it->second.first, false);
    if (table_def) {
      if (it->second.second < table_def->m_key_count) {
        return table_def->m_key_descr_arr[it->second.second];
      }
    }
  } else {
    const auto uncommitted_it =
        m_index_num_to_uncommitted_keydef.find(gl_index_id);
    if (uncommitted_it != m_index_num_to_uncommitted_keydef.end()) {
      return uncommitted_it->second;
    }
  }

  static std::shared_ptr<Rdb_key_def> empty = nullptr;

  return empty;
}

// this method returns the name of the table based on an index id. It acquires
// a read lock on m_rwlock.
const std::string Rdb_ddl_manager::safe_get_table_name(
    const GL_INDEX_ID &gl_index_id) {
  std::string ret;
  mysql_rwlock_rdlock(&m_rwlock);
  auto it = m_index_num_to_keydef.find(gl_index_id);
  if (it != m_index_num_to_keydef.end()) {
    ret = it->second.first;
  }
  mysql_rwlock_unlock(&m_rwlock);
  return ret;
}

void Rdb_ddl_manager::set_stats(
    const std::unordered_map<GL_INDEX_ID, Rdb_index_stats> &stats) {
  mysql_rwlock_wrlock(&m_rwlock);
  for (const auto &src : stats) {
    const auto &keydef = find(src.second.m_gl_index_id);
    if (keydef) {
      keydef->m_stats = src.second;
      m_stats2store[keydef->m_stats.m_gl_index_id] = keydef->m_stats;
    }
  }
  mysql_rwlock_unlock(&m_rwlock);
}

void Rdb_ddl_manager::adjust_stats(
    const std::vector<Rdb_index_stats> &new_data,
    const std::vector<Rdb_index_stats> &deleted_data) {
  mysql_rwlock_wrlock(&m_rwlock);
  int i = 0;
  for (const auto &data : {new_data, deleted_data}) {
    for (const auto &src : data) {
      const auto &keydef = find(src.m_gl_index_id);
      if (keydef) {
        keydef->m_stats.m_distinct_keys_per_prefix.resize(
            keydef->get_key_parts());
        keydef->m_stats.merge(src, i == 0, keydef->max_storage_fmt_length());
        m_stats2store[keydef->m_stats.m_gl_index_id] = keydef->m_stats;
      }
    }
    i++;
  }
  const bool should_save_stats = !m_stats2store.empty();
  mysql_rwlock_unlock(&m_rwlock);
  if (should_save_stats) {
    // Queue an async persist_stats(false) call to the background thread.
    rdb_queue_save_stats_request();
  }
}

void Rdb_ddl_manager::persist_stats(const bool sync) {
  mysql_rwlock_wrlock(&m_rwlock);
  const auto local_stats2store = std::move(m_stats2store);
  m_stats2store.clear();
  mysql_rwlock_unlock(&m_rwlock);

  // Persist stats
  const std::unique_ptr<rocksdb::WriteBatch> wb = m_dict->begin();
  std::vector<Rdb_index_stats> stats;
  std::transform(local_stats2store.begin(), local_stats2store.end(),
                 std::back_inserter(stats),
                 [](const std::pair<GL_INDEX_ID, Rdb_index_stats> &s) {
                   return s.second;
                 });
  m_dict->add_stats(wb.get(), stats);
  m_dict->commit(wb.get(), sync);
}

void Rdb_ddl_manager::set_table_stats(const std::string &tbl_name) {
  timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);

  mysql_rwlock_rdlock(&m_rwlock);
  const auto &tbl_def = find(tbl_name, false /* needs lock */);
  if (tbl_def) {
    assert(tbl_def->m_key_count > 0);
    // Take the number of rows of the first index as the number of rows of
    // the table. This is an estimated value.
    tbl_def->m_tbl_stats.set(tbl_def->m_key_count > 0
                                 ? tbl_def->m_key_descr_arr[0]->m_stats.m_rows
                                 : 0,
                             0, ts.tv_sec);
  }
  mysql_rwlock_unlock(&m_rwlock);
}

/*
  Put table definition of `tbl` into the mapping, and also write it to the
  on-disk data dictionary.
*/

int Rdb_ddl_manager::put_and_write(Rdb_tbl_def *const tbl,
                                   rocksdb::WriteBatch *const batch) {
  Rdb_buf_writer<FN_LEN * 2 + Rdb_key_def::INDEX_NUMBER_SIZE> buf_writer;

  buf_writer.write_index(Rdb_key_def::DDL_ENTRY_INDEX_START_NUMBER);

  const std::string &dbname_tablename = tbl->full_tablename();
  buf_writer.write(dbname_tablename.c_str(), dbname_tablename.size());

  int res;
  if ((res =
           tbl->put_dict(m_dict, m_cf_manager, batch, buf_writer.to_slice()))) {
    return res;
  }
  if ((res = put(tbl))) {
    return res;
  }
  return HA_EXIT_SUCCESS;
}

/* Return 0 - ok, other value - error */
/* TODO:
  This function modifies m_ddl_map and m_index_num_to_keydef.
  However, these changes need to be reversed if dict_manager.commit fails
  See the discussion here: https://reviews.facebook.net/D35925#inline-259167
  Tracked by https://github.com/facebook/mysql-5.6/issues/33
*/
int Rdb_ddl_manager::put(Rdb_tbl_def *const tbl, const bool lock) {
  Rdb_tbl_def *rec;
  const std::string &dbname_tablename = tbl->full_tablename();

  if (lock) mysql_rwlock_wrlock(&m_rwlock);

  // We have to do this find because 'tbl' is not yet in the list.  We need
  // to find the one we are replacing ('rec')
  rec = find(dbname_tablename, false);
  if (rec) {
    // Free the old record.
    delete rec;
    m_ddl_map.erase(dbname_tablename);
  }
  m_ddl_map.emplace(dbname_tablename, tbl);

  for (uint keyno = 0; keyno < tbl->m_key_count; keyno++) {
    m_index_num_to_keydef[tbl->m_key_descr_arr[keyno]->get_gl_index_id()] =
        std::make_pair(dbname_tablename, keyno);
  }
  tbl->check_and_set_read_free_rpl_table();

  if (lock) mysql_rwlock_unlock(&m_rwlock);
  return 0;
}

void Rdb_ddl_manager::remove(Rdb_tbl_def *const tbl,
                             rocksdb::WriteBatch *const batch,
                             const bool lock) {
  if (lock) mysql_rwlock_wrlock(&m_rwlock);

  Rdb_buf_writer<FN_LEN * 2 + Rdb_key_def::INDEX_NUMBER_SIZE> key_writer;
  key_writer.write_index(Rdb_key_def::DDL_ENTRY_INDEX_START_NUMBER);
  const std::string &dbname_tablename = tbl->full_tablename();
  key_writer.write(dbname_tablename.c_str(), dbname_tablename.size());

  m_dict->delete_key(batch, key_writer.to_slice());

  const auto it = m_ddl_map.find(dbname_tablename);
  if (it != m_ddl_map.end()) {
    // Free Rdb_tbl_def
    delete it->second;

    m_ddl_map.erase(it);
  }

  if (lock) mysql_rwlock_unlock(&m_rwlock);
}

bool Rdb_ddl_manager::rename(const std::string &from, const std::string &to,
                             rocksdb::WriteBatch *const batch) {
  Rdb_tbl_def *rec;
  Rdb_tbl_def *new_rec;
  bool res = true;
  Rdb_buf_writer<FN_LEN * 2 + Rdb_key_def::INDEX_NUMBER_SIZE> new_buf_writer;

  mysql_rwlock_wrlock(&m_rwlock);
  if (!(rec = find(from, false))) {
    mysql_rwlock_unlock(&m_rwlock);
    return true;
  }

  new_rec = new Rdb_tbl_def(to);

  new_rec->m_key_count = rec->m_key_count;
  new_rec->m_auto_incr_val =
      rec->m_auto_incr_val.load(std::memory_order_relaxed);
  new_rec->m_key_descr_arr = rec->m_key_descr_arr;

  new_rec->m_hidden_pk_val =
      rec->m_hidden_pk_val.load(std::memory_order_relaxed);

  new_rec->m_tbl_stats = rec->m_tbl_stats;

  // so that it's not free'd when deleting the old rec
  rec->m_key_descr_arr = nullptr;

  // Create a new key
  new_buf_writer.write_index(Rdb_key_def::DDL_ENTRY_INDEX_START_NUMBER);

  const std::string &dbname_tablename = new_rec->full_tablename();
  new_buf_writer.write(dbname_tablename.c_str(), dbname_tablename.size());

  // Create a key to add
  if (!new_rec->put_dict(m_dict, m_cf_manager, batch,
                         new_buf_writer.to_slice())) {
    remove(rec, batch, false);
    put(new_rec, false);
    res = false;  // ok
  }

  mysql_rwlock_unlock(&m_rwlock);
  return res;
}

void Rdb_ddl_manager::cleanup() {
  if (!initialized) return;

  for (const auto &kv : m_ddl_map) {
    delete kv.second;
  }
  m_ddl_map.clear();

  mysql_rwlock_destroy(&m_rwlock);
  m_sequence.cleanup();
}

int Rdb_ddl_manager::scan_for_tables(Rdb_tables_scanner *const tables_scanner) {
  int ret;
  Rdb_tbl_def *rec;

  assert(tables_scanner != nullptr);

  // This method should NOT accquire dict_manager lock and
  // cf_manager lock in order to prevent deadlocks.
  mysql_rwlock_rdlock(&m_rwlock);

  ret = 0;

  for (const auto &kv : m_ddl_map) {
    rec = kv.second;
    ret = tables_scanner->add_table(rec);
    if (ret) break;
  }

  mysql_rwlock_unlock(&m_rwlock);
  return ret;
}

/*
  Rdb_binlog_manager class implementation
*/

bool Rdb_binlog_manager::init(Rdb_dict_manager *const dict_arg) {
  assert(dict_arg != nullptr);
  m_dict = dict_arg;

  m_key_writer.reset();
  m_key_writer.write_index(Rdb_key_def::BINLOG_INFO_INDEX_NUMBER);
  m_key_slice = m_key_writer.to_slice();
  return false;
}

void Rdb_binlog_manager::cleanup() {}

/**
  Set binlog name, pos and optionally gtid into WriteBatch.
  This function should be called as part of transaction commit,
  since binlog info is set only at transaction commit.
  Actual write into RocksDB is not done here, so checking if
  write succeeded or not is not possible here.
  @param binlog_name   Binlog name
  @param binlog_pos    Binlog pos
  @param binlog_gtid   Binlog max GTID
  @param batch         WriteBatch
*/
void Rdb_binlog_manager::update(const char *const binlog_name,
                                const my_off_t binlog_pos,
                                const char *const binlog_max_gtid,
                                rocksdb::WriteBatchBase *const batch) {
  if (binlog_name && binlog_pos) {
    // max binlog length (512) + binlog pos (4) + binlog gtid (57) < 1024
    const size_t RDB_MAX_BINLOG_INFO_LEN = 1024;
    Rdb_buf_writer<RDB_MAX_BINLOG_INFO_LEN> value_writer;

    // store version
    value_writer.write_uint16(Rdb_key_def::BINLOG_INFO_INDEX_NUMBER_VERSION);

    // store binlog file name length
    assert(strlen(binlog_name) <= FN_REFLEN);
    const uint16_t binlog_name_len = strlen(binlog_name);
    value_writer.write_uint16(binlog_name_len);

    // store binlog file name
    value_writer.write(binlog_name, binlog_name_len);

    // store binlog pos
    value_writer.write_uint32(binlog_pos);

    // store binlog gtid length.
    // If gtid was not set, store 0 instead
    const uint16_t binlog_max_gtid_len =
        binlog_max_gtid ? strlen(binlog_max_gtid) : 0;
    value_writer.write_uint16(binlog_max_gtid_len);

    if (binlog_max_gtid_len > 0) {
      // store binlog gtid
      value_writer.write(binlog_max_gtid, binlog_max_gtid_len);
    }

    m_dict->put_key(batch, m_key_slice, value_writer.to_slice());
  }
}

/**
  Persists binlog name, pos and optionally gtid into RocksDB.
  This function allocates a new batch that is independent from
  any transaction and writes the batch into RocksDB.
  @param[IN] file          Binlog file
  @param[IN] offset        Binlog offset
  @param[IN] binlog_gtid   Binlog max GTID
  @param[IN] sync          Wait for flush when true
  @return
    non 0 value when write fails
    0 for Success
  */
int Rdb_binlog_manager::persist_pos(const char *file, const my_off_t offset,
                                    const char *const max_gtid,
                                    const bool sync) {
  assert(file);

  auto wb = std::unique_ptr<rocksdb::WriteBatch>(new rocksdb::WriteBatch);
  update(file, offset, max_gtid, wb.get());
  return m_dict->commit(wb.get(), sync);
}

/**
  Read binlog committed entry stored in RocksDB, then unpack
  @param[OUT] binlog_name  Binlog name
  @param[OUT] binlog_pos   Binlog pos
  @param[OUT] binlog_gtid  Binlog GTID
  @return
    true is binlog info was found (valid behavior)
    false otherwise
*/
bool Rdb_binlog_manager::read(char *const binlog_name,
                              my_off_t *const binlog_pos,
                              char *const binlog_gtid) const {
  bool ret = false;
  if (binlog_name) {
    std::string value;
    rocksdb::Status status = m_dict->get_value(m_key_slice, &value);
    if (status.ok()) {
      if (!unpack_value((const uchar *)value.c_str(), binlog_name, binlog_pos,
                        binlog_gtid)) {
        ret = true;
      }
    }
  }
  return ret;
}

/**
  Unpack value then split into binlog_name, binlog_pos (and binlog_gtid)
  @param[IN]  value        Binlog state info fetched from RocksDB
  @param[OUT] binlog_name  Binlog name
  @param[OUT] binlog_pos   Binlog pos
  @param[OUT] binlog_gtid  Binlog GTID
  @return     true on error
*/
bool Rdb_binlog_manager::unpack_value(const uchar *const value,
                                      char *const binlog_name,
                                      my_off_t *const binlog_pos,
                                      char *const binlog_gtid) const {
  uint pack_len = 0;

  assert(binlog_pos != nullptr);

  // read version
  const uint16_t version = rdb_netbuf_to_uint16(value);
  pack_len += Rdb_key_def::VERSION_SIZE;
  if (version != Rdb_key_def::BINLOG_INFO_INDEX_NUMBER_VERSION) return true;

  // read binlog file name length
  const uint16_t binlog_name_len = rdb_netbuf_to_uint16(value + pack_len);
  pack_len += sizeof(uint16);
  if (binlog_name_len) {
    // read and set binlog name
    memcpy(binlog_name, value + pack_len, binlog_name_len);
    binlog_name[binlog_name_len] = '\0';
    pack_len += binlog_name_len;

    // read and set binlog pos
    *binlog_pos = rdb_netbuf_to_uint32(value + pack_len);
    pack_len += sizeof(uint32);

    // read gtid length
    const uint16_t binlog_gtid_len = rdb_netbuf_to_uint16(value + pack_len);
    pack_len += sizeof(uint16);
    if (binlog_gtid && binlog_gtid_len > 0) {
      // read and set gtid
      memcpy(binlog_gtid, value + pack_len, binlog_gtid_len);
      binlog_gtid[binlog_gtid_len] = '\0';
      pack_len += binlog_gtid_len;
    }
  }
  return false;
}

bool Rdb_dict_manager::init(rocksdb::TransactionDB *const rdb_dict,
                            Rdb_cf_manager *const cf_manager,
                            const bool enable_remove_orphaned_dropped_cfs) {
  assert(rdb_dict != nullptr);
  assert(cf_manager != nullptr);

  mysql_mutex_init(0, &m_mutex, MY_MUTEX_INIT_FAST);

  m_db = rdb_dict;

  // It is safe to get raw pointers here since:
  // 1. System CF and default CF cannot be dropped
  // 2. cf_manager outlives dict_manager
  m_system_cfh =
      cf_manager->get_or_create_cf(m_db, DEFAULT_SYSTEM_CF_NAME).get();
  rocksdb::ColumnFamilyHandle *default_cfh =
      cf_manager->get_cf(DEFAULT_CF_NAME).get();

  // System CF and default CF should be initialized
  if (m_system_cfh == nullptr || default_cfh == nullptr) {
    return HA_EXIT_FAILURE;
  }

  rdb_netbuf_store_index(m_key_buf_max_index_id, Rdb_key_def::MAX_INDEX_ID);

  m_key_slice_max_index_id =
      rocksdb::Slice(reinterpret_cast<char *>(m_key_buf_max_index_id),
                     Rdb_key_def::INDEX_NUMBER_SIZE);

  resume_drop_indexes();
  rollback_ongoing_index_creation();

  // Initialize system CF and default CF flags
  const std::unique_ptr<rocksdb::WriteBatch> wb = begin();
  rocksdb::WriteBatch *const batch = wb.get();

  add_cf_flags(batch, m_system_cfh->GetID(), 0);
  add_cf_flags(batch, default_cfh->GetID(), 0);
  commit(batch);

  if (add_missing_cf_flags(cf_manager)) {
    return HA_EXIT_FAILURE;
  }

  if (remove_orphaned_dropped_cfs(cf_manager,
                                  enable_remove_orphaned_dropped_cfs)) {
    return HA_EXIT_FAILURE;
  }

  initialized = true;
  return HA_EXIT_SUCCESS;
}

std::unique_ptr<rocksdb::WriteBatch> Rdb_dict_manager::begin() const {
  return std::unique_ptr<rocksdb::WriteBatch>(new rocksdb::WriteBatch);
}

void Rdb_dict_manager::put_key(rocksdb::WriteBatchBase *const batch,
                               const rocksdb::Slice &key,
                               const rocksdb::Slice &value) const {
  batch->Put(m_system_cfh, key, value);
}

rocksdb::Status Rdb_dict_manager::get_value(const rocksdb::Slice &key,
                                            std::string *const value) const {
  rocksdb::ReadOptions options;
  options.total_order_seek = true;
  return m_db->Get(options, m_system_cfh, key, value);
}

void Rdb_dict_manager::delete_key(rocksdb::WriteBatchBase *batch,
                                  const rocksdb::Slice &key) const {
  batch->Delete(m_system_cfh, key);
}

rocksdb::Iterator *Rdb_dict_manager::new_iterator() const {
  /* Reading data dictionary should always skip bloom filter */
  rocksdb::ReadOptions read_options;
  read_options.total_order_seek = true;
  return m_db->NewIterator(read_options, m_system_cfh);
}

int Rdb_dict_manager::commit(rocksdb::WriteBatch *const batch,
                             const bool sync) const {
  if (!batch) return HA_ERR_ROCKSDB_COMMIT_FAILED;
  int res = HA_EXIT_SUCCESS;
  rocksdb::WriteOptions options;
  options.sync = (sync && rdb_sync_wal_supported());
  rocksdb::TransactionDBWriteOptimizations optimize;
  optimize.skip_concurrency_control = true;
  rocksdb::Status s = m_db->Write(options, optimize, batch);
  res = !s.ok();  // we return true when something failed
  if (res) {
    rdb_handle_io_error(s, RDB_IO_ERROR_DICT_COMMIT);
  }
  if (!rdb_sync_wal_supported()) {
    // If we don't support SyncWAL, do a flush at least
    m_db->FlushWAL(false);
  }

  batch->Clear();
  return res;
}

void Rdb_dict_manager::dump_index_id(uchar *const netbuf,
                                     Rdb_key_def::DATA_DICT_TYPE dict_type,
                                     const GL_INDEX_ID &gl_index_id) {
  rdb_netbuf_store_uint32(netbuf, dict_type);
  rdb_netbuf_store_uint32(netbuf + Rdb_key_def::INDEX_NUMBER_SIZE,
                          gl_index_id.cf_id);
  rdb_netbuf_store_uint32(netbuf + 2 * Rdb_key_def::INDEX_NUMBER_SIZE,
                          gl_index_id.index_id);
}

void Rdb_dict_manager::delete_with_prefix(
    rocksdb::WriteBatch *const batch, Rdb_key_def::DATA_DICT_TYPE dict_type,
    const GL_INDEX_ID &gl_index_id) const {
  Rdb_buf_writer<Rdb_key_def::INDEX_NUMBER_SIZE * 3> key_writer;
  dump_index_id(&key_writer, dict_type, gl_index_id);

  delete_key(batch, key_writer.to_slice());
}

void Rdb_dict_manager::add_or_update_index_cf_mapping(
    rocksdb::WriteBatch *batch, struct Rdb_index_info *const index_info) const {
  Rdb_buf_writer<Rdb_key_def::INDEX_NUMBER_SIZE * 3> key_writer;
  dump_index_id(&key_writer, Rdb_key_def::INDEX_INFO,
                index_info->m_gl_index_id);

  Rdb_buf_writer<256> value_writer;

  value_writer.write_uint16(Rdb_key_def::INDEX_INFO_VERSION_LATEST);
  value_writer.write_byte(index_info->m_index_type);
  value_writer.write_uint16(index_info->m_kv_version);
  value_writer.write_uint32(index_info->m_index_flags);
  value_writer.write_uint64(index_info->m_ttl_duration);

  batch->Put(m_system_cfh, key_writer.to_slice(), value_writer.to_slice());
}

void Rdb_dict_manager::add_cf_flags(rocksdb::WriteBatch *const batch,
                                    const uint32_t cf_id,
                                    const uint32_t cf_flags) const {
  assert(batch != nullptr);

  Rdb_buf_writer<Rdb_key_def::INDEX_NUMBER_SIZE * 2> key_writer;
  key_writer.write_uint32(Rdb_key_def::CF_DEFINITION);
  key_writer.write_uint32(cf_id);

  Rdb_buf_writer<Rdb_key_def::VERSION_SIZE + Rdb_key_def::INDEX_NUMBER_SIZE>
      value_writer;
  value_writer.write_uint16(Rdb_key_def::CF_DEFINITION_VERSION);
  value_writer.write_uint32(cf_flags);

  batch->Put(m_system_cfh, key_writer.to_slice(), value_writer.to_slice());
}

void Rdb_dict_manager::delete_cf_flags(rocksdb::WriteBatch *const batch,
                                       const uint &cf_id) const {
  assert(batch != nullptr);

  uchar key_buf[Rdb_key_def::INDEX_NUMBER_SIZE * 2] = {0};

  rdb_netbuf_store_uint32(key_buf, Rdb_key_def::CF_DEFINITION);
  rdb_netbuf_store_uint32(key_buf + Rdb_key_def::INDEX_NUMBER_SIZE, cf_id);
  const rocksdb::Slice key =
      rocksdb::Slice(reinterpret_cast<char *>(key_buf), sizeof(key_buf));

  delete_key(batch, key);
}

void Rdb_dict_manager::delete_index_info(rocksdb::WriteBatch *batch,
                                         const GL_INDEX_ID &gl_index_id) const {
  delete_with_prefix(batch, Rdb_key_def::INDEX_INFO, gl_index_id);
  delete_with_prefix(batch, Rdb_key_def::INDEX_STATISTICS, gl_index_id);
  delete_with_prefix(batch, Rdb_key_def::AUTO_INC, gl_index_id);
}

bool Rdb_dict_manager::get_index_info(
    const GL_INDEX_ID &gl_index_id,
    struct Rdb_index_info *const index_info) const {
  if (index_info) {
    index_info->m_gl_index_id = gl_index_id;
  }

  bool found = false;
  bool error = false;
  std::string value;
  Rdb_buf_writer<Rdb_key_def::INDEX_NUMBER_SIZE * 3> key_writer;
  dump_index_id(&key_writer, Rdb_key_def::INDEX_INFO, gl_index_id);

  const rocksdb::Status &status = get_value(key_writer.to_slice(), &value);
  if (status.ok()) {
    if (!index_info) {
      return true;
    }

    const uchar *const val = (const uchar *)value.c_str();
    const uchar *ptr = val;
    index_info->m_index_dict_version = rdb_netbuf_to_uint16(val);
    ptr += RDB_SIZEOF_INDEX_INFO_VERSION;

    switch (index_info->m_index_dict_version) {
      case Rdb_key_def::INDEX_INFO_VERSION_FIELD_FLAGS:
        /* Sanity check to prevent reading bogus TTL record. */
        if (value.size() != RDB_SIZEOF_INDEX_INFO_VERSION +
                                RDB_SIZEOF_INDEX_TYPE + RDB_SIZEOF_KV_VERSION +
                                RDB_SIZEOF_INDEX_FLAGS +
                                ROCKSDB_SIZEOF_TTL_RECORD) {
          error = true;
          break;
        }
        index_info->m_index_type = rdb_netbuf_to_byte(ptr);
        ptr += RDB_SIZEOF_INDEX_TYPE;
        index_info->m_kv_version = rdb_netbuf_to_uint16(ptr);
        ptr += RDB_SIZEOF_KV_VERSION;
        index_info->m_index_flags = rdb_netbuf_to_uint32(ptr);
        ptr += RDB_SIZEOF_INDEX_FLAGS;
        index_info->m_ttl_duration = rdb_netbuf_to_uint64(ptr);
        found = true;
        break;

      default:
        error = true;
        break;
    }

    switch (index_info->m_index_type) {
      case Rdb_key_def::INDEX_TYPE_PRIMARY:
      case Rdb_key_def::INDEX_TYPE_HIDDEN_PRIMARY: {
        error = index_info->m_kv_version >
                Rdb_key_def::PRIMARY_FORMAT_VERSION_LATEST;
        break;
      }
      case Rdb_key_def::INDEX_TYPE_SECONDARY:
        error = index_info->m_kv_version >
                Rdb_key_def::SECONDARY_FORMAT_VERSION_LATEST;
        break;
      default:
        error = true;
        break;
    }
  }

  if (error) {
    // NO_LINT_DEBUG
    sql_print_error(
        "RocksDB: Found invalid key version number (%u, %u, %u, %llu) "
        "from data dictionary. This should never happen "
        "and it may be a bug.",
        index_info->m_index_dict_version, index_info->m_index_type,
        index_info->m_kv_version, index_info->m_ttl_duration);
    abort();
  }

  return found;
}

bool Rdb_dict_manager::get_cf_flags(const uint32_t cf_id,
                                    uint32_t *const cf_flags) const {
  assert(cf_flags != nullptr);

  bool found = false;
  std::string value;
  Rdb_buf_writer<Rdb_key_def::INDEX_NUMBER_SIZE * 2> key_writer;

  key_writer.write_uint32(Rdb_key_def::CF_DEFINITION);
  key_writer.write_uint32(cf_id);

  const rocksdb::Status status = get_value(key_writer.to_slice(), &value);

  if (status.ok()) {
    const uchar *val = (const uchar *)value.c_str();
    assert(val);

    const uint16_t version = rdb_netbuf_to_uint16(val);

    if (version == Rdb_key_def::CF_DEFINITION_VERSION) {
      *cf_flags = rdb_netbuf_to_uint32(val + Rdb_key_def::VERSION_SIZE);
      found = true;
    }
  }

  return found;
}

void Rdb_dict_manager::add_dropped_cf(rocksdb::WriteBatch *const batch,
                                      const uint &cf_id) const {
  assert(batch != nullptr);

  uchar key_buf[Rdb_key_def::INDEX_NUMBER_SIZE * 2] = {0};
  uchar value_buf[Rdb_key_def::VERSION_SIZE] = {0};
  rdb_netbuf_store_uint32(key_buf, Rdb_key_def::DROPPED_CF);
  rdb_netbuf_store_uint32(key_buf + Rdb_key_def::INDEX_NUMBER_SIZE, cf_id);
  const rocksdb::Slice key =
      rocksdb::Slice(reinterpret_cast<char *>(key_buf), sizeof(key_buf));

  rdb_netbuf_store_uint16(value_buf, Rdb_key_def::DROPPED_CF_VERSION);
  const rocksdb::Slice value =
      rocksdb::Slice(reinterpret_cast<char *>(value_buf), sizeof(value_buf));
  batch->Put(m_system_cfh, key, value);
}

bool Rdb_dict_manager::get_dropped_cf(const uint &cf_id) const {
  std::string value;
  uchar key_buf[Rdb_key_def::INDEX_NUMBER_SIZE * 2] = {0};

  rdb_netbuf_store_uint32(key_buf, Rdb_key_def::DROPPED_CF);
  rdb_netbuf_store_uint32(key_buf + Rdb_key_def::INDEX_NUMBER_SIZE, cf_id);

  const rocksdb::Slice key =
      rocksdb::Slice(reinterpret_cast<char *>(key_buf), sizeof(key_buf));
  const rocksdb::Status status = get_value(key, &value);

  return status.ok();
}

void Rdb_dict_manager::delete_dropped_cf_and_flags(
    rocksdb::WriteBatch *const batch, const uint &cf_id) const {
  assert(batch != nullptr);
  delete_dropped_cf(batch, cf_id);
  delete_cf_flags(batch, cf_id);
}

void Rdb_dict_manager::delete_dropped_cf(rocksdb::WriteBatch *const batch,
                                         const uint &cf_id) const {
  assert(batch != nullptr);

  uchar key_buf[Rdb_key_def::INDEX_NUMBER_SIZE * 2] = {0};

  rdb_netbuf_store_uint32(key_buf, Rdb_key_def::DROPPED_CF);
  rdb_netbuf_store_uint32(key_buf + Rdb_key_def::INDEX_NUMBER_SIZE, cf_id);
  const rocksdb::Slice key =
      rocksdb::Slice(reinterpret_cast<char *>(key_buf), sizeof(key_buf));

  delete_key(batch, key);
}

void Rdb_dict_manager::get_all_dropped_cfs(
    std::unordered_set<uint32> *dropped_cf_ids) const {
  uchar dropped_cf_buf[Rdb_key_def::INDEX_NUMBER_SIZE];
  rdb_netbuf_store_uint32(dropped_cf_buf, Rdb_key_def::DROPPED_CF);
  const rocksdb::Slice dropped_cf_slice(
      reinterpret_cast<char *>(dropped_cf_buf), Rdb_key_def::INDEX_NUMBER_SIZE);

  rocksdb::Iterator *it = new_iterator();
  for (it->Seek(dropped_cf_slice); it->Valid(); it->Next()) {
    rocksdb::Slice key = it->key();
    const uchar *const ptr = (const uchar *)key.data();

    if (key.size() != Rdb_key_def::INDEX_NUMBER_SIZE * 2 ||
        rdb_netbuf_to_uint32(ptr) != Rdb_key_def::DROPPED_CF) {
      break;
    }

    uint32 cf_id = rdb_netbuf_to_uint32(ptr + Rdb_key_def::INDEX_NUMBER_SIZE);
    dropped_cf_ids->insert(cf_id);
  }

  delete it;
}

/*
  Returning index ids that were marked as deleted (via DROP TABLE) but
  still not removed by drop_index_thread yet, or indexes that are marked as
  ongoing creation.
 */
void Rdb_dict_manager::get_ongoing_index_operation(
    std::unordered_set<GL_INDEX_ID> *gl_index_ids,
    Rdb_key_def::DATA_DICT_TYPE dd_type) const {
  assert(dd_type == Rdb_key_def::DDL_DROP_INDEX_ONGOING ||
              dd_type == Rdb_key_def::DDL_CREATE_INDEX_ONGOING);

  Rdb_buf_writer<Rdb_key_def::INDEX_NUMBER_SIZE> index_writer;
  index_writer.write_uint32(dd_type);
  const rocksdb::Slice index_slice = index_writer.to_slice();

  rocksdb::Iterator *it = new_iterator();
  for (it->Seek(index_slice); it->Valid(); it->Next()) {
    rocksdb::Slice key = it->key();
    const uchar *const ptr = (const uchar *)key.data();

    /*
      Ongoing drop/create index operations require key to be of the form:
      dd_type + cf_id + index_id (== INDEX_NUMBER_SIZE * 3)

      This may need to be changed in the future if we want to process a new
      ddl_type with different format.
    */
    if (key.size() != Rdb_key_def::INDEX_NUMBER_SIZE * 3 ||
        rdb_netbuf_to_uint32(ptr) != dd_type) {
      break;
    }

    // We don't check version right now since currently we always store only
    // Rdb_key_def::DDL_DROP_INDEX_ONGOING_VERSION = 1 as a value.
    // If increasing version number, we need to add version check logic here.
    GL_INDEX_ID gl_index_id;
    gl_index_id.cf_id =
        rdb_netbuf_to_uint32(ptr + Rdb_key_def::INDEX_NUMBER_SIZE);
    gl_index_id.index_id =
        rdb_netbuf_to_uint32(ptr + 2 * Rdb_key_def::INDEX_NUMBER_SIZE);
    gl_index_ids->insert(gl_index_id);
  }
  delete it;
}

/*
  If mysqld reboots during create table, a column family can be
  created without cf flags. This method adds missing cf flags. It
  only should be called during mysqld startup.
 */
int Rdb_dict_manager::add_missing_cf_flags(
    Rdb_cf_manager *const cf_manager) const {
  for (const auto &cf_name : cf_manager->get_cf_names()) {
    std::shared_ptr<rocksdb::ColumnFamilyHandle> cfh =
        cf_manager->get_cf(cf_name);

    if (cf_manager->create_cf_flags_if_needed(this, cfh->GetID(), cf_name)) {
      return HA_EXIT_FAILURE;
    }
  }

  return HA_EXIT_SUCCESS;
}

/*
  If mysqld reboots during dropping a column family, it can happen
  that the column family is deleted from RocksDB, but its id is
  in the list of cf ids that are to be dropped.
  This method cleans up these orphaned cf ids. It only should be
  called during mysqld startup.
 */
int Rdb_dict_manager::remove_orphaned_dropped_cfs(
    Rdb_cf_manager *const cf_manager,
    const bool &enable_remove_orphaned_dropped_cfs) const {
  const std::unique_ptr<rocksdb::WriteBatch> wb = begin();
  rocksdb::WriteBatch *const batch = wb.get();

  std::unordered_set<uint32> dropped_cf_ids;
  get_all_dropped_cfs(&dropped_cf_ids);
  for (const auto cf_id : dropped_cf_ids) {
    if (!cf_manager->get_cf(cf_id)) {
      // NO_LINT_DEBUG
      sql_print_warning(
          "RocksDB: Column family with id %u doesn't exist in "
          "cf manager, but it is listed to be dropped",
          cf_id);

      if (enable_remove_orphaned_dropped_cfs) {
        delete_dropped_cf_and_flags(batch, cf_id);
      }
    }
  }

  commit(batch);
  return HA_EXIT_SUCCESS;
}

/*
  Returning true if index_id is create/delete ongoing (undergoing creation or
  marked as deleted via DROP TABLE but drop_index_thread has not wiped yet)
  or not.
 */
bool Rdb_dict_manager::is_index_operation_ongoing(
    const GL_INDEX_ID &gl_index_id, Rdb_key_def::DATA_DICT_TYPE dd_type) const {
  assert(dd_type == Rdb_key_def::DDL_DROP_INDEX_ONGOING ||
              dd_type == Rdb_key_def::DDL_CREATE_INDEX_ONGOING);

  bool found = false;
  std::string value;
  Rdb_buf_writer<Rdb_key_def::INDEX_NUMBER_SIZE * 3> key_writer;
  dump_index_id(&key_writer, dd_type, gl_index_id);

  const rocksdb::Status status = get_value(key_writer.to_slice(), &value);
  if (status.ok()) {
    found = true;
  }
  return found;
}

/*
  Adding index_id to data dictionary so that the index id is removed
  by drop_index_thread, or to track online index creation.
 */
void Rdb_dict_manager::start_ongoing_index_operation(
    rocksdb::WriteBatch *const batch, const GL_INDEX_ID &gl_index_id,
    Rdb_key_def::DATA_DICT_TYPE dd_type) const {
  assert(dd_type == Rdb_key_def::DDL_DROP_INDEX_ONGOING ||
              dd_type == Rdb_key_def::DDL_CREATE_INDEX_ONGOING);

  Rdb_buf_writer<Rdb_key_def::INDEX_NUMBER_SIZE * 3> key_writer;
  Rdb_buf_writer<Rdb_key_def::VERSION_SIZE> value_writer;

  dump_index_id(&key_writer, dd_type, gl_index_id);

  // version as needed
  if (dd_type == Rdb_key_def::DDL_DROP_INDEX_ONGOING) {
    value_writer.write_uint16(Rdb_key_def::DDL_DROP_INDEX_ONGOING_VERSION);
  } else {
    value_writer.write_uint16(Rdb_key_def::DDL_CREATE_INDEX_ONGOING_VERSION);
  }

  batch->Put(m_system_cfh, key_writer.to_slice(), value_writer.to_slice());
}

/*
  Removing index_id from data dictionary to confirm drop_index_thread
  completed dropping entire key/values of the index_id
 */
void Rdb_dict_manager::end_ongoing_index_operation(
    rocksdb::WriteBatch *const batch, const GL_INDEX_ID &gl_index_id,
    Rdb_key_def::DATA_DICT_TYPE dd_type) const {
  assert(dd_type == Rdb_key_def::DDL_DROP_INDEX_ONGOING ||
              dd_type == Rdb_key_def::DDL_CREATE_INDEX_ONGOING);

  delete_with_prefix(batch, dd_type, gl_index_id);
}

/*
  Returning true if there is no target index ids to be removed
  by drop_index_thread
 */
bool Rdb_dict_manager::is_drop_index_empty() const {
  std::unordered_set<GL_INDEX_ID> gl_index_ids;
  get_ongoing_drop_indexes(&gl_index_ids);
  return gl_index_ids.empty();
}

/*
  This function is supposed to be called by DROP TABLE. Logging messages
  that dropping indexes started, and adding data dictionary so that
  all associated indexes to be removed
 */
void Rdb_dict_manager::add_drop_table(
    std::shared_ptr<Rdb_key_def> *const key_descr, const uint32 n_keys,
    rocksdb::WriteBatch *const batch) const {
  std::unordered_set<GL_INDEX_ID> dropped_index_ids;
  for (uint32 i = 0; i < n_keys; i++) {
    dropped_index_ids.insert(key_descr[i]->get_gl_index_id());
  }

  add_drop_index(dropped_index_ids, batch);
}

/*
  Called during inplace index drop operations. Logging messages
  that dropping indexes started, and adding data dictionary so that
  all associated indexes to be removed
 */
void Rdb_dict_manager::add_drop_index(
    const std::unordered_set<GL_INDEX_ID> &gl_index_ids,
    rocksdb::WriteBatch *const batch) const {
  for (const auto &gl_index_id : gl_index_ids) {
    log_start_drop_index(gl_index_id, "Begin");
    start_drop_index(batch, gl_index_id);
  }
}

/*
  Called during inplace index creation operations. Logging messages
  that adding indexes started, and updates data dictionary with all associated
  indexes to be added.
 */
void Rdb_dict_manager::add_create_index(
    const std::unordered_set<GL_INDEX_ID> &gl_index_ids,
    rocksdb::WriteBatch *const batch) const {
  for (const auto &gl_index_id : gl_index_ids) {
    // NO_LINT_DEBUG
    sql_print_information("RocksDB: Begin index creation (%u,%u)",
                          gl_index_id.cf_id, gl_index_id.index_id);
    start_create_index(batch, gl_index_id);
  }
}

/*
  This function is supposed to be called by drop_index_thread, when it
  finished dropping any index, or at the completion of online index creation.
 */
void Rdb_dict_manager::finish_indexes_operation(
    const std::unordered_set<GL_INDEX_ID> &gl_index_ids,
    Rdb_key_def::DATA_DICT_TYPE dd_type) const {
  assert(dd_type == Rdb_key_def::DDL_DROP_INDEX_ONGOING ||
              dd_type == Rdb_key_def::DDL_CREATE_INDEX_ONGOING);

  const std::unique_ptr<rocksdb::WriteBatch> wb = begin();
  rocksdb::WriteBatch *const batch = wb.get();

  std::unordered_set<GL_INDEX_ID> incomplete_create_indexes;
  get_ongoing_create_indexes(&incomplete_create_indexes);

  for (const auto &gl_index_id : gl_index_ids) {
    if (is_index_operation_ongoing(gl_index_id, dd_type)) {
      end_ongoing_index_operation(batch, gl_index_id, dd_type);

      /*
        Remove the corresponding incomplete create indexes from data
        dictionary as well
      */
      if (dd_type == Rdb_key_def::DDL_DROP_INDEX_ONGOING) {
        if (incomplete_create_indexes.count(gl_index_id)) {
          end_ongoing_index_operation(batch, gl_index_id,
                                      Rdb_key_def::DDL_CREATE_INDEX_ONGOING);
        }
      }
    }

    if (dd_type == Rdb_key_def::DDL_DROP_INDEX_ONGOING) {
      delete_index_info(batch, gl_index_id);
    }
  }
  commit(batch);
}

/*
  This function is supposed to be called when initializing
  Rdb_dict_manager (at startup). If there is any index ids that are
  drop ongoing, printing out messages for diagnostics purposes.
 */
void Rdb_dict_manager::resume_drop_indexes() const {
  std::unordered_set<GL_INDEX_ID> gl_index_ids;
  get_ongoing_drop_indexes(&gl_index_ids);

  uint max_index_id_in_dict = 0;
  get_max_index_id(&max_index_id_in_dict);

  for (const auto &gl_index_id : gl_index_ids) {
    log_start_drop_index(gl_index_id, "Resume");
    if (max_index_id_in_dict < gl_index_id.index_id) {
      // NO_LINT_DEBUG
      sql_print_error(
          "RocksDB: Found max index id %u from data dictionary "
          "but also found dropped index id (%u,%u) from drop_index "
          "dictionary. This should never happen and is possibly a "
          "bug.",
          max_index_id_in_dict, gl_index_id.cf_id, gl_index_id.index_id);
      abort();
    }
  }
}

void Rdb_dict_manager::rollback_ongoing_index_creation() const {
  std::unordered_set<GL_INDEX_ID> gl_index_ids;

  get_ongoing_create_indexes(&gl_index_ids);
  rollback_ongoing_index_creation(gl_index_ids);
}

void Rdb_dict_manager::rollback_ongoing_index_creation(
    const std::unordered_set<GL_INDEX_ID> &gl_index_ids) const {
  const std::unique_ptr<rocksdb::WriteBatch> wb = begin();
  rocksdb::WriteBatch *const batch = wb.get();

  for (const auto &gl_index_id : gl_index_ids) {
    // NO_LINT_DEBUG
    sql_print_information("RocksDB: Removing incomplete create index (%u,%u)",
                          gl_index_id.cf_id, gl_index_id.index_id);

    start_drop_index(batch, gl_index_id);
  }

  commit(batch);
}

void Rdb_dict_manager::log_start_drop_table(
    const std::shared_ptr<Rdb_key_def> *const key_descr, const uint32 n_keys,
    const char *const log_action) const {
  for (uint32 i = 0; i < n_keys; i++) {
    log_start_drop_index(key_descr[i]->get_gl_index_id(), log_action);
  }
}

void Rdb_dict_manager::log_start_drop_index(GL_INDEX_ID gl_index_id,
                                            const char *log_action
                                                MY_ATTRIBUTE((unused))) const {
  struct Rdb_index_info index_info;
  if (!get_index_info(gl_index_id, &index_info)) {
    /*
      If we don't find the index info, it could be that it's because it was a
      partially created index that isn't in the data dictionary yet that needs
      to be rolled back.
    */
    std::unordered_set<GL_INDEX_ID> incomplete_create_indexes;
    get_ongoing_create_indexes(&incomplete_create_indexes);

    if (!incomplete_create_indexes.count(gl_index_id)) {
      /* If it's not a partially created index, something is very wrong. */
      // NO_LINT_DEBUG
      sql_print_error(
          "RocksDB: Failed to get column family info "
          "from index id (%u,%u). MyRocks data dictionary may "
          "get corrupted.",
          gl_index_id.cf_id, gl_index_id.index_id);
      abort();
    }
  }
}

bool Rdb_dict_manager::get_max_index_id(uint32_t *const index_id) const {
  bool found = false;
  std::string value;

  const rocksdb::Status status = get_value(m_key_slice_max_index_id, &value);
  if (status.ok()) {
    const uchar *const val = (const uchar *)value.c_str();
    const uint16_t version = rdb_netbuf_to_uint16(val);
    if (version == Rdb_key_def::MAX_INDEX_ID_VERSION) {
      *index_id = rdb_netbuf_to_uint32(val + Rdb_key_def::VERSION_SIZE);
      found = true;
    }
  }
  return found;
}

bool Rdb_dict_manager::update_max_index_id(rocksdb::WriteBatch *const batch,
                                           const uint32_t index_id) const {
  assert(batch != nullptr);

  uint32_t old_index_id = -1;
  if (get_max_index_id(&old_index_id)) {
    if (old_index_id > index_id) {
      // NO_LINT_DEBUG
      sql_print_error(
          "RocksDB: Found max index id %u from data dictionary "
          "but trying to update to older value %u. This should "
          "never happen and possibly a bug.",
          old_index_id, index_id);
      return true;
    }
  }

  Rdb_buf_writer<Rdb_key_def::VERSION_SIZE + Rdb_key_def::INDEX_NUMBER_SIZE>
      value_writer;
  value_writer.write_uint16(Rdb_key_def::MAX_INDEX_ID_VERSION);
  value_writer.write_uint32(index_id);

  batch->Put(m_system_cfh, m_key_slice_max_index_id, value_writer.to_slice());
  return false;
}

void Rdb_dict_manager::add_stats(
    rocksdb::WriteBatch *const batch,
    const std::vector<Rdb_index_stats> &stats) const {
  assert(batch != nullptr);

  for (const auto &it : stats) {
    Rdb_buf_writer<Rdb_key_def::INDEX_NUMBER_SIZE * 3> key_writer;
    dump_index_id(&key_writer, Rdb_key_def::INDEX_STATISTICS, it.m_gl_index_id);

    // IndexStats::materialize takes complete care of serialization including
    // storing the version
    const auto value =
        Rdb_index_stats::materialize(std::vector<Rdb_index_stats>{it});

    batch->Put(m_system_cfh, key_writer.to_slice(), value);
  }
}

Rdb_index_stats Rdb_dict_manager::get_stats(GL_INDEX_ID gl_index_id) const {
  Rdb_buf_writer<Rdb_key_def::INDEX_NUMBER_SIZE * 3> key_writer;
  dump_index_id(&key_writer, Rdb_key_def::INDEX_STATISTICS, gl_index_id);

  std::string value;
  const rocksdb::Status status = get_value(key_writer.to_slice(), &value);
  if (status.ok()) {
    std::vector<Rdb_index_stats> v;
    // unmaterialize checks if the version matches
    if (Rdb_index_stats::unmaterialize(value, &v) == 0 && v.size() == 1) {
      return v[0];
    }
  }

  return Rdb_index_stats();
}

rocksdb::Status Rdb_dict_manager::put_auto_incr_val(
    rocksdb::WriteBatchBase *batch, const GL_INDEX_ID &gl_index_id,
    ulonglong val, bool overwrite) const {
  Rdb_buf_writer<Rdb_key_def::INDEX_NUMBER_SIZE * 3> key_writer;
  dump_index_id(&key_writer, Rdb_key_def::AUTO_INC, gl_index_id);

  // Value is constructed by storing the version and the value.
  Rdb_buf_writer<RDB_SIZEOF_AUTO_INCREMENT_VERSION +
                 ROCKSDB_SIZEOF_AUTOINC_VALUE>
      value_writer;
  value_writer.write_uint16(Rdb_key_def::AUTO_INCREMENT_VERSION);
  value_writer.write_uint64(val);

  if (overwrite) {
    return batch->Put(m_system_cfh, key_writer.to_slice(),
                      value_writer.to_slice());
  }
  return batch->Merge(m_system_cfh, key_writer.to_slice(),
                      value_writer.to_slice());
}

bool Rdb_dict_manager::get_auto_incr_val(const GL_INDEX_ID &gl_index_id,
                                         ulonglong *new_val) const {
  Rdb_buf_writer<Rdb_key_def::INDEX_NUMBER_SIZE * 3> key_writer;
  dump_index_id(&key_writer, Rdb_key_def::AUTO_INC, gl_index_id);

  std::string value;
  const rocksdb::Status status = get_value(key_writer.to_slice(), &value);

  if (status.ok()) {
    const uchar *const val = reinterpret_cast<const uchar *>(value.data());

    if (rdb_netbuf_to_uint16(val) <= Rdb_key_def::AUTO_INCREMENT_VERSION) {
      *new_val = rdb_netbuf_to_uint64(val + RDB_SIZEOF_AUTO_INCREMENT_VERSION);
      return true;
    }
  }
  return false;
}

uint Rdb_seq_generator::get_and_update_next_number(
    Rdb_dict_manager *const dict) {
  assert(dict != nullptr);

  uint res;
  RDB_MUTEX_LOCK_CHECK(m_mutex);

  res = m_next_number++;

  const std::unique_ptr<rocksdb::WriteBatch> wb = dict->begin();
  rocksdb::WriteBatch *const batch = wb.get();

  assert(batch != nullptr);
  dict->update_max_index_id(batch, res);
  dict->commit(batch);

  RDB_MUTEX_UNLOCK_CHECK(m_mutex);

  return res;
}

}  // namespace myrocks
