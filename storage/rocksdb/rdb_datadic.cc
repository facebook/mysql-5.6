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
#include <vector>

/* MySQL header files */
#include "./key.h"
#include "./my_bit.h"

/* MyRocks header files */
#include "./ha_rocksdb_proto.h"
#include "./my_stacktrace.h"
#include "./rdb_cf_manager.h"
#include "./rdb_utils.h"

namespace myrocks {

void write_int64(String *out, uint64 val)
{
  DBUG_ASSERT(out != nullptr);

  write_int(out, uint32(val >> 32));
  write_int(out, uint32(val & 0xffffffff));
}

void write_int(String *out, uint32 val)
{
  DBUG_ASSERT(out != nullptr);

  uint buf= htonl(val);
  out->append((char*)&buf, 4);
}

void write_short(String *out, uint16 val)
{
  DBUG_ASSERT(out != nullptr);

  uint16 buf= htons(val);
  out->append((char*)&buf, 2);
}

void write_byte(String *out, uchar val)
{
  DBUG_ASSERT(out != nullptr);

  out->append((char*)&val, 1);
}

uint32 read_int(const char **data)
{
  DBUG_ASSERT(data != nullptr);
  DBUG_ASSERT(*data != nullptr);

  uint buf;
  memcpy(&buf, *data, sizeof(uint32));
  *data += sizeof(uint32);
  return ntohl(buf);
}

uint64 read_int64(const char **data)
{
  DBUG_ASSERT(data != nullptr);
  DBUG_ASSERT(*data != nullptr);

  uint64 n1 = read_int(data);
  uint32 n2 = read_int(data);
  return (n1 << 32) + n2;
}

uint16 read_short(const char **data)
{
  DBUG_ASSERT(data != nullptr);
  DBUG_ASSERT(*data != nullptr);

  uint16 buf;
  memcpy(&buf, *data, sizeof(uint16));
  *data += sizeof(uint16);
  return ntohs(buf);
}


/*
  Rdb_key_def class implementation
*/

Rdb_key_def::Rdb_key_def(
  uint indexnr_arg, uint keyno_arg,
  rocksdb::ColumnFamilyHandle* cf_handle_arg,
  uint16_t index_dict_version_arg,
  uchar index_type_arg,
  uint16_t kv_format_version_arg,
  bool is_reverse_cf_arg, bool is_auto_cf_arg,
  const char* _name,
  Rdb_index_stats _stats
) :
    m_index_number(indexnr_arg),
    m_cf_handle(cf_handle_arg),
    m_index_dict_version(index_dict_version_arg),
    m_index_type(index_type_arg),
    m_kv_format_version(kv_format_version_arg),
    m_is_reverse_cf(is_reverse_cf_arg),
    m_is_auto_cf(is_auto_cf_arg),
    m_name(_name),
    m_stats(_stats),
    m_pk_part_no(nullptr),
    m_pack_info(nullptr),
    m_keyno(keyno_arg),
    m_key_parts(0),
    m_maxlength(0)  // means 'not intialized'
{
  mysql_mutex_init(0, &m_mutex, MY_MUTEX_INIT_FAST);
  store_index_number(m_index_number_storage_form, m_index_number);
  DBUG_ASSERT(m_cf_handle != nullptr);
}

Rdb_key_def::Rdb_key_def(const Rdb_key_def& k) :
    m_index_number(k.m_index_number),
    m_cf_handle(k.m_cf_handle),
    m_is_reverse_cf(k.m_is_reverse_cf),
    m_is_auto_cf(k.m_is_auto_cf),
    m_name(k.m_name),
    m_stats(k.m_stats),
    m_pk_part_no(k.m_pk_part_no),
    m_pack_info(k.m_pack_info),
    m_keyno(k.m_keyno),
    m_key_parts(k.m_key_parts),
    m_maxlength(k.m_maxlength)
{
  mysql_mutex_init(0, &m_mutex, MY_MUTEX_INIT_FAST);
  store_index_number(m_index_number_storage_form, m_index_number);
  if (k.m_pack_info)
  {
    size_t size= sizeof(Rdb_field_packing) * k.m_key_parts;
    m_pack_info= reinterpret_cast<Rdb_field_packing*>(my_malloc(size, MYF(0)));
    memcpy(m_pack_info, k.m_pack_info, size);
  }

  if (k.m_pk_part_no)
  {
    size_t size = sizeof(uint)*m_key_parts;
    m_pk_part_no= reinterpret_cast<uint*>(my_malloc(size, MYF(0)));
    memcpy(m_pk_part_no, k.m_pk_part_no, size);
  }
}

Rdb_key_def::~Rdb_key_def()
{
  mysql_mutex_destroy(&m_mutex);

  my_free(m_pk_part_no);
  m_pk_part_no = nullptr;

  my_free(m_pack_info);
  m_pack_info = nullptr;
}

void Rdb_key_def::setup(TABLE *tbl, Rdb_tbl_def *tbl_def)
{
  DBUG_ASSERT(tbl != nullptr);
  DBUG_ASSERT(tbl_def != nullptr);

  /*
    Set max_length based on the table.  This can be called concurrently from
    multiple threads, so there is a mutex to protect this code.
  */
  const bool is_hidden_pk= (m_index_type == INDEX_TYPE_HIDDEN_PRIMARY);
  const bool hidden_pk_exists= rocksdb_has_hidden_pk(tbl);
  const bool secondary_key= (m_index_type == INDEX_TYPE_SECONDARY);
  if (!m_maxlength)
  {
    mysql_mutex_lock(&m_mutex);
    if (m_maxlength != 0)
    {
      mysql_mutex_unlock(&m_mutex);
      return;
    }

    KEY *key_info= nullptr;
    KEY *pk_info= nullptr;
    if (!is_hidden_pk)
    {
      key_info= &tbl->key_info[m_keyno];
      if (!hidden_pk_exists)
        pk_info=  &tbl->key_info[tbl->s->primary_key];
    }

    if (secondary_key)
      m_pk_key_parts= hidden_pk_exists ? 1 : pk_info->actual_key_parts;
    else
    {
      pk_info= nullptr;
      m_pk_key_parts= 0;
    }

    // "unique" secondary keys support:
    m_key_parts= is_hidden_pk ? 1 : key_info->actual_key_parts;

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
      m_key_parts += m_pk_key_parts;
    }

    if (secondary_key)
      m_pk_part_no= reinterpret_cast<uint*>(my_malloc(sizeof(uint)*m_key_parts,
                                                      MYF(0)));
    else
      m_pk_part_no= nullptr;

    size_t size= sizeof(Rdb_field_packing) * m_key_parts;
    m_pack_info= reinterpret_cast<Rdb_field_packing*>(my_malloc(size, MYF(0)));

    size_t max_len= INDEX_NUMBER_SIZE;
    int unpack_len= 0;
    int max_part_len= 0;
    bool simulating_extkey= false;
    uint dst_i= 0;

    uint keyno_to_set= m_keyno;
    uint keypart_to_set= 0;

    if (is_hidden_pk)
    {
      Field *field= nullptr;
      m_pack_info[dst_i].setup(field, keyno_to_set, 0);
      m_pack_info[dst_i].m_unpack_data_offset= unpack_len;
      max_len    += m_pack_info[dst_i].m_max_image_len;
      unpack_len += m_pack_info[dst_i].m_unpack_data_len;
      max_part_len= std::max(max_part_len, m_pack_info[dst_i].m_max_image_len);
      dst_i++;
    }
    else
    {
      KEY_PART_INFO *key_part= key_info->key_part;

      /* this loop also loops over the 'extended key' tail */
      for (uint src_i= 0; src_i < m_key_parts; src_i++, keypart_to_set++)
      {
        Field *field= key_part ? key_part->field : nullptr;

        if (simulating_extkey && !hidden_pk_exists)
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

        if (field && field->real_maybe_null())
          max_len +=1;  // NULL-byte

        m_pack_info[dst_i].setup(field, keyno_to_set, keypart_to_set);
        m_pack_info[dst_i].m_unpack_data_offset= unpack_len;

        if (pk_info)
        {
          m_pk_part_no[dst_i]= -1;
          for (uint j= 0; j < m_pk_key_parts; j++)
          {
            if (field->field_index == pk_info->key_part[j].field->field_index)
            {
              m_pk_part_no[dst_i]= j;
              break;
            }
          }
        }
        else if (secondary_key && hidden_pk_exists)
        {
          /*
            The hidden pk can never be part of the sk.  So it is always
            appended to the end of the sk.
          */
          m_pk_part_no[dst_i]= -1;
          if (simulating_extkey)
            m_pk_part_no[dst_i]= 0;
        }

        max_len    += m_pack_info[dst_i].m_max_image_len;
        unpack_len += m_pack_info[dst_i].m_unpack_data_len;

        max_part_len= std::max(max_part_len,
                               m_pack_info[dst_i].m_max_image_len);

        key_part++;
        /*
          For "unique" secondary indexes, pretend they have
          "index extensions"
         */
        if (secondary_key && src_i+1 == key_info->actual_key_parts)
        {
          simulating_extkey= true;
          if (!hidden_pk_exists)
          {
            keyno_to_set= tbl->s->primary_key;
            key_part= pk_info->key_part;
            keypart_to_set= (uint)-1;
          }
          else
          {
            keyno_to_set= tbl_def->m_key_count - 1;
            key_part= nullptr;
            keypart_to_set= 0;
          }
        }

        dst_i++;
      }
    }

    m_key_parts= dst_i;
    m_maxlength= max_len;
    m_unpack_data_len= unpack_len;

    /* Initialize the memory needed by the stats structure */
    m_stats.m_distinct_keys_per_prefix.resize(get_m_key_parts());

    mysql_mutex_unlock(&m_mutex);
  }
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

uint Rdb_key_def::get_primary_key_tuple(TABLE *table,
                                        Rdb_key_def *pk_descr,
                                        const rocksdb::Slice *key,
                                        uchar *pk_buffer) const
{
  DBUG_ASSERT(table != nullptr);
  DBUG_ASSERT(pk_descr != nullptr);
  DBUG_ASSERT(key != nullptr);
  DBUG_ASSERT(pk_buffer);

  uint size= 0;
  uchar *buf= pk_buffer;
  DBUG_ASSERT(m_pk_key_parts);

  /* Put the PK number */
  store_index_number(buf, pk_descr->m_index_number);
  buf += INDEX_NUMBER_SIZE;
  size += INDEX_NUMBER_SIZE;

  const char* start_offs[MAX_REF_PARTS];
  const char* end_offs[MAX_REF_PARTS];
  int pk_key_part;
  uint i;
  Rdb_string_reader reader(key);

  // Skip the index number
  if ((!reader.read(INDEX_NUMBER_SIZE)))
    return INVALID_LEN;

  for (i= 0; i < m_key_parts; i++)
  {
    if ((pk_key_part= m_pk_part_no[i]) != -1)
    {
      start_offs[pk_key_part]= reader.get_current_ptr();
    }

    bool have_value= true;
    /* It is impossible to unpack the column. Skip it. */
    if (m_pack_info[i].m_maybe_null)
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
      Rdb_field_packing *fpi= &m_pack_info[i];

      DBUG_ASSERT(table->s != nullptr);
      bool is_hidden_pk_part= (i + 1 == m_key_parts) &&
                              (table->s->primary_key == MAX_INDEXES);
      Field *field= nullptr;
      if (!is_hidden_pk_part)
        field= fpi->get_field_in_table(table);
      if (fpi->m_skip_func(fpi, field, &reader))
        return INVALID_LEN;
    }

    if (pk_key_part != -1)
    {
      end_offs[pk_key_part]= reader.get_current_ptr();
    }
  }

  for (i= 0; i < m_pk_key_parts; i++)
  {
    uint part_size= end_offs[i] - start_offs[i];
    memcpy(buf, start_offs[i], end_offs[i] - start_offs[i]);
    buf += part_size;
    size += part_size;
  }

  return size;
}


/**
  Convert index tuple into storage (i.e. mem-comparable) format

  @detail
    Currently this is done by unpacking into table->record[0] and then
    packing index columns into storage format.

  @param pack_buffer Temporary area for packing varchar columns. Its
                     size is at least max_storage_fmt_length() bytes.
*/

uint Rdb_key_def::pack_index_tuple(const ha_rocksdb *handler, TABLE *tbl,
                                   uchar *pack_buffer, uchar *packed_tuple,
                                   const uchar *key_tuple,
                                   key_part_map keypart_map)
{
  DBUG_ASSERT(handler != nullptr);
  DBUG_ASSERT(tbl != nullptr);
  DBUG_ASSERT(pack_buffer != nullptr);
  DBUG_ASSERT(packed_tuple != nullptr);
  DBUG_ASSERT(key_tuple != nullptr);

  /* We were given a record in KeyTupleFormat. First, save it to record */
  uint key_len= calculate_key_len(tbl, m_keyno, key_tuple, keypart_map);
  key_restore(tbl->record[0], key_tuple, &tbl->key_info[m_keyno], key_len);

  uint n_used_parts= my_count_bits(keypart_map);
  if (keypart_map == HA_WHOLE_KEY)
    n_used_parts= 0; // Full key is used

  /* Then, convert the record into a mem-comparable form */
  return pack_record(handler, tbl, pack_buffer, tbl->record[0], packed_tuple,
                     nullptr, nullptr, n_used_parts);
}


/**
  @brief
    Check if "unpack info" data includes checksum.

  @detail
    This is used only by CHECK TABLE to count the number of rows that have
    checksums. I guess this is a hackish approach.
*/

bool Rdb_key_def::unpack_info_has_checksum(const rocksdb::Slice &unpack_info)
{
  return (unpack_info.size() == CHECKSUM_CHUNK_SIZE &&
          unpack_info.data()[0]== CHECKSUM_DATA_TAG);
}

/*
  @return Number of bytes that were changed
*/
int Rdb_key_def::successor(uchar *packed_tuple, uint len)
{
  DBUG_ASSERT(packed_tuple != nullptr);

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

  @detail
    Some callers do not need the unpack information, they can pass
    unpack_info=nullptr, unpack_info_len=nullptr.

  @return
    Length of the packed tuple
*/

uint Rdb_key_def::pack_record(const ha_rocksdb *handler, TABLE *tbl,
                              uchar *pack_buffer,
                              const uchar *record,
                              uchar *packed_tuple,
                              uchar *unpack_info, int *unpack_info_len,
                              uint n_key_parts,
                              uint *n_null_fields,
                              longlong hidden_pk_id)
{
  DBUG_ASSERT(handler != nullptr);
  DBUG_ASSERT(tbl != nullptr);
  DBUG_ASSERT(pack_buffer != nullptr);
  DBUG_ASSERT(record != nullptr);
  DBUG_ASSERT(packed_tuple != nullptr);

  uchar *tuple= packed_tuple;
  uchar *unpack_end= unpack_info;
  const bool hidden_pk_exists= rocksdb_has_hidden_pk(tbl);

  store_index_number(tuple, m_index_number);
  tuple += INDEX_NUMBER_SIZE;

  // If n_key_parts is 0, it means all columns.
  // The following includes the 'extended key' tail.
  // The 'extended key' includes primary key. This is done to 'uniqify'
  // non-unique indexes
  bool use_all_columns = n_key_parts == 0 || n_key_parts == MAX_REF_PARTS;

  // If hidden pk exists, but hidden pk wasnt passed in, we can't pack the
  // hidden key part.  So we skip it (its always 1 part).
  if (hidden_pk_exists && !hidden_pk_id && use_all_columns)
    n_key_parts= m_key_parts - 1;
  else if (use_all_columns)
    n_key_parts= m_key_parts;

  if (n_null_fields)
    *n_null_fields = 0;

  for (uint i=0; i < n_key_parts; i++)
  {
    // Fill hidden pk id into the last key part for secondary keys for tables
    // with no pk
    if (hidden_pk_exists && hidden_pk_id && i + 1 == n_key_parts)
    {
      m_pack_info[i].fill_hidden_pk_val(&tuple, hidden_pk_id);
      break;
    }

    Field *field= m_pack_info[i].get_field_in_table(tbl);
    DBUG_ASSERT(field != nullptr);

    // Old Field methods expected the record pointer to be at tbl->record[0].
    // The quick and easy way to fix this was to pass along the offset
    // for the pointer.
    my_ptrdiff_t ptr_diff= record - tbl->record[0];

    if (field->real_maybe_null())
    {
      DBUG_ASSERT(is_storage_available(tuple - packed_tuple, 1));
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
    DBUG_ASSERT(is_storage_available(tuple - packed_tuple,
                                     m_pack_info[i].m_max_image_len));
    field->move_field_offset(ptr_diff);
    m_pack_info[i].m_pack_func(&m_pack_info[i], field, pack_buffer, &tuple);

    /* Make "unpack info" to be stored in the value */
    if (unpack_end && m_pack_info[i].m_make_unpack_info_func)
    {
      m_pack_info[i].m_make_unpack_info_func(&m_pack_info[i], field,
                                             unpack_end);
      unpack_end += m_pack_info[i].m_unpack_data_len;
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

  DBUG_ASSERT(is_storage_available(tuple - packed_tuple, 0));

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

uint Rdb_key_def::pack_hidden_pk(longlong hidden_pk_id,
                                 uchar *packed_tuple) const
{
  DBUG_ASSERT(packed_tuple != nullptr);

  uchar *tuple= packed_tuple;
  store_index_number(tuple, m_index_number);
  tuple += INDEX_NUMBER_SIZE;
  DBUG_ASSERT(m_key_parts == 1);
  DBUG_ASSERT(is_storage_available(tuple - packed_tuple,
                                   m_pack_info[0].m_max_image_len));

  m_pack_info[0].fill_hidden_pk_val(&tuple, hidden_pk_id);

  DBUG_ASSERT(is_storage_available(tuple - packed_tuple, 0));
  return tuple - packed_tuple;
}

void pack_with_make_sort_key(Rdb_field_packing *fpi, Field *field,
                             uchar *buf MY_ATTRIBUTE((unused)),
                             uchar **dst)
{
  DBUG_ASSERT(fpi != nullptr);
  DBUG_ASSERT(field != nullptr);
  DBUG_ASSERT(dst != nullptr);
  DBUG_ASSERT(*dst != nullptr);

  const int max_len= fpi->m_max_image_len;
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
int Rdb_key_def::compare_keys(
  const rocksdb::Slice *key1,
  const rocksdb::Slice *key2,
  std::size_t* column_index
)
{
  DBUG_ASSERT(key1 != nullptr);
  DBUG_ASSERT(key2 != nullptr);
  DBUG_ASSERT(column_index != nullptr);

  // the caller should check the return value and
  // not rely on column_index being valid
  *column_index = 0xbadf00d;

  Rdb_string_reader reader1(key1);
  Rdb_string_reader reader2(key2);

  // Skip the index number
  if ((!reader1.read(INDEX_NUMBER_SIZE)))
    return 1;

  if ((!reader2.read(INDEX_NUMBER_SIZE)))
    return 1;

  for (uint i= 0; i < m_key_parts ; i++)
  {
    Rdb_field_packing *fpi= &m_pack_info[i];
    if (fpi->m_maybe_null)
    {
      auto nullp1= reader1.read(1);
      auto nullp2= reader2.read(1);
      if (nullp1 == nullptr || nullp2 == nullptr)
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
    DBUG_ASSERT(fpi->m_skip_func);
    if (fpi->m_skip_func(fpi, nullptr, &reader1))
      return 1;
    if (fpi->m_skip_func(fpi, nullptr, &reader2))
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

size_t Rdb_key_def::key_length(TABLE *table, const rocksdb::Slice &key) const
{
  DBUG_ASSERT(table != nullptr);

  Rdb_string_reader reader(&key);

  if ((!reader.read(INDEX_NUMBER_SIZE)))
    return size_t(-1);

  for (uint i= 0; i < m_key_parts ; i++)
  {
    Rdb_field_packing *fpi= &m_pack_info[i];
    Field *field= nullptr;
    if (m_index_type != INDEX_TYPE_HIDDEN_PRIMARY)
      field= fpi->get_field_in_table(table);
    if (fpi->m_skip_func(fpi, field, &reader))
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

int Rdb_key_def::unpack_record(const ha_rocksdb *handler, TABLE *table,
                               uchar *buf, const rocksdb::Slice *packed_key,
                               const rocksdb::Slice *unpack_info)
{
  Rdb_string_reader reader(packed_key);
  const uchar * const unpack_ptr= (const uchar*)unpack_info->data();
  const bool is_hidden_pk= (m_index_type == INDEX_TYPE_HIDDEN_PRIMARY);
  const bool hidden_pk_exists= rocksdb_has_hidden_pk(table);
  const bool secondary_key= (m_index_type == INDEX_TYPE_SECONDARY);

  // Old Field methods expected the record pointer to be at tbl->record[0].
  // The quick and easy way to fix this was to pass along the offset
  // for the pointer.
  my_ptrdiff_t ptr_diff= buf - table->record[0];

  if (unpack_info->size() != m_unpack_data_len &&
      unpack_info->size() != m_unpack_data_len + CHECKSUM_CHUNK_SIZE)
  {
    return 1;
  }

  // Skip the index number
  if ((!reader.read(INDEX_NUMBER_SIZE)))
    return (uint)-1;

  for (uint i= 0; i < m_key_parts ; i++)
  {
    Rdb_field_packing *fpi= &m_pack_info[i];

    /*
      Hidden pk field is packed at the end of the secondary keys, but the SQL
      layer does not know about it. Skip retrieving field if hidden pk.
    */
    if ((secondary_key && hidden_pk_exists && i + 1 == m_key_parts) ||
         is_hidden_pk)
    {
      DBUG_ASSERT(fpi->m_unpack_func);
      if (fpi->m_unpack_func(fpi, nullptr, &reader,
                       unpack_ptr + fpi->m_unpack_data_offset))
        return 1;
      continue;
    }

    Field *field= fpi->get_field_in_table(table);

    if (fpi->m_unpack_func)
    {
      /* It is possible to unpack this column. Do it. */

      if (fpi->m_maybe_null)
      {
        const char* nullp;
        if (!(nullp= reader.read(1)))
          return 1;
        if (*nullp == 0)
        {
          /* Set the NULL-bit of this field */
          field->set_null(ptr_diff);
          /* Also set the field to its default value */
          uint field_offset= field->ptr - table->record[0];
          memcpy(buf + field_offset,
                 table->s->default_values + field_offset,
                 field->pack_length());
          continue;
        }
        else if (*nullp == 1)
          field->set_notnull(ptr_diff);
        else
          return 1;
      }

      // Set the offset for methods which do not take an offset as an argument
      field->move_field_offset(ptr_diff);
      int res= fpi->m_unpack_func(fpi, field, &reader,
                            unpack_ptr + fpi->m_unpack_data_offset);
      field->move_field_offset(-ptr_diff);

      if (res)
        return 1;
    }
    else
    {
      /* It is impossible to unpack the column. Skip it. */
      if (fpi->m_maybe_null)
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
      if (fpi->m_skip_func(fpi, field, &reader))
        return 1;
    }
  }

  /*
    Check checksum values if present
  */
  if (unpack_info->size() == CHECKSUM_CHUNK_SIZE)
  {
    Rdb_string_reader unp_reader(unpack_info);
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


bool Rdb_key_def::can_unpack(uint kp) const
{
  return (m_pack_info[kp].m_unpack_func != nullptr);
}

bool Rdb_key_def::rocksdb_has_hidden_pk(const TABLE* table)
{
  return table->s->primary_key == MAX_INDEXES;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Rdb_field_packing
///////////////////////////////////////////////////////////////////////////////////////////

int skip_max_length(Rdb_field_packing *fpi, Field *field,
                    Rdb_string_reader *reader)
{
  if (!reader->read(fpi->m_max_image_len))
    return 1;
  return 0;
}


int unpack_integer(Rdb_field_packing *fpi, Field *field,
                   Rdb_string_reader *reader, const uchar *unpack_info)
{
  const int length= fpi->m_max_image_len;

  const uchar *from;
  if (!(from= (const uchar*)reader->read(length)))
    return 1; /* Mem-comparable image doesn't have enough bytes */

  /*
    If no field object, must be hidden pk. Return since there is no field to
    unpack into.
   */
  if (!field)
    return 0;

  uchar *to= field->ptr;

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

#if !defined(WORDS_BIGENDIAN)
static
void swap_double_bytes(uchar *dst, const uchar *src)
{
#if defined(__FLOAT_WORD_ORDER) && (__FLOAT_WORD_ORDER == __BIG_ENDIAN)
  // A few systems store the most-significant _word_ first on little-endian
  dst[0] = src[3]; dst[1] = src[2]; dst[2] = src[1]; dst[3] = src[0];
  dst[4] = src[7]; dst[5] = src[6]; dst[6] = src[5]; dst[7] = src[4];
#else
  dst[0] = src[7]; dst[1] = src[6]; dst[2] = src[5]; dst[3] = src[4];
  dst[4] = src[3]; dst[5] = src[2]; dst[6] = src[1]; dst[7] = src[0];
#endif
}

static
void swap_float_bytes(uchar *dst, const uchar *src)
{
  dst[0] = src[3]; dst[1] = src[2]; dst[2] = src[1]; dst[3] = src[0];
}
#else
#define swap_double_bytes nullptr
#define swap_float_bytes  nullptr
#endif

static
int unpack_floating_point(uchar *dst, Rdb_string_reader *reader, size_t size,
                          int exp_digit, const uchar *zero_pattern,
                          const uchar *zero_val,
                          void (*swap_func)(uchar *, const uchar *))
{
  const uchar* from;

  from= (const uchar*) reader->read(size);
  if (from == nullptr)
    return 1; /* Mem-comparable image doesn't have enough bytes */

  /* Check to see if the value is zero */
  if (memcmp(from, zero_pattern, size) == 0)
  {
    memcpy(dst, zero_val, size);
    return 0;
  }

#if defined(WORDS_BIGENDIAN)
  // On big-endian, output can go directly into result
  uchar *tmp = dst;
#else
  // Otherwise use a temporary buffer to make byte-swapping easier later
  uchar tmp[8];
#endif

  memcpy(tmp, from, size);

  if (tmp[0] & 0x80)
  {
    // If the high bit is set the original value was positive so
    // remove the high bit and subtract one from the exponent.
    ushort exp_part= ((ushort) tmp[0] << 8) | (ushort) tmp[1];
    exp_part &= 0x7FFF;  // clear high bit;
    exp_part -= (ushort) 1 << (16 - 1 - exp_digit);  // subtract from exponent
    tmp[0] = (uchar) (exp_part >> 8);
    tmp[1] = (uchar) exp_part;
  }
  else
  {
    // Otherwise the original value was negative and all bytes have been
    // negated.
    for (size_t ii = 0; ii < size; ii++)
      tmp[ii] ^= 0xFF;
  }

#if !defined(WORDS_BIGENDIAN)
  // On little-endian, swap the bytes around
  swap_func(dst, tmp);
#else
  static_assert(swap_func == nullptr, "Assuming that no swapping is needed.");
#endif

  return 0;
}

#if !defined(DBL_EXP_DIG)
#define DBL_EXP_DIG (sizeof(double) * 8 - DBL_MANT_DIG)
#endif


/*
  Unpack a double by doing the reverse action of change_double_for_sort
  (sql/filesort.cc).  Note that this only works on IEEE values.
  Note also that this code assumes that NaN and +/-Infinity are never
  allowed in the database.
*/
static
int unpack_double(Rdb_field_packing *fpi, Field *field,
                  Rdb_string_reader *reader, const uchar *unpack_info)
{
  static double      zero_val = 0.0;
  static const uchar zero_pattern[8] = { 128, 0, 0, 0, 0, 0, 0, 0 };

  return unpack_floating_point(field->ptr, reader, sizeof(double), DBL_EXP_DIG,
      zero_pattern, (const uchar *) &zero_val, swap_double_bytes);
}

#if !defined(FLT_EXP_DIG)
#define FLT_EXP_DIG (sizeof(float) * 8 - FLT_MANT_DIG)
#endif

/*
  Unpack a float by doing the reverse action of Field_float::make_sort_key
  (sql/field.cc).  Note that this only works on IEEE values.
  Note also that this code assumes that NaN and +/-Infinity are never
  allowed in the database.
*/
static
int unpack_float(Rdb_field_packing *fpi, Field *field,
                 Rdb_string_reader *reader, const uchar *unpack_info)
{
  static float       zero_val = 0.0;
  static const uchar zero_pattern[4] = { 128, 0, 0, 0 };

  return unpack_floating_point(field->ptr, reader, sizeof(float), FLT_EXP_DIG,
      zero_pattern, (const uchar *) &zero_val, swap_float_bytes);
}

/*
  Unpack by doing the reverse action to Field_newdate::make_sort_key.
*/

static
int unpack_newdate(Rdb_field_packing *fpi, Field *field,
                   Rdb_string_reader *reader, const uchar *unpack_info)
{
  const char* from;
  DBUG_ASSERT(fpi->m_max_image_len == 3);
  DBUG_ASSERT(fpi->m_field_data_offset == 0);

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

int unpack_binary_str(Rdb_field_packing *fpi, Field *field,
                      Rdb_string_reader *reader,
                      const uchar *unpack_info)
{
  const char* from;
  if (!(from= reader->read(fpi->m_max_image_len)))
    return 1; /* Mem-comparable image doesn't have enough bytes */

  memcpy(field->ptr + fpi->m_field_data_offset, from, fpi->m_max_image_len);
  return 0;
}


/*
  For UTF-8, we need to convert 2-byte wide-character entities back into
  UTF8 sequences.
*/

int unpack_utf8_str(Rdb_field_packing *fpi, Field *field,
                    Rdb_string_reader *reader,
                    const uchar *unpack_info)
{
  CHARSET_INFO *cset= (CHARSET_INFO*)field->charset();
  const uchar *src;
  if (!(src= (const uchar*)reader->read(fpi->m_max_image_len)))
    return 1; /* Mem-comparable image doesn't have enough bytes */

  const uchar *src_end= src + fpi->m_max_image_len;
  uchar *dst= field->ptr + fpi->m_field_data_offset;
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


void pack_with_varchar_encoding(Rdb_field_packing *fpi, Field *field,
                                uchar *buf, uchar **dst)
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
                                    buf, fpi->m_max_image_len,
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


int unpack_binary_or_utf8_varchar(Rdb_field_packing *fpi, Field *field,
                                  Rdb_string_reader *reader,
                                  const uchar *unpack_info)
{
  const uchar *ptr;
  size_t len= 0;
  bool finished= false;
  uchar *dst= field->ptr + fpi->m_field_data_offset;
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
    if (fpi->m_varchar_charset == &my_charset_utf8_bin)
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
        const CHARSET_INFO *cset= fpi->m_varchar_charset;
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


int skip_variable_length(Rdb_field_packing *fpi, Field *field,
                         Rdb_string_reader *reader)
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

bool Rdb_field_packing::setup(Field *field, uint keynr_arg, uint key_part_arg)
{
  int res= false;
  enum_field_types type= field ? field->real_type() : MYSQL_TYPE_LONGLONG;

  m_keynr= keynr_arg;
  m_key_part= key_part_arg;

  m_maybe_null= field ? field->real_maybe_null() : false;
  m_make_unpack_info_func= nullptr;
  m_unpack_func= nullptr;
  m_unpack_data_len= 0;
  m_field_data_offset= 0;

  /* Calculate image length. By default, is is pack_length() */
  m_max_image_len= field ? field->pack_length() :
                           ROCKSDB_SIZEOF_HIDDEN_PK_COLUMN;
  m_skip_func= skip_max_length;
  m_pack_func= pack_with_make_sort_key;

  switch (type) {
    case MYSQL_TYPE_LONGLONG:
    case MYSQL_TYPE_LONG:
    case MYSQL_TYPE_INT24:
    case MYSQL_TYPE_SHORT:
    case MYSQL_TYPE_TINY:
      m_unpack_func= unpack_integer;
      return true;

    case MYSQL_TYPE_DOUBLE:
      m_unpack_func= unpack_double;
      return true;

    case MYSQL_TYPE_FLOAT:
      m_unpack_func= unpack_float;
      return true;

    case MYSQL_TYPE_DATETIME2:
    case MYSQL_TYPE_TIMESTAMP2:
      /* These are packed with Field_temporal_with_date_and_timef::make_sort_key */
    case MYSQL_TYPE_TIME2: /* TIME is packed with Field_timef::make_sort_key */
    case MYSQL_TYPE_YEAR: /* YEAR is packed with  Field_tiny::make_sort_key */
      /* Everything that comes here is packed with just a memcpy(). */
      m_unpack_func= unpack_binary_str;
      return true;

    case MYSQL_TYPE_NEWDATE:
      /*
        This is packed by Field_newdate::make_sort_key. It assumes the data is
        3 bytes, and packing is done by swapping the byte order (for both big-
        and little-endian)
      */
      m_unpack_func= unpack_newdate;
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
    m_max_image_len= cs->coll->strnxfrmlen(cs, field->field_length);
  }
  const bool is_varchar= (type == MYSQL_TYPE_VARCHAR);
  const CHARSET_INFO *cs= field->charset();
  if (is_varchar)
  {
    m_varchar_charset= cs;
    m_field_data_offset=
      reinterpret_cast<Field_varstring*>(field)->length_bytes;
    m_skip_func= skip_variable_length;
    m_pack_func= pack_with_varchar_encoding;
    m_max_image_len= (m_max_image_len/(ESCAPE_LENGTH-1) + 1) * ESCAPE_LENGTH;
  }

  if (type == MYSQL_TYPE_VARCHAR || type == MYSQL_TYPE_STRING)
  {
    if (cs == &my_charset_bin ||
        cs == &my_charset_latin1_bin)
    {
      m_unpack_func= is_varchar? unpack_binary_or_utf8_varchar :
                                 unpack_binary_str;
      res= true;
    }
    else if(cs == &my_charset_utf8_bin)
    {
      m_unpack_func= is_varchar? unpack_binary_or_utf8_varchar :
                                 unpack_utf8_str;
      res= true;
    }
  }
  return res;
}


Field *Rdb_field_packing::get_field_in_table(TABLE *tbl) const
{
  return tbl->key_info[m_keynr].key_part[m_key_part].field;
}


void report_checksum_mismatch(Rdb_key_def *kd, bool is_key,
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


void Rdb_field_packing::fill_hidden_pk_val(uchar **dst,
                                         longlong hidden_pk_id) const
{
  DBUG_ASSERT(m_max_image_len == 8);

  String to;
  write_int64(&to, hidden_pk_id);
  memcpy(*dst, to.ptr(), m_max_image_len);

  *dst += m_max_image_len;
}


///////////////////////////////////////////////////////////////////////////////////////////
// Rdb_ddl_manager
///////////////////////////////////////////////////////////////////////////////////////////

Rdb_tbl_def::~Rdb_tbl_def()
{
  auto ddl_manager= get_ddl_manager();
  /* Don't free key definitions */
  if (m_key_descr)
  {
    for (uint i= 0; i < m_key_count; i++) {
      if (ddl_manager && m_key_descr[i]) {
        ddl_manager->erase_index_num(m_key_descr[i]->get_gl_index_id());
      }

      delete m_key_descr[i];
      m_key_descr[i] = nullptr;
    }

    delete[] m_key_descr;
    m_key_descr = nullptr;
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

bool Rdb_tbl_def::put_dict(Rdb_dict_manager* dict, rocksdb::WriteBatch *batch,
                           uchar *key, size_t keylen)
{
  StringBuffer<8 * Rdb_key_def::PACKED_SIZE> indexes;
  indexes.alloc(Rdb_key_def::VERSION_SIZE +
                m_key_count * Rdb_key_def::PACKED_SIZE * 2);
  write_short(&indexes, Rdb_key_def::DDL_ENTRY_INDEX_VERSION);

  for (uint i = 0; i < m_key_count; i++)
  {
    Rdb_key_def* kd = m_key_descr[i];

    uchar flags =
      (kd->m_is_reverse_cf ? Rdb_key_def::REVERSE_CF_FLAG : 0) |
      (kd->m_is_auto_cf ? Rdb_key_def::AUTO_CF_FLAG : 0);

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
    write_int(&indexes, kd->m_index_number);
    dict->add_or_update_index_cf_mapping(batch, kd->m_index_type,
                                         kd->m_kv_format_version,
                                         kd->m_index_number, cf_id);
  }

  rocksdb::Slice skey((char*)key, keylen);
  rocksdb::Slice svalue(indexes.c_ptr(), indexes.length());

  dict->put_key(batch, skey, svalue);
  return false;
}

void Rdb_tbl_def::check_if_is_mysql_system_table()
{
  static const char *const system_dbs[] = {
    "mysql",
    "performance_schema",
    "information_schema",
  };

  const char *dotpos= strstr(m_dbname_tablename.c_ptr(), ".");
  DBUG_ASSERT(dotpos != nullptr);

  std::string dbname;
  dbname.assign(m_dbname_tablename.c_ptr(),
                dotpos - m_dbname_tablename.c_ptr());

  m_is_mysql_system_table= false;
  for (uint ii = 0; ii < array_elements(system_dbs); ii++) {
    if (strcmp(dbname.c_str(), system_dbs[ii]) == 0) {
      m_is_mysql_system_table= true;
    }
  }
}

void Rdb_tbl_def::set_name(const char *name, size_t len)
{
  m_dbname_tablename.append(name, len);

  check_if_is_mysql_system_table();
}


/*
  Static function of type my_hash_get_key that gets invoked by
  the m_ddl_hash object of type my_core::HASH.
  It manufactures a key (db+table name in our case) from a record
  (Rdb_tbl_def in our case).
*/
uchar* Rdb_ddl_manager::get_hash_key(
  Rdb_tbl_def *rec, size_t *length,
  my_bool not_used MY_ATTRIBUTE((__unused__)))
{
  *length= rec->m_dbname_tablename.length();
  return rdb_str_to_uchar_ptr(&rec->m_dbname_tablename);
}


/*
  Static function of type void (*my_hash_free_element_func_t)(void*) that gets
  invoked by the m_ddl_hash object of type my_core::HASH.
  It deletes a record (Rdb_tbl_def in our case).
*/
void Rdb_ddl_manager::free_hash_elem(void* data)
{
  Rdb_tbl_def* elem= reinterpret_cast<Rdb_tbl_def*>(data);
  delete elem;
}

void Rdb_ddl_manager::erase_index_num(GL_INDEX_ID gl_index_id)
{
  m_index_num_to_keydef.erase(gl_index_id);
}

typedef std::pair<std::string, bool>                       validate_tbl_info;
typedef std::map<std::string, std::set<validate_tbl_info>> validate_tbl_list;

/*
  Get a list of tables that we expect to have .frm files for.  This will use the
  information just read from the RocksDB data dictionary.
*/
static int add_table_to_validate_list(
    void*            arg,
    Rdb_tbl_def* tdef)
{
  validate_tbl_list* table_list = reinterpret_cast<validate_tbl_list*>(arg);
  StringBuffer<256>  dbname_buff;
  StringBuffer<256>  tablename_buff;
  StringBuffer<256>  partition_buff;

  /* Parse the m_dbname_tablename for the different elements */
  if (rocksdb_split_normalized_tablename(tdef->m_dbname_tablename.ptr(),
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
    /* Find .frm files that are not temp files (those that start with '#') */
    const char* ext = strrchr(file_info->name, '.');
    if (ext != nullptr && !is_prefix(file_info->name, tmp_file_prefix) &&
        strcmp(ext, ".frm") == 0)
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
bool Rdb_ddl_manager::validate_schemas(void)
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

bool Rdb_ddl_manager::init(Rdb_dict_manager *dict_arg,
                           Rdb_cf_manager *cf_manager,
                           uint32_t validate_tables)
{
  m_dict= dict_arg;
  mysql_rwlock_init(0, &m_rwlock);
  (void) my_hash_init(&m_ddl_hash,
                      /*system_charset_info*/ &my_charset_bin,
                      32, 0, 0,
                      (my_hash_get_key) Rdb_ddl_manager::get_hash_key,
                      Rdb_ddl_manager::free_hash_elem,
                      0);

  /* Read the data dictionary and populate the hash */
  uchar ddl_entry[Rdb_key_def::INDEX_NUMBER_SIZE];
  store_index_number(ddl_entry, Rdb_key_def::DDL_ENTRY_INDEX_START_NUMBER);
  rocksdb::Slice ddl_entry_slice((char*)ddl_entry,
                                 Rdb_key_def::INDEX_NUMBER_SIZE);

  /* Reading data dictionary should always skip bloom filter */
  rocksdb::Iterator* it= m_dict->new_iterator();
  int i= 0;

  uint max_index_id_in_dict= 0;
  m_dict->get_max_index_id(&max_index_id_in_dict);

  for (it->Seek(ddl_entry_slice); it->Valid(); it->Next())
  {
    const char *ptr;
    const char *ptr_end;
    rocksdb::Slice key= it->key();
    rocksdb::Slice val= it->value();

    if (key.size() >= Rdb_key_def::INDEX_NUMBER_SIZE &&
        memcmp(key.data(), ddl_entry, Rdb_key_def::INDEX_NUMBER_SIZE))
      break;

    if (key.size() <= Rdb_key_def::INDEX_NUMBER_SIZE)
    {
      sql_print_error("RocksDB: Table_store: key has length %d (corruption?)",
                      (int)key.size());
      return true;
    }

    Rdb_tbl_def *tdef= new Rdb_tbl_def;
    tdef->set_name(key, Rdb_key_def::INDEX_NUMBER_SIZE);

    // Now, read the DDLs.
    int real_val_size= val.size() - Rdb_key_def::VERSION_SIZE;
    if (real_val_size % Rdb_key_def::PACKED_SIZE*2)
    {
      sql_print_error("RocksDB: Table_store: invalid keylist for table %s",
                      tdef->m_dbname_tablename.c_ptr_safe());
      return true;
    }
    tdef->m_key_count= real_val_size / (Rdb_key_def::PACKED_SIZE*2);
    tdef->m_key_descr= new Rdb_key_def*[tdef->m_key_count];

    memset(tdef->m_key_descr, 0, sizeof(Rdb_key_def*) * tdef->m_key_count);

    ptr= (char*)val.data();
    int version= read_short(&ptr);
    if (version != Rdb_key_def::DDL_ENTRY_INDEX_VERSION)
    {
      sql_print_error("RocksDB: DDL ENTRY Version was not expected."
                      "Expected: %d, Actual: %d",
                      Rdb_key_def::DDL_ENTRY_INDEX_VERSION, version);
      return true;
    }
    ptr_end= ptr + real_val_size;
    for (uint keyno=0; ptr < ptr_end; keyno++)
    {
      GL_INDEX_ID gl_index_id;
      gl_index_id.cf_id= read_int(&ptr);
      gl_index_id.index_id= read_int(&ptr);
      uint16 m_index_dict_version= 0;
      uchar m_index_type= 0;
      uint16 kv_version= 0;
      uint flags= 0;
      if (!m_dict->get_index_info(gl_index_id, &m_index_dict_version,
                                  &m_index_type, &kv_version))
      {
        sql_print_error("RocksDB: Could not get index information "
                        "for Index Number (%u,%u), table %s",
                        gl_index_id.cf_id, gl_index_id.index_id,
                        tdef->m_dbname_tablename.c_ptr_safe());
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
      if (!m_dict->get_cf_flags(gl_index_id.cf_id, &flags))
      {
        sql_print_error("RocksDB: Could not get Column Family Flags "
                        "for CF Number %d, table %s",
                        gl_index_id.cf_id,
                        tdef->m_dbname_tablename.c_ptr_safe());
        return true;
      }

      rocksdb::ColumnFamilyHandle* cfh = cf_manager->get_cf(gl_index_id.cf_id);
      DBUG_ASSERT(cfh != nullptr);

      /*
        We can't fully initialize Rdb_key_def object here, because full
        initialization requires that there is an open TABLE* where we could
        look at Field* objects and set max_length and other attributes
      */
      tdef->m_key_descr[keyno]=
          new Rdb_key_def(gl_index_id.index_id, keyno, cfh,
                          m_index_dict_version,
                          m_index_type, kv_version,
                          flags & Rdb_key_def::REVERSE_CF_FLAG,
                          flags & Rdb_key_def::AUTO_CF_FLAG, "",
                          m_dict->get_stats(gl_index_id));
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
  if (max_index_id_in_dict < Rdb_key_def::END_DICT_INDEX_ID)
  {
    max_index_id_in_dict = Rdb_key_def::END_DICT_INDEX_ID;
  }

  m_sequence.init(max_index_id_in_dict+1);

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


Rdb_tbl_def* Rdb_ddl_manager::find(const uchar *table_name,
                                   uint table_name_len,
                                   bool lock)
{
  Rdb_tbl_def *rec;
  if (lock)
    mysql_rwlock_rdlock(&m_rwlock);
  rec= reinterpret_cast<Rdb_tbl_def*>(my_hash_search(&m_ddl_hash,
                                      table_name,
                                      table_name_len));
  if (lock)
    mysql_rwlock_unlock(&m_rwlock);
  return rec;
}

std::unique_ptr<Rdb_key_def>
Rdb_ddl_manager::get_copy_of_keydef(GL_INDEX_ID gl_index_id)
{
  std::unique_ptr<Rdb_key_def> ret;
  mysql_rwlock_rdlock(&m_rwlock);
  auto key_def = find(gl_index_id);
  if (key_def) {
    /* Locking the key_def prevents changes to it while a copy is made */
    key_def->block_setup();
    ret = std::unique_ptr<Rdb_key_def>(new Rdb_key_def(*key_def));
    key_def->unblock_setup();
  }
  mysql_rwlock_unlock(&m_rwlock);
  return ret;
}

// this method assumes at least read-only lock on m_rwlock
Rdb_key_def* Rdb_ddl_manager::find(GL_INDEX_ID gl_index_id)
{
  Rdb_key_def* ret = nullptr;

  auto it= m_index_num_to_keydef.find(gl_index_id);
  if (it != m_index_num_to_keydef.end()) {
    auto table_def = find(it->second.first.data(), it->second.first.size(),
                          false);
    if (table_def) {
      if (it->second.second < table_def->m_key_count) {
        ret = table_def->m_key_descr[it->second.second];
      }
    }
  }
  return ret;
}

void Rdb_ddl_manager::set_stats(
  const std::unordered_map<GL_INDEX_ID, Rdb_index_stats>& stats)
{
  mysql_rwlock_wrlock(&m_rwlock);
  for (auto src : stats) {
    auto keydef = find(src.second.m_gl_index_id);
    if (keydef) {
      keydef->m_stats = src.second;
    }
  }
  mysql_rwlock_unlock(&m_rwlock);
}

void Rdb_ddl_manager::adjust_stats(
  const std::vector<Rdb_index_stats>& new_data,
  const std::vector<Rdb_index_stats>& deleted_data
) {
  mysql_rwlock_wrlock(&m_rwlock);
  int i = 0;
  for (const auto& data : {new_data, deleted_data}) {
    for (const auto& src : data) {
      auto keydef = find(src.m_gl_index_id);
      if (keydef) {
        keydef->m_stats.merge(src, i == 0, keydef->max_storage_fmt_length());
        m_stats2store[keydef->m_stats.m_gl_index_id] = keydef->m_stats;
      }
    }
    i++;
  }
  bool should_save_stats = !m_stats2store.empty();
  mysql_rwlock_unlock(&m_rwlock);
  if (should_save_stats)
    request_save_stats();
}

void Rdb_ddl_manager::persist_stats(bool sync)
{
  mysql_rwlock_wrlock(&m_rwlock);
  auto local_stats2store = std::move(m_stats2store);
  m_stats2store.clear();
  mysql_rwlock_unlock(&m_rwlock);

  // Persist stats
  std::unique_ptr<rocksdb::WriteBatch> wb = m_dict->begin();
  std::vector<Rdb_index_stats> stats;
  std::transform(
    local_stats2store.begin(), local_stats2store.end(),
    std::back_inserter(stats),
    [](
    const std::pair<GL_INDEX_ID, Rdb_index_stats>& s
    ) {return s.second;});
  m_dict->add_stats(wb.get(), stats);
  m_dict->commit(wb.get(), sync);
}

/*
  Put table definition of `tbl` into the mapping, and also write it to the
  on-disk data dictionary.
*/

int Rdb_ddl_manager::put_and_write(Rdb_tbl_def *tbl,
                                   rocksdb::WriteBatch *batch)
{
  uchar buf[FN_LEN * 2 + Rdb_key_def::INDEX_NUMBER_SIZE];
  uint pos= 0;

  store_index_number(buf, Rdb_key_def::DDL_ENTRY_INDEX_START_NUMBER);
  pos+= Rdb_key_def::INDEX_NUMBER_SIZE;

  memcpy(buf + pos, tbl->m_dbname_tablename.ptr(),
         tbl->m_dbname_tablename.length());
  pos += tbl->m_dbname_tablename.length();

  int res;
  if ((res= tbl->put_dict(m_dict, batch, buf, pos)))
  {
    return res;
  }
  if ((res= put(tbl)))
  {
    return res;
  }
  return 0;
}


/* Return 0 - ok, other value - error */
/* TODO:
  This function modifies m_ddl_hash and m_index_num_to_keydef.
  However, these changes need to be reversed if dict_manager.commit fails
  See the discussion here: https://reviews.facebook.net/D35925#inline-259167
  Tracked by https://github.com/MySQLOnRocksDB/mysql-5.6/issues/50
*/
int Rdb_ddl_manager::put(Rdb_tbl_def *tbl, bool lock)
{
  Rdb_tbl_def *rec;
  my_bool result;

  if (lock)
    mysql_rwlock_wrlock(&m_rwlock);

  rec= reinterpret_cast<Rdb_tbl_def*>(
          find(rdb_str_to_uchar_ptr(&tbl->m_dbname_tablename),
               tbl->m_dbname_tablename.length(), false));
  if (rec)
  {
    // this will free the old record.
    my_hash_delete(&m_ddl_hash, reinterpret_cast<uchar*>(rec));
  }
  result= my_hash_insert(&m_ddl_hash, reinterpret_cast<uchar*>(tbl));

  for (uint keyno= 0; keyno < tbl->m_key_count; keyno++) {
    m_index_num_to_keydef[tbl->m_key_descr[keyno]->get_gl_index_id()]=
      std::make_pair(
        std::basic_string<uchar>(
          rdb_str_to_uchar_ptr(&tbl->m_dbname_tablename),
          tbl->m_dbname_tablename.length()
        ),
        keyno
      );
  }

  if (lock)
    mysql_rwlock_unlock(&m_rwlock);
  return result;
}


void Rdb_ddl_manager::remove(Rdb_tbl_def *tbl,
                             rocksdb::WriteBatch *batch, bool lock)
{
  if (lock)
    mysql_rwlock_wrlock(&m_rwlock);

  uchar buf[FN_LEN * 2 + Rdb_key_def::INDEX_NUMBER_SIZE];
  uint pos= 0;

  store_index_number(buf, Rdb_key_def::DDL_ENTRY_INDEX_START_NUMBER);
  pos+= Rdb_key_def::INDEX_NUMBER_SIZE;

  memcpy(buf + pos, tbl->m_dbname_tablename.ptr(),
         tbl->m_dbname_tablename.length());
  pos += tbl->m_dbname_tablename.length();

  rocksdb::Slice tkey((char*)buf, pos);
  m_dict->delete_key(batch, tkey);

  /* The following will also delete the object: */
  my_hash_delete(&m_ddl_hash, reinterpret_cast<uchar*>(tbl));

  if (lock)
    mysql_rwlock_unlock(&m_rwlock);
}


bool Rdb_ddl_manager::rename(uchar *from, uint from_len,
                             uchar *to, uint to_len,
                             rocksdb::WriteBatch *batch)
{
  Rdb_tbl_def *rec;
  Rdb_tbl_def *new_rec;
  bool res= true;
  uchar new_buf[FN_LEN * 2 + Rdb_key_def::INDEX_NUMBER_SIZE];
  uint new_pos= 0;

  mysql_rwlock_wrlock(&m_rwlock);
  if (!(rec= reinterpret_cast<Rdb_tbl_def*>(find(from, from_len, false))))
    goto err;

  new_rec= new Rdb_tbl_def;

  new_rec->set_name(reinterpret_cast<char*>(to), to_len);
  new_rec->m_key_count= rec->m_key_count;
  new_rec->m_auto_incr_val=
    rec->m_auto_incr_val.load(std::memory_order_relaxed);
  new_rec->m_key_descr= rec->m_key_descr;
  // so that it's not free'd when deleting the old rec
  rec->m_key_descr= nullptr;

  // Create a new key
  store_index_number(new_buf, Rdb_key_def::DDL_ENTRY_INDEX_START_NUMBER);
  new_pos+= Rdb_key_def::INDEX_NUMBER_SIZE;

  memcpy(new_buf + new_pos, new_rec->m_dbname_tablename.ptr(),
         new_rec->m_dbname_tablename.length());
  new_pos += new_rec->m_dbname_tablename.length();

  // Create a key to add
  if (new_rec->put_dict(m_dict, batch, new_buf, new_pos))
  {
    goto err;
  }
  remove(rec, batch, false);
  put(new_rec, false);
  res= false; // ok
err:
  mysql_rwlock_unlock(&m_rwlock);
  return res;
}


void Rdb_ddl_manager::cleanup()
{
  my_hash_free(&m_ddl_hash);
  mysql_rwlock_destroy(&m_rwlock);
  m_sequence.cleanup();
}


int Rdb_ddl_manager::scan(void* cb_arg,
                          int (*callback)(void*, Rdb_tbl_def*))
{
  int i, ret;
  Rdb_tbl_def *rec;

  mysql_rwlock_rdlock(&m_rwlock);

  ret= 0;
  i= 0;

  while ((rec = reinterpret_cast<Rdb_tbl_def*>(my_hash_element(&m_ddl_hash,
                                                               i))))
  {
    ret = (*callback)(cb_arg, rec);
    if (ret)
      break;
    i++;
  }

  mysql_rwlock_unlock(&m_rwlock);
  return ret;
}


/*
  Rdb_binlog_manager class implementation
*/

bool Rdb_binlog_manager::init(Rdb_dict_manager *dict_arg)
{
  DBUG_ASSERT(dict_arg != nullptr);
  m_dict= dict_arg;

  store_index_number(m_key_buf, Rdb_key_def::BINLOG_INFO_INDEX_NUMBER);
  m_key_slice = rocksdb::Slice(reinterpret_cast<char*>(m_key_buf),
                               Rdb_key_def::INDEX_NUMBER_SIZE);
  return false;
}

void Rdb_binlog_manager::cleanup()
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
void Rdb_binlog_manager::update(const char* binlog_name,
                                const my_off_t binlog_pos,
                                const char* binlog_gtid,
                                rocksdb::WriteBatchBase* batch)
{
  if (binlog_name && binlog_pos)
  {
    // max binlog length (512) + binlog pos (4) + binlog gtid (57) < 1024
    uchar  value_buf[1024];
    m_dict->put_key(batch, m_key_slice,
                    pack_value(value_buf, binlog_name,
                               binlog_pos, binlog_gtid));
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
bool Rdb_binlog_manager::read(char *binlog_name, my_off_t *binlog_pos,
                              char *binlog_gtid)
{
  bool ret= false;
  if (binlog_name)
  {
    std::string value;
    rocksdb::Status status= m_dict->get_value(m_key_slice, &value);
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
rocksdb::Slice Rdb_binlog_manager::pack_value(uchar *buf,
                                              const char* binlog_name,
                                              const my_off_t binlog_pos,
                                              const char* binlog_gtid)
{
  uint pack_len= 0;

  // store version
  store_big_uint2(buf, Rdb_key_def::BINLOG_INFO_INDEX_NUMBER_VERSION);
  pack_len += Rdb_key_def::VERSION_SIZE;

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
bool Rdb_binlog_manager::unpack_value(const uchar *value, char *binlog_name,
                                      my_off_t *binlog_pos,
                                      char *binlog_gtid)
{
  uint pack_len= 0;

  DBUG_ASSERT(binlog_pos != nullptr);

  // read version
  uint16_t version= read_big_uint2(value);
  pack_len += Rdb_key_def::VERSION_SIZE;
  if (version != Rdb_key_def::BINLOG_INFO_INDEX_NUMBER_VERSION)
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
    *binlog_pos= read_big_uint4(value+pack_len);
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

/**
  Inserts a row into mysql.slave_gtid_info table. Doing this inside
  storage engine is more efficient than inserting/updating through MySQL.

  @param[IN] id Primary key of the table.
  @param[IN] db Database name. This is column 2 of the table.
  @param[IN] gtid Gtid in human readable form. This is column 3 of the table.
  @param[IN] write_batch Handle to storage engine writer.
*/
void Rdb_binlog_manager::update_slave_gtid_info(
  uint id, const char* db, const char* gtid,
  rocksdb::WriteBatchBase* write_batch)
{
  if (id && db && gtid) {
    // Make sure that if the slave_gtid_info table exists we have a
    // pointer to it via m_slave_gtid_info_tbl.
    if (!m_slave_gtid_info_tbl.load()) {
      m_slave_gtid_info_tbl.store(
        get_ddl_manager()->find((const uchar*)("mysql.slave_gtid_info"), 21));
    }
    if (!m_slave_gtid_info_tbl.load()) {
      // slave_gtid_info table is not present. Simply return.
      return;
    }
    DBUG_ASSERT(m_slave_gtid_info_tbl.load()->m_key_count == 1);

    Rdb_key_def* key_def = m_slave_gtid_info_tbl.load()->m_key_descr[0];
    String value;

    // Build key
    uchar key_buf[Rdb_key_def::INDEX_NUMBER_SIZE + 4]= {0};
    uchar* buf= key_buf;
    store_index_number(buf, key_def->get_index_number());
    buf += Rdb_key_def::INDEX_NUMBER_SIZE;
    store_big_uint4(buf, id);
    buf += 4;
    rocksdb::Slice key_slice =
      rocksdb::Slice((const char*)key_buf, buf-key_buf);

    // Build value
    uchar value_buf[128]= {0};
    DBUG_ASSERT(gtid);
    uint db_len= strlen(db);
    uint gtid_len= strlen(gtid);
    buf= value_buf;
    // 1 byte used for flags. Empty here.
    buf++;

    // Write column 1.
    DBUG_ASSERT(strlen(db) <= 64);
    store_big_uint1(buf, db_len);
    buf++;
    memcpy(buf, db, db_len);
    buf += db_len;

    // Write column 2.
    DBUG_ASSERT(gtid_len <= 56);
    store_big_uint1(buf, gtid_len);
    buf++;
    memcpy(buf, gtid, gtid_len);
    buf += gtid_len;
    rocksdb::Slice value_slice =
      rocksdb::Slice((const char*)value_buf, buf-value_buf);

    write_batch->Put(key_def->get_cf(), key_slice, value_slice);
  }
}

bool Rdb_dict_manager::init(rocksdb::DB *rdb_dict, Rdb_cf_manager *cf_manager)
{
  mysql_mutex_init(0, &m_mutex, MY_MUTEX_INIT_FAST);
  m_db= rdb_dict;
  bool is_automatic;
  m_system_cfh= cf_manager->get_or_create_cf(m_db, DEFAULT_SYSTEM_CF_NAME,
                                             nullptr, nullptr, &is_automatic);
  store_index_number(m_key_buf_max_index_id,
                     Rdb_key_def::MAX_INDEX_ID);
  m_key_slice_max_index_id= rocksdb::Slice(
    reinterpret_cast<char*>(m_key_buf_max_index_id),
    Rdb_key_def::INDEX_NUMBER_SIZE);
  resume_drop_indexes();
  return (m_system_cfh == nullptr);
}

std::unique_ptr<rocksdb::WriteBatch> Rdb_dict_manager::begin()
{
  return std::unique_ptr<rocksdb::WriteBatch>(new rocksdb::WriteBatch);
}

void Rdb_dict_manager::put_key(rocksdb::WriteBatchBase *batch,
                               const rocksdb::Slice &key,
                               const rocksdb::Slice &value)
{
  batch->Put(m_system_cfh, key, value);
}

rocksdb::Status Rdb_dict_manager::get_value(const rocksdb::Slice &key,
                                            std::string *value) const
{
  rocksdb::ReadOptions options;
  options.total_order_seek= true;
  return m_db->Get(options, m_system_cfh, key, value);
}

void Rdb_dict_manager::delete_key(rocksdb::WriteBatchBase *batch,
                                  const rocksdb::Slice &key) const
{
  batch->Delete(m_system_cfh, key);
}

rocksdb::Iterator* Rdb_dict_manager::new_iterator()
{
  /* Reading data dictionary should always skip bloom filter */
  rocksdb::ReadOptions read_options;
  read_options.total_order_seek= true;
  return m_db->NewIterator(read_options, m_system_cfh);
}

int Rdb_dict_manager::commit(rocksdb::WriteBatch *batch, bool sync)
{
  if (!batch)
    return 1;
  int res= 0;
  rocksdb::WriteOptions options;
  options.sync= sync;
  rocksdb::Status s= m_db->Write(options, batch);
  res= !s.ok(); // we return true when something failed
  if (res)
    rocksdb_handle_io_error(s, ROCKSDB_IO_ERROR_DICT_COMMIT);
  batch->Clear();
  return res;
}

void Rdb_dict_manager::delete_with_prefix(rocksdb::WriteBatch* batch,
                                          const uint32_t prefix,
                                          const GL_INDEX_ID gl_index_id) const
{
  uchar key_buf[Rdb_key_def::INDEX_NUMBER_SIZE*3]= {0};
  store_big_uint4(key_buf, prefix);
  store_big_uint4(key_buf + Rdb_key_def::INDEX_NUMBER_SIZE, gl_index_id.cf_id);
  store_big_uint4(key_buf + 2*Rdb_key_def::INDEX_NUMBER_SIZE,
                  gl_index_id.index_id);
  rocksdb::Slice key= rocksdb::Slice((char*)key_buf, sizeof(key_buf));

  delete_key(batch, key);
}

void Rdb_dict_manager::add_or_update_index_cf_mapping(
                                                  rocksdb::WriteBatch* batch,
                                                  const uchar m_index_type,
                                                  const uint16_t kv_version,
                                                  const uint32_t index_id,
                                                  const uint32_t cf_id)
{
  uchar key_buf[Rdb_key_def::INDEX_NUMBER_SIZE*3]= {0};
  uchar value_buf[256]= {0};
  store_big_uint4(key_buf, Rdb_key_def::INDEX_INFO);
  store_big_uint4(key_buf+Rdb_key_def::INDEX_NUMBER_SIZE, cf_id);
  store_big_uint4(key_buf+2*Rdb_key_def::INDEX_NUMBER_SIZE, index_id);
  rocksdb::Slice key= rocksdb::Slice((char*)key_buf, sizeof(key_buf));

  uchar* ptr= value_buf;
  store_big_uint2(ptr, Rdb_key_def::INDEX_INFO_VERSION_GLOBAL_ID);
  ptr+= 2;
  store_big_uint1(ptr, m_index_type);
  ptr+= 1;
  store_big_uint2(ptr, kv_version);
  ptr+= 2;

  rocksdb::Slice value= rocksdb::Slice((char*)value_buf, ptr-value_buf);
  batch->Put(m_system_cfh, key, value);
}

void Rdb_dict_manager::add_cf_flags(rocksdb::WriteBatch* batch,
                                    const uint32_t cf_id,
                                    const uint32_t cf_flags)
{
  uchar key_buf[Rdb_key_def::INDEX_NUMBER_SIZE*2]= {0};
  uchar value_buf[Rdb_key_def::VERSION_SIZE+
                  Rdb_key_def::INDEX_NUMBER_SIZE]= {0};
  store_big_uint4(key_buf, Rdb_key_def::CF_DEFINITION);
  store_big_uint4(key_buf+Rdb_key_def::INDEX_NUMBER_SIZE, cf_id);
  rocksdb::Slice key= rocksdb::Slice((char*)key_buf, sizeof(key_buf));

  store_big_uint2(value_buf, Rdb_key_def::CF_DEFINITION_VERSION);
  store_big_uint4(value_buf+Rdb_key_def::VERSION_SIZE, cf_flags);
  rocksdb::Slice value= rocksdb::Slice((char*)value_buf, sizeof(value_buf));
  batch->Put(m_system_cfh, key, value);
}

void Rdb_dict_manager::delete_index_info(rocksdb::WriteBatch* batch,
                                         const GL_INDEX_ID gl_index_id) const
{
  delete_with_prefix(batch, Rdb_key_def::INDEX_INFO, gl_index_id);
}

bool Rdb_dict_manager::get_index_info(const GL_INDEX_ID gl_index_id,
                                      uint16_t *m_index_dict_version,
                                      uchar *m_index_type,
                                      uint16_t *kv_version)
{
  bool found= false;
  std::string value;
  uchar key_buf[Rdb_key_def::INDEX_NUMBER_SIZE*3]= {0};
  store_big_uint4(key_buf, Rdb_key_def::INDEX_INFO);
  store_big_uint4(key_buf+Rdb_key_def::INDEX_NUMBER_SIZE, gl_index_id.cf_id);
  store_big_uint4(key_buf+2*Rdb_key_def::INDEX_NUMBER_SIZE,
      gl_index_id.index_id);
  rocksdb::Slice key= rocksdb::Slice((char*)key_buf, sizeof(key_buf));

  rocksdb::Status status= get_value(key, &value);
  if (status.ok())
  {
    const uchar* val= (const uchar*)value.c_str();
    uchar* ptr= (uchar*)val;
    *m_index_dict_version= read_big_uint2(val);
    ptr+= 2;
    switch (*m_index_dict_version) {

    case Rdb_key_def::INDEX_INFO_VERSION_GLOBAL_ID:
      *m_index_type= read_big_uint1(ptr);
      ptr+= 1;
      *kv_version= read_big_uint2(ptr);
      found= true;
      break;

    default:
        sql_print_error("RocksDB: Found invalid version number %u from "
                        "data dictionary. This should never happen "
                        "and possibly a bug.",
                        *m_index_dict_version);
        abort_with_stack_traces();
      break;
    }
  }
  return found;
}

bool Rdb_dict_manager::get_cf_flags(const uint32_t cf_id, uint32_t *cf_flags)
{
  bool found= false;
  std::string value;
  uchar key_buf[Rdb_key_def::INDEX_NUMBER_SIZE*2]= {0};
  store_big_uint4(key_buf, Rdb_key_def::CF_DEFINITION);
  store_big_uint4(key_buf+Rdb_key_def::INDEX_NUMBER_SIZE, cf_id);
  rocksdb::Slice key= rocksdb::Slice((char*)key_buf, sizeof(key_buf));

  rocksdb::Status status= get_value(key, &value);
  if (status.ok())
  {
    const uchar* val= (const uchar*)value.c_str();
    uint16_t version= read_big_uint2(val);
    if (version == Rdb_key_def::CF_DEFINITION_VERSION)
    {
      *cf_flags= read_big_uint4(val+Rdb_key_def::VERSION_SIZE);
      found= true;
    }
  }
  return found;
}

/*
  Returning index ids that were marked as deleted (via DROP TABLE) but
  still not removed by drop_index_thread yet
 */
void Rdb_dict_manager::get_drop_indexes_ongoing(
    std::vector<GL_INDEX_ID>* gl_index_ids)
{
  uchar drop_index_buf[Rdb_key_def::INDEX_NUMBER_SIZE];
  store_big_uint4(drop_index_buf, Rdb_key_def::DDL_DROP_INDEX_ONGOING);
  rocksdb::Slice drop_index_slice((char*)drop_index_buf,
                                  Rdb_key_def::INDEX_NUMBER_SIZE);

  rocksdb::Iterator* it= new_iterator();
  for (it->Seek(drop_index_slice); it->Valid(); it->Next())
  {
    rocksdb::Slice key= it->key();
    const uchar* ptr= (const uchar*)key.data();

    if (key.size() != Rdb_key_def::INDEX_NUMBER_SIZE * 3)
      break;

    if (read_big_uint4(ptr) != Rdb_key_def::DDL_DROP_INDEX_ONGOING)
      break;

    // We don't check version right now since currently we always store only
    // Rdb_key_def::DDL_DROP_INDEX_ONGOING_VERSION = 1 as a value.
    // If increasing version number, we need to add version check logic here.
    GL_INDEX_ID gl_index_id;
    gl_index_id.cf_id= read_big_uint4(ptr+Rdb_key_def::INDEX_NUMBER_SIZE);
    gl_index_id.index_id= read_big_uint4(ptr+2*Rdb_key_def::INDEX_NUMBER_SIZE);
    gl_index_ids->push_back(gl_index_id);
  }
  delete it;
}

/*
  Returning true if index_id is delete ongoing (marked as deleted via
  DROP TABLE but drop_index_thread has not wiped yet) or not.
 */
bool Rdb_dict_manager::is_drop_index_ongoing(GL_INDEX_ID gl_index_id)
{
  bool found= false;
  std::string value;
  uchar key_buf[Rdb_key_def::INDEX_NUMBER_SIZE*3]= {0};
  store_big_uint4(key_buf, Rdb_key_def::DDL_DROP_INDEX_ONGOING);
  store_big_uint4(key_buf+Rdb_key_def::INDEX_NUMBER_SIZE, gl_index_id.cf_id);
  store_big_uint4(key_buf+2*Rdb_key_def::INDEX_NUMBER_SIZE,
                  gl_index_id.index_id);
  rocksdb::Slice key= rocksdb::Slice((char*)key_buf, sizeof(key_buf));

  rocksdb::Status status= get_value(key, &value);
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
void Rdb_dict_manager::start_drop_index_ongoing(rocksdb::WriteBatch* batch,
                                                GL_INDEX_ID gl_index_id)
{
  uchar key_buf[Rdb_key_def::INDEX_NUMBER_SIZE*3]= {0};
  uchar value_buf[Rdb_key_def::VERSION_SIZE]= {0};
  store_big_uint4(key_buf, Rdb_key_def::DDL_DROP_INDEX_ONGOING);
  store_big_uint4(key_buf+Rdb_key_def::INDEX_NUMBER_SIZE, gl_index_id.cf_id);
  store_big_uint4(key_buf+2*Rdb_key_def::INDEX_NUMBER_SIZE,
      gl_index_id.index_id);
  store_big_uint2(value_buf, Rdb_key_def::DDL_DROP_INDEX_ONGOING_VERSION);
  rocksdb::Slice key= rocksdb::Slice((char*)key_buf, sizeof(key_buf));
  rocksdb::Slice value= rocksdb::Slice((char*)value_buf, sizeof(value_buf));
  batch->Put(m_system_cfh, key, value);
}

/*
  Removing index_id from data dictionary to confirm drop_index_thread
  completed dropping entire key/values of the index_id
 */
void Rdb_dict_manager::end_drop_index_ongoing(rocksdb::WriteBatch* batch,
                                              GL_INDEX_ID gl_index_id)
{
  delete_with_prefix(batch, Rdb_key_def::DDL_DROP_INDEX_ONGOING, gl_index_id);
}

/*
  Returning true if there is no target index ids to be removed
  by drop_index_thread
 */
bool Rdb_dict_manager::is_drop_index_empty()
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
void Rdb_dict_manager::add_drop_table(Rdb_key_def** key_descr,
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
void Rdb_dict_manager::done_drop_indexes(
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
  Rdb_dict_manager (at startup). If there is any index ids that are
  drop ongoing, printing out messages for diagnostics purposes.
 */
void Rdb_dict_manager::resume_drop_indexes()
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

void Rdb_dict_manager::log_start_drop_table(Rdb_key_def** key_descr,
                                            uint32 n_keys,
                                            const char* log_action)
{
  for (uint32 i = 0; i < n_keys; i++) {
    log_start_drop_index(key_descr[i]->get_gl_index_id(), log_action);
  }
}


void Rdb_dict_manager::log_start_drop_index(GL_INDEX_ID gl_index_id,
                                            const char* log_action)
{
  uint16 m_index_dict_version= 0;
  uchar m_index_type= 0;
  uint16 kv_version= 0;
  if (!get_index_info(gl_index_id, &m_index_dict_version,
                      &m_index_type, &kv_version))
  {
    sql_print_error("RocksDB: Failed to get column family info "
                    "from index id (%u,%u). MyRocks data dictionary may "
                    "get corrupted.", gl_index_id.cf_id, gl_index_id.index_id);
    abort_with_stack_traces();
  }
  sql_print_information("RocksDB: %s filtering dropped index (%u,%u)",
                        log_action, gl_index_id.cf_id, gl_index_id.index_id);
}

bool Rdb_dict_manager::get_max_index_id(uint32_t *index_id)
{
  bool found= false;
  std::string value;

  rocksdb::Status status= get_value(m_key_slice_max_index_id, &value);
  if (status.ok())
  {
    const uchar* val= (const uchar*)value.c_str();
    uint16_t version= read_big_uint2(val);
    if (version == Rdb_key_def::MAX_INDEX_ID_VERSION)
    {
      *index_id= read_big_uint4(val+Rdb_key_def::VERSION_SIZE);
      found= true;
    }
  }
  return found;
}

bool Rdb_dict_manager::update_max_index_id(rocksdb::WriteBatch* batch,
                                           const uint32_t index_id)
{
  DBUG_ASSERT(batch != nullptr);

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

  uchar value_buf[Rdb_key_def::VERSION_SIZE + Rdb_key_def::INDEX_NUMBER_SIZE]=
    {0};
  store_big_uint2(value_buf, Rdb_key_def::MAX_INDEX_ID_VERSION);
  store_big_uint4(value_buf+Rdb_key_def::VERSION_SIZE, index_id);
  rocksdb::Slice value= rocksdb::Slice((char*)value_buf, sizeof(value_buf));
  batch->Put(m_system_cfh, m_key_slice_max_index_id, value);
  return false;
}

void Rdb_dict_manager::add_stats(
  rocksdb::WriteBatch* batch,
  const std::vector<Rdb_index_stats>& stats
)
{
  DBUG_ASSERT(batch != nullptr);

  for (const auto& it : stats) {
    uchar key_buf[Rdb_key_def::INDEX_NUMBER_SIZE*3]= {0};
    store_big_uint4(key_buf, Rdb_key_def::INDEX_STATISTICS);
    store_big_uint4(key_buf+Rdb_key_def::INDEX_NUMBER_SIZE,
                    it.m_gl_index_id.cf_id);
    store_big_uint4(key_buf+2*Rdb_key_def::INDEX_NUMBER_SIZE,
                    it.m_gl_index_id.index_id);

    // IndexStats::materialize takes complete care of serialization including
    // storing the version
    auto value = Rdb_index_stats::materialize(
      std::vector<Rdb_index_stats>{it}, 1.);

    batch->Put(
      m_system_cfh,
      rocksdb::Slice((char*)key_buf, sizeof(key_buf)),
      value
    );
  }
}

Rdb_index_stats Rdb_dict_manager::get_stats(GL_INDEX_ID gl_index_id)
{
  uchar key_buf[Rdb_key_def::INDEX_NUMBER_SIZE*3]= {0};
  store_big_uint4(key_buf, Rdb_key_def::INDEX_STATISTICS);
  store_big_uint4(key_buf+Rdb_key_def::INDEX_NUMBER_SIZE, gl_index_id.cf_id);
  store_big_uint4(key_buf+2*Rdb_key_def::INDEX_NUMBER_SIZE,
      gl_index_id.index_id);

  std::string value;
  rocksdb::Status status= get_value(
    rocksdb::Slice(reinterpret_cast<char*>(key_buf), sizeof(key_buf)),
    &value);
  if (status.ok())
  {
    std::vector<Rdb_index_stats> v;
    // unmaterialize checks if the version matches
    if (Rdb_index_stats::unmaterialize(value, v)
        == 0 && v.size() == 1) {
      return v[0];
    }
  }

  return Rdb_index_stats();
}

uint Sequence_generator::get_and_update_next_number(Rdb_dict_manager *dict)
{
  DBUG_ASSERT(dict != nullptr);

  uint res;
  mysql_mutex_lock(&mutex);

  res= next_number++;

  std::unique_ptr<rocksdb::WriteBatch> wb= dict->begin();
  rocksdb::WriteBatch *batch= wb.get();

  DBUG_ASSERT(batch != nullptr);
  dict->update_max_index_id(batch, res);
  dict->commit(batch);

  mysql_mutex_unlock(&mutex);

  return res;
}

}  // namespace myrocks
