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
#include "ha_leveldb.h"
#include "sql_class.h"
#include "my_bit.h"

#include "ldb_datadic.h"


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


LDBSE_KEYDEF::~LDBSE_KEYDEF()
{
  if (pk_key_parts)
    my_free(pk_key_parts);
  if (pack_info)
    my_free(pack_info);
}


void LDBSE_KEYDEF::setup(TABLE *tbl)
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
      pk_key_parts= (PK_KEY_PART*)my_malloc(sizeof(PK_KEY_PART) * n_pk_key_parts, MYF(0));
    }
    else
    {
      pk_info= NULL;
      pk_key_parts= 0;
    }

    size_t size= sizeof(Field_pack_info) * key_info->actual_key_parts;
    pack_info= (Field_pack_info*)my_malloc(size, MYF(0));

    uint len= INDEX_NUMBER_SIZE;
    int unpack_len= 0;
    /* this loop also loops over the 'extended key' tail */
    for (uint i= 0; i < key_info->actual_key_parts; i++)
    {
      Field *field= key_info->key_part[i].field;

      if (field->real_maybe_null())
        len +=1; // NULL-byte

      pack_info[i].setup(key_info->key_part[i].field);
      pack_info[i].image_offset= len;
      pack_info[i].unpack_data_offset= unpack_len;

      if (pk_info)
      {
        for (uint j= 0; j < n_pk_key_parts; j++)
        {
          if (field->field_index == pk_info->key_part[j].field->field_index)
          {
            pk_key_parts[j].offset= len;
            pk_key_parts[j].size=   pack_info[i].image_len;
          }
        }
      }

      len        += pack_info[i].image_len;
      unpack_len += pack_info[i].unpack_data_len;
    }
    maxlength= len;
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

    LevelDB SE supports "Extended keys". This means that PK columns are present
    at the end of every key.  If the key already includes PK columns, then
    these columns are not present at the end of the key.

    Because of the above, we copy each primary key column.
*/

uint LDBSE_KEYDEF::get_primary_key_tuple(LDBSE_KEYDEF *pk_descr,
                                         const char *key, char *pk_buffer)
{
  uint size= 0;
  char *buf= pk_buffer;
  DBUG_ASSERT(n_pk_key_parts);

  // copy the PK number
  store_index_number((uchar*)buf, pk_descr->index_number);
  buf += INDEX_NUMBER_SIZE;
  size += INDEX_NUMBER_SIZE;

  for (uint j= 0; j < n_pk_key_parts; j++)
  {
    uint len= pk_key_parts[j].size;
    memcpy(buf, key + pk_key_parts[j].offset, len);
    buf += len;
    size += len;
  }
  return size;
}


uint LDBSE_KEYDEF::pack_index_tuple(TABLE *tbl, uchar *packed_tuple,
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


void LDBSE_KEYDEF::successor(uchar *packed_tuple, uint len)
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
*/

uint LDBSE_KEYDEF::pack_record(TABLE *tbl, const uchar *record,
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
    n_key_parts= key_info->actual_key_parts;

  for (uint i=0; i < n_key_parts; i++)
  {
    Field *field= key_info->key_part[i].field;
    my_ptrdiff_t ptr_diff= record - tbl->record[0];
    field->move_field_offset(ptr_diff);

    const int length= pack_info[i].image_len;
    if (field->real_maybe_null())
    {
      if (field->is_real_null())
      {
        /* NULL value. store '\0' so that it sorts before non-NULL values */
        *tuple++ = 0;
        memset(tuple, 0, length);
      }
      else
      {
        // store '1'
        *tuple++ = 1;
        field->make_sort_key(tuple, length);
      }
    }
    else
      field->make_sort_key(tuple, length);

    tuple += length;

    if (unpack_end && pack_info && pack_info[i].make_unpack_info_func)
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


/*
  Take mem-comparable form and unpack_info and unpack it to Table->record

  @detail
    not all indexes support this
*/

int LDBSE_KEYDEF::unpack_record(TABLE *table, uchar *buf,
                                 const leveldb::Slice *packed_key,
                                 const leveldb::Slice *unpack_info)
{
  int res= 0;
  KEY * const key_info= &table->key_info[keyno];

  const uchar * const key_ptr= (const uchar*)packed_key->data();
  const uchar * const unpack_ptr= (const uchar*)unpack_info->data();

  if (packed_key->size() != max_storage_fmt_length())
    return 1;

  if (unpack_info->size() != unpack_data_len)
    return 1;

  for (uint i= 0; i < key_info->actual_key_parts ; i++)
  {
    Field *field= key_info->key_part[i].field;
    Field_pack_info *fpi= &pack_info[i];

    if (fpi->unpack_func)
    {
      my_ptrdiff_t ptr_diff= buf - table->record[0];
      field->move_field_offset(ptr_diff);

      if (fpi->maybe_null)
      {
        if (*(key_ptr + (fpi->image_offset - 1)) == 0)
          field->set_null();
        else
          field->set_notnull();
      }

      res= fpi->unpack_func(fpi, field, key_ptr + fpi->image_offset,
                            unpack_ptr + fpi->unpack_data_offset);
      field->move_field_offset(-ptr_diff);

      if (res)
        break; /* Error */
    }
  }
  return res;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Field_pack_info
///////////////////////////////////////////////////////////////////////////////////////////

int unpack_integer(Field_pack_info *fpi, Field *field,
                   const uchar *from, const uchar *unpack_info)
{
  const int length= field->pack_length();
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


/* Unpack the string by copying it over */
int unpack_binary_str(Field_pack_info *fpi, Field *field,
                      const uchar *tuple,
                      const uchar *unpack_info)
{
  memcpy(field->ptr + fpi->field_data_offset, tuple, fpi->image_len);
  return 0;
}


/*
  For UTF-8, we need to convert 2-byte wide-character entities back into
  UTF8 sequences.
*/

int unpack_utf8_str(Field_pack_info *fpi, Field *field,
                    const uchar *tuple,
                    const uchar *unpack_info)
{
  CHARSET_INFO *cset= (CHARSET_INFO*)field->charset();
  const uchar *src= tuple;
  const uchar *src_end= tuple + fpi->image_len;
  uchar *dst= field->ptr + fpi->field_data_offset;
  uchar *dst_end= dst + fpi->image_len;

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


int unpack_binary_varchar(Field_pack_info *fpi, Field *field,
                          const uchar *tuple,
                          const uchar *unpack_info)
{
  uint32 length_bytes= ((Field_varstring*)field)->length_bytes;
  //copy the length bytes
  memcpy(field->ptr, unpack_info, length_bytes);

  return unpack_binary_str(fpi, field, tuple, unpack_info);
}


int unpack_utf8_varchar(Field_pack_info *fpi, Field *field,
                        const uchar *tuple,
                        const uchar *unpack_info)
{
  uint32 length_bytes= ((Field_varstring*)field)->length_bytes;
  //copy the length bytes
  memcpy(field->ptr, unpack_info, length_bytes);

  return unpack_utf8_str(fpi, field, tuple, unpack_info);
}


/*
  For varchar, save the length.
*/
void make_varchar_unpack_info(Field_pack_info *fsi, Field *field, uchar *unpack_data)
{
  // TODO: use length from fsi.
  Field_varstring *fv= (Field_varstring*)field;
  memcpy(unpack_data, fv->ptr, fv->length_bytes);
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
  image_len= field->pack_length();
  if (type == MYSQL_TYPE_VARCHAR || type == MYSQL_TYPE_STRING)
  {
    /*
      For CHAR-based columns, check how strxfrm image will take.
      field->field_length = field->char_length() * cs->mbmaxlen.
    */
    const CHARSET_INFO *cs= field->charset();
    image_len= cs->coll->strnxfrmlen(cs, field->field_length);
  }

  if (type == MYSQL_TYPE_LONGLONG ||
      type == MYSQL_TYPE_LONG ||
      type == MYSQL_TYPE_INT24 ||
      type == MYSQL_TYPE_SHORT ||
      type == MYSQL_TYPE_TINY)
  {
    unpack_func= unpack_integer;
    make_unpack_info_func= NULL;
    return true;
  }

  const bool is_varchar= (type == MYSQL_TYPE_VARCHAR);
  if (is_varchar)
  {
    make_unpack_info_func= make_varchar_unpack_info;
    unpack_data_len= ((Field_varstring*)field)->length_bytes;
    field_data_offset= ((Field_varstring*)field)->length_bytes;
  }

  if (type == MYSQL_TYPE_VARCHAR || type == MYSQL_TYPE_STRING)
  {
    const CHARSET_INFO *cs= field->charset();

    if (cs == &my_charset_bin ||
        cs == &my_charset_latin1_bin)
    {
      unpack_func= is_varchar? unpack_binary_varchar : unpack_binary_str;
      res= true;
    }
    else if(cs == &my_charset_utf8_bin)
    {
      unpack_func= is_varchar? unpack_utf8_varchar : unpack_utf8_str;
      res= true;
    }
  }
  return res;
}


#if 0
void _ldbse_store_blob_length(uchar *pos,uint pack_length,uint length)
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

void LDBSE_TABLE_DEF::write_to(leveldb::DB *ldb_dict, uchar *key, size_t keylen)
{
  StringBuffer<32> indexes;

  for (uint i=0; i < n_keys; i++)
  {
    write_int(&indexes, key_descr[i]->index_number);
  }
  leveldb::Slice skey((char*)key, keylen);
  leveldb::Slice svalue(indexes.c_ptr(), indexes.length());

  leveldb::WriteOptions options;
  options.sync= true;
  ldb_dict->Put(options, skey, svalue);
}


uchar* Table_ddl_manager::get_hash_key(LDBSE_TABLE_DEF *rec, size_t *length,
                                       my_bool not_used __attribute__((unused)))
{
  *length= rec->dbname_tablename.length();
  return (uchar*) rec->dbname_tablename.c_ptr();
}


void Table_ddl_manager::free_hash_elem(void* data)
{
  LDBSE_TABLE_DEF* elem= (LDBSE_TABLE_DEF*)data;
  delete elem;
}


bool Table_ddl_manager::init(leveldb::DB *ldb_dict)
{
  mysql_rwlock_init(0, &rwlock);
  (void) my_hash_init(&ddl_hash, /*system_charset_info*/&my_charset_bin, 32,0,0,
                      (my_hash_get_key) Table_ddl_manager::get_hash_key,
                      Table_ddl_manager::free_hash_elem, 0);

  /* Read the data dictionary and populate the hash */
  uchar ddl_entry[LDBSE_KEYDEF::INDEX_NUMBER_SIZE];
  store_index_number(ddl_entry, DDL_ENTRY_INDEX_NUMBER);
  leveldb::Slice ddl_entry_slice((char*)ddl_entry, LDBSE_KEYDEF::INDEX_NUMBER_SIZE);

  leveldb::Iterator* it;
  it= ldb_dict->NewIterator(leveldb::ReadOptions());
  int i= 0;
  int max_number= DDL_ENTRY_INDEX_NUMBER + 1;
  for (it->Seek(ddl_entry_slice); it->Valid(); it->Next())
  {
    char *ptr;
    char *ptr_end;
    LDBSE_TABLE_DEF *tdef= new LDBSE_TABLE_DEF;
    leveldb::Slice key= it->key();
    leveldb::Slice val= it->value();

    if (key.size() <= LDBSE_KEYDEF::INDEX_NUMBER_SIZE)
    {
      sql_print_error("LevelDB: Table_store: key has length %d (corruption?)",
                      (int)key.size());
      return true;
    }

    if (memcmp(key.data(), ddl_entry, LDBSE_KEYDEF::INDEX_NUMBER_SIZE))
      break;

    tdef->dbname_tablename.append(key.data() + LDBSE_KEYDEF::INDEX_NUMBER_SIZE,
                                  key.size() - LDBSE_KEYDEF::INDEX_NUMBER_SIZE);

    // Now, read the DDLs.

    if (val.size() < LDBSE_KEYDEF::INDEX_NUMBER_SIZE)
    {
      sql_print_error("LevelDB: Table_store: no keys defined in %*s",
                      (int)key.size(), key.data());
      return true;
    }
    if (val.size() % LDBSE_KEYDEF::INDEX_NUMBER_SIZE)
    {
      sql_print_error("LevelDB: Table_store: invalid keylist for table %s",
                      tdef->dbname_tablename.c_ptr_safe());
      return true;
    }
    tdef->n_keys= val.size() / LDBSE_KEYDEF::INDEX_NUMBER_SIZE;
    if (!(tdef->key_descr= (LDBSE_KEYDEF**)my_malloc(sizeof(LDBSE_KEYDEF*) *
                                                     tdef->n_keys,
                                                     MYF(MY_ZEROFILL))))
      return true;

    ptr= (char*)val.data();
    ptr_end= ptr + val.size();
    for (uint keyno=0; ptr < ptr_end; keyno++)
    {
      int index_number= read_int(&ptr);

      /*
        We can't fully initialize LDBSE_KEYDEF object here, because full
        initialization requires that there is an open TABLE* where we could
        look at Field* objects and set max_length and other attributes.
      */
      tdef->key_descr[keyno]= new LDBSE_KEYDEF(index_number, keyno);

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
    sql_print_error("LevelDB: Table_store: load error: %s", s.c_str());
    return true;
  }
  delete it;
  sql_print_information("LevelDB: Table_store: loaded DDL data for %d tables", i);
  return false;
}


LDBSE_TABLE_DEF* Table_ddl_manager::find(uchar *table_name,
                                         uint table_name_len,
                                         bool lock)
{
  LDBSE_TABLE_DEF *rec;
  if (lock)
    mysql_rwlock_rdlock(&rwlock);
  rec= (LDBSE_TABLE_DEF*)my_hash_search(&ddl_hash, (uchar*)table_name,
                                        table_name_len);
  if (lock)
    mysql_rwlock_unlock(&rwlock);
  return rec;
}


int Table_ddl_manager::put_and_write(LDBSE_TABLE_DEF *tbl, leveldb::DB *ldb_dict)
{
  uchar buf[NAME_LEN * 2 + LDBSE_KEYDEF::INDEX_NUMBER_SIZE];
  uint pos= 0;

  store_index_number(buf, DDL_ENTRY_INDEX_NUMBER);
  pos+= LDBSE_KEYDEF::INDEX_NUMBER_SIZE;

  memcpy(buf + pos, tbl->dbname_tablename.ptr(), tbl->dbname_tablename.length());
  pos += tbl->dbname_tablename.length();

  int res;
  if ((res= put(tbl)))
    return res;

  tbl->write_to(ldb_dict, buf, pos);
  return 0;
}


/* Return 0 - ok, other value - error */

int Table_ddl_manager::put(LDBSE_TABLE_DEF *tbl, bool lock)
{
  LDBSE_TABLE_DEF *rec;
  my_bool result;

  if (lock)
    mysql_rwlock_wrlock(&rwlock);
  rec= (LDBSE_TABLE_DEF*)find((uchar*)tbl->dbname_tablename.c_ptr(),
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


void Table_ddl_manager::remove(LDBSE_TABLE_DEF *tbl, leveldb::DB *ldb_dict, bool lock)
{
  if (lock)
    mysql_rwlock_wrlock(&rwlock);

  uchar buf[NAME_LEN * 2 + LDBSE_KEYDEF::INDEX_NUMBER_SIZE];
  uint pos= 0;

  store_index_number(buf, DDL_ENTRY_INDEX_NUMBER);
  pos+= LDBSE_KEYDEF::INDEX_NUMBER_SIZE;

  memcpy(buf + pos, tbl->dbname_tablename.ptr(), tbl->dbname_tablename.length());
  pos += tbl->dbname_tablename.length();

  leveldb::Slice tkey((char*)buf, pos);
  ldb_dict->Delete(leveldb::WriteOptions(), tkey);

  /* The following will also delete the object: */
  my_hash_delete(&ddl_hash, (uchar*) tbl);

  if (lock)
    mysql_rwlock_unlock(&rwlock);
}


bool Table_ddl_manager::rename(uchar *from, uint from_len,
                               uchar *to, uint to_len,
                               leveldb::DB *ldb_dict)
{
  LDBSE_TABLE_DEF *rec;
  LDBSE_TABLE_DEF *new_rec;
  bool res= true;
  uchar new_buf[NAME_LEN * 2 + LDBSE_KEYDEF::INDEX_NUMBER_SIZE];
  uint new_pos= 0;

  mysql_rwlock_wrlock(&rwlock);
  if (!(rec= (LDBSE_TABLE_DEF*)find(from, from_len, false)))
    goto err;

  if (!(new_rec= new LDBSE_TABLE_DEF))
    goto err;

  new_rec->dbname_tablename.append((char*)to, to_len);
  new_rec->n_keys= rec->n_keys;
  new_rec->auto_incr_val= rec->auto_incr_val;
  new_rec->key_descr= rec->key_descr;
  rec->key_descr= NULL; /* so that it's not free'd when deleting the old rec */

  // Create a new key
  store_index_number(new_buf, DDL_ENTRY_INDEX_NUMBER);
  new_pos+= LDBSE_KEYDEF::INDEX_NUMBER_SIZE;

  memcpy(new_buf + new_pos, new_rec->dbname_tablename.ptr(),
         new_rec->dbname_tablename.length());
  new_pos += new_rec->dbname_tablename.length();

  // Create a key to add
  new_rec->write_to(ldb_dict, new_buf, new_pos);
  remove(rec, ldb_dict, false);
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
