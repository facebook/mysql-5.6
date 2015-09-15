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
#pragma once

class Dict_manager;
class RDBSE_KEYDEF;
class Field_pack_info;
class Column_family_manager;
class Table_ddl_manager;

/* C++ standard header files */
#include <map>
#include <mutex>
#include <unordered_set>
#include <utility>

/* C standard header files */
#include <arpa/inet.h>

/* MyRocks header files */
#include "./ha_rocksdb.h"
#include "./properties_collector.h"

bool is_myrocks_collation_supported(Field *field);
void write_int64(String *out, uint64 val);
void write_int(String *out, uint32 val);
void write_short(String *out, uint16 val);
void write_byte(String *out, uchar val);
uint32 read_int(const char **data);
uint64 read_int64(const char **data);
uint16 read_short(const char **data);
uchar read_byte(const char **data);

inline void store_big_uint4(uchar *dst, uint32_t n)
{
  uint32_t src= htonl(n);
  memcpy(dst, &src, 4);
}

inline void store_big_uint2(uchar *dst, uint16_t n)
{
  uint16_t src= htons(n);
  memcpy(dst, &src, 2);
}

inline void store_big_uint1(uchar *dst, uchar n)
{
  *dst= n;
}

inline uint32_t read_big_uint4(const uchar* b)
{
  return(((uint32_t)(b[0]) << 24)
    | ((uint32_t)(b[1]) << 16)
    | ((uint32_t)(b[2]) << 8)
    | (uint32_t)(b[3])
    );
}

inline uint16_t read_big_uint2(const uchar* b)
{
  return(((uint16_t)(b[0]) << 8)
    | (uint16_t)(b[1])
    );
}

inline uchar read_big_uint1(const uchar* b)
{
  return(uchar)b[0];
}

inline void store_index_number(uchar *dst, uint32 number)
{
  store_big_uint4(dst, number);
}

/*
  A simple string reader.
  - it keeps position within the string that we read from
  - it prevents one from reading beyond the end of the string.
  (todo: rename to String_reader)
*/

class Stream_reader
{
  const char* ptr;
  uint len;
public:
  explicit Stream_reader(const std::string &str)
  {
    len= str.length();
    if (len)
      ptr= &str.at(0);
    else
    {
      /*
        One can a create a Stream_reader for reading from an empty string
        (although attempts to read anything will fail).
        We must not access str.at(0), since len==0, we can set ptr to any
        value.
      */
      ptr= NULL;
    }
  }

  explicit Stream_reader(const rocksdb::Slice *slice)
  {
    ptr= slice->data();
    len= slice->size();
  }

  /*
    Read the next @param size bytes. Returns pointer to the bytes read, or
    NULL if the remaining string doesn't have that many bytes.
  */
  const char *read(uint size)
  {
    const char *res;
    if (len < size)
      res= NULL;
    else
    {
      res= ptr;
      ptr += size;
      len -= size;
    }
    return res;
  }
  uint remaining_bytes() { return len; }

  /*
    Return pointer to data that will be read by next read() call (if there is
    nothing left to read, returns pointer to beyond the end of previous read()
    call)
  */
  const char *get_current_ptr() { return ptr; }
};


const uint INVALID_LEN= uint(-1);

/* How much one checksum occupies when stored in the record */
const size_t CHECKSUM_SIZE= sizeof(uint32_t);

/*
  How much the checksum data occupies in record, in total.
  It is storing two checksums plus 1 tag-byte.
*/
const size_t CHECKSUM_CHUNK_SIZE= 2 * CHECKSUM_SIZE + 1;

/*
  Checksum data starts from CHECKSUM_DATA_TAG which is followed by two CRC32
  checksums.
*/
const char CHECKSUM_DATA_TAG=0x01;



void report_checksum_mismatch(RDBSE_KEYDEF *kd, bool is_key,
                              const char *data, size_t data_size);

void hexdump_value(char *strbuf, size_t strbuf_size,
                   const rocksdb::Slice &val);

/*
  An object of this class represents information about an index in an SQL
  table. It provides services to encode and decode index tuples.

  Note: a table (as in, on-disk table) has a single RDBSE_KEYDEF object which
  is shared across multiple TABLE* objects and may be used simultaneously from
  different threads.

  There are several data encodings:

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
  uint pack_index_tuple(const ha_rocksdb *handler, TABLE *tbl,
                        uchar *pack_buffer,
                        uchar *packed_tuple,
                        const uchar *key_tuple, key_part_map keypart_map);

  /* Convert a key from Table->record format to mem-comparable form */
  uint pack_record(const ha_rocksdb *handler,
                   TABLE *tbl,
                   uchar *pack_buffer,
                   const uchar *record,
                   uchar *packed_tuple,
                   uchar *unpack_info,
                   int *unpack_info_len,
                   uint n_key_parts=0,
                   uint *n_null_fields=NULL);
  int unpack_record(const ha_rocksdb *handler,
                    TABLE *table, uchar *buf, const rocksdb::Slice *packed_key,
                    const rocksdb::Slice *unpack_info);

  bool unpack_info_has_checksum(const rocksdb::Slice& unpack_info);
  int compare_keys(const rocksdb::Slice *key1, const rocksdb::Slice *key2,
                   std::size_t* column_index);

  size_t key_length(TABLE *table, const rocksdb::Slice &key);
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

    @detail
      n_parts parameter is not used anymore. TODO: remove it.
  */
  // {pb, b_len} describe the lookup key, which can be a prefix of pa/a_len.
  int cmp_full_keys(const char *pa, uint a_len, const char *pb, uint b_len,
                    uint n_parts=0) const
  {
    DBUG_ASSERT(covers_key(pa, a_len));
    DBUG_ASSERT(covers_key(pb, b_len));

    uint min_len= a_len < b_len? a_len : b_len;
    int res= memcmp(pa, pb, min_len);
    return res;
  }

  /* Check if given mem-comparable key belongs to this index */
  bool covers_key(const char *key, uint keylen) const
  {
    if (keylen < INDEX_NUMBER_SIZE)
      return false;
    if (memcmp(key, index_number_storage_form, INDEX_NUMBER_SIZE))
      return false;
    else
      return true;
  }

  /*
    Return true if the passed mem-comparable key
    - is from this index, and
    - it matches the passed key prefix (the prefix is also in mem-comparable
      form)
  */
  bool value_matches_prefix(const rocksdb::Slice &value,
                            const rocksdb::Slice &prefix) const
  {
    return covers_key(value.data(), value.size()) &&
           !cmp_full_keys(value.data(), value.size(),
                          prefix.data(), prefix.size());
  }

  uint32 get_index_number()
  {
    return index_number;
  }

  GL_INDEX_ID get_gl_index_id()
  {
    GL_INDEX_ID gl_index_id = { cf_handle->GetID(), index_number };
    return gl_index_id;
  }

  /* Must only be called for secondary keys: */
  uint get_primary_key_tuple(TABLE *tbl, RDBSE_KEYDEF *pk_descr,
                             const rocksdb::Slice *key, char *pk_buffer);

  /* Return max length of mem-comparable form */
  uint max_storage_fmt_length()
  {
    return maxlength;
  }

  uint get_m_key_parts()
  {
    return m_key_parts;
  }

  const std::string& get_name() const {
    return name;
  }

  RDBSE_KEYDEF(const RDBSE_KEYDEF& k);
  RDBSE_KEYDEF(uint indexnr_arg, uint keyno_arg,
               rocksdb::ColumnFamilyHandle* cf_handle_arg,
               uint16_t index_dict_version_arg,
               uchar index_type_arg,
               uint16_t kv_format_version_arg,
               bool is_reverse_cf_arg, bool is_auto_cf_arg,
               const char* _name,
               MyRocksTablePropertiesCollector::IndexStats _stats
                 =MyRocksTablePropertiesCollector::IndexStats()
              );
  ~RDBSE_KEYDEF();

  enum {
    INDEX_NUMBER_SIZE= 4,
    VERSION_SIZE= 2,
    CF_NUMBER_SIZE= 4,
    CF_FLAG_SIZE= 4,
    PACKED_SIZE= 4, // one int
  };

  // bit flags for combining bools when writing to disk
  enum {
    REVERSE_CF_FLAG = 1,
    AUTO_CF_FLAG = 2,
  };

  // Data dictionary types
  enum {
    DDL_ENTRY_INDEX_START_NUMBER= 1,
    INDEX_INFO= 2,
    CF_DEFINITION= 3,
    BINLOG_INFO_INDEX_NUMBER= 4,
    DDL_DROP_INDEX_ONGOING= 5,
    INDEX_STATISTICS= 6,
    MAX_INDEX_ID= 7,
    END_DICT_INDEX_ID=255,
  };

  // Data dictionary schema version. Introduce newer versions
  // if changing schema layout
  enum {
    DDL_ENTRY_INDEX_VERSION= 1,
    CF_DEFINITION_VERSION= 1,
    BINLOG_INFO_INDEX_NUMBER_VERSION= 1,
    DDL_DROP_INDEX_ONGOING_VERSION= 1,
    MAX_INDEX_ID_VERSION= 1,
    // Version for index stats is stored in IndexStats struct
  };

  // Index info version.  Introduce newer versions when changing the
  // INDEX_INFO layout
  enum {
    INDEX_INFO_VERSION_INITIAL= 1,    // Obsolete
    INDEX_INFO_VERSION_KV_FORMAT,
    INDEX_INFO_VERSION_GLOBAL_ID,
  };

  // MyRocks index types
  enum {
    INDEX_TYPE_PRIMARY= 1,
    INDEX_TYPE_SECONDARY= 2,
  };

  // Key/Value format version for each index type
  enum {
    PRIMARY_FORMAT_VERSION_INITIAL= 10,
    SECONDARY_FORMAT_VERSION_INITIAL= 10,
  };

  void setup(TABLE *table);

  rocksdb::ColumnFamilyHandle *get_cf() { return cf_handle; }

  /* Check if keypart #kp can be unpacked from index tuple */
  bool can_unpack(uint kp) const;

  /*
    Current code assumes that unpack_data occupies fixed length regardless of
    the value that is stored.
  */
  bool get_unpack_data_len() { return unpack_data_len; }
private:

  /* Global number of this index (used as prefix in StorageFormat) */
  const uint32 index_number;

  uchar index_number_storage_form[INDEX_NUMBER_SIZE];

  rocksdb::ColumnFamilyHandle* cf_handle;

public:
  void set_keyno(uint _keyno) { keyno = _keyno; }

  uint16_t index_dict_version;
  uchar index_type;
  /* KV format version for the index id */
  uint16_t kv_format_version;
  /* If true, the column family stores data in the reverse order */
  bool is_reverse_cf;

  bool is_auto_cf;
  std::string name;
  MyRocksTablePropertiesCollector::IndexStats stats;
private:

  friend class RDBSE_TABLE_DEF; // for index_number above

  /* Number of key parts in the primary key*/
  uint n_pk_key_parts;

  /*
     pk_part_no[X]=Y means that keypart #X of this key is key part #Y of the
     primary key.  Y==-1 means this column is not present in the primary key.
  */
  uint *pk_part_no;

  /* Array of index-part descriptors. */
  Field_pack_info *pack_info;

  uint keyno; /* number of this index in the table */

  /*
    Number of key parts in the index (including "index extension"). This is how
    many elemants are in the pack_info array.
  */
  uint m_key_parts;

  /* Maximum length of the mem-comparable form. */
  uint maxlength;

  /* Length of the unpack_data */
  uint unpack_data_len;
};


typedef void (*make_unpack_info_t) (Field_pack_info *fpi, Field *field, uchar *dst);
typedef int (*index_field_unpack_t)(Field_pack_info *fpi, Field *field,
                                    Stream_reader *reader,
                                    const uchar *unpack_info);

typedef int (*index_field_skip_t)(Field_pack_info *fpi, Field *field, Stream_reader *reader);

typedef void (*index_field_pack_t)(Field_pack_info *fpi, Field *field, uchar* buf, uchar **dst);

/*
  This stores information about how a field can be packed to mem-comparable
  form and unpacked back.
*/

class Field_pack_info
{
public:
  /* Length of mem-comparable image of the field, in bytes */
  int max_image_len;

  /* Length of image in the unpack data */
  int unpack_data_len;
  int unpack_data_offset;

  /* Offset of field data in table->record[i] from field->ptr. */
  int field_data_offset;

  bool maybe_null; /* TRUE <=> NULL-byte is stored */

  /*
    Valid only for VARCHAR fields.
  */
  const CHARSET_INFO *varchar_charset;

  index_field_pack_t pack_func;

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

  /*
    This function skips over mem-comparable form.
  */
  index_field_skip_t skip_func;

private:
  /*
    Location of the field in the table (key number and key part number).

    Note that this describes not the field, but rather a position of field in
    the index. Consider an example:

      col1 VARCHAR (100),
      INDEX idx1 (col1)),
      INDEX idx2 (col1(10)),

    Here, idx2 has a special Field object that is set to describe a 10-char
    prefix of col1.

    We must also store the keynr. It is needed for implicit "extended keys".
    Every key in MyRocks needs to include PK columns.  Generally, SQL layer
    includes PK columns as part of its "Extended Keys" feature, but sometimes
    it does not (known examples are unique secondary indexes and partitioned
    tables).
    In that case, MyRocks's index descriptor has invisible suffix of PK
    columns (and the point is that these columns are parts of PK, not parts
    of the current index).
  */
  uint keynr;
  uint key_part;
public:
  bool setup(Field *field, uint keynr_arg, uint key_part_arg);
  Field *get_field_in_table(TABLE *tbl);
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
  ~RDBSE_TABLE_DEF();

  /* Stores 'dbname.tablename' */
  StringBuffer<64> dbname_tablename;

  /* Number of indexes */
  uint n_keys;

  /* Array of index descriptors */
  RDBSE_KEYDEF **key_descr;

  mysql_mutex_t mutex; // guards the following:
  longlong auto_incr_val;

  bool put_dict(Dict_manager *dict, rocksdb::WriteBatch *batch,
                uchar *key, size_t keylen);
};


/*
  A thread-safe sequential number generator. Its performance is not a concern
*/

class Sequence_generator
{
  uint next_number;

  mysql_mutex_t mutex;
public:
  void init(uint initial_number)
  {
    mysql_mutex_init(0 , &mutex, MY_MUTEX_INIT_FAST);
    next_number= initial_number;
  }

  uint get_and_update_next_number(Dict_manager *dict);

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
  Dict_manager *dict;
  HASH ddl_hash; // Contains RDBSE_TABLE_DEF elements
  // maps index id to <table_name, index number>
  std::map<GL_INDEX_ID, std::pair<std::basic_string<uchar>, uint>>
    index_num_to_keydef;
  mysql_rwlock_t rwlock;

  Sequence_generator sequence;
  // A queue of table stats to write into data dictionary
  // It is produced by event listener (ie compaction and flush threads)
  // and consumed by the rocksdb background thread
  std::map<GL_INDEX_ID, MyRocksTablePropertiesCollector::IndexStats>
    stats2store;
public:
  /* Load the data dictionary from on-disk storage */
  bool init(Dict_manager *dict_arg, Column_family_manager *cf_manager,
            uint32_t validate_tables);

  void cleanup();

  RDBSE_TABLE_DEF *find(const uchar *table_name, uint len, bool lock=true);
  RDBSE_KEYDEF* find(GL_INDEX_ID gl_index_id);
  std::unique_ptr<RDBSE_KEYDEF> get_copy_of_keydef(GL_INDEX_ID gl_index_id);
  void set_stats(
    const std::unordered_map<GL_INDEX_ID,
    MyRocksTablePropertiesCollector::IndexStats>& stats
  );
  void adjust_stats(
    const std::vector<MyRocksTablePropertiesCollector::IndexStats>& new_data,
    const std::vector<MyRocksTablePropertiesCollector::IndexStats>& deleted_data
     =std::vector<MyRocksTablePropertiesCollector::IndexStats>());
  void persist_stats();

  /* Modify the mapping and write it to on-disk storage */
  int put_and_write(RDBSE_TABLE_DEF *key_descr, rocksdb::WriteBatch *batch);
  void remove(RDBSE_TABLE_DEF *rec, rocksdb::WriteBatch *batch, bool lock=true);
  bool rename(uchar *from, uint from_len, uchar *to, uint to_len,
              rocksdb::WriteBatch *batch);

  uint get_and_update_next_number(Dict_manager *dict)
    { return sequence.get_and_update_next_number(dict); }

  /* Walk the data dictionary */
  int scan(void* cb_arg, int (*callback)(void* cb_arg, RDBSE_TABLE_DEF*));

  void erase_index_num(GL_INDEX_ID gl_index_id);
private:
  /* Put the data into in-memory table (only) */
  int put(RDBSE_TABLE_DEF *key_descr, bool lock= true);

  static uchar* get_hash_key(RDBSE_TABLE_DEF *rec, size_t *length,
                             my_bool not_used __attribute__((unused)));
  static void free_hash_elem(void* data);
  rocksdb::ColumnFamilyHandle* system_cfh;

  bool validate_schemas();
};


/*
  Writing binlog information into RocksDB at commit(),
  and retrieving binlog information at crash recovery.
  commit() and recovery are always executed by at most single client
  at the same time, so concurrency control is not needed.

  Binlog info is stored in RocksDB as the following.
   key: BINLOG_INFO_INDEX_NUMBER
   value: packed single row:
     binlog_name_length (2 byte form)
     binlog_name
     binlog_position (4 byte form)
     binlog_gtid_length (2 byte form)
     binlog_gtid
*/
class Binlog_info_manager
{
public:
  bool init(Dict_manager *dict);
  void cleanup();
  void update(const char* binlog_name, const my_off_t binlog_pos,
              const char* binlog_gtid, rocksdb::WriteBatchBase* batch);
  bool read(char* binlog_name, my_off_t& binlog_pos, char* binlog_gtid);

private:
  Dict_manager *dict;
  uchar key_buf[RDBSE_KEYDEF::INDEX_NUMBER_SIZE];
  rocksdb::Slice key_slice;
  rocksdb::Slice pack_value(uchar *buf,
                            const char *binlog_name,
                            const my_off_t binlog_pos,
                            const char *binlog_gtid);
  bool unpack_value(const uchar *value, char *binlog_name,
                    my_off_t &binlog_pos, char *binlog_gtid);
};


/*
   Dict_manager manages how MySQL on RocksDB (MyRocks) stores its
  internal data dictionary.
   MyRocks stores data dictionary on dedicated system column family
  named __system__. The system column family is used by MyRocks
  internally only, and not used by applications.

   Currently MyRocks has the following data dictionary data models.

  1. Table Name => internal index id mappings
  key: RDBSE_KEYDEF::DDL_ENTRY_INDEX_START_NUMBER(0x1) + dbname.tablename
  value: version, {cf_id, index_id}*n_indexes_of_the_table
  version is 2 bytes. cf_id and index_id are 4 bytes.

  2. internal cf_id, index id => index information
  key: RDBSE_KEYDEF::INDEX_INFO(0x2) + cf_id + index_id
  value: version, index_type, kv_format_version
  index_type is 1 byte, version and kv_format_version are 2 bytes.

  3. CF id => CF flags
  key: RDBSE_KEYDEF::CF_DEFINITION(0x3) + cf_id
  value: version, {is_reverse_cf, is_auto_cf}
  cf_flags is 4 bytes in total.

  4. Binlog entry (updated at commit)
  key: RDBSE_KEYDEF::BINLOG_INFO_INDEX_NUMBER (0x4)
  value: version, {binlog_name,binlog_pos,binlog_gtid}

  5. Ongoing drop index entry (not implemented yet)
  key: RDBSE_KEYDEF::DDL_DROP_INDEX_ONGOING(0x5) + cf_id + index_id
  value: version

  6. index stats
  key: RDBSE_KEYDEF::INDEX_STATISTICS(0x6) + cf_id + index_id
  value: version, {materialized PropertiesCollector::IndexStats}

  7. maximum index id
  key: RDBSE_KEYDEF::MAX_INDEX_ID(0x7)
  value: index_id
  index_id is 4 bytes

   Data dictionary operations are atomic inside RocksDB. For example,
  when creating a table with two indexes, it is necessary to call Put
  three times. They have to be atomic. Dict_manager has a wrapper function
  begin() and commit() to make it easier to do atomic operations.

*/
class Dict_manager
{
private:
  mysql_mutex_t mutex;
  rocksdb::DB *rdb;
  rocksdb::ColumnFamilyHandle *system_cfh;
  /* Utility to put INDEX_INFO and CF_DEFINITION */

  uchar key_buf_max_index_id[RDBSE_KEYDEF::INDEX_NUMBER_SIZE];
  rocksdb::Slice key_slice_max_index_id;
  void delete_util(rocksdb::WriteBatch* batch,
                   const uint32_t prefix,
                   const GL_INDEX_ID gl_index_id);
  /* Functions for fast DROP TABLE/INDEX */
  void resume_drop_indexes();
  void log_start_drop_table(RDBSE_KEYDEF** key_descr,
                            uint32 n_keys,
                            const char* log_action);
  void log_start_drop_index(GL_INDEX_ID gl_index_id,
                            const char* log_action);
public:
  bool init(rocksdb::DB *rdb_dict, Column_family_manager *cf_manager);
  void cleanup();
  void lock();
  void unlock();
  /* Raw RocksDB operations */
  std::unique_ptr<rocksdb::WriteBatch> begin();
  int commit(rocksdb::WriteBatch *batch, bool sync = true);
  rocksdb::Status Get(const rocksdb::Slice& key, std::string *value);
  void Put(rocksdb::WriteBatchBase *batch, const rocksdb::Slice &key,
           const rocksdb::Slice &value);
  void Delete(rocksdb::WriteBatchBase *batch, const rocksdb::Slice &key);
  rocksdb::Iterator *NewIterator();

  /* Internal Index id => CF */
  void add_or_update_index_cf_mapping(rocksdb::WriteBatch *batch,
                                      const uchar index_type,
                                      const uint16_t kv_version,
                                      const uint index_id,
                                      const uint cf_id);
  void delete_index_info(rocksdb::WriteBatch* batch,
                         const GL_INDEX_ID index_id);
  bool get_index_info(GL_INDEX_ID gl_index_id, uint16_t *index_dict_version,
                      uchar *index_type, uint16_t *kv_version);

  /* CF id => CF flags */
  void add_cf_flags(rocksdb::WriteBatch *batch,
                    const uint cf_id,
                    const uint cf_flags);
  bool get_cf_flags(const uint cf_id, uint *cf_flags);

  /* Functions for fast DROP TABLE/INDEX */
  void get_drop_indexes_ongoing(std::vector<GL_INDEX_ID> *gl_index_ids);
  bool is_drop_index_ongoing(GL_INDEX_ID gl_index_id);
  void start_drop_index_ongoing(rocksdb::WriteBatch* batch,
                                GL_INDEX_ID gl_index_id);
  void end_drop_index_ongoing(rocksdb::WriteBatch* batch,
                              GL_INDEX_ID gl_index_id);
  bool is_drop_index_empty();
  void add_drop_table(RDBSE_KEYDEF** key_descr,
                      uint32 n_keys,
                      rocksdb::WriteBatch *batch);
  void done_drop_indexes(const std::unordered_set<GL_INDEX_ID>& gl_index_ids);

  bool get_max_index_id(uint32_t *index_id);
  bool update_max_index_id(rocksdb::WriteBatch* batch,
                           const uint32_t index_id);
  void add_stats(
    rocksdb::WriteBatch* batch,
    const std::vector<MyRocksTablePropertiesCollector::IndexStats>& stats
  );
  MyRocksTablePropertiesCollector::IndexStats get_stats(
    GL_INDEX_ID gl_index_id
  );
};
