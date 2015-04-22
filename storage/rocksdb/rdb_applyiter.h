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

class Row_table;
class Row_table_iter;

class RDBSE_KEYDEF;

/*
  A class that looks like RocksDB's iterator, but internally it takes into
  account the changes made by the transaction.

  In other words, the iterator gives a view of the data insidde rocksdb, but
  also applies the changes made by the transaction.
*/

class Apply_changes_iter
{
  bool valid;
  bool cur_is_trx;

  /* These are the iterators we're merging. We own them, so should free them */
  Row_table_iter *trx;
  rocksdb::Iterator* rdb;

  /* If true, we're scanning reverse-ordered data */
  bool is_reverse;
public:
  Apply_changes_iter();
  ~Apply_changes_iter();
  void init(bool is_reverse_arg, Row_table *trx_arg,
            rocksdb::Iterator *rdb_arg);

  void Next();
  void Prev();

  void Seek(rocksdb::Slice &key);
  void SeekToLast();

  bool Valid() { return valid; }
  rocksdb::Slice key();
  rocksdb::Slice value();
private:
  void advance(int direction);

  /* Stored the direction requested of the latest advance call */
  int latest_direction;
  void adjust_keys(int direction);
};

