/*
   Copyright (c) 2014, SkySQL Ab

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

#include <map>


/*
  Expected from outside: a function that fills CF options for a given name.
*/
void get_cf_options(const std::string &cf_name, rocksdb::ColumnFamilyOptions *opts);

void get_per_index_cf_name(const char *db_table_name, const char *index_name,
                           std::string *res);

/*
  We need a column family manager. Its functions:
  - create column families (synchronized, don't create the same twice)
  - keep count in each column family.
     = the count is kept on-disk.
     = there are no empty CFs. initially count=1.
     = then, when doing DDL, we increase or decrease it.
       (atomicity is maintained by being in the same WriteBatch with DDLs)
     = if DROP discovers that now count=0, it removes the CF.

  Current state is:
  - CFs are created in a synchronized way. We can't remove them, yet.
*/

class Column_family_manager
{
  typedef std::map<std::string, rocksdb::ColumnFamilyHandle*> ColumnFamilyHandleMap;

  ColumnFamilyHandleMap cf_map;

  rocksdb::ColumnFamilyHandle *default_cf;

  mysql_mutex_t cfm_mutex;
public:
  /*
    This is called right after the DB::Open() call. The parameters describe column
    families that are present in the database. The first CF is the default CF.
  */
  void init(std::vector<std::string> *names,
            std::vector<rocksdb::ColumnFamilyHandle*> *handles);
  void cleanup();

  /*
    Used by CREATE TABLE.
    - cf_name=NULL means use default column family
    - cf_name=_auto_ means use 'dbname.tablename.indexname'
  */
  rocksdb::ColumnFamilyHandle* get_or_create_cf(rocksdb::DB *rdb,
                                                const char *cf_name,
                                                const char *db_table_name,
                                                const char *index_name,
                                                bool *is_automatic);

  /* Used by table open */
  rocksdb::ColumnFamilyHandle* get_cf(const char *cf_name,
                                      const char *db_table_name,
                                      const char *index_name,
                                      bool *is_automatic);

  // void drop_cf(); -- not implemented so far.
};
