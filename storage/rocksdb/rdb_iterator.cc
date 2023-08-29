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

#include "./rdb_iterator.h"

#include <algorithm>

/* MySQL includes */
#include "scope_guard.h"
#include "sql/sql_class.h"
#include "sql/thr_malloc.h"

namespace myrocks {

Rdb_iterator::~Rdb_iterator() {}

Rdb_iterator_base::Rdb_iterator_base(THD *thd, ha_rocksdb *rocksdb_handler,
                                     const std::shared_ptr<Rdb_key_def> kd,
                                     const std::shared_ptr<Rdb_key_def> pkd,
                                     const Rdb_tbl_def *tbl_def)
    : m_kd(kd),
      m_pkd(pkd),
      m_tbl_def(tbl_def),
      m_thd(thd),
      m_rocksdb_handler(rocksdb_handler),
      m_scan_it(nullptr),
      m_scan_it_skips_bloom(false),
      m_scan_it_snapshot(nullptr),
      m_scan_it_lower_bound(nullptr),
      m_scan_it_upper_bound(nullptr),
      m_prefix_buf(nullptr),
      m_table_type(tbl_def->get_table_type()),
      m_valid(false),
      m_check_iterate_bounds(false) {
  if (tbl_def->get_table_type() == INTRINSIC_TMP) {
    if (m_rocksdb_handler) {
      add_tmp_table_handler(m_thd, m_rocksdb_handler);
    }
  }
}

Rdb_iterator_base::~Rdb_iterator_base() {
  release_scan_iterator();
  my_free(m_scan_it_lower_bound);
  m_scan_it_lower_bound = nullptr;
  my_free(m_scan_it_upper_bound);
  m_scan_it_upper_bound = nullptr;
  my_free(m_prefix_buf);
  m_prefix_buf = nullptr;
  if (m_table_type == INTRINSIC_TMP) {
    if (m_rocksdb_handler) {
      remove_tmp_table_handler(m_thd, m_rocksdb_handler);
    }
  }
}

int Rdb_iterator_base::read_before_key(const bool full_key_match,
                                       const rocksdb::Slice &key_slice) {
  /*
    We are looking for the first record such that

      index_tuple $LT lookup_tuple

    with HA_READ_BEFORE_KEY, $LT = '<',
    with HA_READ_PREFIX_LAST_OR_PREV, $LT = '<='
    with HA_READ_PREFIX_LAST, $LT = '=='

    Symmetry with read_after_key is possible if rocksdb supported prefix seeks.
  */
  rocksdb_smart_seek(!m_kd->m_is_reverse_cf, m_scan_it, key_slice);

  while (is_valid_iterator(m_scan_it)) {
    if (thd_killed(m_thd)) {
      return HA_ERR_QUERY_INTERRUPTED;
    }
    /*
      We are using full key and we've hit an exact match.
      */
    if ((full_key_match &&
         m_kd->value_matches_prefix(m_scan_it->key(), key_slice))) {
      rocksdb_smart_next(!m_kd->m_is_reverse_cf, m_scan_it);
      continue;
    }

    return HA_EXIT_SUCCESS;
  }

  return HA_ERR_END_OF_FILE;
}

int Rdb_iterator_base::read_after_key(const rocksdb::Slice &key_slice) {
  /*
    We are looking for the first record such that

    index_tuple $GT lookup_tuple

    with HA_READ_AFTER_KEY, $GT = '>',
    with HA_READ_KEY_OR_NEXT, $GT = '>='
    with HA_READ_KEY_EXACT, $GT = '=='
  */
  rocksdb_smart_seek(m_kd->m_is_reverse_cf, m_scan_it, key_slice);

  return is_valid_iterator(m_scan_it) ? HA_EXIT_SUCCESS : HA_ERR_END_OF_FILE;
}

void Rdb_iterator_base::release_scan_iterator() {
  delete m_scan_it;
  m_scan_it = nullptr;

  if (m_scan_it_snapshot) {
    auto rdb = rdb_get_rocksdb_db();
    rdb->ReleaseSnapshot(m_scan_it_snapshot);
    m_scan_it_snapshot = nullptr;
  }
}

void Rdb_iterator_base::setup_scan_iterator(const rocksdb::Slice *const slice,
                                            const uint eq_cond_len,
                                            bool read_current) {
  assert(slice->size() >= eq_cond_len);

  bool skip_bloom = true;

  const rocksdb::Slice eq_cond(slice->data(), eq_cond_len);

  // The size of m_scan_it_lower_bound (and upper) is technically
  // max_packed_sk_len as calculated in ha_rocksdb::alloc_key_buffers.  Rather
  // than recalculating that number, we pass in the max of eq_cond_len and
  // Rdb_key_def::INDEX_NUMBER_SIZE which is guaranteed to be smaller than
  // max_packed_sk_len, hence ensuring no buffer overrun.
  //
  // See setup_iterator_bounds on how the bound_len parameter is
  // used.
  if (ha_rocksdb::check_bloom_and_set_bounds(
          m_thd, *m_kd, eq_cond,
          std::max(eq_cond_len, (uint)Rdb_key_def::INDEX_NUMBER_SIZE),
          m_scan_it_lower_bound, m_scan_it_upper_bound,
          &m_scan_it_lower_bound_slice, &m_scan_it_upper_bound_slice,
          &m_check_iterate_bounds)) {
    skip_bloom = false;
  }

  /*
    In some cases, setup_scan_iterator() is called multiple times from
    the same query but bloom filter can not always be used.
    Suppose the following query example. id2 is VARCHAR(30) and PRIMARY KEY
    (id1, id2).
    select count(*) from t2 WHERE id1=100 and id2 IN ('00000000000000000000',
    '100');
    In this case, setup_scan_iterator() is called twice, the first time is for
    (id1, id2)=(100, '00000000000000000000') and the second time is for (100,
    '100').
    If prefix bloom filter length is 24 bytes, prefix bloom filter can be used
    for the
    first condition but not for the second condition.
    If bloom filter condition is changed, currently it is necessary to destroy
    and
    re-create Iterator.
    */
  if (m_scan_it_skips_bloom != skip_bloom) {
    release_scan_iterator();
  }

  /*
    SQL layer can call rnd_init() multiple times in a row.
    In that case, re-use the iterator, but re-position it at the table start.
    */
  if (!m_scan_it) {
    m_scan_it = rdb_tx_get_iterator(
        m_thd, m_kd->get_cf(), skip_bloom, m_scan_it_lower_bound_slice,
        m_scan_it_upper_bound_slice, &m_scan_it_snapshot, m_table_type,
        read_current, !read_current);
    m_scan_it_skips_bloom = skip_bloom;
  }
}

void Rdb_iterator_base::setup_prefix_buffer(enum ha_rkey_function find_flag,
                                            const rocksdb::Slice start_key) {
  uint prefix_key_len;

  if (!m_prefix_buf) {
    const uint packed_len = m_kd->max_storage_fmt_length();
    m_scan_it_lower_bound = reinterpret_cast<uchar *>(
        my_malloc(PSI_NOT_INSTRUMENTED, packed_len, MYF(0)));
    m_scan_it_upper_bound = reinterpret_cast<uchar *>(
        my_malloc(PSI_NOT_INSTRUMENTED, packed_len, MYF(0)));
    m_prefix_buf = reinterpret_cast<uchar *>(
        my_malloc(PSI_NOT_INSTRUMENTED, packed_len, MYF(0)));
  }

  if (find_flag == HA_READ_KEY_EXACT || find_flag == HA_READ_PREFIX_LAST) {
    memcpy(m_prefix_buf, start_key.data(), start_key.size());
    prefix_key_len = start_key.size();
  } else {
    m_kd->get_infimum_key(m_prefix_buf, &prefix_key_len);
  }
  m_prefix_tuple = rocksdb::Slice((char *)m_prefix_buf, prefix_key_len);
}

int Rdb_iterator_base::calc_eq_cond_len(enum ha_rkey_function find_flag,
                                        const rocksdb::Slice &start_key,
                                        const int bytes_changed_by_succ,
                                        const rocksdb::Slice &end_key) {
  if (find_flag == HA_READ_KEY_EXACT) return start_key.size();

  if (find_flag == HA_READ_PREFIX_LAST) {
    /*
      We have made the kd.successor(m_sk_packed_tuple) call above.

      The slice is at least Rdb_key_def::INDEX_NUMBER_SIZE bytes long.
    */
    return start_key.size() - bytes_changed_by_succ;
  }

  if (!end_key.empty()) {
    /*
      Calculating length of the equal conditions here. 4 byte index id is
      included.
      Example1: id1 BIGINT, id2 INT, id3 BIGINT, PRIMARY KEY (id1, id2, id3)
       WHERE id1=1 AND id2=1 AND id3>=2 => eq_cond_len= 4+8+4= 16
       WHERE id1=1 AND id2>=1 AND id3>=2 => eq_cond_len= 4+8= 12
      Example2: id1 VARCHAR(30), id2 INT, PRIMARY KEY (id1, id2)
       WHERE id1 = 'AAA' and id2 < 3; => eq_cond_len=13 (varchar used 9 bytes)
    */
    return start_key.difference_offset(end_key);
  }

  /*
    On range scan without any end key condition, there is no
    eq cond, and eq cond length is the same as index_id size (4 bytes).
    Example1: id1 BIGINT, id2 INT, id3 BIGINT, PRIMARY KEY (id1, id2, id3)
     WHERE id1>=1 AND id2 >= 2 and id2 <= 5 => eq_cond_len= 4
  */
  return Rdb_key_def::INDEX_NUMBER_SIZE;
}

int Rdb_iterator_base::next_with_direction(bool move_forward, bool skip_next) {
  int rc = 0;
  const auto &kd = *m_kd;
  Rdb_transaction *const tx = get_tx_from_thd(m_thd);

  if (!m_valid) return HA_ERR_END_OF_FILE;
  const rocksdb::Comparator *kd_comp = kd.get_cf()->GetComparator();

  for (;;) {
    DEBUG_SYNC(m_thd, "rocksdb.check_flags_nwd");
    if (thd_killed(m_thd)) {
      rc = HA_ERR_QUERY_INTERRUPTED;
      break;
    }

    assert(m_scan_it != nullptr);
    if (m_scan_it == nullptr) {
      rc = HA_ERR_INTERNAL_ERROR;
      break;
    }

    if (skip_next) {
      skip_next = false;
    } else {
      if (move_forward) {
        rocksdb_smart_next(kd.m_is_reverse_cf, m_scan_it);
      } else {
        rocksdb_smart_prev(kd.m_is_reverse_cf, m_scan_it);
      }
    }

    if (!is_valid_iterator(m_scan_it)) {
      rc = HA_ERR_END_OF_FILE;
      break;
    }

    const rocksdb::Slice &key = m_scan_it->key();
    const rocksdb::Slice &value = m_scan_it->value();

    // Outside our range, return EOF.
    if (!kd.value_matches_prefix(key, m_prefix_tuple)) {
      rc = HA_ERR_END_OF_FILE;
      break;
    }

    // Check specified lower/upper bounds
    // For example, retrieved key is 00077
    // in     cf, lower_bound: 0076 and uppper bound: 0078
    //     cf->Compare(0077, 0078) > 0 ==> False
    //     cf->Compare(0077, 0076) < 0 ==> False
    // in rev cf, lower_bound: 0078 and uppper bound: 0076
    //     revcf->Compare(0077, 0076) > 0 ==> False
    //     revcf->Compare(0077, 0078) < 0 ==> False
    if (m_check_iterate_bounds &&
        ((!m_scan_it_upper_bound_slice.empty() &&
          kd_comp->Compare(key, m_scan_it_upper_bound_slice) > 0) ||
         (!m_scan_it_lower_bound_slice.empty() &&
          kd_comp->Compare(key, m_scan_it_lower_bound_slice) < 0))) {
      rc = HA_ERR_END_OF_FILE;
      break;
    }

    // Record is not visible due to TTL, move to next record.
    if (m_pkd->has_ttl() && rdb_should_hide_ttl_rec(kd, &value, tx)) {
      continue;
    }

    break;
  }

  if (rc) {
    assert(m_valid);
    m_valid = false;
  }
  return rc;
}

int Rdb_iterator_base::seek(enum ha_rkey_function find_flag,
                            const rocksdb::Slice start_key, bool full_key_match,
                            const rocksdb::Slice end_key, bool read_current) {
  int rc = 0;
  int bytes_changed_by_succ = 0;

  setup_prefix_buffer(find_flag, start_key);

  uchar *start_key_buf = (uchar *)start_key.data();
  // We need to undo mutating the start key in case of retries using the same
  // buffer.
  auto start_key_guard = create_scope_guard([this, start_key_buf, start_key] {
    this->m_kd->predecessor(start_key_buf, start_key.size());
  });
  if (find_flag == HA_READ_PREFIX_LAST_OR_PREV ||
      find_flag == HA_READ_PREFIX_LAST || find_flag == HA_READ_AFTER_KEY) {
    bytes_changed_by_succ = m_kd->successor(start_key_buf, start_key.size());
  } else {
    start_key_guard.commit();
  }

  const uint eq_cond_len =
      calc_eq_cond_len(find_flag, start_key, bytes_changed_by_succ, end_key);

  /*
    This will open the iterator and position it at a record that's equal or
    greater than the lookup tuple.
  */
  setup_scan_iterator(&start_key, eq_cond_len, read_current);

  /*
    Once we are positioned on from above, move to the position we really
    want: See storage/rocksdb/rocksdb-range-access.txt
  */
  bool direction = (find_flag == HA_READ_KEY_EXACT) ||
                   (find_flag == HA_READ_AFTER_KEY) ||
                   (find_flag == HA_READ_KEY_OR_NEXT);
  if (direction) {
    rc = read_after_key(start_key);
  } else {
    rc = read_before_key(full_key_match, start_key);
  }

  if (!rc) {
    m_valid = true;
    rc = next_with_direction(direction, true);
  }

  m_valid = !rc;
  return rc;
}

int Rdb_iterator_base::get(const rocksdb::Slice *key,
                           rocksdb::PinnableSlice *value, Rdb_lock_type type,
                           bool skip_ttl_check, bool skip_wait) {
  int rc = HA_EXIT_SUCCESS;
  m_valid = false;
  Rdb_transaction *const tx = get_tx_from_thd(m_thd);
  rocksdb::Status s;
  if (type == RDB_LOCK_NONE) {
    s = rdb_tx_get(tx, m_kd->get_cf(), *key, value, m_table_type);
  } else {
    s = rdb_tx_get_for_update(tx, *m_kd, *key, value, m_table_type,
                              type == RDB_LOCK_WRITE, skip_wait);
  }

  DBUG_EXECUTE_IF("rocksdb_return_status_corrupted",
                  { s = rocksdb::Status::Corruption(); });

  if (!s.IsNotFound() && !s.ok()) {
    return rdb_tx_set_status_error(tx, s, *m_kd, m_tbl_def);
  }

  const bool hide_ttl_rec =
      !skip_ttl_check && m_kd->has_ttl() &&
      rdb_should_hide_ttl_rec(*m_kd, s.IsNotFound() ? nullptr : value, tx);

  if (hide_ttl_rec || s.IsNotFound()) {
    return HA_ERR_KEY_NOT_FOUND;
  }

  return rc;
}

Rdb_iterator_partial::Rdb_iterator_partial(
    THD *thd, const std::shared_ptr<Rdb_key_def> kd,
    const std::shared_ptr<Rdb_key_def> pkd, const Rdb_tbl_def *tbl_def,
    TABLE *table, const dd::Table *dd_table)
    : Rdb_iterator_base(thd, nullptr, kd, pkd, tbl_def),
      m_table(table),
      m_iterator_pk(thd, nullptr, pkd, pkd, tbl_def),
      m_converter(thd, tbl_def, table, dd_table),
      m_partial_valid(false),
      m_materialized(false),
      m_iterator_pk_position(Iterator_position::UNKNOWN),
      m_threshold(kd->partial_index_threshold()),
      m_prefix_keyparts(kd->partial_index_keyparts()),
      m_cur_prefix_key_len(0),
      m_records_it(m_records.end()),
      m_comparator(slice_comparator(m_kd->get_cf()->GetComparator())) {
  init_sql_alloc(PSI_NOT_INSTRUMENTED, &m_mem_root, 4096);
  auto max_mem = get_partial_index_sort_max_mem(thd);
  if (max_mem) {
    m_mem_root.set_max_capacity(max_mem);
  }
  m_converter.setup_field_decoders(table->read_set, table->s->primary_key,
                                   true /* keyread_only */,
                                   true /* decode all */);

  const uint packed_len =
      std::max(m_kd->max_storage_fmt_length(), m_pkd->max_storage_fmt_length());
  m_cur_prefix_key = reinterpret_cast<uchar *>(
      my_malloc(PSI_NOT_INSTRUMENTED, packed_len, MYF(0)));
  m_record_buf = reinterpret_cast<uchar *>(
      my_malloc(PSI_NOT_INSTRUMENTED, table->s->reclength, MYF(0)));
  m_pack_buffer = reinterpret_cast<uchar *>(
      my_malloc(PSI_NOT_INSTRUMENTED, packed_len, MYF(0)));
  m_sk_packed_tuple = reinterpret_cast<uchar *>(
      my_malloc(PSI_NOT_INSTRUMENTED, packed_len, MYF(0)));
}

Rdb_iterator_partial::~Rdb_iterator_partial() {
  reset();
  my_free(m_cur_prefix_key);
  m_cur_prefix_key = nullptr;
  my_free(m_record_buf);
  m_record_buf = nullptr;
  my_free(m_pack_buffer);
  m_pack_buffer = nullptr;
  my_free(m_sk_packed_tuple);
  m_sk_packed_tuple = nullptr;
}

int Rdb_iterator_partial::get_prefix_len(const rocksdb::Slice &start_key,
                                         uint *prefix_cnt, uint *prefix_len) {
  Rdb_string_reader reader(&start_key);
  if ((!reader.read(Rdb_key_def::INDEX_NUMBER_SIZE))) {
    return HA_ERR_INTERNAL_ERROR;
  }

  for (uint i = 0; i < m_prefix_keyparts; i++) {
    if (reader.remaining_bytes() == 0) {
      *prefix_cnt = i;
      *prefix_len = reader.get_current_ptr() - start_key.data();
      return HA_EXIT_SUCCESS;
    }

    if (m_kd->read_memcmp_key_part(&reader, i) > 0) {
      return HA_ERR_INTERNAL_ERROR;
    }
  }

  *prefix_cnt = m_prefix_keyparts;
  *prefix_len = reader.get_current_ptr() - start_key.data();

  return HA_EXIT_SUCCESS;
}

/*
 * Determines the correct prefix from start_key by reading from primary key if
 * needed.
 *
 * Populates m_cur_prefix_key/m_cur_prefix_key_len.
 */
int Rdb_iterator_partial::get_prefix_from_start(
    enum ha_rkey_function find_flag, const rocksdb::Slice &start_key) {
  int rc = 0;
  uint prefix_cnt = 0;
  uint prefix_len = 0;

  rc = get_prefix_len(start_key, &prefix_cnt, &prefix_len);
  if (rc) {
    return rc;
  }
  assert_IMP(prefix_cnt == 0, prefix_len == Rdb_key_def::INDEX_NUMBER_SIZE);

  // There are 2 scenarios where a read is required to determine the prefix:
  // 1. There are not enough keyparts in the start_key.
  // 2. An exclusive seek key is provided, meaning that we need to read the next
  // prefix.
  m_iterator_pk_position = Iterator_position::UNKNOWN;
  if (prefix_cnt < m_prefix_keyparts ||
      (prefix_len == start_key.size() &&
       (find_flag == HA_READ_AFTER_KEY || find_flag == HA_READ_BEFORE_KEY))) {
    uint tmp;

    rocksdb::Slice empty_end_key;

    // Since the PK/SK share the same prefix, the primary key can be constructed
    // using the secondary key, with the index_id overwritten.
    memcpy(m_cur_prefix_key, start_key.data(), prefix_len);
    rocksdb::Slice seek_key((const char *)m_cur_prefix_key, prefix_len);
    m_pkd->get_infimum_key(m_cur_prefix_key, &tmp);

    rc = m_iterator_pk.seek(find_flag, seek_key, false, empty_end_key);
    if (rc) {
      return rc;
    }

    rc = get_prefix_len(m_iterator_pk.key(), &prefix_cnt, &prefix_len);
    if (rc) {
      return rc;
    }
    memcpy(m_cur_prefix_key, m_iterator_pk.key().data(), prefix_len);
  } else {
    memcpy(m_cur_prefix_key, start_key.data(), prefix_len);
  }

  m_cur_prefix_key_len = prefix_len;
  return HA_EXIT_SUCCESS;
}

/*
 * Determines the next prefix given the current m_cur_prefix_key value.
 *
 * Populates m_cur_prefix_key/m_cur_prefix_key_len.
 */
int Rdb_iterator_partial::get_next_prefix(bool direction) {
  rocksdb::Slice cur_prefix_key((const char *)m_cur_prefix_key,
                                m_cur_prefix_key_len);
  uint tmp;
  int rc = 0;

  if (direction && m_iterator_pk_position != Iterator_position::UNKNOWN &&
      m_iterator_pk_position != Iterator_position::START_CUR_PREFIX) {
    if (m_iterator_pk_position == Iterator_position::END_OF_FILE) {
      return HA_ERR_END_OF_FILE;
    } else if (m_iterator_pk_position == Iterator_position::START_NEXT_PREFIX) {
      uint prefix_cnt = 0;
      uint prefix_len = 0;
      rc = get_prefix_len(m_iterator_pk.key(), &prefix_cnt, &prefix_len);
      if (rc) {
        m_iterator_pk_position = Iterator_position::UNKNOWN;
        return rc;
      }
      memcpy(m_cur_prefix_key, m_iterator_pk.key().data(), prefix_len);
      m_cur_prefix_key_len = prefix_len;
      m_iterator_pk_position = Iterator_position::START_CUR_PREFIX;
    }
  } else {
    m_iterator_pk_position = Iterator_position::UNKNOWN;
    rc = get_prefix_from_start(
        direction ? HA_READ_AFTER_KEY : HA_READ_BEFORE_KEY, cur_prefix_key);
  }
  m_kd->get_infimum_key(m_cur_prefix_key, &tmp);

  cur_prefix_key =
      rocksdb::Slice((const char *)m_cur_prefix_key, m_cur_prefix_key_len);
  if (!rc && !m_kd->value_matches_prefix(cur_prefix_key, m_prefix_tuple)) {
    rc = HA_ERR_END_OF_FILE;
  }

  return rc;
}

/*
 * Positions the cursor to the first row of the next prefix.
 */
int Rdb_iterator_partial::seek_next_prefix(bool direction) {
  rocksdb::Slice empty_end_key;
  uint tmp;

  // Fetch next prefix using PK.
  int rc = get_next_prefix(direction);
  if (rc) return rc;

  // First try reading from SK in the current prefix.
  rocksdb::Slice cur_prefix_key((const char *)m_cur_prefix_key,
                                m_cur_prefix_key_len);
  m_kd->get_infimum_key(m_cur_prefix_key, &tmp);

  rocksdb::PinnableSlice value;
  rc = Rdb_iterator_base::get(&cur_prefix_key, &value, RDB_LOCK_NONE,
                              true /* skip ttl check*/);

  if (rc == HA_ERR_KEY_NOT_FOUND) {
    // Nothing in SK, so check PK.
    rc = read_prefix_from_pk();

    if (rc == 0) {
      // Not materialized on disk, seek to beginning/end of map.
      m_materialized = false;
      if (direction ^ m_kd->m_is_reverse_cf) {
        m_records_it = m_records.begin();
      } else {
        m_records_it = m_records.end();
        m_records_it--;
      }
    } else {
      // The current prefix was determined by reading from PK in
      // get_next_prefix, so rows must exist within this prefix on the PK.
      assert(rc != HA_ERR_END_OF_FILE);
    }
  } else if (rc == 0) {
    // Found rows in SK, so use them
    m_materialized = true;
    assert(value.size() == 0);

    // Rdb_iterator_base::seek below will overwrite m_prefix_tuple, so we save a
    // copy here.
    size_t prefix_buf_len = m_prefix_tuple.size();
    uchar *prefix_buf_copy = (uchar *)my_alloca(prefix_buf_len);
    memcpy(prefix_buf_copy, m_prefix_buf, prefix_buf_len);

    rc = Rdb_iterator_base::seek(
        direction ? HA_READ_KEY_EXACT : HA_READ_PREFIX_LAST, cur_prefix_key,
        true, empty_end_key);
    // Skip sentinel values.
    if (rc == 0 && Rdb_iterator_base::key().size() == m_cur_prefix_key_len) {
      rc = direction ? Rdb_iterator_base::next() : Rdb_iterator_base::prev();
    }

    // Restore m_prefix_tuple
    memcpy(m_prefix_buf, prefix_buf_copy, prefix_buf_len);
    m_prefix_tuple = rocksdb::Slice((char *)m_prefix_buf, prefix_buf_len);
  }

  return rc;
}

/*
 * Materializes large groups by reading from the primary key and writing into
 * the secondary key in our own writebatch.
 *
 * This is done while exclusively locking the SK prefix. This will block other
 * queries from materializing the same group, and any pending writes in the same
 * group.
 */
int Rdb_iterator_partial::materialize_prefix() {
  uint tmp;
  int rc = HA_EXIT_SUCCESS;
  Rdb_transaction *const tx = get_tx_from_thd(m_thd);
  m_kd->get_infimum_key(m_cur_prefix_key, &tmp);
  rocksdb::Slice cur_prefix_key((const char *)m_cur_prefix_key,
                                m_cur_prefix_key_len);

  const char *old_proc_info = m_thd->proc_info();
  thd_proc_info(m_thd, "Materializing group in partial index");

  // It is possible that someone else has already materialized this group
  // before we locked. Double check by doing a locking read on the sentinel.
  rocksdb::PinnableSlice value;
  auto s = rdb_tx_get_for_update(tx, *m_kd, cur_prefix_key, &value,
                                 m_table_type, true, false);
  if (s.ok()) {
    rdb_tx_release_lock(tx, *m_kd, cur_prefix_key, true /* force */);
    thd_proc_info(m_thd, old_proc_info);
    return HA_EXIT_SUCCESS;
  } else if (!s.IsNotFound()) {
    thd_proc_info(m_thd, old_proc_info);
    return rdb_tx_set_status_error(tx, s, *m_kd, m_tbl_def);
  }

  rocksdb::WriteOptions options;
  options.sync = false;
  rocksdb::TransactionDBWriteOptimizations optimize;
  optimize.skip_concurrency_control = true;

  auto wb = std::unique_ptr<rocksdb::WriteBatch>(new rocksdb::WriteBatch);
  // Write sentinel key with empty value.
  s = wb->Put(m_kd->get_cf(), cur_prefix_key, rocksdb::Slice());
  if (!s.ok()) {
    rc = rdb_tx_set_status_error(tx, s, *m_kd, m_tbl_def);
    rdb_tx_release_lock(tx, *m_kd, cur_prefix_key, true /* force */);
    thd_proc_info(m_thd, old_proc_info);
    return rc;
  }

  m_pkd->get_infimum_key(m_cur_prefix_key, &tmp);
  Rdb_iterator_base iter_pk(m_thd, nullptr, m_pkd, m_pkd, m_tbl_def);
  rc = iter_pk.seek(HA_READ_KEY_EXACT, cur_prefix_key, false, cur_prefix_key,
                    true /* read current */);
  size_t num_rows = 0;

  while (!rc) {
    if (thd_killed(m_thd)) {
      rc = HA_ERR_QUERY_INTERRUPTED;
      goto exit;
    }

    const rocksdb::Slice &rkey = iter_pk.key();
    const rocksdb::Slice &rval = iter_pk.value();

    // Unpack from PK format
    rc = m_converter.decode(m_pkd, m_record_buf, &rkey, &rval);
    if (rc) {
      goto exit;
    }

    // Repack into SK format
    uint sk_packed_size = m_kd->pack_record(
        m_table, m_pack_buffer, m_record_buf, m_sk_packed_tuple, &m_sk_tails,
        false /* store_row_debug_checksums */, 0 /* hidden_pk_id */, 0, nullptr,
        m_converter.get_ttl_bytes_buffer());

    s = wb->Put(m_kd->get_cf(),
                rocksdb::Slice((const char *)m_sk_packed_tuple, sk_packed_size),
                rocksdb::Slice((const char *)m_sk_tails.ptr(),
                               m_sk_tails.get_current_pos()));
    if (!s.ok()) {
      rc = rdb_tx_set_status_error(tx, s, *m_kd, m_tbl_def);
      goto exit;
    }

    num_rows++;
    rc = iter_pk.next();
  }

  if (rc != HA_ERR_END_OF_FILE) goto exit;
  rc = HA_EXIT_SUCCESS;

  s = rdb_get_rocksdb_db()->Write(options, optimize, wb.get());
  if (!s.ok()) {
    rc = rdb_tx_set_status_error(tx, s, *m_kd, m_tbl_def);
    goto exit;
  }

  rocksdb_partial_index_groups_materialized++;
  rocksdb_partial_index_rows_materialized += num_rows;

exit:
  m_kd->get_infimum_key(m_cur_prefix_key, &tmp);
  rdb_tx_release_lock(tx, *m_kd, cur_prefix_key, true /* force */);
  thd_proc_info(m_thd, old_proc_info);
  return rc;
}

/*
 * Reads keys from PK in m_cur_prefix_key and populates them into m_records.
 * Will also materialize the prefix group if needed.
 */
int Rdb_iterator_partial::read_prefix_from_pk() {
  uint tmp;
  int rc = 0;
  size_t num_rows = 0;

  m_mem_root.ClearForReuse();
  m_records.clear();

  rocksdb::Slice cur_prefix_key((const char *)m_cur_prefix_key,
                                m_cur_prefix_key_len);
  m_pkd->get_infimum_key(m_cur_prefix_key, &tmp);

  // Since order does not matter (as we will reorder in SK order later), it is
  // better to read in reverse direction for rev cf.
  //
  // However rocksdb does not support reverse prefix seeks, so we always seek in
  // the forwards direction (even if PK is a reverse cf). This ensures that we
  // can make good use of bloom filters.
  //
  assert(m_iterator_pk_position != Iterator_position::END_OF_FILE);
  if (m_iterator_pk_position != Iterator_position::START_CUR_PREFIX) {
    rocksdb::Slice empty_end_key;
    rc = m_iterator_pk.seek(HA_READ_KEY_OR_NEXT, cur_prefix_key, false,
                            empty_end_key);
  }

  while (true) {
    if (thd_killed(m_thd)) {
      rc = HA_ERR_QUERY_INTERRUPTED;
      goto exit;
    }

    if (rc == HA_ERR_END_OF_FILE) {
      m_iterator_pk_position = Iterator_position::END_OF_FILE;
      break;
    } else if (!m_pkd->value_matches_prefix(m_iterator_pk.key(),
                                            cur_prefix_key)) {
      rc = HA_ERR_END_OF_FILE;
      m_iterator_pk_position = Iterator_position::START_NEXT_PREFIX;
      break;
    }

    const rocksdb::Slice &rkey = m_iterator_pk.key();
    const rocksdb::Slice &rval = m_iterator_pk.value();

    // Unpack from PK format
    rc = m_converter.decode(m_pkd, m_record_buf, &rkey, &rval);
    if (rc) goto exit;

    // Repack into SK format
    uint sk_packed_size = m_kd->pack_record(
        m_table, m_pack_buffer, m_record_buf, m_sk_packed_tuple, &m_sk_tails,
        false /* store_row_debug_checksums */, 0 /* hidden_pk_id */, 0, nullptr,
        m_converter.get_ttl_bytes_buffer());

    const char *key = (const char *)memdup_root(&m_mem_root, m_sk_packed_tuple,
                                                sk_packed_size);
    const char *val = (const char *)memdup_root(&m_mem_root, m_sk_tails.ptr(),
                                                m_sk_tails.get_current_pos());

    if (key == nullptr || val == nullptr) {
      rc = HA_ERR_OUT_OF_MEM;
      goto exit;
    }

    m_records.emplace_back(rocksdb::Slice(key, sk_packed_size),
                           rocksdb::Slice(val, m_sk_tails.get_current_pos()));

    num_rows++;
    rc = m_iterator_pk.next();
  }

  if (rc != HA_ERR_END_OF_FILE) goto exit;
  rc = HA_EXIT_SUCCESS;

  std::sort(m_records.begin(), m_records.end(), m_comparator);
  rocksdb_partial_index_groups_sorted++;
  rocksdb_partial_index_rows_sorted += num_rows;

  if (num_rows > m_threshold) {
    rc = materialize_prefix();
  } else if (num_rows == 0) {
    rc = HA_ERR_END_OF_FILE;
  }

exit:
  return rc;
}

int Rdb_iterator_partial::seek(enum ha_rkey_function find_flag,
                               const rocksdb::Slice start_key, bool,
                               const rocksdb::Slice end_key,
                               bool read_current) {
  int rc = 0;
  uint tmp;

  assert(!read_current);
  if (read_current) {
    return HA_ERR_INTERNAL_ERROR;
  }

  reset();
  Rdb_iterator_base::setup_prefix_buffer(find_flag, start_key);

  bool direction = (find_flag == HA_READ_KEY_EXACT) ||
                   (find_flag == HA_READ_AFTER_KEY) ||
                   (find_flag == HA_READ_KEY_OR_NEXT);

  // Get current prefix.
  if ((rc = get_prefix_from_start(find_flag, start_key)) != 0) {
    return rc;
  }

  // First try reading from SK in the current prefix.
  rocksdb::Slice cur_prefix_key((const char *)m_cur_prefix_key,
                                m_cur_prefix_key_len);
  m_kd->get_infimum_key(m_cur_prefix_key, &tmp);

  rocksdb::PinnableSlice value;
  rc = Rdb_iterator_base::get(&cur_prefix_key, &value, RDB_LOCK_NONE,
                              true /* skip ttl check*/);

  bool next_prefix = false;
  if (rc == HA_ERR_KEY_NOT_FOUND) {
    // Nothing in SK, so check PK.
    rc = read_prefix_from_pk();

    if (rc == HA_ERR_END_OF_FILE) {
      // Nothing in PK, so move to next prefix.
      next_prefix = true;
    } else if (rc == 0) {
      // Not materialized on disk.
      m_materialized = false;

      // Seek to correct spot.
      uchar *start_key_buf = (uchar *)start_key.data();

      // Similar to Rdb_iterator_base::seek, convert start_key into an rocksdb
      // key that we will actually seek to.
      auto start_key_guard =
          create_scope_guard([this, start_key_buf, start_key] {
            this->m_kd->predecessor(start_key_buf, start_key.size());
          });
      if (find_flag == HA_READ_PREFIX_LAST_OR_PREV ||
          find_flag == HA_READ_PREFIX_LAST || find_flag == HA_READ_AFTER_KEY) {
        m_kd->successor(start_key_buf, start_key.size());
      } else {
        start_key_guard.commit();
      }

      if (direction) {
        if (m_kd->m_is_reverse_cf) {
          // Emulate "SeekForPrev" behaviour.
          m_records_it = std::upper_bound(m_records.begin(), m_records.end(),
                                          start_key, m_comparator);
          if (m_records_it == m_records.begin()) {
            next_prefix = true;
          } else {
            m_records_it--;
          }
        } else {
          m_records_it = std::lower_bound(m_records.begin(), m_records.end(),
                                          start_key, m_comparator);
          if (m_records_it == m_records.end()) {
            next_prefix = true;
          }
        }
      } else {
        if (m_kd->m_is_reverse_cf) {
          m_records_it = std::upper_bound(m_records.begin(), m_records.end(),
                                          start_key, m_comparator);
          if (m_records_it == m_records.end()) {
            next_prefix = true;
          }
        } else {
          // Emulate "SeekForPrev" behaviour.
          m_records_it = std::lower_bound(m_records.begin(), m_records.end(),
                                          start_key, m_comparator);
          if (m_records_it == m_records.begin()) {
            next_prefix = true;
          } else {
            m_records_it--;
          }
        }
      }
    }
  } else if (rc == 0) {
    assert(value.size() == 0);
    m_materialized = true;

    rc = Rdb_iterator_base::seek(find_flag, start_key, true, end_key,
                                 read_current);
    while (rc == 0 && Rdb_iterator_base::key().size() == m_cur_prefix_key_len) {
      if (thd_killed(m_thd)) {
        return HA_ERR_QUERY_INTERRUPTED;
      }
      rc = direction ? Rdb_iterator_base::next() : Rdb_iterator_base::prev();
    }

    // Group is materialized, but no keys found. Check next prefix.
    if (rc == 0 &&
        !m_kd->value_matches_prefix(Rdb_iterator_base::key(), cur_prefix_key)) {
      next_prefix = true;
    }
  }

  if (next_prefix) {
    rc = seek_next_prefix(direction);
  }

  if (!rc) {
    if (!m_kd->value_matches_prefix(key(), m_prefix_tuple)) {
      rc = HA_ERR_END_OF_FILE;
    } else {
      m_partial_valid = true;
    }
  }

  return rc;
}

int Rdb_iterator_partial::get(const rocksdb::Slice *key,
                              rocksdb::PinnableSlice *value, Rdb_lock_type type,
                              bool skip_ttl_check, bool skip_wait) {
  int rc = Rdb_iterator_base::get(key, value, type, skip_ttl_check, skip_wait);

  if (rc == HA_ERR_KEY_NOT_FOUND) {
    const uint size =
        m_kd->get_primary_key_tuple(*m_pkd, key, m_sk_packed_tuple);
    if (size == RDB_INVALID_KEY_LEN) {
      return HA_ERR_ROCKSDB_CORRUPT_DATA;
    }

    rocksdb::Slice pk_key((const char *)m_sk_packed_tuple, size);

    rc = m_iterator_pk.get(&pk_key, value, type, skip_ttl_check, skip_wait);
    if (rc) return rc;

    // Unpack from PK format
    rc = m_converter.decode(m_pkd, m_record_buf, &pk_key, value);
    if (rc) return rc;

    // Repack into SK format
    uint sk_packed_size = m_kd->pack_record(
        m_table, m_pack_buffer, m_record_buf, m_sk_packed_tuple, &m_sk_tails,
        false /* store_row_debug_checksums */, 0 /* hidden_pk_id */, 0, nullptr,
        m_converter.get_ttl_bytes_buffer());

    value->PinSelf(
        rocksdb::Slice((const char *)m_sk_packed_tuple, sk_packed_size));
    rc = 0;
  }

  m_partial_valid = false;
  return rc;
}

int Rdb_iterator_partial::next_with_direction_in_group(bool direction) {
  uint tmp;
  int rc = HA_EXIT_SUCCESS;
  if (m_materialized) {
    rc = direction ? Rdb_iterator_base::next() : Rdb_iterator_base::prev();
    if (rc == 0 && Rdb_iterator_base::key().size() == m_cur_prefix_key_len) {
      rc = direction ? Rdb_iterator_base::next() : Rdb_iterator_base::prev();
    }

    if (rc == HA_EXIT_SUCCESS) {
      rocksdb::Slice cur_prefix_key((const char *)m_cur_prefix_key,
                                    m_cur_prefix_key_len);
      m_kd->get_infimum_key(m_cur_prefix_key, &tmp);

      if (!m_kd->value_matches_prefix(Rdb_iterator_base::key(),
                                      cur_prefix_key)) {
        return HA_ERR_END_OF_FILE;
      }
    }
  } else {
    if (direction ^ m_kd->m_is_reverse_cf) {
      m_records_it++;
      if (m_records_it == m_records.end()) return HA_ERR_END_OF_FILE;
    } else {
      if (m_records_it == m_records.begin()) return HA_ERR_END_OF_FILE;
      m_records_it--;
    }
  }

  return rc;
}

int Rdb_iterator_partial::next_with_direction(bool direction) {
  if (!m_partial_valid) return HA_ERR_END_OF_FILE;

  int rc = next_with_direction_in_group(direction);

  if (!rc) {
    // On success, check if key is still within prefix.
    if (!m_kd->value_matches_prefix(key(), m_prefix_tuple)) {
      rc = HA_ERR_END_OF_FILE;
    }
  } else if (rc == HA_ERR_END_OF_FILE) {
    uint tmp;
    rocksdb::Slice cur_prefix_key((const char *)m_cur_prefix_key,
                                  m_cur_prefix_key_len);
    m_kd->get_infimum_key(m_cur_prefix_key, &tmp);

    if (m_prefix_tuple.size() >= cur_prefix_key.size()) {
      assert(memcmp(m_prefix_tuple.data(), cur_prefix_key.data(),
                    cur_prefix_key.size()) == 0);
      return HA_ERR_END_OF_FILE;
    }

    rc = seek_next_prefix(direction);
  }

  if (rc) {
    assert(m_partial_valid);
    m_partial_valid = false;
  }
  return rc;
}

int Rdb_iterator_partial::next() {
  int rc = next_with_direction(true);
  return rc;
}

int Rdb_iterator_partial::prev() {
  int rc = next_with_direction(false);
  return rc;
}

void Rdb_iterator_partial::reset() {
  m_partial_valid = false;
  m_materialized = false;
  m_mem_root.ClearForReuse();
  m_iterator_pk_position = Iterator_position::UNKNOWN;
  m_records.clear();
  m_iterator_pk.reset();
  Rdb_iterator_base::reset();
}

rocksdb::Slice Rdb_iterator_partial::key() {
  return m_materialized ? Rdb_iterator_base::key() : m_records_it->first;
}

rocksdb::Slice Rdb_iterator_partial::value() {
  return m_materialized ? Rdb_iterator_base::value() : m_records_it->second;
}

}  // namespace myrocks
