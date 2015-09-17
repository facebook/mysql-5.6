/*
   Copyright (c) 2013 Monty Program Ab

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

#include "rocksdb/db.h"

#include "./my_global.h"                   /* ulonglong */
#include "./my_tree.h"

class RDBSE_KEYDEF;

typedef struct st_row_data
{
  size_t key_len;

  /* Can have a special value: DATA_IS_TOMBSTONE */
  size_t value_len;

  /* RocksDB-SE index this row is from. This allows to get the Column Family */
  RDBSE_KEYDEF *keydef;

  /* Previous version of the row with this key */
  struct st_row_data *prev_version;

  /* Number of the statement that inserted this row/tombstone */
  int stmt_id;

  /*
    This structure is always followed by the key, which is followed by the
    value
  */
} ROW_DATA;

const size_t ROW_DATA_SIZE= ALIGN_SIZE(sizeof(ROW_DATA));

const size_t DATA_IS_TOMBSTONE= size_t(-1);

class Row_table;


/*
  A rocksdb-like iterator for traversing contents of Row_table.

  Changes (insertion/removal of records) to the underlying Row_table will not
  invalidate this iterator (internally, the iterator will detect the change
  and re-position itself).

  The iterator uses ideas from B-TREE index scans on ha_heap tables.
*/

class Row_table_iter
{
  Row_table *rtable; /* Table this iterator is for*/

  /* Whether we are iterating through forward or reverse keyspace */
  bool is_reverse;

  /* The following are for tree iteration: */
  TREE_ELEMENT *parents[MAX_TREE_HEIGHT+1];
  TREE_ELEMENT **last_pos;
  ROW_DATA **row_ptr;

  /*
    If rtable->change_id is greater than ours, the iterator is invalidated and
    we need to re-position in the tree
  */
  int change_id;
  friend class Row_table;
public:
  Row_table_iter(Row_table *rtable_arg, bool is_reverse_arg);

  /* Scanning functions */
  void Seek(const rocksdb::Slice &slice);
  void SeekToFirst();
  void SeekToLast();

  void Next();
  void Prev();

  /* Functions to get information about the current element */
  bool Valid();
  bool is_tombstone();
  rocksdb::Slice key();
  rocksdb::Slice value();
  /*
    RocksDB-SE index this row belongs to (this also allows to get the column
    family)
  */
  RDBSE_KEYDEF *keydef();
};


/*
  A storage for rows, or their tombstones. One can use rocksdb-like iterators
  to traverse the rows.

  == Relationship with Column Families ==
  There is only one Row_table object that stores rows from all Column Families.
  We rely on the fact that no two rows have the same key, even if they are in
  different column families.

  The rows store pointer to their RDBSE_KEYDEF, one can find out which Column
  Family the row is from by calling Row_table_iter::keydef().

  == Forward/backward ordered CFs ==
  We use two trees - one for rows from forward-ordered CFs, and one for rows
  from backward-ordered CFs.

  (we could theoretically use a separate tree for each CF, but TREE stucture is
   too heavy for this.

   I've also considered a solution where both forward an backward-ordered data
   is kept in the same tree. We could compare by index_no first, and then
   compare the remainder in either forward or reverse sorting. This defines an
   ordering sufficient for TREE object to operate, but causes difficult issues
   at table start/end.
  )
*/

class Row_table
{
  /* Tree for column families using forward ordering */
  TREE fw_tree;
  /* Tree for column families using backward ordering */
  TREE bw_tree;

  MEM_ROOT mem_root;

  /* Current statement id */
  int stmt_id;

  /*
    This is incremented on every change, so iterators can know
    if they were invalidated and should re-position themselves.
    (todo: can have a separate change_id for every tree)
  */
  int change_id;

  friend class Row_table_iter;
public:
  /* This is like a constructor */
  void init();

  void cleanup();
  void reinit();

  /* Operations to put a row, or a tombstone */
  bool Put(RDBSE_KEYDEF *keydef, rocksdb::Slice& key,
           rocksdb::Slice& val);
  bool Delete(RDBSE_KEYDEF *keydef, rocksdb::Slice& key);

  /* Lookup may find nothing, find row, of find a tombstone */
  bool Get(RDBSE_KEYDEF *keydef, rocksdb::Slice &key,
           std::string *record, bool *found);

  /*
    Statement support. It is possible to rollback all changes made by the
    current statement.
  */
  void start_stmt();
  void rollback_stmt();

  /* This may return false when there are really no changes (TODO: still true?) */
  bool is_empty()
  {
    return (fw_tree.elements_in_tree == 0 && bw_tree.elements_in_tree == 0);
  };
private:
  static int compare_rows(const void* arg, const void *a,const void *b);
  static int compare_rows_rev(const void* arg, const void *a,const void *b);
};
