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

#include "scope_guard.h"

namespace myrocks {

Rdb_iterator::~Rdb_iterator() {}

Rdb_iterator_base::Rdb_iterator_base(THD *thd,
                                     const std::shared_ptr<Rdb_key_def> kd,
                                     const std::shared_ptr<Rdb_key_def> pkd,
                                     const Rdb_tbl_def *tbl_def)
    : m_kd(kd),
      m_pkd(pkd),
      m_tbl_def(tbl_def),
      m_thd(thd),
      m_scan_it(nullptr),
      m_scan_it_skips_bloom(false),
      m_scan_it_snapshot(nullptr),
      m_scan_it_lower_bound(nullptr),
      m_scan_it_upper_bound(nullptr),
      m_prefix_buf(nullptr) {}

Rdb_iterator_base::~Rdb_iterator_base() {
  release_scan_iterator();
  my_free(m_scan_it_lower_bound);
  m_scan_it_lower_bound = nullptr;
  my_free(m_scan_it_upper_bound);
  m_scan_it_upper_bound = nullptr;
  my_free(m_prefix_buf);
  m_prefix_buf = nullptr;
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
          &m_scan_it_lower_bound_slice, &m_scan_it_upper_bound_slice)) {
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
        m_scan_it_upper_bound_slice, &m_scan_it_snapshot, read_current,
        !read_current);
    m_scan_it_skips_bloom = skip_bloom;
  }
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

    // Record is not visible due to TTL, move to next record.
    if (m_pkd->has_ttl() && rdb_should_hide_ttl_rec(kd, value, tx)) {
      continue;
    }

    break;
  }

  return rc;
}

int Rdb_iterator_base::seek(enum ha_rkey_function find_flag,
                            const rocksdb::Slice start_key, bool full_key_match,
                            const rocksdb::Slice end_key, bool read_current) {
  int rc = 0;

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

  int bytes_changed_by_succ = 0;
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

  if (rc) {
    return rc;
  }

  rc = next_with_direction(direction, true);
  return rc;
}

int Rdb_iterator_base::get(const rocksdb::Slice *key,
                           rocksdb::PinnableSlice *value, Rdb_lock_type type,
                           bool skip_ttl_check, bool skip_wait) {
  int rc = HA_EXIT_SUCCESS;
  Rdb_transaction *const tx = get_tx_from_thd(m_thd);
  rocksdb::Status s;
  if (type == RDB_LOCK_NONE) {
    s = rdb_tx_get(tx, m_kd->get_cf(), *key, value);
  } else {
    s = rdb_tx_get_for_update(tx, *m_kd, *key, value, type == RDB_LOCK_WRITE,
                              skip_wait);
  }

  DBUG_EXECUTE_IF("rocksdb_return_status_corrupted",
                  { s = rocksdb::Status::Corruption(); });

  if (!s.IsNotFound() && !s.ok()) {
    return rdb_tx_set_status_error(tx, s, *m_kd, m_tbl_def);
  }

  if (s.IsNotFound()) {
    return HA_ERR_KEY_NOT_FOUND;
  }

  if (!skip_ttl_check && m_kd->has_ttl() &&
      rdb_should_hide_ttl_rec(*m_kd, *value, tx)) {
    return HA_ERR_KEY_NOT_FOUND;
  }

  return rc;
}

}  // namespace myrocks
