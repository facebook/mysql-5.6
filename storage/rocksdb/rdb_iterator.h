/*
   Copyright (c) 2020, Facebook, Inc.

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

// MySQL header files
#include "sql/debug_sync.h"
#include "sql/handler.h"

// MyRocks header files
#include "./ha_rocksdb.h"
#include "./ha_rocksdb_proto.h"
#include "./rdb_converter.h"
#include "./rdb_datadic.h"

namespace myrocks {

// If the iterator is not valid it might be because of EOF but might be due
// to IOError or corruption. The good practice is always check it.
// https://github.com/facebook/rocksdb/wiki/Iterator#error-handling
[[nodiscard]] bool is_valid_rdb_iterator(const rocksdb::Iterator &it);

class Rdb_iterator {
 public:
  virtual ~Rdb_iterator() = 0;

  /*
    find_flag has the same semantics as the SQL layer and current accepts 6
    different values.
                   direction
      seek method  ASC                   DESC
      prefix       HA_READ_KEY_EXACT     HA_READ_PREFIX_LAST
      inclusive    HA_READ_KEY_OR_NEXT   HA_READ_PREFIX_LAST_OR_PREV
      exclusive    HA_READ_AFTER_KEY     HA_READ_BEFORE_KEY

    - Inclusive vs exclusive seek means exactly that, whether the seek key is
       included or not in the resultset.
    - Prefix seek is essentially the same as inclusive seek, except that the
      iterator will return EOF once outside the prefix, whereas inclusive seek
      will continue iterating until end of the table.
    - For each flag, there is a corresponding ascending vs descending version.
      In addition to the seek direction, it also specifies whether to start
      from the upper or lower end of a prefix. For descending scans, we seek
      to the upper end and vice versa for ascending scans.

    full_key_match indicates whether the seek key is a full key or not, and is
    needed for now to do the correct seek internally. Ideally, we wouldn't
    need this flag, and once rocksdb supports prefix seeks, the API can be
    simplified to remove this parameter.
  */
  virtual int seek(enum ha_rkey_function find_flag,
                   const rocksdb::Slice start_key, bool full_key_match,
                   const rocksdb::Slice end_key, bool read_current = false) = 0;
  virtual int get(const rocksdb::Slice *key, rocksdb::PinnableSlice *value,
                  Rdb_lock_type type, bool skip_ttl_check = false,
                  bool skip_wait = false) = 0;
  virtual void multi_get(const std::vector<rocksdb::Slice> &key_slices,
                         std::vector<rocksdb::PinnableSlice> &value_slices,
                         std::vector<int> &rtn_codes, bool sorted_input) = 0;
  virtual int next() = 0;
  virtual int prev() = 0;
  virtual rocksdb::Slice key() = 0;
  virtual rocksdb::Slice value() = 0;
  virtual void reset() = 0;
  virtual bool is_valid() = 0;
};

class Rdb_iterator_base : public Rdb_iterator {
 private:
  int read_before_key(const bool full_key_match,
                      const rocksdb::Slice &key_slice);
  int read_after_key(const rocksdb::Slice &key_slice);
  void release_scan_iterator();
  void setup_scan_iterator(const rocksdb::Slice *const slice,
                           const uint eq_cond_len, bool read_current);
  int calc_eq_cond_len(enum ha_rkey_function find_flag,
                       const rocksdb::Slice &start_key,
                       const int bytes_changed_by_succ,
                       const rocksdb::Slice &end_key);
  int next_with_direction(bool move_forward, bool skip_next);
  [[nodiscard]] int convert_get_status(myrocks::Rdb_transaction &tx,
                                       const rocksdb::Status &status,
                                       rocksdb::PinnableSlice *value,
                                       bool skip_ttl_check) const;
  [[nodiscard]] int convert_iterator_status() const;

 public:
  Rdb_iterator_base(THD *thd, ha_rocksdb *rocksdb_handler,
                    const Rdb_key_def &kd, const Rdb_key_def &pkd,
                    const Rdb_tbl_def *tbl_def);

  ~Rdb_iterator_base() override;

  int seek(enum ha_rkey_function find_flag, const rocksdb::Slice start_key,
           bool full_key_match, const rocksdb::Slice end_key,
           bool read_current = false) override;
  int get(const rocksdb::Slice *key, rocksdb::PinnableSlice *value,
          Rdb_lock_type type, bool skip_ttl_check = false,
          bool skip_wait = false) override;
  void multi_get(const std::vector<rocksdb::Slice> &key_slices,
                 std::vector<rocksdb::PinnableSlice> &value_slices,
                 std::vector<int> &rtn_codes, bool sorted_input) override;

  int next() override { return next_with_direction(true, false); }

  int prev() override { return next_with_direction(false, false); }

  rocksdb::Slice key() override { return m_scan_it->key(); }

  rocksdb::Slice value() override { return m_scan_it->value(); }

  void reset() override {
    release_scan_iterator();
    m_valid = false;
  }

  bool is_valid() override { return m_valid; }
  void set_ignore_killed(bool flag) { m_ignore_killed = flag; }

 protected:
  friend class Rdb_iterator;

  void setup_prefix_buffer(enum ha_rkey_function find_flag,
                           const rocksdb::Slice start_key);

  const Rdb_key_def &m_kd;

  // Rdb_key_def of the primary key
  const Rdb_key_def &m_pkd;

  const Rdb_tbl_def *m_tbl_def;

  THD *m_thd;

  ha_rocksdb *m_rocksdb_handler;

  /* Iterator used for range scans and for full table/index scans */
  rocksdb::Iterator *m_scan_it;

  /* Whether m_scan_it was created with skip_bloom=true */
  bool m_scan_it_skips_bloom;

  const rocksdb::Snapshot *m_scan_it_snapshot;

  /* Buffers used for upper/lower bounds for m_scan_it. */
  uchar *m_scan_it_lower_bound;
  uchar *m_scan_it_upper_bound;
  rocksdb::Slice m_scan_it_lower_bound_slice;
  rocksdb::Slice m_scan_it_upper_bound_slice;

  uchar *m_prefix_buf;
  rocksdb::Slice m_prefix_tuple;
  TABLE_TYPE m_table_type;
  bool m_valid;
  bool m_check_iterate_bounds;
  bool m_ignore_killed;

  Rdb_iterator_base(const Rdb_iterator_base &) = delete;
  Rdb_iterator_base(Rdb_iterator_base &&) = delete;
  Rdb_iterator_base &operator=(const Rdb_iterator_base &) = delete;
  Rdb_iterator_base &operator=(Rdb_iterator_base &&) = delete;
};

class Rdb_iterator_partial : public Rdb_iterator_base {
 private:
  TABLE *m_table;
  MEM_ROOT m_mem_root;

  Rdb_iterator_base m_iterator_pk;
  Rdb_converter m_converter;

  bool m_partial_valid;
  bool m_materialized;

  enum class Iterator_position {
    UNKNOWN,
    START_NEXT_PREFIX,
    START_CUR_PREFIX,
    END_OF_FILE
  };

  Iterator_position m_iterator_pk_position;

  const uint m_threshold;
  const uint m_prefix_keyparts;

  uchar *m_cur_prefix_key;
  uint m_cur_prefix_key_len;

  uchar *m_record_buf;
  uchar *m_pack_buffer;
  uchar *m_sk_packed_tuple;

  Rdb_string_writer m_sk_tails;

  int get_prefix_len(const rocksdb::Slice &start_key, uint *prefix_cnt,
                     uint *prefix_len);
  int get_prefix_from_start(enum ha_rkey_function find_flag,
                            const rocksdb::Slice &start_key);
  int get_next_prefix(bool direction);
  int seek_next_prefix(bool direction);
  int materialize_prefix();
  int read_prefix_from_pk();
  int next_with_direction_in_group(bool direction);
  int next_with_direction(bool direction);
  int handle_get_result(int rtn_code, const rocksdb::Slice *key,
                        rocksdb::PinnableSlice *value, Rdb_lock_type type,
                        bool skip_ttl_check, bool skip_wait);

  using Slice_pair = std::pair<rocksdb::Slice, rocksdb::Slice>;
  using Records = std::vector<Slice_pair>;

  struct slice_comparator {
    explicit slice_comparator(const rocksdb::Comparator *c) : m_comparator(c) {}
    const rocksdb::Comparator *const m_comparator;

    bool operator()(const rocksdb::Slice &lhs, const Slice_pair &rhs) {
      return m_comparator->Compare(lhs, rhs.first) < 0;
    }
    bool operator()(const Slice_pair &lhs, const rocksdb::Slice &rhs) {
      return m_comparator->Compare(lhs.first, rhs) < 0;
    }
    bool operator()(const Slice_pair &lhs, const Slice_pair &rhs) {
      return m_comparator->Compare(lhs.first, rhs.first) < 0;
    }
  };

  Records m_records;
  Records::iterator m_records_it;
  slice_comparator m_comparator;

 public:
  Rdb_iterator_partial(THD *thd, const Rdb_key_def &kd, const Rdb_key_def &pkd,
                       const Rdb_tbl_def *tbl_def, TABLE *table,
                       const dd::Table *dd_table);
  ~Rdb_iterator_partial() override;

  int seek(enum ha_rkey_function find_flag, const rocksdb::Slice start_key,
           bool full_key_match, const rocksdb::Slice end_key,
           bool read_current = false) override;
  int get(const rocksdb::Slice *key, rocksdb::PinnableSlice *value,
          Rdb_lock_type type, bool skip_ttl_check = false,
          bool skip_wait = false) override;
  void multi_get(const std::vector<rocksdb::Slice> &key_slices,
                 std::vector<rocksdb::PinnableSlice> &value_slices,
                 std::vector<int> &rtn_codes, bool sorted_input) override;
  int next() override;
  int prev() override;
  rocksdb::Slice key() override;
  rocksdb::Slice value() override;
  void reset() override;
  bool is_valid() override {
    // This function only used for intrinsic temp tables.
    assert(false);
    return false;
  }

  Rdb_iterator_partial(const Rdb_iterator_partial &) = delete;
  Rdb_iterator_partial(Rdb_iterator_partial &&) = delete;
  Rdb_iterator_partial &operator=(const Rdb_iterator_partial &) = delete;
  Rdb_iterator_partial &operator=(Rdb_iterator_partial &&) = delete;
};

}  // namespace myrocks
