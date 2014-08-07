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

#include <mysql/plugin.h>
#include "ha_rocksdb.h"
#include "sql_class.h"
#include "my_bit.h"

#include "rdb_datadic.h"


void key_restore(uchar *to_record, uchar *from_key, KEY *key_info,
                 uint key_length);


void write_int(String *out, uint32 val)
{
  uint buf= htonl(val);
  out->append((char*)&buf, 4);
}


uint32 read_int(char **data)
{
  uint buf;
  memcpy(&buf, *data, sizeof(uint32));
  *data += sizeof(uint32);
  return ntohl(buf);
}


RDBSE_KEYDEF::~RDBSE_KEYDEF()
{
  if (pk_part_no)
    my_free(pk_part_no);
  if (pack_info)
    my_free(pack_info);
  if (pack_buffer)
    my_free(pack_buffer);
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
  if (!maxlength)
  {
    KEY *key_info= &tbl->key_info[keyno];
    KEY *pk_info=  &tbl->key_info[tbl->s->primary_key];

    if (keyno != tbl->s->primary_key)
    {
      n_pk_key_parts= pk_info->actual_key_parts;
      pk_part_no= (uint*)my_malloc(sizeof(uint)*n_pk_key_parts, MYF(0));
    }
    else
    {
      pk_info= NULL;
      pk_part_no= NULL;
    }

    // "unique" secondary keys support:
    bool unique_secondary_index= false;
    m_key_parts= key_info->actual_key_parts;
    if (keyno != tbl->s->primary_key && (key_info->flags & HA_NOSAME))
    {
      // From SQL layer's point of view, Unique secondary indexes do not
      // have primary key columns at the end. Internally, they do.
      m_key_parts += n_pk_key_parts;
      unique_secondary_index= true;
    }

    size_t size= sizeof(Field_pack_info) * m_key_parts;
    pack_info= (Field_pack_info*)my_malloc(size, MYF(0));

    size_t max_len= INDEX_NUMBER_SIZE;
    int unpack_len= 0;
    KEY_PART_INFO *key_part= key_info->key_part;
    int max_part_len= 0;
    /* this loop also loops over the 'extended key' tail */
    for (uint i= 0; i < m_key_parts; i++)
    {
      Field *field= key_part->field;

      if (field->real_maybe_null())
        max_len +=1; // NULL-byte

      pack_info[i].setup(field);
      pack_info[i].unpack_data_offset= unpack_len;

      if (pk_info)
      {
        pk_part_no[i]= -1;
        for (uint j= 0; j < n_pk_key_parts; j++)
        {
          if (field->field_index == pk_info->key_part[j].field->field_index)
          {
            pk_part_no[i]= j;
            break;
          }
        }
      }

      max_len    += pack_info[i].max_image_len;
      unpack_len += pack_info[i].unpack_data_len;

      max_part_len= std::max(max_part_len, pack_info[i].max_image_len);

      key_part++;
      /* For "unique" secondary indexes, pretend they have "index extensions" */
      if (unique_secondary_index && i+1 == key_info->actual_key_parts)
      {
        key_part= pk_info->key_part;
      }
    }
    maxlength= max_len;
    unpack_data_len= unpack_len;

    pack_buffer= (uchar*)my_malloc(max_part_len, MYF(0));
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
*/

uint RDBSE_KEYDEF::get_primary_key_tuple(RDBSE_KEYDEF *pk_descr,
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
    return (uint)-1;

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
        return (uint)-1;
      if (*nullp == 0)
      {
        /* This is a NULL value */
        have_value= false;
      }
      else
      {
        /* If NULL marker is not '0', it can be only '1'  */
        if (*nullp != 1)
          return (uint)-1;
      }
    }

    if (have_value)
    {
      if (pack_info[i].skip_func(&pack_info[i], &reader))
        return (uint)-1;
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


uint RDBSE_KEYDEF::pack_index_tuple(TABLE *tbl, uchar *packed_tuple,
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
  return pack_record(tbl, tbl->record[0], packed_tuple, NULL, NULL,
                     n_used_parts);
}


void RDBSE_KEYDEF::successor(uchar *packed_tuple, uint len)
{
  uchar *p= packed_tuple + len - 1;
  for (; p > packed_tuple; p--)
  {
    if (*p != uchar(0xFF))
    {
      *p= *p + 1;
      break;
    }
    *p='\0';
  }
}

static Field *get_field_by_keynr(TABLE *tbl, KEY *key_info, uint part)
{
  if (part < key_info->actual_key_parts)
  {
    return key_info->key_part[part].field;
  }
  else
  {
    uint pk= tbl->s->primary_key;
    KEY *pk_info= &tbl->key_info[pk];
    return pk_info->key_part[part - key_info->actual_key_parts].field;
  }
}


/*
  Get index columns from the record and pack them into mem-comparable form.

  @param
    tbl                   Table we're working on
    record           IN   Record buffer with fields in table->record format
    packed_tuple     OUT  Key in the mem-comparable form
    unpack_info      OUT  Unpack data
    unpack_info_len  OUT  Unpack data length
    n_key_parts           Number of keyparts to process. 0 means all of them.

  @detail
    Some callers do not need the unpack information, they can pass
    unpack_info=NULL, unpack_info_len=NULL.

  @return
    Length of the packed tuple
*/

uint RDBSE_KEYDEF::pack_record(TABLE *tbl, const uchar *record,
                               uchar *packed_tuple,
                               uchar *unpack_info, int *unpack_info_len,
                               uint n_key_parts)
{
  uchar *tuple= packed_tuple;
  uchar *unpack_end= unpack_info;
  KEY *key_info= &tbl->key_info[keyno];

  store_index_number(tuple, index_number);
  tuple += INDEX_NUMBER_SIZE;

  // The following includes the 'extended key' tail:
  if (n_key_parts == 0 || n_key_parts == MAX_REF_PARTS)
    n_key_parts= m_key_parts;

  for (uint i=0; i < n_key_parts; i++)
  {
    Field *field= get_field_by_keynr(tbl, key_info, i);

    my_ptrdiff_t ptr_diff= record - tbl->record[0];

    if (field->real_maybe_null())
    {
      if (field->is_real_null(ptr_diff))
      {
        /* NULL value. store '\0' so that it sorts before non-NULL values */
        *tuple++ = 0;
        /* That's it, don't store anything else */
        continue;
      }
      else
      {
        /* Not a NULL value. Store '1' */
        *tuple++ = 1;
      }
    }

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
    *unpack_info_len= unpack_end - unpack_info;

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
  Take mem-comparable form and unpack_info and unpack it to Table->record

  @detail
    not all indexes support this

  @return
    0 - Ok
    1 - Data format error.
*/

int RDBSE_KEYDEF::unpack_record(TABLE *table, uchar *buf,
                                 const rocksdb::Slice *packed_key,
                                 const rocksdb::Slice *unpack_info)
{
  KEY * const key_info= &table->key_info[keyno];

  Stream_reader reader(packed_key);
  const uchar * const unpack_ptr= (const uchar*)unpack_info->data();
  my_ptrdiff_t ptr_diff= buf - table->record[0];

  if (unpack_info->size() != unpack_data_len)
    return 1;

  // Skip the index number
  if ((!reader.read(INDEX_NUMBER_SIZE)))
    return (uint)-1;

  for (uint i= 0; i < m_key_parts ; i++)
  {
    Field_pack_info *fpi= &pack_info[i];
    Field *field= get_field_by_keynr(table, key_info, i);

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
      if (fpi->skip_func(fpi, &reader))
        return 1;
    }
  }
  return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Field_pack_info
///////////////////////////////////////////////////////////////////////////////////////////

int skip_max_length(Field_pack_info *fpi, Stream_reader *reader)
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


int skip_variable_length(Field_pack_info *fpi, Stream_reader *reader)
{
  const uchar *ptr;
  bool finished= false;

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

    if (used_bytes < ESCAPE_LENGTH - 1)
    {
      finished= true;
      break;
    }
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

bool Field_pack_info::setup(Field *field)
{
  int res= false;
  enum_field_types type= field->real_type();

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

  if (type == MYSQL_TYPE_VARCHAR || type == MYSQL_TYPE_STRING)
  {
    /*
      For CHAR-based columns, check how strxfrm image will take.
      field->field_length = field->char_length() * cs->mbmaxlen.
    */
    const CHARSET_INFO *cs= field->charset();
    max_image_len= cs->coll->strnxfrmlen(cs, field->field_length);
  }

  if (type == MYSQL_TYPE_LONGLONG ||
      type == MYSQL_TYPE_LONG ||
      type == MYSQL_TYPE_INT24 ||
      type == MYSQL_TYPE_SHORT ||
      type == MYSQL_TYPE_TINY)
  {
    unpack_func= unpack_integer;
    return true;
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

/*
  Write table definition DDL entry.

  We write
    dbname.tablename -> {index_nr, index_nr, index_nr, ... }
*/

void RDBSE_TABLE_DEF::write_to(rocksdb::DB *rdb_dict, uchar *key, size_t keylen)
{
  StringBuffer<32> indexes;

  for (uint i=0; i < n_keys; i++)
  {
    write_int(&indexes, key_descr[i]->index_number);
  }
  rocksdb::Slice skey((char*)key, keylen);
  rocksdb::Slice svalue(indexes.c_ptr(), indexes.length());

  rocksdb::WriteOptions options;
  options.sync= true;
  rdb_dict->Put(options, skey, svalue);
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


bool Table_ddl_manager::init(rocksdb::DB *rdb_dict)
{
  mysql_rwlock_init(0, &rwlock);
  (void) my_hash_init(&ddl_hash, /*system_charset_info*/&my_charset_bin, 32,0,0,
                      (my_hash_get_key) Table_ddl_manager::get_hash_key,
                      Table_ddl_manager::free_hash_elem, 0);

  /* Read the data dictionary and populate the hash */
  uchar ddl_entry[RDBSE_KEYDEF::INDEX_NUMBER_SIZE];
  store_index_number(ddl_entry, DDL_ENTRY_INDEX_NUMBER);
  rocksdb::Slice ddl_entry_slice((char*)ddl_entry, RDBSE_KEYDEF::INDEX_NUMBER_SIZE);

  rocksdb::Iterator* it;
  it= rdb_dict->NewIterator(rocksdb::ReadOptions());
  int i= 0;
  int max_number= DDL_ENTRY_INDEX_NUMBER + 1;
  for (it->Seek(ddl_entry_slice); it->Valid(); it->Next())
  {
    char *ptr;
    char *ptr_end;
    RDBSE_TABLE_DEF *tdef= new RDBSE_TABLE_DEF;
    rocksdb::Slice key= it->key();
    rocksdb::Slice val= it->value();

    if (key.size() <= RDBSE_KEYDEF::INDEX_NUMBER_SIZE)
    {
      sql_print_error("RocksDB: Table_store: key has length %d (corruption?)",
                      (int)key.size());
      return true;
    }

    if (memcmp(key.data(), ddl_entry, RDBSE_KEYDEF::INDEX_NUMBER_SIZE))
      break;

    tdef->dbname_tablename.append(key.data() + RDBSE_KEYDEF::INDEX_NUMBER_SIZE,
                                  key.size() - RDBSE_KEYDEF::INDEX_NUMBER_SIZE);

    // Now, read the DDLs.

    if (val.size() < RDBSE_KEYDEF::INDEX_NUMBER_SIZE)
    {
      sql_print_error("RocksDB: Table_store: no keys defined in %*s",
                      (int)key.size(), key.data());
      return true;
    }
    if (val.size() % RDBSE_KEYDEF::INDEX_NUMBER_SIZE)
    {
      sql_print_error("RocksDB: Table_store: invalid keylist for table %s",
                      tdef->dbname_tablename.c_ptr_safe());
      return true;
    }
    tdef->n_keys= val.size() / RDBSE_KEYDEF::INDEX_NUMBER_SIZE;
    if (!(tdef->key_descr= (RDBSE_KEYDEF**)my_malloc(sizeof(RDBSE_KEYDEF*) *
                                                     tdef->n_keys,
                                                     MYF(MY_ZEROFILL))))
      return true;

    ptr= (char*)val.data();
    ptr_end= ptr + val.size();
    for (uint keyno=0; ptr < ptr_end; keyno++)
    {
      int index_number= read_int(&ptr);

      /*
        We can't fully initialize RDBSE_KEYDEF object here, because full
        initialization requires that there is an open TABLE* where we could
        look at Field* objects and set max_length and other attributes.
      */
      tdef->key_descr[keyno]= new RDBSE_KEYDEF(index_number, keyno);

      /* Keep track of what was the last index number we saw */
      if (max_number < index_number)
        max_number= index_number;
    }
    put(tdef);
    i++;
  }

  sequence.init(max_number+1);

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


RDBSE_TABLE_DEF* Table_ddl_manager::find(uchar *table_name,
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


int Table_ddl_manager::put_and_write(RDBSE_TABLE_DEF *tbl, rocksdb::DB *rdb_dict)
{
  uchar buf[NAME_LEN * 2 + RDBSE_KEYDEF::INDEX_NUMBER_SIZE];
  uint pos= 0;

  store_index_number(buf, DDL_ENTRY_INDEX_NUMBER);
  pos+= RDBSE_KEYDEF::INDEX_NUMBER_SIZE;

  memcpy(buf + pos, tbl->dbname_tablename.ptr(), tbl->dbname_tablename.length());
  pos += tbl->dbname_tablename.length();

  int res;
  if ((res= put(tbl)))
    return res;

  tbl->write_to(rdb_dict, buf, pos);
  return 0;
}


/* Return 0 - ok, other value - error */

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

  if (lock)
    mysql_rwlock_unlock(&rwlock);
  return result;
}


void Table_ddl_manager::remove(RDBSE_TABLE_DEF *tbl, rocksdb::DB *rdb_dict, bool lock)
{
  if (lock)
    mysql_rwlock_wrlock(&rwlock);

  uchar buf[NAME_LEN * 2 + RDBSE_KEYDEF::INDEX_NUMBER_SIZE];
  uint pos= 0;

  store_index_number(buf, DDL_ENTRY_INDEX_NUMBER);
  pos+= RDBSE_KEYDEF::INDEX_NUMBER_SIZE;

  memcpy(buf + pos, tbl->dbname_tablename.ptr(), tbl->dbname_tablename.length());
  pos += tbl->dbname_tablename.length();

  rocksdb::Slice tkey((char*)buf, pos);
  rdb_dict->Delete(rocksdb::WriteOptions(), tkey);

  /* The following will also delete the object: */
  my_hash_delete(&ddl_hash, (uchar*) tbl);

  if (lock)
    mysql_rwlock_unlock(&rwlock);
}


bool Table_ddl_manager::rename(uchar *from, uint from_len,
                               uchar *to, uint to_len,
                               rocksdb::DB *rdb_dict)
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
  store_index_number(new_buf, DDL_ENTRY_INDEX_NUMBER);
  new_pos+= RDBSE_KEYDEF::INDEX_NUMBER_SIZE;

  memcpy(new_buf + new_pos, new_rec->dbname_tablename.ptr(),
         new_rec->dbname_tablename.length());
  new_pos += new_rec->dbname_tablename.length();

  // Create a key to add
  new_rec->write_to(rdb_dict, new_buf, new_pos);
  remove(rec, rdb_dict, false);
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
