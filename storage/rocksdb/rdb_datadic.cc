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

#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation        // gcc: Class implementation
#endif

/* This C++ file's header file */
#include "./rdb_datadic.h"

/* C++ standard header files */
#include <map>
#include <set>
#include <utility>

/* MySQL header files */
#include "./my_bit.h"

/* MyRocks header files */
#include "./ha_rocksdb_proto.h"
#include "./my_stacktrace.h"
#include "./rdb_cf_manager.h"

void key_restore(uchar *to_record, uchar *from_key, KEY *key_info,
                 uint key_length);

void write_int64(String *out, uint64 val)
{
  write_int(out, uint32(val >> 32));
  write_int(out, uint32(val & 0xffffffff));
}

void write_int(String *out, uint32 val)
{
  uint buf= htonl(val);
  out->append((char*)&buf, 4);
}

void write_short(String *out, uint16 val)
{
  uint16 buf= htons(val);
  out->append((char*)&buf, 2);
}

void write_byte(String *out, uchar val)
{
  out->append((char*)&val, 1);
}

uint32 read_int(const char **data)
{
  uint buf;
  memcpy(&buf, *data, sizeof(uint32));
  *data += sizeof(uint32);
  return ntohl(buf);
}

uint64 read_int64(const char **data)
{
  uint64 n1 = read_int(data);
  uint32 n2 = read_int(data);
  return (n1 << 32) + n2;
}

uint16 read_short(const char **data)
{
  uint16 buf;
  memcpy(&buf, *data, sizeof(uint16));
  *data += sizeof(uint16);
  return ntohs(buf);
}

uchar read_byte(const char **data)
{
  uchar buf;
  memcpy(&buf, *data, sizeof(uchar));
  *data += sizeof(uchar);
  return buf;
}

RDBSE_KEYDEF::RDBSE_KEYDEF(
  uint indexnr_arg, uint keyno_arg,
  rocksdb::ColumnFamilyHandle* cf_handle_arg,
  uint16_t index_dict_version_arg,
  uchar index_type_arg,
  uint16_t kv_format_version_arg,
  bool is_reverse_cf_arg, bool is_auto_cf_arg,
  const char* _name,
  MyRocksTablePropertiesCollector::IndexStats _stats
) :
    index_number(indexnr_arg),
    cf_handle(cf_handle_arg),
    index_dict_version(index_dict_version_arg),
    index_type(index_type_arg),
    kv_format_version(kv_format_version_arg),
    is_reverse_cf(is_reverse_cf_arg),
    is_auto_cf(is_auto_cf_arg),
    name(_name),
    stats(_stats),
    pk_part_no(NULL),
    pack_info(NULL),
    keyno(keyno_arg),
    m_key_parts(0),
    maxlength(0) // means 'not intialized'
{
  store_index_number(index_number_storage_form, index_number);
  DBUG_ASSERT(cf_handle != nullptr);
}

RDBSE_KEYDEF::RDBSE_KEYDEF(const RDBSE_KEYDEF& k) :
    index_number(k.index_number),
    cf_handle(k.cf_handle),
    is_reverse_cf(k.is_reverse_cf),
    is_auto_cf(k.is_auto_cf),
    name(k.name),
    stats(k.stats),
    pk_part_no(k.pk_part_no),
    pack_info(k.pack_info),
    keyno(k.keyno),
    m_key_parts(k.m_key_parts),
    maxlength(k.maxlength)
{
  store_index_number(index_number_storage_form, index_number);
  if (k.pack_info)
  {
    size_t size= sizeof(Field_pack_info) * k.m_key_parts;
    pack_info= (Field_pack_info*)my_malloc(size, MYF(0));
    memcpy(pack_info, k.pack_info, size);
  }

  if (k.pk_part_no)
  {
    size_t size = sizeof(uint)*m_key_parts;
    pk_part_no= (uint*)my_malloc(size, MYF(0));
    memcpy(pk_part_no, k.pk_part_no, size);
  }
}

RDBSE_KEYDEF::~RDBSE_KEYDEF()
{
  if (pk_part_no)
    my_free(pk_part_no);
  if (pack_info)
    my_free(pack_info);
}

void RDBSE_KEYDEF::setup(TABLE *tbl)
{
  /*
    set max_length based on the table. If we're unlucky, setup() may be
    called concurrently from multiple threads but that is ok because result of
    compuation is assignment of maxlength to the same value.
    ^^ TODO: is this still true? concurrent setup() calls are not safe
    anymore...
  */
  const bool secondary_key= (keyno != tbl->s->primary_key);
  if (!maxlength)
  {
    KEY *key_info= &tbl->key_info[keyno];
    KEY *pk_info=  &tbl->key_info[tbl->s->primary_key];

    if (secondary_key)
    {
      n_pk_key_parts= pk_info->actual_key_parts;
    }
    else
    {
      pk_info= NULL;
      n_pk_key_parts= 0;
    }

    // "unique" secondary keys support:
    m_key_parts= key_info->actual_key_parts;
    if (secondary_key)
    {
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
      m_key_parts += n_pk_key_parts;
    }

    if (keyno != tbl->s->primary_key)
      pk_part_no= (uint*)my_malloc(sizeof(uint)*m_key_parts, MYF(0));
    else
      pk_part_no= NULL;

    size_t size= sizeof(Field_pack_info) * m_key_parts;
    pack_info= (Field_pack_info*)my_malloc(size, MYF(0));

    size_t max_len= INDEX_NUMBER_SIZE;
    int unpack_len= 0;
    KEY_PART_INFO *key_part= key_info->key_part;
    int max_part_len= 0;
    bool simulating_extkey= false;
    uint dst_i= 0;

    uint keyno_to_set= keyno;
    uint keypart_to_set= 0;
    /* this loop also loops over the 'extended key' tail */
    for (uint src_i= 0; src_i < m_key_parts; src_i++, keypart_to_set++)
    {
      Field *field= key_part->field;

      if (simulating_extkey)
      {
        /* Check if this field is already present in the key definition */
        bool found= false;
        for (uint j= 0; j < key_info->actual_key_parts; j++)
        {
          if (field->field_index == key_info->key_part[j].field->field_index)
          {
            found= true;
            break;
          }
        }

        if (found)
        {
          key_part++;
          continue;
        }
      }

      if (field->real_maybe_null())
        max_len +=1; // NULL-byte

      pack_info[dst_i].setup(field, keyno_to_set, keypart_to_set);
      pack_info[dst_i].unpack_data_offset= unpack_len;

      if (pk_info)
      {
        pk_part_no[dst_i]= -1;
        for (uint j= 0; j < n_pk_key_parts; j++)
        {
          if (field->field_index == pk_info->key_part[j].field->field_index)
          {
            pk_part_no[dst_i]= j;
            break;
          }
        }
      }

      max_len    += pack_info[dst_i].max_image_len;
      unpack_len += pack_info[dst_i].unpack_data_len;

      max_part_len= std::max(max_part_len, pack_info[dst_i].max_image_len);

      key_part++;
      /* For "unique" secondary indexes, pretend they have "index extensions" */
      if (secondary_key && src_i+1 == key_info->actual_key_parts)
      {
        simulating_extkey= true;
        keyno_to_set= tbl->s->primary_key;
        keypart_to_set= (uint)-1;
        key_part= pk_info->key_part;
      }

      dst_i++;
    }
    m_key_parts= dst_i;
    maxlength= max_len;
    unpack_data_len= unpack_len;
  }
}


/*
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

uint RDBSE_KEYDEF::get_primary_key_tuple(TABLE *table,
                                         RDBSE_KEYDEF *pk_descr,
                                         const rocksdb::Slice *key,
                                         char *pk_buffer)
{
  uint size= 0;
  char *buf= pk_buffer;
  DBUG_ASSERT(n_pk_key_parts);

  /* Put the PK number */
  store_index_number((uchar*)buf, pk_descr->index_number);
  buf += INDEX_NUMBER_SIZE;
  size += INDEX_NUMBER_SIZE;

  const char* start_offs[MAX_REF_PARTS];
  const char* end_offs[MAX_REF_PARTS];
  int pk_key_part;
  uint i;
  Stream_reader reader(key);

  // Skip the index number
  if ((!reader.read(INDEX_NUMBER_SIZE)))
    return INVALID_LEN;

  for (i= 0; i < m_key_parts; i++)
  {
    if ((pk_key_part= pk_part_no[i]) != -1)
    {
      start_offs[pk_key_part]= reader.get_current_ptr();
    }

    bool have_value= true;
    /* It is impossible to unpack the column. Skip it. */
    if (pack_info[i].maybe_null)
    {
      const char* nullp;
      if (!(nullp= reader.read(1)))
        return INVALID_LEN;
      if (*nullp == 0)
      {
        /* This is a NULL value */
        have_value= false;
      }
      else
      {
        /* If NULL marker is not '0', it can be only '1'  */
        if (*nullp != 1)
          return INVALID_LEN;
      }
    }

    if (have_value)
    {
      Field_pack_info *fpi= &pack_info[i];
      Field *field= fpi->get_field_in_table(table);
      if (fpi->skip_func(fpi, field, &reader))
        return INVALID_LEN;
    }

    if (pk_key_part != -1)
    {
      end_offs[pk_key_part]= reader.get_current_ptr();
    }
  }

  for (i=0; i < n_pk_key_parts; i++)
  {
    uint part_size= end_offs[i] - start_offs[i];
    memcpy(buf, start_offs[i], end_offs[i] - start_offs[i]);
    buf += part_size;
    size += part_size;
  }

  return size;
}


/*
  Convert index tuple into storage (i.e. mem-comparable) format

  @detail
    Currently this is done by unpacking into table->record[0] and then
    packing index columns into storage format.

  @param pack_buffer Temporary area for packing varchar columns. Its
                     size is at least max_storage_fmt_length() bytes.
*/

uint RDBSE_KEYDEF::pack_index_tuple(const ha_rocksdb *handler, TABLE *tbl,
                                    uchar *pack_buffer, uchar *packed_tuple,
                                    const uchar *key_tuple,
                                    key_part_map keypart_map)
{
  /* We were given a record in KeyTupleFormat. First, save it to record */
  uint key_len= calculate_key_len(tbl, keyno, key_tuple, keypart_map);
  key_restore(tbl->record[0], (uchar*)key_tuple, &tbl->key_info[keyno],
              key_len);

  uint n_used_parts= my_count_bits(keypart_map);
  if (keypart_map == HA_WHOLE_KEY)
    n_used_parts= 0; // Full key is used

  /* Then, convert the record into a mem-comparable form */
  return pack_record(handler, tbl, pack_buffer, tbl->record[0], packed_tuple,
                     NULL, NULL, n_used_parts);
}


/*
  @brief
    Check if "unpack info" data includes checksum.

  @detail
    This is used only by CHECK TABLE to count the number of rows that have
    checksums. I guess this is a hackish approach.
*/

bool RDBSE_KEYDEF::unpack_info_has_checksum(const rocksdb::Slice &unpack_info)
{
  return (unpack_info.size() == CHECKSUM_CHUNK_SIZE &&
          unpack_info.data()[0]== CHECKSUM_DATA_TAG);
}

/*
  @return Number of bytes that were changed
*/
int RDBSE_KEYDEF::successor(uchar *packed_tuple, uint len)
{
  int changed= 0;
  uchar *p= packed_tuple + len - 1;
  for (; p > packed_tuple; p--)
  {
    changed++;
    if (*p != uchar(0xFF))
    {
      *p= *p + 1;
      break;
    }
    *p='\0';
  }
  return changed;
}


/*
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

  @detail
    Some callers do not need the unpack information, they can pass
    unpack_info=NULL, unpack_info_len=NULL.

  @return
    Length of the packed tuple
*/

uint RDBSE_KEYDEF::pack_record(const ha_rocksdb *handler, TABLE *tbl,
                               uchar *pack_buffer,
                               const uchar *record,
                               uchar *packed_tuple,
                               uchar *unpack_info, int *unpack_info_len,
                               uint n_key_parts,
                               uint *n_null_fields)
{
  uchar *tuple= packed_tuple;
  uchar *unpack_end= unpack_info;

  store_index_number(tuple, index_number);
  tuple += INDEX_NUMBER_SIZE;

  // If n_key_parts is 0, it means all columns.
  // The following includes the 'extended key' tail.
  // The 'extended key' includes primary key. This is done to 'uniqify'
  // non-unique indexes
  if (n_key_parts == 0 || n_key_parts == MAX_REF_PARTS)
    n_key_parts= m_key_parts;

  if (n_null_fields)
    *n_null_fields = 0;

  for (uint i=0; i < n_key_parts; i++)
  {
    Field *field= pack_info[i].get_field_in_table(tbl);

    // Old Field methods expected the record pointer to be at tbl->record[0].
    // The quick and easy way to fix this was to pass along the offset
    // for the pointer.
    my_ptrdiff_t ptr_diff= record - tbl->record[0];

    if (field->real_maybe_null())
    {
      DBUG_ASSERT((int(max_storage_fmt_length())-(tuple - packed_tuple)) >= 1);
      if (field->is_real_null(ptr_diff))
      {
        /* NULL value. store '\0' so that it sorts before non-NULL values */
        *tuple++ = 0;
        /* That's it, don't store anything else */
        if (n_null_fields)
          (*n_null_fields)++;
        continue;
      }
      else
      {
        /* Not a NULL value. Store '1' */
        *tuple++ = 1;
      }
    }

    // Set the offset for methods which do not take an offset as an argument
    DBUG_ASSERT((int(max_storage_fmt_length())-(tuple - packed_tuple)) >=
                 int(pack_info[i].max_image_len));
    field->move_field_offset(ptr_diff);
    pack_info[i].pack_func(&pack_info[i], field, pack_buffer, &tuple);

    /* Make "unpack info" to be stored in the value */
    if (unpack_end && pack_info[i].make_unpack_info_func)
    {
      pack_info[i].make_unpack_info_func(&pack_info[i], field, unpack_end);
      unpack_end += pack_info[i].unpack_data_len;
    }
    field->move_field_offset(-ptr_diff);
  }

  if (unpack_info_len)
  {
    if (handler->should_store_checksums())
    {
      uint32_t key_crc32= crc32(0, packed_tuple, tuple - packed_tuple);
      uint32_t val_crc32= crc32(0, unpack_info, unpack_end - unpack_info);

      *unpack_end++ = CHECKSUM_DATA_TAG;

      store_big_uint4(unpack_end, key_crc32);
      unpack_end += CHECKSUM_SIZE;

      store_big_uint4(unpack_end, val_crc32);
      unpack_end += CHECKSUM_SIZE;
    }
    *unpack_info_len= unpack_end - unpack_info;
  }

  DBUG_ASSERT((int(max_storage_fmt_length())-(tuple - packed_tuple)) >= 0);
  return tuple - packed_tuple;
}


void pack_with_make_sort_key(Field_pack_info *fpi, Field *field,
                             uchar *buf __attribute__((unused)),
                             uchar **dst)
{
  const int max_len= fpi->max_image_len;
  field->make_sort_key(*dst, max_len);
  *dst += max_len;
}

/*
  Compares two keys without unpacking

  @detail
  @return
    0 - Ok. column_index is the index of the first column which is different.
          -1 if two kes are equal
    1 - Data format error.
*/
int RDBSE_KEYDEF::compare_keys(
  const rocksdb::Slice *key1,
  const rocksdb::Slice *key2,
  std::size_t* column_index
)
{
  // the caller should check the return value and
  // not rely on column_index being valid
  *column_index = 0xbadf00d;

  Stream_reader reader1(key1);
  Stream_reader reader2(key2);

  // Skip the index number
  if ((!reader1.read(INDEX_NUMBER_SIZE)))
    return 1;

  if ((!reader2.read(INDEX_NUMBER_SIZE)))
    return 1;

  for (uint i= 0; i < m_key_parts ; i++)
  {
    Field_pack_info *fpi= &pack_info[i];
    if (fpi->maybe_null)
    {
      auto nullp1= reader1.read(1);
      auto nullp2= reader2.read(1);
      if (nullp1 == NULL || nullp2 == NULL)
        return 1; //error

      if (*nullp1 != *nullp2)
      {
        *column_index = i;
        return 0;
      }

      if (*nullp1 == 0)
      {
        /* This is a NULL value */
        continue;
      }
    }

    auto before_skip1 = reader1.get_current_ptr();
    auto before_skip2 = reader2.get_current_ptr();
    assert(fpi->skip_func);
    if (fpi->skip_func(fpi, NULL, &reader1))
      return 1;
    if (fpi->skip_func(fpi, NULL, &reader2))
      return 1;
    auto size1 = reader1.get_current_ptr() - before_skip1;
    auto size2 = reader2.get_current_ptr() - before_skip2;
    if (size1 != size2)
    {
      *column_index = i;
      return 0;
    }

    if (memcmp(before_skip1, before_skip2, size1) != 0) {
      *column_index = i;
      return 0;
    }
  }

  *column_index = m_key_parts;
  return 0;

}


/*
  @brief
    Given a zero-padded key, determine its real key length

  @detail
    Fixed-size skip functions just read.
*/

size_t RDBSE_KEYDEF::key_length(TABLE *table, const rocksdb::Slice &key)
{
  Stream_reader reader(&key);

  if ((!reader.read(INDEX_NUMBER_SIZE)))
    return size_t(-1);

  for (uint i= 0; i < m_key_parts ; i++)
  {
    Field_pack_info *fpi= &pack_info[i];
    Field *field= fpi->get_field_in_table(table);
    if (fpi->skip_func(fpi, field, &reader))
      return size_t(-1);
  }
  return key.size() - reader.remaining_bytes();
}


/*
  Take mem-comparable form and unpack_info and unpack it to Table->record

  @detail
    not all indexes support this

  @return
    0 - Ok
    1 - Data format error.
*/

int RDBSE_KEYDEF::unpack_record(const ha_rocksdb *handler, TABLE *table,
                                uchar *buf, const rocksdb::Slice *packed_key,
                                const rocksdb::Slice *unpack_info)
{
  Stream_reader reader(packed_key);
  const uchar * const unpack_ptr= (const uchar*)unpack_info->data();

  // Old Field methods expected the record pointer to be at tbl->record[0].
  // The quick and easy way to fix this was to pass along the offset
  // for the pointer.
  my_ptrdiff_t ptr_diff= buf - table->record[0];

  if (unpack_info->size() != unpack_data_len &&
      unpack_info->size() != unpack_data_len + CHECKSUM_CHUNK_SIZE)
  {
    return 1;
  }

  // Skip the index number
  if ((!reader.read(INDEX_NUMBER_SIZE)))
    return (uint)-1;

  for (uint i= 0; i < m_key_parts ; i++)
  {
    Field_pack_info *fpi= &pack_info[i];
    Field *field= fpi->get_field_in_table(table);

    if (fpi->unpack_func)
    {
      /* It is possible to unpack this column. Do it. */

      if (fpi->maybe_null)
      {
        const char* nullp;
        if (!(nullp= reader.read(1)))
          return 1;
        if (*nullp == 0)
        {
          field->set_null(ptr_diff);
          continue;
        }
        else if (*nullp == 1)
          field->set_notnull(ptr_diff);
        else
          return 1;
      }

      // Set the offset for methods which do not take an offset as an argument
      field->move_field_offset(ptr_diff);
      int res= fpi->unpack_func(fpi, field, &reader,
                            unpack_ptr + fpi->unpack_data_offset);
      field->move_field_offset(-ptr_diff);

      if (res)
        return 1;
    }
    else
    {
      /* It is impossible to unpack the column. Skip it. */
      if (fpi->maybe_null)
      {
        const char* nullp;
        if (!(nullp= reader.read(1)))
          return 1;
        if (*nullp == 0)
        {
          /* This is a NULL value */
          continue;
        }
        /* If NULL marker is not '0', it can be only '1'  */
        if (*nullp != 1)
          return 1;
      }
      if (fpi->skip_func(fpi, field, &reader))
        return 1;
    }
  }

  /*
    Check checksum values if present
  */
  if (unpack_info->size() == CHECKSUM_CHUNK_SIZE)
  {
    Stream_reader unp_reader(unpack_info);
    if (unp_reader.read(1)[0] == CHECKSUM_DATA_TAG)
    {
      if (handler->verify_checksums)
      {
        uint32_t stored_key_chksum;
        uint32_t stored_val_chksum;
        stored_key_chksum= read_big_uint4((const uchar*)unp_reader.read(CHECKSUM_SIZE));
        stored_val_chksum= read_big_uint4((const uchar*)unp_reader.read(CHECKSUM_SIZE));

        uint32_t computed_key_chksum=
          crc32(0, (const uchar*)packed_key->data(), packed_key->size());
        uint32_t computed_val_chksum=
          crc32(0, (const uchar*) unpack_info->data(),
                unpack_info->size() - CHECKSUM_CHUNK_SIZE);

        DBUG_EXECUTE_IF("myrocks_simulate_bad_key_checksum1",
                        stored_key_chksum++;);

        if (stored_key_chksum != computed_key_chksum)
        {
          report_checksum_mismatch(this, true, packed_key->data(),
                                   packed_key->size());
          return HA_ERR_INTERNAL_ERROR;
        }

        if (stored_val_chksum != computed_val_chksum)
        {
          report_checksum_mismatch(this, false, unpack_info->data(),
                                   unpack_info->size() - CHECKSUM_CHUNK_SIZE);
          return HA_ERR_INTERNAL_ERROR;
        }

      }
      else
      {
        /* The checksums are present but we are not checking checksums */
      }
    }
  }

  if (reader.remaining_bytes())
    return HA_ERR_INTERNAL_ERROR;

  return 0;
}


bool RDBSE_KEYDEF::can_unpack(uint kp) const
{
  return (pack_info[kp].unpack_func != NULL);
}

///////////////////////////////////////////////////////////////////////////////////////////
// Field_pack_info
///////////////////////////////////////////////////////////////////////////////////////////

int skip_max_length(Field_pack_info *fpi, Field *field, Stream_reader *reader)
{
  if (!reader->read(fpi->max_image_len))
    return 1;
  return 0;
}


int unpack_integer(Field_pack_info *fpi, Field *field,
                   Stream_reader *reader, const uchar *unpack_info)
{
  const int length= fpi->max_image_len;
  uchar *to= field->ptr;
  const uchar *from;

  if (!(from= (const uchar*)reader->read(length)))
    return 1; /* Mem-comparable image doesn't have enough bytes */

#ifdef WORDS_BIGENDIAN
  {
    if (((Field_num*)field)->unsigned_flag)
      to[0]= from[0];
    else
      to[0]= (char)(from[0] ^ 128); // Reverse the sign bit.
    memcpy(to + 1, from + 1, length - 1);
  }
#else
  {
    const int sign_byte= from[0];
    if (((Field_num*)field)->unsigned_flag)
      to[length - 1]= sign_byte;
    else
      to[length - 1]= static_cast<char>(sign_byte ^ 128); // Reverse the sign bit.
    for (int i= 0, j= length - 1; i < length-1; ++i, --j)
      to[i]= from[j];
  }
#endif
  return 0;
}


/*
  Unpack by doing the reverse action to Field_newdate::make_sort_key.
*/

static
int unpack_newdate(Field_pack_info *fpi, Field *field,
                   Stream_reader *reader, const uchar *unpack_info)
{
  const char* from;
  DBUG_ASSERT(fpi->max_image_len == 3);
  DBUG_ASSERT(fpi->field_data_offset == 0);

  if (!(from= reader->read(3)))
    return 1; /* Mem-comparable image doesn't have enough bytes */

  field->ptr[0]= from[2];
  field->ptr[1]= from[1];
  field->ptr[2]= from[0];
  return 0;
}


/*
  Unpack the string by copying it over.
  This is for BINARY(n) where the value occupies the whole length.
*/

int unpack_binary_str(Field_pack_info *fpi, Field *field,
                      Stream_reader *reader,
                      const uchar *unpack_info)
{
  const char* from;
  if (!(from= reader->read(fpi->max_image_len)))
    return 1; /* Mem-comparable image doesn't have enough bytes */

  memcpy(field->ptr + fpi->field_data_offset, from, fpi->max_image_len);
  return 0;
}


/*
  For UTF-8, we need to convert 2-byte wide-character entities back into
  UTF8 sequences.
*/

int unpack_utf8_str(Field_pack_info *fpi, Field *field,
                    Stream_reader *reader,
                    const uchar *unpack_info)
{
  CHARSET_INFO *cset= (CHARSET_INFO*)field->charset();
  const uchar *src;
  if (!(src= (const uchar*)reader->read(fpi->max_image_len)))
    return 1; /* Mem-comparable image doesn't have enough bytes */

  const uchar *src_end= src + fpi->max_image_len;
  uchar *dst= field->ptr + fpi->field_data_offset;
  uchar *dst_end= dst + field->pack_length();

  while (src < src_end)
  {
    my_wc_t wc= (src[0] <<8) | src[1];
    src += 2;
    int res= cset->cset->wc_mb(cset, wc, dst, dst_end);
    DBUG_ASSERT(res > 0 && res <=3);
    if (res < 0)
      return 1;
    dst += res;
  }
  return 0;
}


/*
  (ESCAPE_LENGTH-1) must be an even number so that pieces of lines are not
  split in the middle of an UTF-8 character.
*/
const uint ESCAPE_LENGTH=9;


void pack_with_varchar_encoding(Field_pack_info *fpi, Field *field, uchar *buf,
                                uchar **dst)
{
  /*
    Use a flag byte every Nth byte. Set it to (255 - #pad) where #pad is 0
    when the var length field filled all N-1 previous bytes and #pad is
    otherwise the number of padding bytes used.

    If N=8 and the field is:
    * 3 bytes (1, 2, 3) this is encoded as: 1, 2, 3, 0, 0, 0, 0, 251
    * 4 bytes (1, 2, 3, 0) this is encoded as: 1, 2, 3, 0, 0, 0, 0, 252
    And the 4 byte string compares as greater than the 3 byte string
  */
  const CHARSET_INFO *charset= field->charset();
  Field_varstring *field_var= (Field_varstring*)field;

  size_t value_length= (field_var->length_bytes == 1) ?
                       (uint) *field->ptr :
                       uint2korr(field->ptr);
  size_t xfrm_len;
  xfrm_len= charset->coll->strnxfrm(charset,
                                    buf, fpi->max_image_len,
                                    field_var->char_length(),
                                    field_var->ptr + field_var->length_bytes,
                                    value_length,
                                    0);

  /* Got a mem-comparable image in 'buf'. Now, produce varlength encoding */

  size_t encoded_size= 0;
  uchar *ptr= *dst;
  while (1)
  {
    size_t copy_len= std::min((size_t)ESCAPE_LENGTH-1, xfrm_len);
    size_t padding_bytes= ESCAPE_LENGTH - 1 - copy_len;
    memcpy(ptr, buf, copy_len);
    ptr += copy_len;
    buf += copy_len;
    // pad with zeros if necessary;
    for (size_t idx= 0; idx < padding_bytes; idx++)
      *(ptr++)= 0;
    *(ptr++) = 255 - padding_bytes;

    xfrm_len     -= copy_len;
    encoded_size += ESCAPE_LENGTH;
    if (padding_bytes !=0)
      break;
  }
  *dst += encoded_size;
}


bool is_myrocks_collation_supported(Field *field)
{
  enum_field_types type= field->real_type();
  /* Handle [VAR](CHAR|BINARY) or TEXT|BLOB */
  if (type == MYSQL_TYPE_VARCHAR || type == MYSQL_TYPE_STRING ||
      type == MYSQL_TYPE_BLOB)
  {
    return MYROCKS_INDEX_COLLATIONS.find(field->charset()) !=
      MYROCKS_INDEX_COLLATIONS.end();
  }
  return true;
}


int unpack_binary_or_utf8_varchar(Field_pack_info *fpi, Field *field,
                                  Stream_reader *reader,
                                  const uchar *unpack_info)
{
  const uchar *ptr;
  size_t len= 0;
  bool finished= false;
  uchar *dst= field->ptr + fpi->field_data_offset;
  Field_varstring* field_var= (Field_varstring*)field;
  size_t dst_len= field_var->pack_length() - field_var->length_bytes; // How much we can unpack
  uchar *dst_end= dst + dst_len;

  /* Decode the length-emitted encoding here */
  while ((ptr= (const uchar*)reader->read(ESCAPE_LENGTH)))
  {
    /*
      ESCAPE_LENGTH-th byte has:
      Set it to (255 - #pad) where #pad is 0 when the var length field filled
      all N-1 previous bytes and #pad is otherwise the number of padding
      bytes used.
    */
    uchar pad= 255 - ptr[ESCAPE_LENGTH - 1]; //number of padding bytes
    uchar used_bytes= ESCAPE_LENGTH - 1 - pad;

    if (used_bytes > ESCAPE_LENGTH - 1)
      return 1; /* cannot store that much, invalid data */

    if (dst_len < used_bytes)
    {
      /* Encoded index tuple is longer than the size in the record buffer? */
      return 1;
    }

    /*
      Now, we need to decode used_bytes of data and append them to the value.
    */
    if (fpi->varchar_charset == &my_charset_utf8_bin)
    {
      if (used_bytes & 1)
      {
        /*
          UTF-8 characters are encoded into two-byte entities. There is no way
          we can an odd number of bytes after encoding.
        */
        return 1;
      }

      const uchar *src= ptr;
      const uchar *src_end= ptr + used_bytes;
      while (src < src_end)
      {
        my_wc_t wc= (src[0] <<8) | src[1];
        src += 2;
        const CHARSET_INFO *cset= fpi->varchar_charset;
        int res= cset->cset->wc_mb(cset, wc, dst, dst_end);
        DBUG_ASSERT(res > 0 && res <=3);
        if (res < 0)
          return 1;
        dst += res;
        len += res;
        dst_len -= res;
      }
    }
    else
    {
      memcpy(dst, ptr, used_bytes);
      dst += used_bytes;
      dst_len -= used_bytes;
      len += used_bytes;
    }

    if (used_bytes < ESCAPE_LENGTH - 1)
    {
      finished= true;
      break;
    }
  }

  if (!finished)
    return 1;

  /* Save the length */
  if (field_var->length_bytes == 1)
  {
    field->ptr[0]= len;
  }
  else
  {
    DBUG_ASSERT(field_var->length_bytes == 2);
    int2store(field->ptr, len);
  }
  return 0;
}


int skip_variable_length(Field_pack_info *fpi, Field *field, Stream_reader *reader)
{
  const uchar *ptr;
  bool finished= false;

  size_t dst_len; /* How much data can be there */
  if (field)
  {
    Field_varstring* field_var= (Field_varstring*)field;
    dst_len= field_var->pack_length() - field_var->length_bytes;
  }
  else
    dst_len= UINT_MAX;

  /* Decode the length-emitted encoding here */
  while ((ptr= (const uchar*)reader->read(ESCAPE_LENGTH)))
  {
    /*
      ESCAPE_LENGTH-th byte has:
      Set it to (255 - #pad) where #pad is 0 when the var length field filled
      all N-1 previous bytes and #pad is otherwise the number of padding
      bytes used.
    */
    uchar pad= 255 - ptr[ESCAPE_LENGTH - 1]; //number of padding bytes
    uchar used_bytes= ESCAPE_LENGTH - 1 - pad;

    if (used_bytes > ESCAPE_LENGTH - 1 || used_bytes > dst_len)
      return 1; /* cannot store that much, invalid data */

    if (used_bytes < ESCAPE_LENGTH - 1)
    {
      finished= true;
      break;
    }
    dst_len -= used_bytes;
  }

  if (!finished)
    return 1;

  return 0;
}


/*
  Setup index-only read of a field

  @param
    field  IN  field to be packed/un-packed

  @return
    TRUE  -  Field can be read with index-only reads
    FALSE -  Otherwise
*/

bool Field_pack_info::setup(Field *field, uint keynr_arg, uint key_part_arg)
{
  int res= false;
  enum_field_types type= field->real_type();

  keynr= keynr_arg;
  key_part= key_part_arg;

  maybe_null= field->real_maybe_null();
  make_unpack_info_func= NULL;
  unpack_func= NULL;
  unpack_data_len= 0;
  field_data_offset= 0;

  /* Calculate image length. By default, is is pack_length() */
  max_image_len= field->pack_length();

  skip_func= skip_max_length;
  pack_func= pack_with_make_sort_key;

  make_unpack_info_func= NULL;
  unpack_data_len= 0;

  switch (type) {
    case MYSQL_TYPE_LONGLONG:
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_INT24:
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_TINY:
      unpack_func= unpack_integer;
      return true;

    case MYSQL_TYPE_DATETIME2:
    case MYSQL_TYPE_TIMESTAMP2:
      /* These are packed with Field_temporal_with_date_and_timef::make_sort_key */
    case MYSQL_TYPE_TIME2: /* TIME is packed with Field_timef::make_sort_key */
    case MYSQL_TYPE_YEAR: /* YEAR is packed with  Field_tiny::make_sort_key */
      /* Everything that comes here is packed with just a memcpy(). */
      unpack_func= unpack_binary_str;
      return true;

    case MYSQL_TYPE_NEWDATE:
      /*
        This is packed by Field_newdate::make_sort_key. It assumes the data is
        3 bytes, and packing is done by swapping the byte order (for both big-
        and little-endian)
      */
      unpack_func= unpack_newdate;
      return true;
    default:
      break;
  }

  /* Handle [VAR](CHAR|BINARY) */

  if (type == MYSQL_TYPE_VARCHAR || type == MYSQL_TYPE_STRING)
  {
    /*
      For CHAR-based columns, check how strxfrm image will take.
      field->field_length = field->char_length() * cs->mbmaxlen.
    */
    const CHARSET_INFO *cs= field->charset();
    max_image_len= cs->coll->strnxfrmlen(cs, field->field_length);
  }
  const bool is_varchar= (type == MYSQL_TYPE_VARCHAR);
  const CHARSET_INFO *cs= field->charset();
  if (is_varchar)
  {
    varchar_charset= cs;
    field_data_offset= ((Field_varstring*)field)->length_bytes;
    skip_func= skip_variable_length;
    pack_func= pack_with_varchar_encoding;
    max_image_len= (max_image_len/(ESCAPE_LENGTH-1) + 1) * ESCAPE_LENGTH;
  }

  if (type == MYSQL_TYPE_VARCHAR || type == MYSQL_TYPE_STRING)
  {
    if (cs == &my_charset_bin ||
        cs == &my_charset_latin1_bin)
    {
      unpack_func= is_varchar? unpack_binary_or_utf8_varchar : unpack_binary_str;
      res= true;
    }
    else if(cs == &my_charset_utf8_bin)
    {
      unpack_func= is_varchar? unpack_binary_or_utf8_varchar : unpack_utf8_str;
      res= true;
    }
  }
  return res;
}


Field *Field_pack_info::get_field_in_table(TABLE *tbl)
{
  return tbl->key_info[keynr].key_part[key_part].field;
}


void report_checksum_mismatch(RDBSE_KEYDEF *kd, bool is_key,
                              const char *data, size_t data_size)
{
  char buf[1024];
  sql_print_error("Checksum mismatch in %s of key-value pair for index 0x%x",
                   is_key? "key" : "value",
                   kd->get_index_number());
  hexdump_value(buf, sizeof(buf), rocksdb::Slice(data, data_size));
  sql_print_error("Data with incorrect checksum (%ld bytes): %s",
                  (long)data_size, buf);

  my_error(ER_INTERNAL_ERROR, MYF(0), "Record checksum mismatch");
}


#if 0
void _rdbse_store_blob_length(uchar *pos,uint pack_length,uint length)
{
  switch (pack_length) {
  case 1:
    *pos= (uchar) length;
    break;
  case 2:
    int2store(pos,length);
    break;
  case 3:
    int3store(pos,length);
    break;
  case 4:
    int4store(pos,length);
  default:
    break;
  }
  return;
}
#endif


///////////////////////////////////////////////////////////////////////////////////////////
// Table_ddl_manager
///////////////////////////////////////////////////////////////////////////////////////////
RDBSE_TABLE_DEF::~RDBSE_TABLE_DEF()
{
  auto ddl_manager= get_ddl_manager();
  mysql_mutex_destroy(&mutex);
  /* Don't free key definitions */
  if (key_descr)
  {
    for (uint i= 0; i < n_keys; i++) {
      if (ddl_manager && key_descr[i]) {
        ddl_manager->erase_index_num(key_descr[i]->get_gl_index_id());
      }
      delete key_descr[i];
    }
    delete[] key_descr;
  }
}

/*
  Put table definition DDL entry. Actual write is done at Dict_manager::commit

  We write
    dbname.tablename -> version + {key_entry, key_entry, key_entry, ... }

  Where key entries are a tuple of
    ( cf_id, index_nr )
*/

bool RDBSE_TABLE_DEF::put_dict(Dict_manager* dict, rocksdb::WriteBatch *batch,
                               uchar *key, size_t keylen)
{
  StringBuffer<8 * RDBSE_KEYDEF::PACKED_SIZE> indexes;
  indexes.alloc(RDBSE_KEYDEF::VERSION_SIZE+n_keys*RDBSE_KEYDEF::PACKED_SIZE*2);
  write_short(&indexes, RDBSE_KEYDEF::DDL_ENTRY_INDEX_VERSION);

  for (uint i=0; i < n_keys; i++)
  {
    RDBSE_KEYDEF* kd = key_descr[i];

    uchar flags =
      (kd->is_reverse_cf ? RDBSE_KEYDEF::REVERSE_CF_FLAG : 0) |
      (kd->is_auto_cf ? RDBSE_KEYDEF::AUTO_CF_FLAG : 0);

    uint cf_id= kd->get_cf()->GetID();
    /*
      If cf_id already exists, cf_flags must be the same.
      To prevent race condition, reading/modifying/committing CF flags
      need to be protected by mutex (dict_manager->lock()).
      When RocksDB supports transaction with pessimistic concurrency
      control, we can switch to use it and removing mutex.
    */
    uint existing_cf_flags;
    if (dict->get_cf_flags(cf_id, &existing_cf_flags))
    {
      if (existing_cf_flags != flags)
      {
        my_printf_error(ER_UNKNOWN_ERROR,
                        "Column Family Flag is different from existing flag. "
                        "Assign a new CF flag, or do not change existing "
                        "CF flag.", MYF(0));
        return true;
      }
    }
    else
    {
      dict->add_cf_flags(batch, cf_id, flags);
    }

    write_int(&indexes, cf_id);
    write_int(&indexes, kd->index_number);
    dict->add_or_update_index_cf_mapping(batch, kd->index_type,
                                         kd->kv_format_version,
                                         kd->index_number, cf_id);
  }

  rocksdb::Slice skey((char*)key, keylen);
  rocksdb::Slice svalue(indexes.c_ptr(), indexes.length());

  dict->Put(batch, skey, svalue);
  return false;
}


uchar* Table_ddl_manager::get_hash_key(RDBSE_TABLE_DEF *rec, size_t *length,
                                       my_bool not_used __attribute__((unused)))
{
  *length= rec->dbname_tablename.length();
  return (uchar*) rec->dbname_tablename.c_ptr();
}


void Table_ddl_manager::free_hash_elem(void* data)
{
  RDBSE_TABLE_DEF* elem= (RDBSE_TABLE_DEF*)data;
  delete elem;
}

void Table_ddl_manager::erase_index_num(GL_INDEX_ID gl_index_id)
{
  index_num_to_keydef.erase(gl_index_id);
}

typedef std::pair<std::string, bool>                       validate_tbl_info;
typedef std::map<std::string, std::set<validate_tbl_info>> validate_tbl_list;

/*
  Get a list of tables that we expect to have .frm files for.  This will use the
  information just read from the RocksDB data dictionary.
*/
static int add_table_to_validate_list(
    void*            arg,
    RDBSE_TABLE_DEF* tdef)
{
  validate_tbl_list* table_list = reinterpret_cast<validate_tbl_list*>(arg);
  StringBuffer<256>  dbname_buff;
  StringBuffer<256>  tablename_buff;
  StringBuffer<256>  partition_buff;

  /* Parse the dbname_tablename for the different elements */
  if (rocksdb_split_normalized_tablename(tdef->dbname_tablename.ptr(),
                                         &dbname_buff, &tablename_buff,
                                         &partition_buff) != 0)
  {
    return 1;
  }

  /* Add the database/table into the list */
  std::string dbname = std::string(dbname_buff.ptr());
  std::string tablename = std::string(tablename_buff.ptr());
  bool        is_partition = partition_buff.length() != 0;
  (*table_list)[dbname].insert(validate_tbl_info(tablename, is_partition));

  return 0;
}

/*
  Access the .frm file for this dbname/tablename and see if it is a RocksDB
  table (or partition table).
*/
static bool check_frm_file(
    const std::string& fullpath,
    const std::string& dbname,
    const std::string& tablename,
    validate_tbl_list* table_list,
    bool*              has_errors)
{
  /* Check this .frm file to see what engine it uses */
  String fullfilename(fullpath.c_str(), &my_charset_bin);
  fullfilename.append("/");
  fullfilename.append(tablename.c_str());
  fullfilename.append(".frm");

  /*
    This function will return the legacy_db_type of the table.  Currently
    it does not reference the first parameter (THD* thd), but if it ever
    did in the future we would need to make a version that does it without
    the connection handle as we don't have one here.
  */
  enum legacy_db_type eng_type;
  frm_type_enum type = dd_frm_type(nullptr, fullfilename.c_ptr(), &eng_type);
  if (type == FRMTYPE_ERROR)
  {
    sql_print_warning("RocksDB: Failed to open/read .from file: %s",
        fullfilename.ptr());
    return false;
  }

  if (type == FRMTYPE_TABLE)
  {
    /* For a RocksDB table do we have a reference in the data dictionary? */
    if (eng_type == DB_TYPE_ROCKSDB)
    {
      /*
        Attempt to remove the table entry from the list of tables.  If this
        fails then we know we had a .frm file that wasn't registered in RocksDB.
      */
      validate_tbl_info element(tablename, false);
      if (table_list->count(dbname) == 0 ||
          (*table_list)[dbname].erase(element) == 0)
      {
        sql_print_warning("RocksDB: Schema mismatch - "
                          "A .frm file exists for table %s.%s, "
                          "but that table is not registered in RocksDB",
                          dbname.c_str(), tablename.c_str());
        *has_errors = true;
      }
    }
    else if (eng_type == DB_TYPE_PARTITION_DB)
    {
      /*
        For partition tables, see if it is in the table_list as a partition,
        but don't generate an error if it isn't there - we don't know that the
        .frm is for RocksDB.
      */
      if (table_list->count(dbname) > 0)
        (*table_list)[dbname].erase(validate_tbl_info(tablename, true));
    }
  }

  return true;
}

/* Scan the database subdirectory for .frm files */
static bool scan_for_frms(
    const std::string& datadir,
    const std::string& dbname,
    validate_tbl_list* table_list,
    bool*              has_errors)
{
  bool              result = true;
  std::string       fullpath = datadir + dbname;
  struct st_my_dir* dir_info = my_dir(fullpath.c_str(), MYF(MY_DONT_SORT));

  /* Access the directory */
  if (dir_info == nullptr)
  {
    sql_print_warning("RocksDB: Could not open database directory: %s",
        fullpath.c_str());
    return false;
  }

  /* Scan through the files in the directory */
  struct fileinfo* file_info = dir_info->dir_entry;
  for (uint ii = 0; ii < dir_info->number_off_files; ii++, file_info++)
  {
    /* Find .frm files */
    const char* ext = strrchr(file_info->name, '.');
    if (ext != nullptr && strcmp(ext, ".frm") == 0)
    {
      std::string tablename = std::string(file_info->name,
                                         ext - file_info->name);

      /* Check to see if the .frm file is from RocksDB */
      if (!check_frm_file(fullpath, dbname, tablename, table_list,
                          has_errors))
      {
        result = false;
        break;
      }
    }
  }

  /* Remove any databases who have no more tables listed */
  if (table_list->count(dbname) == 1 && (*table_list)[dbname].size() == 0)
    table_list->erase(dbname);

  /* Release the directory entry */
  my_dirend(dir_info);

  return result;
}

/*
  Scan the datadir for all databases (subdirectories) and get a list of .frm
  files they contain
*/
static bool compare_to_actual_tables(
    const std::string& datadir,
    validate_tbl_list* table_list,
    bool*              has_errors)
{
  bool              result = true;
  struct st_my_dir* dir_info;
  struct fileinfo*  file_info;

  dir_info = my_dir(datadir.c_str(), MYF(MY_DONT_SORT | MY_WANT_STAT));
  if (dir_info == nullptr)
  {
    sql_print_warning("RocksDB: could not open datadir: %s", datadir.c_str());
    return false;
  }

  file_info = dir_info->dir_entry;
  for (uint ii = 0; ii < dir_info->number_off_files; ii++, file_info++)
  {
    /* Ignore files/dirs starting with '.' */
    if (file_info->name[0] == '.')
      continue;

    /* Ignore all non-directory files */
    if (!MY_S_ISDIR(file_info->mystat->st_mode))
      continue;

    /* Scan all the .frm files in the directory */
    if (!scan_for_frms(datadir, file_info->name, table_list, has_errors))
    {
      result = false;
      break;
    }
  }

  /* Release the directory info */
  my_dirend(dir_info);

  return result;
}

/*
  Validate that all the tables in the RocksDB database dictionary match the .frm
  files in the datdir
*/
bool Table_ddl_manager::validate_schemas(void)
{
  bool              has_errors = false;
  std::string       datadir = std::string(mysql_real_data_home);
  validate_tbl_list table_list;

  /* Get the list of tables from the database dictionary */
  if (scan(&table_list, add_table_to_validate_list) != 0)
    return false;

  /* Compare that to the list of actual .frm files */
  if (!compare_to_actual_tables(datadir, &table_list, &has_errors))
    return false;

  /*
    Any tables left in the tables list are ones that are registered in RocksDB
    but don't have .frm files.
  */
  for (const auto& db : table_list)
  {
    for (const auto& table : db.second)
    {
      sql_print_warning("RocksDB: Schema mismatch - "
                        "Table %s.%s is registered in RocksDB "
                        "but does not have a .frm file", db.first.c_str(),
                        table.first.c_str());
      has_errors = true;
    }
  }

  return !has_errors;
}

bool Table_ddl_manager::init(Dict_manager *dict_arg,
                             Column_family_manager *cf_manager,
                             uint32_t validate_tables)
{
  dict= dict_arg;
  mysql_rwlock_init(0, &rwlock);
  (void) my_hash_init(&ddl_hash, /*system_charset_info*/&my_charset_bin, 32,0,0,
                      (my_hash_get_key) Table_ddl_manager::get_hash_key,
                      Table_ddl_manager::free_hash_elem,
                      0);

  /* Read the data dictionary and populate the hash */
  uchar ddl_entry[RDBSE_KEYDEF::INDEX_NUMBER_SIZE];
  store_index_number(ddl_entry, RDBSE_KEYDEF::DDL_ENTRY_INDEX_START_NUMBER);
  rocksdb::Slice ddl_entry_slice((char*)ddl_entry,
                                 RDBSE_KEYDEF::INDEX_NUMBER_SIZE);

  /* Reading data dictionary should always skip bloom filter */
  rocksdb::Iterator* it= dict->NewIterator();
  int i= 0;

  uint max_index_id_in_dict= 0;
  dict->get_max_index_id(&max_index_id_in_dict);

  for (it->Seek(ddl_entry_slice); it->Valid(); it->Next())
  {
    const char *ptr;
    const char *ptr_end;
    rocksdb::Slice key= it->key();
    rocksdb::Slice val= it->value();

    if (key.size() >= RDBSE_KEYDEF::INDEX_NUMBER_SIZE &&
        memcmp(key.data(), ddl_entry, RDBSE_KEYDEF::INDEX_NUMBER_SIZE))
      break;

    if (key.size() <= RDBSE_KEYDEF::INDEX_NUMBER_SIZE)
    {
      sql_print_error("RocksDB: Table_store: key has length %d (corruption?)",
                      (int)key.size());
      return true;
    }

    RDBSE_TABLE_DEF *tdef= new RDBSE_TABLE_DEF;
    tdef->dbname_tablename.append(key.data() + RDBSE_KEYDEF::INDEX_NUMBER_SIZE,
                                  key.size() - RDBSE_KEYDEF::INDEX_NUMBER_SIZE);

    // Now, read the DDLs.
    int real_val_size= val.size() - RDBSE_KEYDEF::VERSION_SIZE;
    if (real_val_size % RDBSE_KEYDEF::PACKED_SIZE*2)
    {
      sql_print_error("RocksDB: Table_store: invalid keylist for table %s",
                      tdef->dbname_tablename.c_ptr_safe());
      return true;
    }
    tdef->n_keys= real_val_size / (RDBSE_KEYDEF::PACKED_SIZE*2);
    if (!(tdef->key_descr= new RDBSE_KEYDEF*[tdef->n_keys]))
      return true;

    memset(tdef->key_descr, 0, sizeof(RDBSE_KEYDEF*) * tdef->n_keys);

    ptr= (char*)val.data();
    int version= read_short(&ptr);
    if (version != RDBSE_KEYDEF::DDL_ENTRY_INDEX_VERSION)
    {
      sql_print_error("RocksDB: DDL ENTRY Version was not expected."
                      "Expected: %d, Actual: %d",
                      RDBSE_KEYDEF::DDL_ENTRY_INDEX_VERSION, version);
      return true;
    }
    ptr_end= ptr + real_val_size;
    for (uint keyno=0; ptr < ptr_end; keyno++)
    {
      GL_INDEX_ID gl_index_id;
      gl_index_id.cf_id= read_int(&ptr);
      gl_index_id.index_id= read_int(&ptr);
      uint16 index_dict_version= 0;
      uchar index_type= 0;
      uint16 kv_version= 0;
      uint flags= 0;
      if (!dict->get_index_info(gl_index_id, &index_dict_version,
                           &index_type, &kv_version))
      {
        sql_print_error("RocksDB: Could not get index information "
                        "for Index Number (%u,%u), table %s",
                        gl_index_id.cf_id, gl_index_id.index_id,
                        tdef->dbname_tablename.c_ptr_safe());
        return true;
      }
      if (max_index_id_in_dict < gl_index_id.index_id)
      {
        sql_print_error("RocksDB: Found max index id %u from data dictionary "
                        "but also found larger index id %u from dictionary. "
                        "This should never happen and possibly a bug.",
                        max_index_id_in_dict, gl_index_id.index_id);
        return true;
      }
      if (!dict->get_cf_flags(gl_index_id.cf_id, &flags))
      {
        sql_print_error("RocksDB: Could not get Column Family Flags "
                        "for CF Number %d, table %s",
                        gl_index_id.cf_id, tdef->dbname_tablename.c_ptr_safe());
        return true;
      }

      rocksdb::ColumnFamilyHandle* cfh = cf_manager->get_cf(gl_index_id.cf_id);
      DBUG_ASSERT(cfh != nullptr);

      /*
        We can't fully initialize RDBSE_KEYDEF object here, because full
        initialization requires that there is an open TABLE* where we could
        look at Field* objects and set max_length and other attributes
      */
      tdef->key_descr[keyno]=
          new RDBSE_KEYDEF(gl_index_id.index_id, keyno, cfh, index_dict_version,
                           index_type, kv_version,
                           flags & RDBSE_KEYDEF::REVERSE_CF_FLAG,
                           flags & RDBSE_KEYDEF::AUTO_CF_FLAG, "",
                           dict->get_stats(gl_index_id));
    }
    put(tdef);
    i++;
  }

  /*
    If validate_tables is greater than 0 run the validation.  Only fail the
    initialzation if the setting is 1.  If the setting is 2 we continue.
  */
  if (validate_tables > 0 && !validate_schemas()) {
    if (validate_tables == 1) {
      sql_print_error("RocksDB: Problems validating data dictionary "
                      "against .frm files, exiting");
      return true;
    }
  }

  // index ids used by applications should not conflict with
  // data dictionary index ids
  if (max_index_id_in_dict < RDBSE_KEYDEF::END_DICT_INDEX_ID)
  {
    max_index_id_in_dict = RDBSE_KEYDEF::END_DICT_INDEX_ID;
  }

  sequence.init(max_index_id_in_dict+1);

  if (!it->status().ok())
  {
    std::string s= it->status().ToString();
    sql_print_error("RocksDB: Table_store: load error: %s", s.c_str());
    return true;
  }
  delete it;
  sql_print_information("RocksDB: Table_store: loaded DDL data for %d tables", i);
  return false;
}


RDBSE_TABLE_DEF* Table_ddl_manager::find(const uchar *table_name,
                                         uint table_name_len,
                                         bool lock)
{
  RDBSE_TABLE_DEF *rec;
  if (lock)
    mysql_rwlock_rdlock(&rwlock);
  rec= (RDBSE_TABLE_DEF*)my_hash_search(&ddl_hash, (uchar*)table_name,
                                        table_name_len);
  if (lock)
    mysql_rwlock_unlock(&rwlock);
  return rec;
}

std::unique_ptr<RDBSE_KEYDEF>
Table_ddl_manager::get_copy_of_keydef(GL_INDEX_ID gl_index_id)
{
  std::unique_ptr<RDBSE_KEYDEF> ret;
  mysql_rwlock_rdlock(&rwlock);
  auto key_def = find(gl_index_id);
  if (key_def) {
    ret = std::unique_ptr<RDBSE_KEYDEF>(new RDBSE_KEYDEF(*key_def));
  }
  mysql_rwlock_unlock(&rwlock);
  return ret;
}

// this method assumes at least read-only lock on rwlock
RDBSE_KEYDEF* Table_ddl_manager::find(GL_INDEX_ID gl_index_id)
{
  RDBSE_KEYDEF* ret = NULL;

  auto it= index_num_to_keydef.find(gl_index_id);
  if (it != index_num_to_keydef.end()) {
    auto table_def = find(it->second.first.data(), it->second.first.size(),
                          false);
    if (table_def) {
      if (it->second.second < table_def->n_keys) {
        ret = table_def->key_descr[it->second.second];
      }
    }
  }
  return ret;
}

void Table_ddl_manager::set_stats(
  const std::vector<MyRocksTablePropertiesCollector::IndexStats>& stats
) {
  mysql_rwlock_wrlock(&rwlock);
  for (const auto& src : stats) {
    auto keydef = find(src.gl_index_id);
    if (keydef) {
      keydef->stats = src;
    }
  }
  mysql_rwlock_unlock(&rwlock);
}

/*
  Put table definition of `tbl` into the mapping, and also write it to the
  on-disk data dictionary.
*/

int Table_ddl_manager::put_and_write(RDBSE_TABLE_DEF *tbl,
                                     rocksdb::WriteBatch *batch)
{
  uchar buf[NAME_LEN * 2 + RDBSE_KEYDEF::INDEX_NUMBER_SIZE];
  uint pos= 0;

  store_index_number(buf, RDBSE_KEYDEF::DDL_ENTRY_INDEX_START_NUMBER);
  pos+= RDBSE_KEYDEF::INDEX_NUMBER_SIZE;

  memcpy(buf + pos, tbl->dbname_tablename.ptr(), tbl->dbname_tablename.length());
  pos += tbl->dbname_tablename.length();

  int res;
  if ((res= tbl->put_dict(dict, batch, buf, pos)))
    return res;

  if ((res= put(tbl)))
    return res;
  return 0;
}


/* Return 0 - ok, other value - error */
/* TODO:
  This function modifies ddl_hash and index_num_to_keydef.
  However, these changes need to be reversed if dict_manager.commit fails
  See the discussion here: https://reviews.facebook.net/D35925#inline-259167
  Tracked by https://github.com/MySQLOnRocksDB/mysql-5.6/issues/50
*/
int Table_ddl_manager::put(RDBSE_TABLE_DEF *tbl, bool lock)
{
  RDBSE_TABLE_DEF *rec;
  my_bool result;

  if (lock)
    mysql_rwlock_wrlock(&rwlock);
  rec= (RDBSE_TABLE_DEF*)find((uchar*)tbl->dbname_tablename.c_ptr(),
                               tbl->dbname_tablename.length(), false);
  if (rec)
  {
    // this will free the old record.
    my_hash_delete(&ddl_hash, (uchar*) rec);
  }
  result= my_hash_insert(&ddl_hash, (uchar*)tbl);

  for (uint keyno = 0; keyno < tbl->n_keys; keyno++) {
    index_num_to_keydef[tbl->key_descr[keyno]->get_gl_index_id()]=
      std::make_pair(
        std::basic_string<uchar>(
          (uchar*) tbl->dbname_tablename.c_ptr(),
          tbl->dbname_tablename.length()
        ),
        keyno
      );
  }

  if (lock)
    mysql_rwlock_unlock(&rwlock);
  return result;
}


void Table_ddl_manager::remove(RDBSE_TABLE_DEF *tbl,
                               rocksdb::WriteBatch *batch, bool lock)
{
  if (lock)
    mysql_rwlock_wrlock(&rwlock);

  uchar buf[NAME_LEN * 2 + RDBSE_KEYDEF::INDEX_NUMBER_SIZE];
  uint pos= 0;

  store_index_number(buf, RDBSE_KEYDEF::DDL_ENTRY_INDEX_START_NUMBER);
  pos+= RDBSE_KEYDEF::INDEX_NUMBER_SIZE;

  memcpy(buf + pos, tbl->dbname_tablename.ptr(), tbl->dbname_tablename.length());
  pos += tbl->dbname_tablename.length();

  rocksdb::Slice tkey((char*)buf, pos);
  dict->Delete(batch, tkey);

  /* The following will also delete the object: */
  my_hash_delete(&ddl_hash, (uchar*) tbl);

  if (lock)
    mysql_rwlock_unlock(&rwlock);
}


bool Table_ddl_manager::rename(uchar *from, uint from_len,
                               uchar *to, uint to_len,
                               rocksdb::WriteBatch *batch)
{
  RDBSE_TABLE_DEF *rec;
  RDBSE_TABLE_DEF *new_rec;
  bool res= true;
  uchar new_buf[NAME_LEN * 2 + RDBSE_KEYDEF::INDEX_NUMBER_SIZE];
  uint new_pos= 0;

  mysql_rwlock_wrlock(&rwlock);
  if (!(rec= (RDBSE_TABLE_DEF*)find(from, from_len, false)))
    goto err;

  if (!(new_rec= new RDBSE_TABLE_DEF))
    goto err;

  new_rec->dbname_tablename.append((char*)to, to_len);
  new_rec->n_keys= rec->n_keys;
  new_rec->auto_incr_val= rec->auto_incr_val;
  new_rec->key_descr= rec->key_descr;
  rec->key_descr= NULL; /* so that it's not free'd when deleting the old rec */

  // Create a new key
  store_index_number(new_buf, RDBSE_KEYDEF::DDL_ENTRY_INDEX_START_NUMBER);
  new_pos+= RDBSE_KEYDEF::INDEX_NUMBER_SIZE;

  memcpy(new_buf + new_pos, new_rec->dbname_tablename.ptr(),
         new_rec->dbname_tablename.length());
  new_pos += new_rec->dbname_tablename.length();

  // Create a key to add
  if (new_rec->put_dict(dict, batch, new_buf, new_pos))
    goto err;
  remove(rec, batch, false);
  put(new_rec, false);
  res= false; // ok
err:
  mysql_rwlock_unlock(&rwlock);
  return res;
}


void Table_ddl_manager::cleanup()
{
  my_hash_free(&ddl_hash);
  mysql_rwlock_destroy(&rwlock);
  sequence.cleanup();
}

void Table_ddl_manager::add_changed_indexes(
  const std::vector<GL_INDEX_ID>& v)
{
  std::lock_guard<std::mutex> lock(changed_indexes_mutex);
  changed_indexes.insert(v.begin(), v.end());
}

std::unordered_set<GL_INDEX_ID> Table_ddl_manager::get_changed_indexes()
{
  std::lock_guard<std::mutex> lock(changed_indexes_mutex);
  auto ret = std::move(changed_indexes);
  changed_indexes.clear();
  return ret;
}

int Table_ddl_manager::scan(void* cb_arg,
                            int (*callback)(void*, RDBSE_TABLE_DEF*))
{
  int i, ret;
  RDBSE_TABLE_DEF *rec;

  mysql_rwlock_rdlock(&rwlock);

  ret= 0;
  i= 0;

  while ((rec = (RDBSE_TABLE_DEF*)my_hash_element(&ddl_hash, i))) {
    ret = (*callback)(cb_arg, rec);
    if (ret)
      break;
    i++;
  }

  mysql_rwlock_unlock(&rwlock);
  return ret;
}

bool Binlog_info_manager::init(Dict_manager *dict_arg)
{
  dict= dict_arg;

  store_index_number(key_buf, RDBSE_KEYDEF::BINLOG_INFO_INDEX_NUMBER);
  key_slice = rocksdb::Slice((char*)key_buf, RDBSE_KEYDEF::INDEX_NUMBER_SIZE);
  return false;
}


void Binlog_info_manager::cleanup()
{
}

/**
  Set binlog name, pos and optionally gtid into WriteBatch.
  This function should be called as part of transaction commit,
  since binlog info is set only at transaction commit.
  Actual write into RocksDB is not done here, so checking if
  write succeeded or not is not possible here.
  @param binlog_name   Binlog name
  @param binlog_pos    Binlog pos
  @param binlog_gtid   Binlog GTID
  @param batch         WriteBatch
*/
void Binlog_info_manager::update(const char* binlog_name,
                                 const my_off_t binlog_pos,
                                 const char* binlog_gtid,
                                 rocksdb::WriteBatchBase* batch)
{
  if (binlog_name && binlog_pos)
  {
    // max binlog length (512) + binlog pos (4) + binlog gtid (57) < 1024
    uchar  value_buf[1024];
    dict->Put(batch, key_slice,
              pack_value(value_buf, binlog_name, binlog_pos, binlog_gtid));
  }
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
bool Binlog_info_manager::read(char *binlog_name, my_off_t &binlog_pos,
                               char *binlog_gtid)
{
  bool ret= false;
  if (binlog_name)
  {
    std::string value;
    rocksdb::Status status= dict->Get(key_slice, &value);
    if(status.ok())
    {
      if (!unpack_value((const uchar*)value.c_str(),
                        binlog_name, binlog_pos, binlog_gtid))
        ret= true;
    }
  }
  return ret;
}

/**
  Pack binlog_name, binlog_pos, binlog_gtid into preallocated
  buffer, then converting and returning a RocksDB Slice
  @param buf           Preallocated buffer to set binlog info.
  @param binlog_name   Binlog name
  @param binlog_pos    Binlog pos
  @param binlog_gtid   Binlog GTID
  @return              rocksdb::Slice converted from buf and its length
*/
rocksdb::Slice Binlog_info_manager::pack_value(uchar *buf,
                                               const char* binlog_name,
                                               const my_off_t binlog_pos,
                                               const char* binlog_gtid)
{
  uint pack_len= 0;

  // store version
  store_big_uint2(buf, RDBSE_KEYDEF::BINLOG_INFO_INDEX_NUMBER_VERSION);
  pack_len += RDBSE_KEYDEF::VERSION_SIZE;

  // store binlog file name length
  DBUG_ASSERT(strlen(binlog_name) <= 65535);
  uint16_t binlog_name_len = strlen(binlog_name);
  store_big_uint2(buf+pack_len, binlog_name_len);
  pack_len += 2;

  // store binlog file name
  memcpy(buf+pack_len, binlog_name, binlog_name_len);
  pack_len += binlog_name_len;

  // store binlog pos
  store_big_uint4(buf+pack_len, binlog_pos);
  pack_len += 4;

  // store binlog gtid length.
  // If gtid was not set, store 0 instead
  uint16_t binlog_gtid_len = binlog_gtid? strlen(binlog_gtid) : 0;
  store_big_uint2(buf+pack_len, binlog_gtid_len);
  pack_len += 2;

  if (binlog_gtid_len > 0)
  {
    // store binlog gtid
    memcpy(buf+pack_len, binlog_gtid, binlog_gtid_len);
    pack_len += binlog_gtid_len;
  }

  return rocksdb::Slice((char*)buf, pack_len);
}

/**
  Unpack value then split into binlog_name, binlog_pos (and binlog_gtid)
  @param[IN]  value        Binlog state info fetched from RocksDB
  @param[OUT] binlog_name  Binlog name
  @param[OUT] binlog_pos   Binlog pos
  @param[OUT] binlog_gtid  Binlog GTID
  @return     true on error
*/
bool Binlog_info_manager::unpack_value(const uchar *value, char *binlog_name,
                                       my_off_t &binlog_pos,
                                       char *binlog_gtid)
{
  uint pack_len= 0;

  // read version
  uint16_t version= read_big_uint2(value);
  pack_len += RDBSE_KEYDEF::VERSION_SIZE;
  if (version != RDBSE_KEYDEF::BINLOG_INFO_INDEX_NUMBER_VERSION)
    return true;

  // read binlog file name length
  uint16_t binlog_name_len= read_big_uint2(value+pack_len);
  pack_len += 2;
  if (binlog_name_len)
  {
    // read and set binlog name
    memcpy(binlog_name, value+pack_len, binlog_name_len);
    binlog_name[binlog_name_len]= '\0';
    pack_len += binlog_name_len;

    // read and set binlog pos
    binlog_pos= read_big_uint4(value+pack_len);
    pack_len += 4;

    // read gtid length
    uint16_t binlog_gtid_len= read_big_uint2(value+pack_len);
    pack_len += 2;
    if (binlog_gtid && binlog_gtid_len > 0)
    {
      // read and set gtid
      memcpy(binlog_gtid, value+pack_len, binlog_gtid_len);
      binlog_gtid[binlog_gtid_len]= '\0';
      pack_len += binlog_gtid_len;
    }
  }
  return false;
}

bool Dict_manager::init(rocksdb::DB *rdb_dict, Column_family_manager *cf_manager)
{
  mysql_mutex_init(0, &mutex, MY_MUTEX_INIT_FAST);
  rdb= rdb_dict;
  bool is_automatic;
  system_cfh= cf_manager->get_or_create_cf(rdb, DEFAULT_SYSTEM_CF_NAME,
                                           NULL, NULL, &is_automatic);
  store_index_number(key_buf_max_index_id,
                     RDBSE_KEYDEF::MAX_INDEX_ID);
  key_slice_max_index_id = rocksdb::Slice((char*)key_buf_max_index_id,
                                          RDBSE_KEYDEF::INDEX_NUMBER_SIZE);
  resume_drop_indexes();
  return (system_cfh == NULL);
}

void Dict_manager::cleanup()
{
  mysql_mutex_destroy(&mutex);
}

void Dict_manager::lock()
{
  mysql_mutex_lock(&mutex);
}

void Dict_manager::unlock()
{
  mysql_mutex_unlock(&mutex);
}

std::unique_ptr<rocksdb::WriteBatch> Dict_manager::begin()
{
  return std::unique_ptr<rocksdb::WriteBatch>(new rocksdb::WriteBatch);
}

void Dict_manager::Put(rocksdb::WriteBatchBase *batch,
                       const rocksdb::Slice &key,
                       const rocksdb::Slice &value)
{
  batch->Put(system_cfh, key, value);
}

rocksdb::Status Dict_manager::Get(const rocksdb::Slice &key, std::string *value)
{
  rocksdb::ReadOptions options;
  options.total_order_seek= true;
  return rdb->Get(options, system_cfh, key, value);
}

void Dict_manager::Delete(rocksdb::WriteBatchBase *batch,
                          const rocksdb::Slice &key)
{
  batch->Delete(system_cfh, key);
}

rocksdb::Iterator* Dict_manager::NewIterator()
{
  /* Reading data dictionary should always skip bloom filter */
  rocksdb::ReadOptions read_options;
  read_options.total_order_seek= true;
  return rdb->NewIterator(read_options, system_cfh);
}

int Dict_manager::commit(rocksdb::WriteBatch *batch, bool sync)
{
  if (!batch)
    return 1;
  int res= 0;
  rocksdb::WriteOptions options;
  options.sync= sync;
  rocksdb::Status s= rdb->Write(options, batch);
  res= !s.ok(); // we return true when something failed
  if (res)
    rocksdb_handle_io_error(s, ROCKSDB_IO_ERROR_DICT_COMMIT);
  batch->Clear();
  return res;
}


void Dict_manager::delete_util(rocksdb::WriteBatch* batch,
                               const uint32_t prefix,
                               const GL_INDEX_ID gl_index_id)
{
  uchar key_buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*3]= {0};
  store_big_uint4(key_buf, prefix);
  store_big_uint4(key_buf+RDBSE_KEYDEF::INDEX_NUMBER_SIZE, gl_index_id.cf_id);
  store_big_uint4(key_buf+2*RDBSE_KEYDEF::INDEX_NUMBER_SIZE,
                  gl_index_id.index_id);
  rocksdb::Slice key= rocksdb::Slice((char*)key_buf, sizeof(key_buf));

  Delete(batch, key);
}


void Dict_manager::add_or_update_index_cf_mapping(rocksdb::WriteBatch* batch,
                                                  const uchar index_type,
                                                  const uint16_t kv_version,
                                                  const uint32_t index_id,
                                                  const uint32_t cf_id)
{
  uchar key_buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*3]= {0};
  uchar value_buf[256]= {0};
  store_big_uint4(key_buf, RDBSE_KEYDEF::INDEX_INFO);
  store_big_uint4(key_buf+RDBSE_KEYDEF::INDEX_NUMBER_SIZE, cf_id);
  store_big_uint4(key_buf+2*RDBSE_KEYDEF::INDEX_NUMBER_SIZE, index_id);
  rocksdb::Slice key= rocksdb::Slice((char*)key_buf, sizeof(key_buf));

  uchar* ptr= value_buf;
  store_big_uint2(ptr, RDBSE_KEYDEF::INDEX_INFO_VERSION_GLOBAL_ID);
  ptr+= 2;
  store_big_uint1(ptr, index_type);
  ptr+= 1;
  store_big_uint2(ptr, kv_version);
  ptr+= 2;

  rocksdb::Slice value= rocksdb::Slice((char*)value_buf, ptr-value_buf);
  batch->Put(system_cfh, key, value);
}

void Dict_manager::add_cf_flags(rocksdb::WriteBatch* batch,
                                const uint32_t cf_id,
                                const uint32_t cf_flags)
{
  uchar key_buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*2]= {0};
  uchar value_buf[RDBSE_KEYDEF::VERSION_SIZE+
                  RDBSE_KEYDEF::INDEX_NUMBER_SIZE]= {0};
  store_big_uint4(key_buf, RDBSE_KEYDEF::CF_DEFINITION);
  store_big_uint4(key_buf+RDBSE_KEYDEF::INDEX_NUMBER_SIZE, cf_id);
  rocksdb::Slice key= rocksdb::Slice((char*)key_buf, sizeof(key_buf));

  store_big_uint2(value_buf, RDBSE_KEYDEF::CF_DEFINITION_VERSION);
  store_big_uint4(value_buf+RDBSE_KEYDEF::VERSION_SIZE, cf_flags);
  rocksdb::Slice value= rocksdb::Slice((char*)value_buf, sizeof(value_buf));
  batch->Put(system_cfh, key, value);
}

void Dict_manager::delete_index_info(rocksdb::WriteBatch* batch,
                                     const GL_INDEX_ID gl_index_id)
{
  delete_util(batch, RDBSE_KEYDEF::INDEX_INFO, gl_index_id);
}

bool Dict_manager::get_index_info(const GL_INDEX_ID gl_index_id,
                                  uint16_t *index_dict_version,
                                  uchar *index_type,
                                  uint16_t *kv_version)
{
  bool found= false;
  std::string value;
  uchar key_buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*3]= {0};
  store_big_uint4(key_buf, RDBSE_KEYDEF::INDEX_INFO);
  store_big_uint4(key_buf+RDBSE_KEYDEF::INDEX_NUMBER_SIZE, gl_index_id.cf_id);
  store_big_uint4(key_buf+2*RDBSE_KEYDEF::INDEX_NUMBER_SIZE,
      gl_index_id.index_id);
  rocksdb::Slice key= rocksdb::Slice((char*)key_buf, sizeof(key_buf));

  rocksdb::Status status= Get(key, &value);
  if (status.ok())
  {
    const uchar* val= (const uchar*)value.c_str();
    uchar* ptr= (uchar*)val;
    *index_dict_version= read_big_uint2(val);
    ptr+= 2;
    switch (*index_dict_version) {

    case RDBSE_KEYDEF::INDEX_INFO_VERSION_GLOBAL_ID:
      *index_type= read_big_uint1(ptr);
      ptr+= 1;
      *kv_version= read_big_uint2(ptr);
      found= true;
      break;

    default:
        sql_print_error("RocksDB: Found invalid version number %u from "
                        "data dictionary. This should never happen "
                        "and possibly a bug.",
                        *index_dict_version);
        abort_with_stack_traces();
      break;
    }
  }
  return found;
}

bool Dict_manager::get_cf_flags(const uint32_t cf_id, uint32_t *cf_flags)
{
  bool found= false;
  std::string value;
  uchar key_buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*2]= {0};
  store_big_uint4(key_buf, RDBSE_KEYDEF::CF_DEFINITION);
  store_big_uint4(key_buf+RDBSE_KEYDEF::INDEX_NUMBER_SIZE, cf_id);
  rocksdb::Slice key= rocksdb::Slice((char*)key_buf, sizeof(key_buf));

  rocksdb::Status status= Get(key, &value);
  if (status.ok())
  {
    const uchar* val= (const uchar*)value.c_str();
    uint16_t version= read_big_uint2(val);
    if (version == RDBSE_KEYDEF::CF_DEFINITION_VERSION)
    {
      *cf_flags= read_big_uint4(val+RDBSE_KEYDEF::VERSION_SIZE);
      found= true;
    }
  }
  return found;
}

/*
  Returning index ids that were marked as deleted (via DROP TABLE) but
  still not removed by drop_index_thread yet
 */
void Dict_manager::get_drop_indexes_ongoing(
    std::vector<GL_INDEX_ID>* gl_index_ids)
{
  uchar drop_index_buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE];
  store_big_uint4(drop_index_buf, RDBSE_KEYDEF::DDL_DROP_INDEX_ONGOING);
  rocksdb::Slice drop_index_slice((char*)drop_index_buf,
                                  RDBSE_KEYDEF::INDEX_NUMBER_SIZE);

  rocksdb::Iterator* it= NewIterator();
  for (it->Seek(drop_index_slice); it->Valid(); it->Next())
  {
    rocksdb::Slice key= it->key();
    const uchar* ptr= (const uchar*)key.data();

    if (key.size() != RDBSE_KEYDEF::INDEX_NUMBER_SIZE * 3)
      break;

    if (read_big_uint4(ptr) != RDBSE_KEYDEF::DDL_DROP_INDEX_ONGOING)
      break;

    // We don't check version right now since currently we always store only
    // RDBSE_KEYDEF::DDL_DROP_INDEX_ONGOING_VERSION = 1 as a value.
    // If increasing version number, we need to add version check logic here.
    GL_INDEX_ID gl_index_id;
    gl_index_id.cf_id= read_big_uint4(ptr+RDBSE_KEYDEF::INDEX_NUMBER_SIZE);
    gl_index_id.index_id= read_big_uint4(ptr+2*RDBSE_KEYDEF::INDEX_NUMBER_SIZE);
    gl_index_ids->push_back(gl_index_id);
  }
  delete it;
}

/*
  Returning true if index_id is delete ongoing (marked as deleted via
  DROP TABLE but drop_index_thread has not wiped yet) or not.
 */
bool Dict_manager::is_drop_index_ongoing(GL_INDEX_ID gl_index_id)
{
  bool found= false;
  std::string value;
  uchar key_buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*3]= {0};
  store_big_uint4(key_buf, RDBSE_KEYDEF::DDL_DROP_INDEX_ONGOING);
  store_big_uint4(key_buf+RDBSE_KEYDEF::INDEX_NUMBER_SIZE, gl_index_id.cf_id);
  store_big_uint4(key_buf+2*RDBSE_KEYDEF::INDEX_NUMBER_SIZE,
                  gl_index_id.index_id);
  rocksdb::Slice key= rocksdb::Slice((char*)key_buf, sizeof(key_buf));

  rocksdb::Status status= Get(key, &value);
  if (status.ok())
  {
    found= true;
  }
  return found;
}

/*
  Adding index_id to data dictionary so that the index id is removed
  by drop_index_thread
 */
void Dict_manager::start_drop_index_ongoing(rocksdb::WriteBatch* batch,
                                            GL_INDEX_ID gl_index_id)
{
  uchar key_buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*3]= {0};
  uchar value_buf[RDBSE_KEYDEF::VERSION_SIZE]= {0};
  store_big_uint4(key_buf, RDBSE_KEYDEF::DDL_DROP_INDEX_ONGOING);
  store_big_uint4(key_buf+RDBSE_KEYDEF::INDEX_NUMBER_SIZE, gl_index_id.cf_id);
  store_big_uint4(key_buf+2*RDBSE_KEYDEF::INDEX_NUMBER_SIZE,
      gl_index_id.index_id);
  store_big_uint2(value_buf, RDBSE_KEYDEF::DDL_DROP_INDEX_ONGOING_VERSION);
  rocksdb::Slice key= rocksdb::Slice((char*)key_buf, sizeof(key_buf));
  rocksdb::Slice value= rocksdb::Slice((char*)value_buf, sizeof(value_buf));
  batch->Put(system_cfh, key, value);
}

/*
  Removing index_id from data dictionary to confirm drop_index_thread
  completed dropping entire key/values of the index_id
 */
void Dict_manager::end_drop_index_ongoing(rocksdb::WriteBatch* batch,
                                          GL_INDEX_ID gl_index_id)
{
  delete_util(batch, RDBSE_KEYDEF::DDL_DROP_INDEX_ONGOING, gl_index_id);
}

/*
  Returning true if there is no target index ids to be removed
  by drop_index_thread
 */
bool Dict_manager::is_drop_index_empty()
{
  std::vector<GL_INDEX_ID> index_ids;
  get_drop_indexes_ongoing(&index_ids);
  return index_ids.empty();
}

/*
  This function is supposed to be called by DROP TABLE. Logging messages
  that dropping indexes started, and adding data dictionary so that
  all associated indexes to be removed
 */
void Dict_manager::add_drop_table(RDBSE_KEYDEF** key_descr,
                                  uint32 n_keys,
                                  rocksdb::WriteBatch *batch)
{
  log_start_drop_table(key_descr, n_keys, "Begin");

  for (uint32 i = 0; i < n_keys; i++)
  {
    start_drop_index_ongoing(batch, key_descr[i]->get_gl_index_id());
  }
}

/*
  This function is supposed to be called by drop_index_thread, when it
  finished dropping any index.
 */
void Dict_manager::done_drop_indexes(
    const std::unordered_set<GL_INDEX_ID>& gl_index_ids)
{
  std::unique_ptr<rocksdb::WriteBatch> wb= begin();
  rocksdb::WriteBatch *batch= wb.get();

  for (auto gl_index_id : gl_index_ids)
  {
    if (is_drop_index_ongoing(gl_index_id))
    {
      sql_print_information("RocksDB: Finished filtering dropped index (%u,%u)",
                            gl_index_id.cf_id, gl_index_id.index_id);
      end_drop_index_ongoing(batch, gl_index_id);
      delete_index_info(batch, gl_index_id);
    }
  }
  commit(batch);
}

/*
  This function is supposed to be called when initializing
  Dict_manager (at startup). If there is any index ids that are
  drop ongoing, printing out messages for diagnostics purposes.
 */
void Dict_manager::resume_drop_indexes()
{
  std::vector<GL_INDEX_ID> gl_index_ids;
  get_drop_indexes_ongoing(&gl_index_ids);

  uint max_index_id_in_dict= 0;
  get_max_index_id(&max_index_id_in_dict);

  for (auto gl_index_id : gl_index_ids)
  {
    log_start_drop_index(gl_index_id, "Resume");
    if (max_index_id_in_dict < gl_index_id.index_id)
    {
      sql_print_error("RocksDB: Found max index id %u from data dictionary "
                      "but also found dropped index id (%u,%u) from drop_index "
                      "dictionary. This should never happen and is possibly a "
                      "bug.", max_index_id_in_dict, gl_index_id.cf_id,
                      gl_index_id.index_id);
      abort_with_stack_traces();
    }
  }
}

void Dict_manager::log_start_drop_table(RDBSE_KEYDEF** key_descr,
                                        uint32 n_keys,
                                        const char* log_action)
{
  for (uint32 i = 0; i < n_keys; i++) {
    log_start_drop_index(key_descr[i]->get_gl_index_id(), log_action);
  }
}


void Dict_manager::log_start_drop_index(GL_INDEX_ID gl_index_id,
                                        const char* log_action)
{
  uint16 index_dict_version= 0;
  uchar index_type= 0;
  uint16 kv_version= 0;
  if (!get_index_info(gl_index_id, &index_dict_version,
                      &index_type, &kv_version))
  {
    sql_print_error("RocksDB: Failed to get column family info "
                    "from index id (%u,%u). MyRocks data dictionary may "
                    "get corrupted.", gl_index_id.cf_id, gl_index_id.index_id);
    abort_with_stack_traces();
  }
  sql_print_information("RocksDB: %s filtering dropped index (%u,%u)",
                        log_action, gl_index_id.cf_id, gl_index_id.index_id);
}

bool Dict_manager::get_max_index_id(uint32_t *index_id)
{
  bool found= false;
  std::string value;

  rocksdb::Status status= Get(key_slice_max_index_id, &value);
  if (status.ok())
  {
    const uchar* val= (const uchar*)value.c_str();
    uint16_t version= read_big_uint2(val);
    if (version == RDBSE_KEYDEF::MAX_INDEX_ID_VERSION)
    {
      *index_id= read_big_uint4(val+RDBSE_KEYDEF::VERSION_SIZE);
      found= true;
    }
  }
  return found;
}

bool Dict_manager::update_max_index_id(rocksdb::WriteBatch* batch,
                                       const uint32_t index_id)
{
  uint32_t old_index_id= -1;
  if (get_max_index_id(&old_index_id))
  {
    if (old_index_id > index_id)
    {
      sql_print_error("RocksDB: Found max index id %u from data dictionary "
                      "but trying to update to older value %u. This should "
                      "never happen and possibly a bug.", old_index_id,
                      index_id);
      return true;
    }
  }

  uchar value_buf[RDBSE_KEYDEF::VERSION_SIZE+RDBSE_KEYDEF::INDEX_NUMBER_SIZE]= {0};
  store_big_uint2(value_buf, RDBSE_KEYDEF::MAX_INDEX_ID_VERSION);
  store_big_uint4(value_buf+RDBSE_KEYDEF::VERSION_SIZE, index_id);
  rocksdb::Slice value= rocksdb::Slice((char*)value_buf, sizeof(value_buf));
  batch->Put(system_cfh, key_slice_max_index_id, value);
  return false;
}

void Dict_manager::add_stats(
  rocksdb::WriteBatch* batch,
  const std::vector<MyRocksTablePropertiesCollector::IndexStats>& stats
)
{
  for (const auto& it : stats) {
    uchar key_buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*3]= {0};
    store_big_uint4(key_buf, RDBSE_KEYDEF::INDEX_STATISTICS);
    store_big_uint4(key_buf+RDBSE_KEYDEF::INDEX_NUMBER_SIZE,
                    it.gl_index_id.cf_id);
    store_big_uint4(key_buf+2*RDBSE_KEYDEF::INDEX_NUMBER_SIZE,
                    it.gl_index_id.index_id);

    // IndexStats::materialize takes complete care of serialization including
    // storing the version
    auto value = MyRocksTablePropertiesCollector::IndexStats::materialize(
      std::vector<MyRocksTablePropertiesCollector::IndexStats>{it});

    batch->Put(
      system_cfh,
      rocksdb::Slice((char*)key_buf, sizeof(key_buf)),
      value
    );
  }
}

MyRocksTablePropertiesCollector::IndexStats
Dict_manager::get_stats(
  GL_INDEX_ID gl_index_id
) {
  uchar key_buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE*3]= {0};
  store_big_uint4(key_buf, RDBSE_KEYDEF::INDEX_STATISTICS);
  store_big_uint4(key_buf+RDBSE_KEYDEF::INDEX_NUMBER_SIZE, gl_index_id.cf_id);
  store_big_uint4(key_buf+2*RDBSE_KEYDEF::INDEX_NUMBER_SIZE,
      gl_index_id.index_id);

  std::string value;
  rocksdb::Status status= Get(
    rocksdb::Slice(reinterpret_cast<char*>(key_buf), sizeof(key_buf)),
    &value
  );
  if (status.ok())
  {
    std::vector<MyRocksTablePropertiesCollector::IndexStats> v;
    // unmaterialize checks if the version matches
    if (MyRocksTablePropertiesCollector::IndexStats::unmaterialize(value, v)
        == 0 && v.size() == 1) {
      return v[0];
    }
  }

  return MyRocksTablePropertiesCollector::IndexStats();
}

uint Sequence_generator::get_and_update_next_number(Dict_manager *dict)
{
  uint res;
  mysql_mutex_lock(&mutex);
  res= next_number++;
  std::unique_ptr<rocksdb::WriteBatch> wb= dict->begin();
  rocksdb::WriteBatch *batch= wb.get();
  dict->update_max_index_id(batch, res);
  dict->commit(batch);
  mysql_mutex_unlock(&mutex);
  return res;
}
