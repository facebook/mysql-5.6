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

#ifdef USE_PRAGMA_INTERFACE
#pragma interface			/* gcc class implementation */
#endif

/* MySQL header files */
#include "./handler.h"                     /* handler */
#include "./my_global.h"                   /* ulonglong */
#include "./sql_string.h"
#include "./ut0counter.h"

/* RocksDB header files */
#include "rocksdb/cache.h"
#include "rocksdb/perf_context.h"
#include "rocksdb/statistics.h"
#include "rocksdb/utilities/options_util.h"
#include "rocksdb/utilities/transaction_db.h"
#include "rocksdb/utilities/write_batch_with_index.h"

/* MyRocks header files */
#include "rdb_perf_context.h"

namespace myrocks {

/*
  This is
  - the name of the default Column Family (the CF which stores indexes which
    didn't explicitly specify which CF they are in)
  - the name used to set the default column family parameter for per-cf
    arguments.
*/
const char * const DEFAULT_CF_NAME= "default";

/*
  This is the name of the Column Family used for storing the data dictionary.
*/
const char * const DEFAULT_SYSTEM_CF_NAME= "__system__";

/*
  Column family name which means "put this index into its own column family".
  See Rdb_cf_manager::get_per_index_cf_name().
*/
const char * const PER_INDEX_CF_NAME = "$per_index_cf";

/*
  Default, minimal valid, and maximum valid sampling rate values when collecting
  statistics about table.
*/
#define MYROCKS_DEFAULT_SAMPLE_PCT                       10
#define MYROCKS_SAMPLE_PCT_MIN                            1
#define MYROCKS_SAMPLE_PCT_MAX                          100

/*
  Default and maximum values for rocksdb-compaction-sequential-deletes and
  rocksdb-compaction-sequential-deletes-window to add basic boundary checking.
*/
#define DEFAULT_COMPACTION_SEQUENTIAL_DELETES             0
#define MAX_COMPACTION_SEQUENTIAL_DELETES            100000

#define DEFAULT_COMPACTION_SEQUENTIAL_DELETES_WINDOW      0
#define MAX_COMPACTION_SEQUENTIAL_DELETES_WINDOW     100000

/*
  Default and maximum values for various compaction and flushing related
  options. Numbers are based on the hardware we currently use and our internal
  benchmarks which indicate that parallelization helps with the speed of
  compactions.

  Ideally of course we'll use heuristic technique to determine the number of
  CPU-s and derive the values from there. This however has its own set of
  problems and we'll choose simplicity for now.
*/
#define MAX_BACKGROUND_COMPACTIONS                       64
#define MAX_BACKGROUND_FLUSHES                           64

#define DEFAULT_SUBCOMPACTIONS                           16
#define MAX_SUBCOMPACTIONS                               64

/*
  To fix an unhandled exception we specify the upper bound as LONGLONGMAX
  instead of ULONGLONGMAX because the latter is -1 and causes an exception when
  cast to jlong (signed) of JNI

  The reason behind the cast issue is the lack of unsigned int support in Java.
*/
#define MAX_RATE_LIMITER_BYTES_PER_SEC  static_cast<uint64_t>(LONGLONG_MAX)

/*
  Hidden PK column (for tables with no primary key) is a longlong (aka 8 bytes).
  static_assert() in code will validate this assumption.
*/
#define ROCKSDB_SIZEOF_HIDDEN_PK_COLUMN sizeof(longlong)

/*
  The intent behind a SHIP_ASSERT() macro is to have a mechanism for validating
  invariants in retail builds. Traditionally assertions (such as macros defined
  in <cassert>) are evaluated for performance reasons only in debug builds and
  become NOOP in retail builds when NDEBUG is defined.

  This macro is intended to validate the invariants which are critical for
  making sure that data corruption and data loss won't take place. Proper
  intended usage can be described as "If a particular condition is not true then
  stop everything what's going on and terminate the process because continued
  execution will cause really bad things to happen".

  Use the power of SHIP_ASSERT() wisely.
*/

#define SHIP_ASSERT(expr)                                               \
  do {                                                                  \
    if (!(expr)) {                                                      \
      my_safe_printf_stderr("\nShip assert failure: \'%s\'\n", #expr);  \
      abort_with_stack_traces();                                        \
    }                                                                   \
  } while (0)                                                           \

/* MyRocks supports only the following collations for indexed columns */
const std::set<const CHARSET_INFO *> MYROCKS_INDEX_COLLATIONS=
  {&my_charset_bin, &my_charset_utf8_bin, &my_charset_latin1_bin};

inline bool looks_like_per_index_cf_typo(const char *name)
{
  return (name && name[0]=='$' && strcmp(name, PER_INDEX_CF_NAME));
}

}  // namespace myrocks

extern PSI_stage_info stage_waiting_on_row_lock;

extern "C"
{
void thd_enter_cond(MYSQL_THD thd, mysql_cond_t *cond, mysql_mutex_t *mutex,
                    const PSI_stage_info *stage, PSI_stage_info *old_stage);
void thd_exit_cond(MYSQL_THD thd, const PSI_stage_info *stage);

/**
  Mark transaction to rollback and mark error as fatal to a sub-statement.
  @param  thd   Thread handle
  @param  all   TRUE <=> rollback main transaction.
*/
void thd_mark_transaction_to_rollback(MYSQL_THD thd, bool all);

/**
 *   Get the user thread's binary logging format
 *   @param thd  user thread
 *   @return Value to be used as index into the binlog_format_names array
*/
int thd_binlog_format(const MYSQL_THD thd);

/**
 *   Check if binary logging is filtered for thread's current db.
 *   @param  thd   Thread handle
 *   @retval 1 the query is not filtered, 0 otherwise.
*/
bool thd_binlog_filter_ok(const MYSQL_THD thd);
}

//#endif

namespace myrocks {

/** @brief
  ROCKSDB_SHARE is a structure that will be shared among all open handlers.
  This example implements the minimum of what you will probably need.

  //TODO: join this with Rdb_tbl_def ?
*/
typedef struct st_rocksdb_share {
  char *table_name;
  uint table_name_length,use_count;
  //mysql_mutex_t mutex;

  THR_LOCK lock;

  /* Stores cumulative table statistics */
  my_io_perf_atomic_t io_perf_read;
  rdb_perf_context_shared table_perf_context;
} ROCKSDB_SHARE;

typedef enum io_error_type {
  ROCKSDB_IO_ERROR_TRX_COMMIT,
  ROCKSDB_IO_ERROR_DICT_COMMIT,
  ROCKSDB_IO_ERROR_BG_THREAD
} ROCKSDB_IO_ERROR_TYPES;

void rocksdb_handle_io_error(rocksdb::Status status, enum io_error_type type);

class Rdb_key_def;
class Rdb_tbl_def;
class Row_lock;
class Rdb_transaction;

const char *const rocksdb_hton_name= "ROCKSDB";

typedef struct _gl_index_id_s {
  uint32_t cf_id;
  uint32_t index_id;
  bool operator==(const struct _gl_index_id_s& other) const
  {
    return cf_id == other.cf_id && index_id == other.index_id;
  }
  bool operator!=(const struct _gl_index_id_s& other) const
  {
    return cf_id != other.cf_id || index_id != other.index_id;
  }
  bool operator<(const struct _gl_index_id_s& other) const
  {
    return cf_id < other.cf_id ||
        (cf_id == other.cf_id && index_id < other.index_id);
  }
  bool operator<=(const struct _gl_index_id_s& other) const
  {
    return cf_id < other.cf_id ||
        (cf_id == other.cf_id && index_id <= other.index_id);
  }
  bool operator>(const struct _gl_index_id_s& other) const
  {
    return cf_id > other.cf_id ||
        (cf_id == other.cf_id && index_id > other.index_id);
  }
  bool operator>=(const struct _gl_index_id_s& other) const
  {
    return cf_id > other.cf_id ||
        (cf_id == other.cf_id && index_id >= other.index_id);
  }
} GL_INDEX_ID;

enum operation_type {
  ROWS_DELETED = 0,
  ROWS_INSERTED,
  ROWS_READ,
  ROWS_UPDATED,
  ROWS_MAX
};

/* Global statistics struct used inside MyRocks */
struct st_global_stats {
  ib_counter_t<ulonglong, 64> rows[ROWS_MAX];

  // system_rows_ stats are only for system
  // tables. They are not counted in rows_* stats.
  ib_counter_t<ulonglong, 64> system_rows[ROWS_MAX];
};

/* Struct used for exporting status to MySQL */
struct st_export_stats {
  ulonglong rows_deleted;
  ulonglong rows_inserted;
  ulonglong rows_read;
  ulonglong rows_updated;

  ulonglong system_rows_deleted;
  ulonglong system_rows_inserted;
  ulonglong system_rows_read;
  ulonglong system_rows_updated;
};

}  // namespace myrocks

/* Provide hash function for GL_INDEX_ID so we can include it in sets */
namespace std {
  template <>
  struct hash<myrocks::GL_INDEX_ID>
  {
    std::size_t operator()(const myrocks::GL_INDEX_ID& gl_index_id) const
    {
      uint64_t val = ((uint64_t) gl_index_id.cf_id << 32 |
                      (uint64_t) gl_index_id.index_id);
      return std::hash<uint64_t>()(val);
    }
  };
}  // namespace std

namespace myrocks {

/** @brief
  Class definition for the storage engine
*/

class ha_rocksdb: public handler
{
  THR_LOCK_DATA lock;      ///< MySQL lock
  ROCKSDB_SHARE *share;    ///< Shared lock info

  /* Iterator used for range scans and for full table/index scans */
  rocksdb::Iterator *scan_it;

  /* Whether scan_it was created with skip_bloom=true */
  bool scan_it_skips_bloom;

  Rdb_tbl_def *tbl_def;

  /* Primary Key encoder from KeyTupleFormat to StorageFormat */
  Rdb_key_def *pk_descr;

  /* Array of index descriptors */
  Rdb_key_def **key_descr;

  bool check_keyread_allowed(uint inx, uint part, bool all_parts) const;
  /*
    Number of key parts in PK. This is the same as
      table->key_info[table->s->primary_key].keyparts
  */
  uint pk_key_parts;

  /*
    TRUE <=> Primary Key columns can be decoded from the index
  */
  mutable bool pk_can_be_decoded;

  uchar *pk_tuple; /* Buffer for storing PK in KeyTupleFormat */
  uchar *pk_packed_tuple; /* Buffer for storing PK in StorageFormat */
  // ^^ todo: change it to 'char*'? TODO: ^ can we join this with last_rowkey?

  /*
    Temporary buffers for storing the key part of the Key/Value pair
    for secondary indexes.
  */
  uchar *sec_key_packed_tuple;

  /*
    Temporary buffers for storing end key part of the Key/Value pair.
    This is used for range scan only.
  */
  uchar *end_key_packed_tuple;

  /*
    Same as above, but it stores the value part. It holds unpack info which
    is currently not used (always empty).

    TODO: why does ha_rocksdb::open() assume that an upper bound of the size of
    this buffer is max_packed_sec_key_len? This is technically true currently,
    but doesn't look meaningful.
  */
  uchar *sec_key_tails;

  /*
    ha_rockdb->index_read_map(.. HA_READ_KEY_EXACT or similar) will save here
    mem-comparable form of the index lookup tuple.
  */
  uchar *sec_key_match_prefix;
  uint sec_key_match_length;

  /* Buffer space for the above */
  uchar *sec_key_match_prefix_buf;

  /* Second buffers, used by UPDATE. */
  uchar *sec_key_packed_tuple_old;
  uchar *sec_key_tails_old;

  /*
    Temporary space for packing VARCHARs (we provide it to
    pack_record()/pack_index_tuple() calls).
  */
  uchar *pack_buffer;

  /* rowkey of the last record we've read, in StorageFormat. */
  String last_rowkey;

  /* Buffer used by convert_record_to_storage_format() */
  String storage_record;

  /*
    Last retrieved record, in table->record[0] data format. It also serves as
    as storage for blob data (Field_blob object have pointers to here)

    TODO: Dont we lose one malloc() per record read by having it as std::string
    instead of rocksdb::Slice?
  */
  std::string retrieved_record;

  /* If TRUE, reads should place locks on rows */
  bool lock_rows;

  /*
    When lock_rows==true, specifies what kind of locks should be placed
    (true means write locks, false means read locks)
  */
  bool lock_for_write;

  /* Lock that we've placed on the row we've just read. */
  Row_lock *last_row_lock;

  /* TRUE means we're doing an index-only read. FALSE means otherwise. */
  bool keyread_only;

  bool skip_scan_it_next_call;

  /* TRUE means we are accessing the first row after a snapshot was created */
  bool rnd_scan_is_new_snapshot;

  /*
    Valid inside UPDATE statements, when updated_indexes_is_valid=true:

    This is a bitmap of indexes whose keys (in future, values) may be changed
    by this statement. Indexes that are not in the bitmap do not need to be
    updated.
  */
  key_map updated_indexes;
  bool updated_indexes_is_valid;

  /*
    MySQL index number for duplicate key error
  */
  int dupp_errkey;

  int create_key_defs(TABLE *table_arg, const char *db_table, const uint len,
                      HA_CREATE_INFO *create_info)
      __attribute__((__nonnull__, __warn_unused_result__));
  int secondary_index_read(const int keyno, uchar *buf)
      __attribute__((__nonnull__, __warn_unused_result__));
  void setup_iterator_for_rnd_scan();
  void setup_scan_iterator(Rdb_key_def *keydef, rocksdb::Slice *slice)
      __attribute__((__nonnull__))
  {
    setup_scan_iterator(keydef, slice, false, false, 0);
  }
  bool is_ascending(Rdb_key_def *keydef, enum ha_rkey_function find_flag)
    __attribute__((__nonnull__, __warn_unused_result__));
  void setup_scan_iterator(Rdb_key_def *keydef, rocksdb::Slice *slice,
                        const bool use_all_keys, const bool is_ascending,
                        const uint eq_cond_len)
    __attribute__((__nonnull__));
  void release_scan_iterator(void)
  {
    delete scan_it;
    scan_it= nullptr;
  }

  int get_row_by_rowid(uchar *buf, const char *pk_tuple,
    const uint pk_tuple_size)
    __attribute__((__nonnull__, __warn_unused_result__));
  int get_row_by_rowid(uchar *buf, const uchar *pk_tuple,
    const uint pk_tuple_size)
    __attribute__((__nonnull__, __warn_unused_result__))
  {
    return get_row_by_rowid(buf, reinterpret_cast<const char *>(pk_tuple),
                            pk_tuple_size);
  }

  void update_auto_incr_val();
  void load_auto_incr_value();
  longlong update_hidden_pk_val();
  int load_hidden_pk_value() __attribute__((__warn_unused_result__));
  int read_hidden_pk_id_from_rowkey(longlong *hidden_pk_id)
    __attribute__((__nonnull__, __warn_unused_result__));
  bool can_use_single_delete(uint index)
    __attribute__((__warn_unused_result__));
  bool skip_unique_check() __attribute__((__warn_unused_result__));
  bool commit_in_the_middle() __attribute__((__warn_unused_result__));
  bool has_hidden_pk(const TABLE* table)
    __attribute__((__nonnull__, __warn_unused_result__));

  void update_row_stats(operation_type type);

  void set_last_rowkey(const uchar *old_data);

  /*
    Descriptor telling how to decode/encode a field to on-disk record storage
    format. Not all information is in the structure yet, but eventually we
    want to have as much as possible there to avoid virtual calls.

    For encoding/decoding of index tuples, see Rdb_key_def.
  */
  typedef struct st_field_encoder
  {
    /*
      This is set to true for columns of Primary Key that can be decoded from
      their mem-comparable form.
      Since we can get them from the key part of RocksDB key->value pair, we
      don't need to store them in the value part.
    */
    bool dont_store;

    uint null_offset;
    uchar null_mask;  // 0 means the field cannot be null

    enum_field_types field_type;

    bool maybe_null() { return null_mask != 0; }
  } FIELD_ENCODER;

  /*
    Array of table->s->fields elements telling how to store fields in the
    record.
  */
  FIELD_ENCODER *field_enc;

  /*
    Number of bytes in on-disk (storage) record format that are used for
    storing SQL NULL flags.
  */
  uint null_bytes_in_rec;

  void setup_field_converters();

  // the buffer size should be at least 2*Rdb_key_def::INDEX_NUMBER_SIZE
  rocksdb::Range get_range(int i, uchar buf[]) const;

  /*
    Perf timers for data reads
   */
  struct {
    uint64_t block_read_byte;
    uint64_t block_read_count;
    uint64_t block_read_time;
  } io_perf;
  rdb_perf_context_local local_perf_context;
  void io_perf_start(void);
  void io_perf_end_and_record(void);

  /*
    A counter of how many row checksums were checked for this table. Note that
    this does not include checksums for secondary index entries.
  */
  ha_rows row_checksums_checked;

  /*
    Update stats
  */
  void update_stats(void);

public:
  /*
    Controls whether writes include checksums. This is updated from the session variable
    at the start of each query.
  */
  bool store_checksums;

  /* Same as above but for verifying checksums when reading */
  bool verify_checksums;
  int checksums_pct;

  ha_rocksdb(handlerton *hton, TABLE_SHARE *table_arg);
  ~ha_rocksdb() {}

  /** @brief
    The name that will be used for display purposes.
   */
  const char *table_type() const { return rocksdb_hton_name; }

  /* The following is only used by SHOW KEYS: */
  const char *index_type(uint inx) { return "LSMTREE"; }

  /** @brief
    The file extensions.
   */
  const char **bas_ext() const;

  /** @brief
    This is a list of flags that indicate what functionality the storage engine
    implements. The current table flags are documented in handler.h
  */
  ulonglong table_flags() const
  {
    /*
      HA_BINLOG_STMT_CAPABLE
        We are saying that this engine is just statement capable to have
        an engine that can only handle statement-based logging. This is
        used in testing.
      HA_REC_NOT_IN_SEQ
        If we don't set it, filesort crashes, because it assumes rowids are
        1..8 byte numbers
    */
    return HA_BINLOG_ROW_CAPABLE | HA_BINLOG_STMT_CAPABLE |
           HA_REC_NOT_IN_SEQ | HA_CAN_INDEX_BLOBS |
           (pk_can_be_decoded? HA_PRIMARY_KEY_IN_READ_INDEX:0) |
           HA_PRIMARY_KEY_REQUIRED_FOR_POSITION |
           HA_NULL_IN_KEY;
  }

  bool init_with_fields() override;

  /** @brief
    This is a bitmap of flags that indicates how the storage engine
    implements indexes. The current index flags are documented in
    handler.h. If you do not implement indexes, just return zero here.

      @details
    part is the key part to check. First key part is 0.
    If all_parts is set, MySQL wants to know the flags for the combined
    index, up to and including 'part'.
  */
  ulong index_flags(uint inx, uint part, bool all_parts) const;

  const key_map * keys_to_use_for_scanning()
  {
    return &key_map_full;
  }

  bool primary_key_is_clustered()
  {
    return true;
  }

  bool should_store_checksums() const
  {
    return store_checksums && (rand() % 100 < checksums_pct);
  }

  int rename_table(const char *from, const char *to)
      __attribute__((__nonnull__, __warn_unused_result__));

  int convert_record_from_storage_format(const rocksdb::Slice *key,
                                         const rocksdb::Slice *value,
                                         uchar *buf)
    __attribute__((__nonnull__, __warn_unused_result__));

  int convert_record_from_storage_format(const rocksdb::Slice *key,
                                         uchar *buf)
    __attribute__((__nonnull__, __warn_unused_result__));

  void convert_record_to_storage_format(const char *pk_packed_tuple,
                                        const size_t pk_packed_size,
                                        rocksdb::Slice *packed_rec)
    __attribute__((__nonnull__));

  bool is_hidden_pk(const uint index, const TABLE* table,
                    const Rdb_tbl_def* tbl_def)
    __attribute__((__nonnull__, __warn_unused_result__));

  int pk_index(const TABLE* table, const Rdb_tbl_def* tbl_def)
    __attribute__((__nonnull__, __warn_unused_result__));

  bool is_pk(const uint index, const TABLE* table,
             const Rdb_tbl_def* tbl_def)
    __attribute__((__nonnull__, __warn_unused_result__));

  /** @brief
    unireg.cc will call max_supported_record_length(), max_supported_keys(),
    max_supported_key_parts(), uint max_supported_key_length()
    to make sure that the storage engine can handle the data it is about to
    send. Return *real* limits of your storage engine here; MySQL will do
    min(your_limits, MySQL_limits) automatically.
   */
  uint max_supported_record_length() const { return HA_MAX_REC_LENGTH; }

  uint max_supported_keys()          const { return MAX_INDEXES; }
  uint max_supported_key_parts()     const { return MAX_REF_PARTS; }
  uint max_supported_key_part_length() const { return 2048; }
  /** @brief
    unireg.cc will call this to make sure that the storage engine can handle
    the data it is about to send. Return *real* limits of your storage engine
    here; MySQL will do min(your_limits, MySQL_limits) automatically.

      @details
    There is no need to implement ..._key_... methods if your engine doesn't
    support indexes.
   */
  uint max_supported_key_length()    const { return 16*1024; /* just to return something*/ }

  /**
    TODO: return actual upper bound of number of records in the table.
    (e.g. save number of records seen on full table scan and/or use file size
    as upper bound)
  */
  ha_rows estimate_rows_upper_bound() { return HA_POS_ERROR; }

  /* At the moment, we're ok with default handler::index_init() implementation. */
  int index_read_map(uchar * buf, const uchar * key,
                     key_part_map keypart_map,
                     enum ha_rkey_function find_flag);
    __attribute__((__warn_unused_result__));

  int index_read_map_impl(uchar * buf, const uchar * key,
                          key_part_map keypart_map,
                          enum ha_rkey_function find_flag,
                          const key_range *end_key)
    __attribute__((__warn_unused_result__));;

  int index_read_last_map(uchar * buf, const uchar * key,
                          key_part_map keypart_map)
    __attribute__((__warn_unused_result__));;

  int read_range_first(const key_range *start_key,
                       const key_range *end_key,
                       bool eq_range, bool sorted)
    __attribute__((__warn_unused_result__));;

  virtual double scan_time() { return (double) (stats.records+stats.deleted) / 20.0+10; }
  virtual double read_time(uint, uint, ha_rows rows)
  { return (double) rows /  20.0+1; }

  int open(const char *name, int mode, uint test_if_locked)
    __attribute__((__warn_unused_result__));
  int close(void) __attribute__((__warn_unused_result__));

  int write_row(uchar *buf) __attribute__((__warn_unused_result__));
  int update_row(const uchar *old_data, uchar *new_data)
    __attribute__((__warn_unused_result__));
  int delete_row(const uchar *buf) __attribute__((__warn_unused_result__));
  rocksdb::Status delete_or_singledelete(uint index,
                                         Rdb_transaction *trx,
                                         rocksdb::ColumnFamilyHandle* cf,
                                         const rocksdb::Slice& key)
    __attribute__((__warn_unused_result__));

  int index_next(uchar *buf) __attribute__((__warn_unused_result__));
  int index_next_with_direction(uchar *buf, bool move_forward)
    __attribute__((__warn_unused_result__));
  int index_prev(uchar *buf) __attribute__((__warn_unused_result__));

  int index_first(uchar *buf) __attribute__((__warn_unused_result__));
  int index_last(uchar *buf) __attribute__((__warn_unused_result__));

  class Item* idx_cond_push(uint keyno, class Item* idx_cond);
  /*
    Default implementation from cancel_pushed_idx_cond() suits us
  */
private:
  int index_first_intern(uchar *buf)
    __attribute__((__nonnull__, __warn_unused_result__));
  int index_last_intern(uchar *buf)
    __attribute__((__nonnull__, __warn_unused_result__));

  enum icp_result check_index_cond();
  int find_icp_matching_index_rec(bool move_forward, uchar *buf)
    __attribute__((__nonnull__, __warn_unused_result__));

  void calc_updated_indexes();
  int update_write_row(const uchar *old_data, uchar *new_data,
                       const bool skip_unique_check)
    __attribute__((__warn_unused_result__));

  int read_key_exact(Rdb_key_def *kd, rocksdb::Iterator* scan_it,
                     bool using_full_key, const rocksdb::Slice& key_slice)
    __attribute__((__nonnull__, __warn_unused_result__));
  int read_before_key(Rdb_key_def *kd, bool using_full_key,
                      const rocksdb::Slice& key_slice)
    __attribute__((__nonnull__, __warn_unused_result__));
  int read_after_key(Rdb_key_def *kd, bool using_full_key,
                     const rocksdb::Slice& key_slice)
    __attribute__((__nonnull__, __warn_unused_result__));

  int position_to_correct_key(Rdb_key_def *kd,
                              enum ha_rkey_function find_flag,
                              bool full_key_match, const uchar* key,
                              key_part_map keypart_map,
                              const rocksdb::Slice& key_slice,
                              bool* move_forward)
    __attribute__((__warn_unused_result__));

  int read_row_from_primary_key(uchar* buf)
    __attribute__((__nonnull__, __warn_unused_result__));
  int read_row_from_secondary_key(uchar* buf, Rdb_key_def* kd,
                                  bool move_forward)
    __attribute__((__nonnull__, __warn_unused_result__));

  int calc_eq_cond_len(Rdb_key_def *kd, enum ha_rkey_function find_flag,
                       const rocksdb::Slice& slice, int bytes_changed_by_succ,
                       const key_range *end_key, uint* end_key_packed_size)
    __attribute__((__warn_unused_result__));

  Rdb_tbl_def* get_table_if_exists(const char* tablename)
    __attribute__((__nonnull__, __warn_unused_result__));
  void read_thd_vars(THD *thd)
    __attribute__((__nonnull__));
public:
  int index_init(uint idx, bool sorted) __attribute__((__warn_unused_result__));
  int index_end() __attribute__((__warn_unused_result__));

  void unlock_row();

  /** @brief
    Unlike index_init(), rnd_init() can be called two consecutive times
    without rnd_end() in between (it only makes sense if scan=1). In this
    case, the second call should prepare for the new table scan (e.g if
    rnd_init() allocates the cursor, the second call should position the
    cursor to the start of the table; no need to deallocate and allocate
    it again. This is a required method.
  */
  int rnd_init(bool scan) __attribute__((__warn_unused_result__));
  int rnd_end() __attribute__((__warn_unused_result__));

  int rnd_next(uchar *buf) __attribute__((__warn_unused_result__));
  int rnd_next_with_direction(uchar *buf, bool move_forward)
    __attribute__((__warn_unused_result__));

  int rnd_pos(uchar *buf, uchar *pos) __attribute__((__warn_unused_result__));
  void position(const uchar *record);
  int info(uint);

  /* This function will always return success, therefore no annotation related
   * to checking the return value. Can't change the signature because it's
   * required by the interface. */
  int extra(enum ha_extra_function operation);

  int start_stmt(THD *thd, thr_lock_type lock_type)
    __attribute__((__warn_unused_result__));
  int external_lock(THD *thd, int lock_type)
    __attribute__((__warn_unused_result__));
  int delete_all_rows(ha_rows* nrows) __attribute__((__warn_unused_result__));
  int truncate() __attribute__((__warn_unused_result__));

  int reset()
  {
    /* Free blob data */
    retrieved_record.clear();
    return 0;
  }

  int check(THD* thd, HA_CHECK_OPT* check_opt)
    __attribute__((__warn_unused_result__));
  void remove_rows(Rdb_tbl_def *tbl);
  ha_rows records_in_range(uint inx, key_range *min_key,
                           key_range *max_key)
    __attribute__((__warn_unused_result__));
  int delete_table(const char *from) __attribute__((__warn_unused_result__));
  int create(const char *name, TABLE *form, HA_CREATE_INFO *create_info)
    __attribute__((__warn_unused_result__));
  bool check_if_incompatible_data(HA_CREATE_INFO *info,
                                  uint table_changes)
    __attribute__((__warn_unused_result__));

  THR_LOCK_DATA **store_lock(THD *thd, THR_LOCK_DATA **to,
                             enum thr_lock_type lock_type)
    __attribute__((__warn_unused_result__));

  my_bool register_query_cache_table(THD *thd, char *table_key,
                                     uint key_length,
                                     qc_engine_callback
                                     *engine_callback,
                                     ulonglong *engine_data)
  {
    /* Currently, we don't support query cache */
    return FALSE;
  }

  bool get_error_message(const int error, String *buf)
    __attribute__((__nonnull__));

  void get_auto_increment(ulonglong offset, ulonglong increment,
                          ulonglong nb_desired_values,
                          ulonglong *first_value,
                          ulonglong *nb_reserved_values);
  void update_create_info(HA_CREATE_INFO *create_info);
  int optimize(THD *thd, HA_CHECK_OPT *check_opt)
    __attribute__((__warn_unused_result__));
  int analyze(THD* thd, HA_CHECK_OPT* check_opt)
    __attribute__((__warn_unused_result__));

 public:
  virtual void rpl_before_delete_rows() override;
  virtual void rpl_after_delete_rows() override;
  virtual void rpl_before_update_rows() override;
  virtual void rpl_after_update_rows() override;
  virtual bool rpl_lookup_rows();

 private:
  bool in_rpl_delete_rows;
  bool in_rpl_update_rows;
};

}  // namespace myrocks
