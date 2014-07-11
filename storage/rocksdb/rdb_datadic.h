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

class RDBSE_KEYDEF;
class Field_pack_info;

inline void store_index_number(uchar *dst, uint32 number)
{
#ifdef WORDS_BIGENDIAN
    memcpy(dst, &number, RDBSE_KEYDEF::INDEX_NUMBER_SIZE);
#else
    const uchar *src= (uchar*)&number;
    dst[0]= src[3];
    dst[1]= src[2];
    dst[2]= src[1];
    dst[3]= src[0];
#endif
}

/*
  An object of this class represents information about an index in an SQL
  table. It provides services to encode and decode index tuples.

  There are several data encodings.

  === SQL LAYER ===
  SQL layer uses two encodings:

  - "Table->record format". This is the format that is used for the data in
     the record buffers, table->record[i]

  - KeyTupleFormat (see opt_range.cc) - this is used in parameters to index
    lookup functions, like handler::index_read_map().

  === Inside RocksDB ===
  Primary Key is stored as a mapping:

    index_tuple -> StoredRecord

  StoredRecord is in Table->record format, except for blobs, which are stored
  in-place. See ha_rocksdb::convert_record_to_storage_format for details.

  Secondary indexes are stored as one of two variants:

    index_tuple -> unpack_info
    index_tuple -> empty_string

  index_tuple here is the form of key that can be compared with memcmp(), aka
  "mem-comparable form".

  unpack_info is extra data that allows to restore the original value from its
  mem-comparable form. It is present only if the index supports index-only
  reads.
*/

class RDBSE_KEYDEF
{
public:
  /* Convert a key from KeyTupleFormat to mem-comparable form */
  uint pack_index_tuple(TABLE *tbl, uchar *packed_tuple,
                        const uchar *key_tuple, key_part_map keypart_map);

  /* Convert a key from Table->record format to mem-comparable form */
  uint pack_record(TABLE *tbl, const uchar *record, uchar *packed_tuple,
                   uchar *unpack_info, int *unpack_info_len,
                   uint n_key_parts=0);
  int unpack_record(TABLE *table, uchar *buf, const rocksdb::Slice *packed_key,
                    const rocksdb::Slice *unpack_info);

  /* Get the key that is the "infimum" for this index */
  inline void get_infimum_key(uchar *key, uint *size)
  {
    store_index_number(key, index_number);
    *size= INDEX_NUMBER_SIZE;
  }

  /* Get the key that is a "supremum" for this index */
  inline void get_supremum_key(uchar *key, uint *size)
  {
    store_index_number(key, index_number+1);
    *size= INDEX_NUMBER_SIZE;
  }

  /* Make a key that is right after the given key. */
  void successor(uchar *packed_tuple, uint len);

  /*
    This can be used to compare prefixes.
    if  X is a prefix of Y, then we consider that X = Y.
  */
  int cmp_full_keys(const char *pa, uint a_len, const char *pb, uint b_len,
                    uint n_parts)
  {
    DBUG_ASSERT(covers_key(pa, a_len));
    DBUG_ASSERT(covers_key(pb, b_len));

    uint min_len= a_len < b_len? a_len : b_len;
    int res= memcmp(pa, pb, min_len);
    return res;
  }

  /* Check if given mem-comparable key belongs to this index */
  bool covers_key(const char *key, uint keylen)
  {
    if (keylen < INDEX_NUMBER_SIZE)
      return false;
    if (memcmp(key, index_number_storage_form, INDEX_NUMBER_SIZE))
      return false;
    else
      return true;
  }

  /* Must only be called for secondary keys: */
  uint get_primary_key_tuple(RDBSE_KEYDEF *pk_descr, const char *key,
                             char *pk_buffer);

  /* Return max length of mem-comparable form */
  uint max_storage_fmt_length()
  {
    return maxlength;
  }

  RDBSE_KEYDEF(uint indexnr_arg, uint keyno_arg) :
    index_number(indexnr_arg),
    pk_key_parts(NULL),
    pack_info(NULL),
    keyno(keyno_arg),
    maxlength(0) // means 'not intialized'
  {
    store_index_number(index_number_storage_form, indexnr_arg);
  }
  ~RDBSE_KEYDEF();

  enum {
    INDEX_NUMBER_SIZE= 4
  };

  void setup(TABLE *table);

private:

  /* Global number of this index (used as prefix in StorageFormat) */
  const uint32 index_number;

  uchar index_number_storage_form[INDEX_NUMBER_SIZE];

  friend class RDBSE_TABLE_DEF; // for index_number above

  class PK_KEY_PART
  {
  public:
    uint offset;
    uint size;
  };

  /*
    Array of descriptions of primary key columns.
     - element #0 describes the first PK column,
     - element #1 describes the second PK column, and so forth.
    the offsets are offsets of column representation in StorageFormat
    representation of this index.
  */
  PK_KEY_PART *pk_key_parts;
  uint n_pk_key_parts;

  /* Array of index-part descriptors. */
  Field_pack_info *pack_info;

  uint keyno; /* number of this index in the table */

  /*
    Length of the mem-comparable form. In the encoding we're using, it is
    constant (any value will have this length).
  */
  uint maxlength;

  /* Length of the unpack_data */
  uint unpack_data_len;
};


typedef void (*make_unpack_info_t) (Field_pack_info *fpi, Field *field, uchar *dst);
typedef int (*index_field_unpack_t)(Field_pack_info *fpi, Field *field,
                                    const uchar *tuple,
                                    const uchar *unpack_info);

/*
  This stores information about how a field can be packed to mem-comparable
  form and unpacked back.
*/

class Field_pack_info
{
public:
  /*
    Offset of the image of this field in the mem-comparable image. This field
    must be set from outside of the class
  */
  int image_offset;

  /* Length of mem-comparable image of the field, in bytes */
  int image_len;

  /* Length of image in the unpack data */
  int unpack_data_len;
  int unpack_data_offset;

  /* Offset of field data in table->record[i] from field->ptr. */
  int field_data_offset;

  bool maybe_null; /* TRUE <=> NULL-byte is stored */

  /*
    Pack function is assumed to be:
     - store NULL-byte, if needed
     - call field->make_sort_key();
    If you neeed to unpack, you should also call
  */
  make_unpack_info_t make_unpack_info_func;

  /*
    This function takes
    - mem-comparable form
    - unpack_info data
    and restores the original value.
  */
  index_field_unpack_t unpack_func;

  bool setup(Field *field);
};


/*
  A table definition. This is an entry in the mapping

    dbname.tablename -> {index_nr, index_nr, ... }

  There is only one RDBSE_TABLE_DEF object for a given table.
  That's why we keep auto_increment value here, too.
*/

class RDBSE_TABLE_DEF
{
public:
  RDBSE_TABLE_DEF() : key_descr(NULL), auto_incr_val(1)
  {
    mysql_mutex_init(0, &mutex, MY_MUTEX_INIT_FAST);
  }
  ~RDBSE_TABLE_DEF()
  {
    mysql_mutex_destroy(&mutex);
    /* Don't free key definitions */
    if (key_descr)
    {
      for (uint i= 0; i < n_keys; i++)
        delete key_descr[i];
      my_free(key_descr);
    }
  }
  /* Stores 'dbname.tablename' */
  StringBuffer<64> dbname_tablename;

  /* Number of indexes */
  uint n_keys;

  /* Array of index descriptors */
  RDBSE_KEYDEF **key_descr;

  mysql_mutex_t mutex; // guards the following:
  longlong auto_incr_val;

  void write_to(rocksdb::DB *rdb_dict, uchar *key, size_t keylen);
};


/*
  A thread-safe sequential number generator. Its performance is not a concern
*/

class Sequence_generator
{
  int next_number;

  mysql_mutex_t mutex;
public:
  void init(int initial_number)
  {
    mysql_mutex_init(0 , &mutex, MY_MUTEX_INIT_FAST);
    next_number= initial_number;
  }

  int get_next_number()
  {
    int res;
    mysql_mutex_lock(&mutex);
    res= next_number++;
    mysql_mutex_unlock(&mutex);
    return res;
  }

  void cleanup()
  {
    mysql_mutex_destroy(&mutex);
  }
};


/*
  This contains a mapping of

     dbname.table_name -> array{RDBSE_KEYDEF}.

  objects are shared among all threads.
*/

class Table_ddl_manager
{
  HASH ddl_hash; // Contains RDBSE_TABLE_DEF elements
  mysql_rwlock_t rwlock;

  Sequence_generator sequence;

  enum {
    DDL_ENTRY_INDEX_NUMBER=1
  };

public:
  bool init(rocksdb::DB *rdb_dict);
  void cleanup();

  int put_and_write(RDBSE_TABLE_DEF *key_descr, rocksdb::DB *rdb_dict);
  int put(RDBSE_TABLE_DEF *key_descr, bool lock= true);
  void remove(RDBSE_TABLE_DEF *rec, rocksdb::DB *rdb_dict, bool lock=true);

  RDBSE_TABLE_DEF *find(uchar *table_name, uint len, bool lock=true);

  bool rename(uchar *from, uint from_len, uchar *to, uint to_len,
              rocksdb::DB *rdb_dict);

  int get_next_number() { return sequence.get_next_number(); }
private:
  static uchar* get_hash_key(RDBSE_TABLE_DEF *rec, size_t *length,
                             my_bool not_used __attribute__((unused)));
  static void free_hash_elem(void* data);
};
