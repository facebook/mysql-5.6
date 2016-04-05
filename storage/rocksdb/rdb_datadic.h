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

/* C++ standard header files */
#include <algorithm>
#include <atomic>
#include <map>
#include <mutex>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

/* C standard header files */
#include <arpa/inet.h>

/* MyRocks header files */
#include "./ha_rocksdb.h"
#include "./properties_collector.h"
#include "./rdb_utils.h"

namespace myrocks {

class Rdb_dict_manager;
class Rdb_key_def;
class Rdb_field_packing;
class Rdb_cf_manager;
class Rdb_ddl_manager;

bool is_myrocks_collation_supported(Field *field);
void write_int64(String *out, uint64 val);
void write_int(String *out, uint32 val);
void write_short(String *out, uint16 val);
void write_byte(String *out, uchar val);
uint32 read_int(const char **data);
uint64 read_int64(const char **data);
uint16 read_short(const char **data);


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
  A simple string reader:
  - it keeps position within the string that we read from
  - it prevents one from reading beyond the end of the string.
*/

class Rdb_string_reader
{
  const char* m_ptr;
  uint m_len;
public:
  explicit Rdb_string_reader(const std::string &str)
  {
    m_len= str.length();
    if (m_len)
      m_ptr= &str.at(0);
    else
    {
      /*
        One can a create a Rdb_string_reader for reading from an empty string
        (although attempts to read anything will fail).
        We must not access str.at(0), since len==0, we can set ptr to any
        value.
      */
      m_ptr= nullptr;
    }
  }

  explicit Rdb_string_reader(const rocksdb::Slice *slice)
  {
    m_ptr= slice->data();
    m_len= slice->size();
  }

  /*
    Read the next @param size bytes. Returns pointer to the bytes read, or
    nullptr if the remaining string doesn't have that many bytes.
  */
  const char *read(uint size)
  {
    const char *res;
    if (m_len < size)
      res= nullptr;
    else
    {
      res= m_ptr;
      m_ptr += size;
      m_len -= size;
    }
    return res;
  }

  uint remaining_bytes() const { return m_len; }

  /*
    Return pointer to data that will be read by next read() call (if there is
    nothing left to read, returns pointer to beyond the end of previous read()
    call)
  */
  const char *get_current_ptr() const { return m_ptr; }
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



void report_checksum_mismatch(Rdb_key_def *kd, bool is_key,
                              const char *data, size_t data_size);

void hexdump_value(char *strbuf, size_t strbuf_size,
                   const rocksdb::Slice &val);

/*
  An object of this class represents information about an index in an SQL
  table. It provides services to encode and decode index tuples.

  Note: a table (as in, on-disk table) has a single Rdb_key_def object which
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

class Rdb_key_def
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
                   uint *n_null_fields= nullptr,
                   longlong hidden_pk_id= 0);
  /* Pack the hidden primary key into mem-comparable form. */
  uint pack_hidden_pk(longlong hidden_pk_id,
                      uchar *packed_tuple) const;
  int unpack_record(const ha_rocksdb *handler,
                    TABLE *table, uchar *buf, const rocksdb::Slice *packed_key,
                    const rocksdb::Slice *unpack_info);

  static bool unpack_info_has_checksum(const rocksdb::Slice& unpack_info);
  int compare_keys(const rocksdb::Slice *key1, const rocksdb::Slice *key2,
                   std::size_t* column_index);

  size_t key_length(TABLE *table, const rocksdb::Slice &key) const;

  /* Get the key that is the "infimum" for this index */
  inline void get_infimum_key(uchar *key, uint *size) const
  {
    store_index_number(key, m_index_number);
    *size= INDEX_NUMBER_SIZE;
  }

  /* Get the key that is a "supremum" for this index */
  inline void get_supremum_key(uchar *key, uint *size) const
  {
    store_index_number(key, m_index_number+1);
    *size= INDEX_NUMBER_SIZE;
  }

  /* Make a key that is right after the given key. */
  static int successor(uchar *packed_tuple, uint len);

  /*
    This can be used to compare prefixes.
    if  X is a prefix of Y, then we consider that X = Y.
  */
  // b describes the lookup key, which can be a prefix of a.
  int cmp_full_keys(const rocksdb::Slice& a, const rocksdb::Slice& b) const
  {
    DBUG_ASSERT(covers_key(a));
    DBUG_ASSERT(covers_key(b));

    return memcmp(a.data(), b.data(), std::min(a.size(), b.size()));
  }

  /* Check if given mem-comparable key belongs to this index */
  bool covers_key(const rocksdb::Slice &slice) const
  {
    if (slice.size() < INDEX_NUMBER_SIZE)
      return false;

    if (memcmp(slice.data(), m_index_number_storage_form, INDEX_NUMBER_SIZE))
      return false;

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
    return covers_key(value) && !cmp_full_keys(value, prefix);
  }

  uint32 get_index_number() const
  {
    return m_index_number;
  }

  GL_INDEX_ID get_gl_index_id() const
  {
    GL_INDEX_ID gl_index_id = { m_cf_handle->GetID(), m_index_number };
    return gl_index_id;
  }

  /* Must only be called for secondary keys: */
  uint get_primary_key_tuple(TABLE *tbl, Rdb_key_def *pk_descr,
                             const rocksdb::Slice *key, uchar *pk_buffer) const;

  /* Return max length of mem-comparable form */
  uint max_storage_fmt_length() const
  {
    return m_maxlength;
  }

  uint get_m_key_parts() const
  {
    return m_key_parts;
  }

  /*
    Get a field object for key part #part_no

    @detail
      SQL layer thinks unique secondary indexes and indexes in partitioned
      tables are not "Extended" with Primary Key columns.

      Internally, we always extend all indexes with PK columns. This function
      uses our definition of how the index is Extended.
  */
  inline Field* get_table_field_for_part_no(TABLE *table, uint part_no) const;

  const std::string& get_name() const {
    return m_name;
  }

  Rdb_key_def(const Rdb_key_def& k);
  Rdb_key_def(uint indexnr_arg, uint keyno_arg,
              rocksdb::ColumnFamilyHandle* cf_handle_arg,
              uint16_t index_dict_version_arg,
              uchar index_type_arg,
              uint16_t kv_format_version_arg,
              bool is_reverse_cf_arg, bool is_auto_cf_arg,
              const char* name,
              Rdb_index_stats stats= Rdb_index_stats());
  ~Rdb_key_def();

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
    INDEX_TYPE_HIDDEN_PRIMARY= 3,
  };

  // Key/Value format version for each index type
  enum {
    PRIMARY_FORMAT_VERSION_INITIAL= 10,
    SECONDARY_FORMAT_VERSION_INITIAL= 10,
  };

  void setup(TABLE *table, Rdb_tbl_def *tbl_def);

  rocksdb::ColumnFamilyHandle *get_cf() { return m_cf_handle; }

  /* Check if keypart #kp can be unpacked from index tuple */
  bool can_unpack(uint kp) const;

  /*
    Current code assumes that unpack_data occupies fixed length regardless of
    the value that is stored.
  */
  bool get_unpack_data_len() { return m_unpack_data_len; }

  /* Check if given table has a primary key */
  static bool rocksdb_has_hidden_pk(const TABLE* table);
private:

#ifndef DBUG_OFF
  inline bool is_storage_available(int offset, int needed) const
  {
    int storage_length= static_cast<int>(max_storage_fmt_length());
    return (storage_length - offset) >= needed;
  }
#endif  // DBUG_OFF

  /* Global number of this index (used as prefix in StorageFormat) */
  const uint32 m_index_number;

  uchar m_index_number_storage_form[INDEX_NUMBER_SIZE];

  rocksdb::ColumnFamilyHandle* m_cf_handle;

public:
  void set_keyno(uint keyno) { m_keyno = keyno; }

  uint16_t m_index_dict_version;
  uchar m_index_type;
  /* KV format version for the index id */
  uint16_t m_kv_format_version;
  /* If true, the column family stores data in the reverse order */
  bool m_is_reverse_cf;

  bool m_is_auto_cf;
  std::string m_name;
  Rdb_index_stats m_stats;
private:

  friend class Rdb_tbl_def;  // for m_index_number above

  /* Number of key parts in the primary key*/
  uint m_pk_key_parts;

  /*
     pk_part_no[X]=Y means that keypart #X of this key is key part #Y of the
     primary key.  Y==-1 means this column is not present in the primary key.
  */
  uint *m_pk_part_no;

  /* Array of index-part descriptors. */
  Rdb_field_packing *m_pack_info;

  uint m_keyno; /* number of this index in the table */

  /*
    Number of key parts in the index (including "index extension"). This is how
    many elements are in the m_pack_info array.
  */
  uint m_key_parts;

  /* Maximum length of the mem-comparable form. */
  uint m_maxlength;

  /* Length of the unpack_data */
  uint m_unpack_data_len;

  /* mutex to protect setup */
  mysql_mutex_t m_mutex;

 public:
  void block_setup()
  {
    if (m_maxlength == 0)
    {
      mysql_mutex_lock(&m_mutex);
      if (m_maxlength != 0)
        mysql_mutex_unlock(&m_mutex);
    }
  }
  void unblock_setup()
  {
    if (m_maxlength == 0)
      mysql_mutex_unlock(&m_mutex);
  }
};


/*
  C-style "virtual table" allowing different handling of packing logic
  depending upon the field type. See Rdb_field_packing::setup() implementation.
*/

typedef void (*make_unpack_info_t) (Rdb_field_packing *fpi, Field *field,
                                    uchar *dst);

typedef int (*index_field_unpack_t)(Rdb_field_packing *fpi, Field *field,
                                    Rdb_string_reader *reader,
                                    const uchar *unpack_info);

typedef int (*index_field_skip_t)(Rdb_field_packing *fpi, Field *field,
                                  Rdb_string_reader *reader);

typedef void (*index_field_pack_t)(Rdb_field_packing *fpi, Field *field,
                                   uchar* buf, uchar **dst);

/*
  This stores information about how a field can be packed to mem-comparable
  form and unpacked back.
*/

class Rdb_field_packing
{
public:
  /* Length of mem-comparable image of the field, in bytes */
  int m_max_image_len;

  /* Length of image in the unpack data */
  int m_unpack_data_len;
  int m_unpack_data_offset;

  /* Offset of field data in table->record[i] from field->ptr. */
  int m_field_data_offset;

  bool m_maybe_null; /* TRUE <=> NULL-byte is stored */

  /*
    Valid only for VARCHAR fields.
  */
  const CHARSET_INFO *m_varchar_charset;

  index_field_pack_t m_pack_func;

  /*
    Pack function is assumed to be:
     - store NULL-byte, if needed
     - call field->make_sort_key();
    If you neeed to unpack, you should also call
  */
  make_unpack_info_t m_make_unpack_info_func;

  /*
    This function takes
    - mem-comparable form
    - unpack_info data
    and restores the original value.
  */
  index_field_unpack_t m_unpack_func;

  /*
    This function skips over mem-comparable form.
  */
  index_field_skip_t m_skip_func;

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
  uint m_keynr;
  uint m_key_part;
public:
  bool setup(Field *field, uint keynr_arg, uint key_part_arg);
  Field *get_field_in_table(TABLE *tbl) const;
  void fill_hidden_pk_val(uchar **dst, longlong hidden_pk_id) const;
};

inline Field* Rdb_key_def::get_table_field_for_part_no(TABLE *table,
                                                       uint part_no) const
{
  DBUG_ASSERT(part_no < get_m_key_parts());
  return m_pack_info[part_no].get_field_in_table(table);
}

/*
  A table definition. This is an entry in the mapping

    dbname.tablename -> {index_nr, index_nr, ... }

  There is only one Rdb_tbl_def object for a given table.
  That's why we keep auto_increment value here, too.
*/

class Rdb_tbl_def
{
  void check_if_is_mysql_system_table();

public:
  Rdb_tbl_def() : m_key_descr(nullptr), m_hidden_pk_val(1), m_auto_incr_val(1)
    {}
  ~Rdb_tbl_def();

  /* Stores 'dbname.tablename' */
  StringBuffer<64> m_dbname_tablename;

  /* Number of indexes */
  uint m_key_count;

  /* Array of index descriptors */
  Rdb_key_def **m_key_descr;

  std::atomic<longlong> m_hidden_pk_val;
  std::atomic<longlong> m_auto_incr_val;

  /* Is this a system table */
  bool m_is_mysql_system_table;

  bool put_dict(Rdb_dict_manager *dict, rocksdb::WriteBatch *batch,
                uchar *key, size_t keylen);

  void set_name(const char *name, size_t len);
  void set_name(const rocksdb::Slice& slice, size_t pos = 0) {
    set_name(slice.data() + pos, slice.size() - pos);
  }
};


/*
  A thread-safe sequential number generator. Its performance is not a concern
  hence it is ok to protect it by a mutex.
*/

class Rdb_seq_generator
{
  uint m_next_number= 0;

  mysql_mutex_t m_mutex;
public:
  void init(uint initial_number)
  {
    mysql_mutex_init(0 , &m_mutex, MY_MUTEX_INIT_FAST);
    m_next_number= initial_number;
  }

  uint get_and_update_next_number(Rdb_dict_manager *dict);

  void cleanup()
  {
    mysql_mutex_destroy(&m_mutex);
  }
};


/*
  This contains a mapping of

     dbname.table_name -> array{Rdb_key_def}.

  objects are shared among all threads.
*/

class Rdb_ddl_manager
{
  Rdb_dict_manager *m_dict= nullptr;
  my_core::HASH m_ddl_hash;  // Contains Rdb_tbl_def elements
  // maps index id to <table_name, index number>
  std::map<GL_INDEX_ID, std::pair<std::basic_string<uchar>, uint>>
    m_index_num_to_keydef;
  mysql_rwlock_t m_rwlock;

  Rdb_seq_generator m_sequence;
  // A queue of table stats to write into data dictionary
  // It is produced by event listener (ie compaction and flush threads)
  // and consumed by the rocksdb background thread
  std::map<GL_INDEX_ID, Rdb_index_stats> m_stats2store;
public:
  /* Load the data dictionary from on-disk storage */
  bool init(Rdb_dict_manager *dict_arg, Rdb_cf_manager *cf_manager,
            uint32_t validate_tables);

  void cleanup();

  Rdb_tbl_def* find(const uchar *table_name, uint len, bool lock= true);
  Rdb_key_def* find(GL_INDEX_ID gl_index_id);
  std::unique_ptr<Rdb_key_def> get_copy_of_keydef(GL_INDEX_ID gl_index_id);
  void set_stats(
    const std::unordered_map<GL_INDEX_ID, Rdb_index_stats>& stats);
  void adjust_stats(
    const std::vector<Rdb_index_stats>& new_data,
    const std::vector<Rdb_index_stats>& deleted_data
     =std::vector<Rdb_index_stats>());
  void persist_stats(bool sync = false);

  /* Modify the mapping and write it to on-disk storage */
  int put_and_write(Rdb_tbl_def *key_descr, rocksdb::WriteBatch *batch);
  void remove(Rdb_tbl_def *rec, rocksdb::WriteBatch *batch, bool lock= true);
  bool rename(uchar *from, uint from_len, uchar *to, uint to_len,
              rocksdb::WriteBatch *batch);

  uint get_and_update_next_number(Rdb_dict_manager *dict)
    { return m_sequence.get_and_update_next_number(dict); }

  /* Walk the data dictionary */
  int scan(void* cb_arg, int (*callback)(void* cb_arg, Rdb_tbl_def*));

  void erase_index_num(GL_INDEX_ID gl_index_id);
private:
  /* Put the data into in-memory table (only) */
  int put(Rdb_tbl_def *key_descr, bool lock= true);

  /* Helper functions to be passed to my_core::HASH object */
  static uchar* get_hash_key(Rdb_tbl_def *rec, size_t *length,
                             my_bool not_used MY_ATTRIBUTE((unused)));
  static void free_hash_elem(void* data);

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
class Rdb_binlog_manager
{
public:
  bool init(Rdb_dict_manager *dict);
  void cleanup();
  void update(const char* binlog_name, const my_off_t binlog_pos,
              const char* binlog_gtid, rocksdb::WriteBatchBase* batch);
  bool read(char* binlog_name, my_off_t* binlog_pos, char* binlog_gtid);
  void update_slave_gtid_info(uint id, const char* db, const char* gtid,
                              rocksdb::WriteBatchBase *write_batch);

private:
  Rdb_dict_manager *m_dict= nullptr;
  uchar m_key_buf[Rdb_key_def::INDEX_NUMBER_SIZE]= {0};
  rocksdb::Slice m_key_slice;

  rocksdb::Slice pack_value(uchar *buf,
                            const char *binlog_name,
                            const my_off_t binlog_pos,
                            const char *binlog_gtid);
  bool unpack_value(const uchar *value, char *binlog_name,
                    my_off_t *binlog_pos, char *binlog_gtid);

  std::atomic<Rdb_tbl_def*> m_slave_gtid_info_tbl;
};


/*
   Rdb_dict_manager manages how MySQL on RocksDB (MyRocks) stores its
  internal data dictionary.
   MyRocks stores data dictionary on dedicated system column family
  named __system__. The system column family is used by MyRocks
  internally only, and not used by applications.

   Currently MyRocks has the following data dictionary data models.

  1. Table Name => internal index id mappings
  key: Rdb_key_def::DDL_ENTRY_INDEX_START_NUMBER(0x1) + dbname.tablename
  value: version, {cf_id, index_id}*n_indexes_of_the_table
  version is 2 bytes. cf_id and index_id are 4 bytes.

  2. internal cf_id, index id => index information
  key: Rdb_key_def::INDEX_INFO(0x2) + cf_id + index_id
  value: version, index_type, kv_format_version
  index_type is 1 byte, version and kv_format_version are 2 bytes.

  3. CF id => CF flags
  key: Rdb_key_def::CF_DEFINITION(0x3) + cf_id
  value: version, {is_reverse_cf, is_auto_cf}
  cf_flags is 4 bytes in total.

  4. Binlog entry (updated at commit)
  key: Rdb_key_def::BINLOG_INFO_INDEX_NUMBER (0x4)
  value: version, {binlog_name,binlog_pos,binlog_gtid}

  5. Ongoing drop index entry
  key: Rdb_key_def::DDL_DROP_INDEX_ONGOING(0x5) + cf_id + index_id
  value: version

  6. index stats
  key: Rdb_key_def::INDEX_STATISTICS(0x6) + cf_id + index_id
  value: version, {materialized PropertiesCollector::IndexStats}

  7. maximum index id
  key: Rdb_key_def::MAX_INDEX_ID(0x7)
  value: index_id
  index_id is 4 bytes

   Data dictionary operations are atomic inside RocksDB. For example,
  when creating a table with two indexes, it is necessary to call Put
  three times. They have to be atomic. Rdb_dict_manager has a wrapper function
  begin() and commit() to make it easier to do atomic operations.

*/
class Rdb_dict_manager
{
private:
  mysql_mutex_t m_mutex;
  rocksdb::DB *m_db= nullptr;
  rocksdb::ColumnFamilyHandle *m_system_cfh= nullptr;
  /* Utility to put INDEX_INFO and CF_DEFINITION */

  uchar m_key_buf_max_index_id[Rdb_key_def::INDEX_NUMBER_SIZE]= {0};
  rocksdb::Slice m_key_slice_max_index_id;
  void delete_with_prefix(rocksdb::WriteBatch* batch,
                          const uint32_t prefix,
                          const GL_INDEX_ID gl_index_id) const;
  /* Functions for fast DROP TABLE/INDEX */
  void resume_drop_indexes();
  void log_start_drop_table(Rdb_key_def** key_descr,
                            uint32 n_keys,
                            const char* log_action);
  void log_start_drop_index(GL_INDEX_ID gl_index_id,
                            const char* log_action);
public:
  bool init(rocksdb::DB *rdb_dict, Rdb_cf_manager *cf_manager);

  inline void cleanup()
  {
    mysql_mutex_destroy(&m_mutex);
  }

  inline void lock()
  {
    mysql_mutex_lock(&m_mutex);
  }

  inline void unlock()
  {
    mysql_mutex_unlock(&m_mutex);
  }

  /* Raw RocksDB operations */
  std::unique_ptr<rocksdb::WriteBatch> begin();
  int commit(rocksdb::WriteBatch *batch, bool sync = true);
  rocksdb::Status get_value(const rocksdb::Slice& key,
                            std::string *value) const;
  void put_key(rocksdb::WriteBatchBase *batch, const rocksdb::Slice &key,
               const rocksdb::Slice &value);
  void delete_key(rocksdb::WriteBatchBase *batch,
                  const rocksdb::Slice &key) const;
  rocksdb::Iterator *new_iterator();

  /* Internal Index id => CF */
  void add_or_update_index_cf_mapping(rocksdb::WriteBatch *batch,
                                      const uchar index_type,
                                      const uint16_t kv_version,
                                      const uint index_id,
                                      const uint cf_id);
  void delete_index_info(rocksdb::WriteBatch* batch,
                         const GL_INDEX_ID index_id) const;
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
  void add_drop_table(Rdb_key_def** key_descr,
                      uint32 n_keys,
                      rocksdb::WriteBatch *batch);
  void done_drop_indexes(const std::unordered_set<GL_INDEX_ID>& gl_index_ids);

  bool get_max_index_id(uint32_t *index_id);
  bool update_max_index_id(rocksdb::WriteBatch* batch,
                           const uint32_t index_id);
  void add_stats(rocksdb::WriteBatch* batch,
                 const std::vector<Rdb_index_stats>& stats);
  Rdb_index_stats get_stats(GL_INDEX_ID gl_index_id);
};

}  // namespace myrocks
