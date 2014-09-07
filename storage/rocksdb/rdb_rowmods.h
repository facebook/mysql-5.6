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

#include "my_tree.h"


typedef struct st_row_data
{
  size_t key_len;

  /* Can have a special value: DATA_IS_TOMBSTONE */
  size_t value_len;

  rocksdb::ColumnFamilyHandle *cf;

  /* Previous version */
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
  Row_table_iter(Row_table *rtable_arg);

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

  rocksdb::ColumnFamilyHandle *cf_handle();
};


/*
  A storage for rows, or their tombstones. One can use rocksdb-like iterators
  to traverse the rows.
*/

class Row_table
{
  TREE tree;
  MEM_ROOT mem_root;

  /* Current statement id */
  int stmt_id;

  /*
    This is incremented on every change, so iterators can know
    if they were invalidated and should re-position themselves.
  */
  int change_id;

  friend class Row_table_iter;
public:
  /* This is like a constructor */
  void init();

  void cleanup();
  void reinit();

  /* Operations to put a row, or a tombstone */
  bool Put(rocksdb::ColumnFamilyHandle *cf, rocksdb::Slice& key,
           rocksdb::Slice& val);
  bool Delete(rocksdb::ColumnFamilyHandle *cf, rocksdb::Slice& key);

  /* Lookup may find nothing, find row, of find a tombstone */
  bool Get(rocksdb::Slice &key, std::string *record, bool *found);

  /*
    Statement support. It is possible to rollback all changes made by the
    current statement.
  */
  void start_stmt();
  void rollback_stmt();

  /* This may return false when there are really no changes (TODO: still true?) */
  bool is_empty()
  {
    return (tree.elements_in_tree == 0);
  };
private:
  static int compare_rows(const void* arg, const void *a,const void *b);
};
