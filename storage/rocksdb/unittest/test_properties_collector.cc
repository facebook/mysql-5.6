/*
   Copyright (c) 2015, Facebook, Inc.

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

/* MyRocks header files */
#include "../ha_rocksdb.h"
#include "../rdb_datadic.h"

void putKeys(MyRocksTablePropertiesCollector& coll, int num, bool is_delete,
             uint64_t expected_deleted) {
  std::string str("aaaaaaaaaaaaaa");
  rocksdb::Slice sl(str.data(), str.size());

  for (int i=0; i < num; i++) {
    coll.AddUserKey(
      sl, sl,
      is_delete?rocksdb::kEntryDelete:rocksdb::kEntryPut, 0, 100);
  }
  assert(coll.GetMaxDeletedRows() ==  expected_deleted);
}

int main(int argc, char** argv) {
  // test the circular buffer for delete flags
  CompactionParams params;
  params.file_size_= 333;
  params.deletes_= 333; //irrelevant
  params.window_= 10;

  MyRocksTablePropertiesCollector coll(NULL, params);
  putKeys(coll, 2, true, 2); // [xx]
  putKeys(coll, 3, false, 2); // [xxo]
  putKeys(coll, 1, true, 3); // [xxox]
  putKeys(coll, 6, false, 3); // [xxoxoooooo]
  putKeys(coll, 3, true, 4); // xxo[xooooooxxx]
  putKeys(coll, 1, false, 4); // xxox[ooooooxxxo]
  putKeys(coll, 100, false, 4); // ....[oooooooooo]
  putKeys(coll, 100, true, 10); // ....[xxxxxxxxxx]
  putKeys(coll, 100, true, 10); // ....[oooooooooo]

  return 0;
}
