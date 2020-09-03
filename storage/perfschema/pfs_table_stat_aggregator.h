/* Copyright (c) 2010, 2020, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License, version 2.0,
  as published by the Free Software Foundation.

  This program is also distributed with certain software (including
  but not limited to OpenSSL) that is licensed under separate terms,
  as designated in a particular file or component or in included license
  documentation.  The authors of MySQL hereby grant you an additional
  permission to link the program and your derivative works with the
  separately licensed software that they have included with MySQL.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License, version 2.0, for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

#ifndef PFS_TABLE_AGGREGATOR_H
#define PFS_TABLE_AGGREGATOR_H

#include <sys/types.h>

#include "my_compiler.h"
#include "mysqld_error.h"

#include "storage/perfschema/pfs_buffer_container.h"
#include "storage/perfschema/pfs_engine_table.h"
#include "storage/perfschema/pfs_instr.h"
#include "storage/perfschema/pfs_instr_class.h"
#include "storage/perfschema/pfs_stat.h"
#include "storage/perfschema/pfs_visitor.h"

/**
  Provides std::hash function for std::pair
 */
struct pair_hash {
  template <class T1, class T2>
  std::size_t operator()(const std::pair<T1, T2> &pair) const {
    return std::hash<T1>()(pair.first) ^ std::hash<T2>()(pair.second);
  }
};

/**
  Traits class for abstracting different table stat types - they will be
  used later for building hash tables with Table_stat_aggregator
  This class defines types for PFS_table_query_stat processing per table
 */
class table_query_stat_traits {
 public:
  typedef PFS_table_share *stat_index_type;
  typedef PFS_table_query_stat stat_type;
  typedef PFS_table_query_stat_visitor visitor_type;

  /* stat_index_type -> stat_type hash table */
  typedef std::unordered_map<stat_index_type, stat_type> stat_container_type;
};

/**
  Traits class for abstracting different table stat types - they will be
  used later for building hash tables with Table_stat_aggregator
  This class defines types for PFS_table_io_stat processing per table
 */
class table_io_stat_traits {
 public:
  typedef PFS_table_share *stat_index_type;
  typedef PFS_table_io_stat stat_type;
  typedef PFS_table_io_stat_visitor visitor_type;

  /* stat_index_type -> stat_type hash table */
  typedef std::unordered_map<stat_index_type, stat_type> stat_container_type;
};

/**
  Traits class for abstracting different table stat types - they will be
  used later for building hash tables with Table_stat_aggregator
  This class defines types for PFS_table_io_stat processing per index
 */
class table_index_stat_traits {
 public:
  typedef std::pair<PFS_table_share *, uint> stat_index_type;
  typedef PFS_table_io_stat stat_type;
  typedef PFS_index_io_stat_visitor visitor_type;

  /* stat_index_type -> stat_type hash table */
  typedef std::unordered_map<stat_index_type, stat_type, pair_hash>
      stat_container_type;
};

/**
  Generator class for collecting different table stat types - they will be
  used later for building hash tables with Table_stat_aggregator
  This class collects stats on each table / table_share to
  (PFS_table_share *) -> stat_type
 */
template <typename table_stat_traits>
class table_stat_generator {
 public:
  using visitor_type = typename table_stat_traits::visitor_type;
  using stat_container_type = typename table_stat_traits::stat_container_type;

  table_stat_generator(PFS_table_share *share) : m_share(share) {}

  /* Visit table share to collect stats on visitor */
  void visit_table_share() { m_visitor.visit_table_share(m_share); }

  /* Visit table to collect stats on visitor */
  void visit_table(PFS_table *table) { m_visitor.visit_table(table); }

  /* Aggregate visitor stats into container / hash table */
  void aggregate(stat_container_type *container) {
    (*container)[m_share].aggregate(&m_visitor.m_stat);
  }

 private:
  visitor_type m_visitor;
  PFS_table_share *m_share;
};

/**
  Generator class for collecting different table index stat types - they will be
  used later for building hash tables with Table_stat_aggregator
  The difference with table_stat_generator is that it'll aggregate per index to
  (PFS_table_share *, index) -> stat_type
 */
template <typename table_stat_traits>
class table_index_stat_generator {
 public:
  using visitor_type = typename table_stat_traits::visitor_type;
  using stat_type = typename table_stat_traits::stat_type;
  using stat_container_type = typename table_stat_traits::stat_container_type;

  table_index_stat_generator(PFS_table_share *share) : m_share(share) {
    m_safe_key_count = sanitize_index_count(share->m_key_count);
  }

  /* Visit table share to collect index stats on visitor */
  void visit_table_share() {
    for (uint i = 0; i < m_safe_key_count; ++i) {
      populate_stats_for_table_share_index(i);
    }
    populate_stats_for_table_share_index(MAX_INDEXES);
  }

  /* Visit table to collect index stats on visitor */
  void visit_table(PFS_table *table) {
    for (uint i = 0; i < m_safe_key_count; ++i) {
      populate_stats_for_table_index(table, i);
    }
    populate_stats_for_table_index(table, MAX_INDEXES);
  }

  /* Aggregate visitor stats into container / hash table per index */
  void aggregate(stat_container_type *container) {
    for (uint i = 0; i < m_safe_key_count; ++i) {
      (*container)[std::make_pair(m_share, i)].aggregate(&m_stats[i]);
    }
    (*container)[std::make_pair(m_share, MAX_INDEXES)].aggregate(
        &m_stats[MAX_INDEXES]);
  }

 private:
  void populate_stats_for_table_share_index(uint index) {
    visitor_type visitor;
    visitor.visit_table_share_index(m_share, index);

    m_stats[index].aggregate(&visitor.m_stat);
  }

  void populate_stats_for_table_index(PFS_table *table, uint index) {
    visitor_type visitor;
    visitor.visit_table_index(table, index);

    m_stats[index].aggregate(&visitor.m_stat);
  }

  PFS_table_share *m_share;
  std::array<stat_type, MAX_INDEXES + 1> m_stats;
  uint m_safe_key_count;
};

/**
 Class to help building a hash table of PFS_table_share -> stat using
 global list of table / table share, making sure we only walk through
 these lists once exactly once
 @param table_stat_traits defines the various types for collecting the specific
                          table stat
 @param generator_type    generator_type used for collecting stats on the hash
                          table, and it also takes table_stat_traits as
                          template argument as well for the types
 */
template <typename table_stat_traits, template <typename> class generator_type>
class table_stat_aggregator_impl {
 public:
  table_stat_aggregator_impl() : m_stats_inited(false) {}

  using stat_type = typename table_stat_traits::stat_type;
  using stat_index_type = typename table_stat_traits::stat_index_type;
  using stat_generator_type = generator_type<table_stat_traits>;
  using stat_container_type = typename table_stat_traits::stat_container_type;

  const stat_type *get_stat(stat_index_type stat_index) {
    assert(m_stats_inited);
    auto it = m_aggregate_stats.find(stat_index);
    if (it == m_aggregate_stats.end()) {
      return NULL;
    }

    return &it->second;
  }

  void build_stats_if_needed() {
    if (!m_stats_inited) {
      evaluate_aggregate_stats();
      m_stats_inited = true;
    }
  }

 private:
  /**
   Populate aggregate stats for all the table shares. This is evaluated
   when making the first row of the scan. Evaluating aggregate stats
   ensures that the full table and index scan of the table becomes
   extremely efficient. This ensures that no iteration happens through all the
   table handles in case of every call to rnd_next, index_next.
   */
  void evaluate_aggregate_stats(void) {
    PFS_table_share *pfs;
    uint index = 0;
    PFS_table_share_iterator pfs_it =
        global_table_share_container.iterate(index);
    do {
      pfs = pfs_it.scan_next(&index);
      if (pfs != NULL) {
        populate_stats_for_share(pfs);
      }
    } while (pfs != NULL);

    index = 0;
    PFS_table *table;
    PFS_table_iterator table_it = global_table_container.iterate(index);
    do {
      table = table_it.scan_next(&index);
      if (table != NULL) {
        populate_stats_for_table(table);
      }
    } while (table != NULL);
  }

  /**
    Populate aggregate stats container for table share.
   */
  void populate_stats_for_share(PFS_table_share *pfs) {
    pfs_optimistic_state lock;
    pfs->m_lock.begin_optimistic_lock(&lock);

    stat_generator_type generator(pfs);
    generator.visit_table_share();

    /*
      Check if table share has mutated since we took the optimistic
      lock. In case it was mutated, it is not safe to use stats for this
      share. Return in that case.
    */
    if (!pfs->m_lock.end_optimistic_lock(&lock)) {
      return;
    }

    generator.aggregate(&m_aggregate_stats);
  }

  /**
    Populate aggregate stats container for table handle.
   */
  void populate_stats_for_table(PFS_table *table) {
    pfs_optimistic_state lock;
    table->m_lock.begin_optimistic_lock(&lock);

    PFS_table_share *share = sanitize_table_share(table->m_share);
    if (share == NULL) {
      return;
    }

    stat_generator_type generator(share);
    generator.visit_table(table);

    /*
      Check if table handle has mutated since we took the optimistic
      lock. In case it was mutated, it is not safe to use stats for this
      table. Return in that case.
    */
    if (!table->m_lock.end_optimistic_lock(&lock)) {
      return;
    }

    generator.aggregate(&m_aggregate_stats);
  }

 private:
  /* Container holding aggregated stats during iteration */
  stat_container_type m_aggregate_stats;
  bool m_stats_inited;
};

typedef table_stat_aggregator_impl<table_query_stat_traits,
                                   table_stat_generator>
    table_query_stat_aggregator;
typedef table_stat_aggregator_impl<table_io_stat_traits, table_stat_generator>
    table_io_stat_aggregator;
typedef table_stat_aggregator_impl<table_index_stat_traits,
                                   table_index_stat_generator>
    table_index_stat_aggregator;

#endif
