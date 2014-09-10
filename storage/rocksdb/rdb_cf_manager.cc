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

#ifdef USE_PRAGMA_IMPLEMENTATION
#pragma implementation        // gcc: Class implementation
#endif

#include <mysql/plugin.h>
#include "sql_class.h"
#include "ha_rocksdb.h"

#include "rdb_cf_manager.h"


void Column_family_manager::init(std::vector<std::string> *names,
                                 std::vector<rocksdb::ColumnFamilyHandle*> *handles)
{
  mysql_mutex_init(NULL, &cfm_mutex, MY_MUTEX_INIT_FAST);
  DBUG_ASSERT(names->size() == handles->size());
  DBUG_ASSERT(names->size() > 0);

  default_cf= (*handles)[0];
  for (size_t i = 0; i < names->size(); ++i)
    cf_map[(*names)[i]]= (*handles)[i];
}


void Column_family_manager::cleanup()
{
  ColumnFamilyHandleMap::iterator it;
  for (it= cf_map.begin(); it!=cf_map.end(); it++)
  {
    delete it->second;
  }
  mysql_mutex_destroy(&cfm_mutex);
}


rocksdb::ColumnFamilyHandle*
Column_family_manager::get_or_create_cf(rocksdb::DB *rdb, const char *name)
{
  rocksdb::ColumnFamilyHandle* cf_handle;
  ColumnFamilyHandleMap::iterator it;

  mysql_mutex_lock(&cfm_mutex);
  if (name == NULL)
  {
    cf_handle= default_cf;
  }
  else if ((it= cf_map.find(name)) != cf_map.end())
    cf_handle= it->second;
  else
  {
    /* Create a Column Family. */
    std::string cf_name(name);
    rocksdb::ColumnFamilyOptions opts;
    get_cf_options(cf_name, &opts);

    sql_print_information("RocksDB: creating column family %s", cf_name.c_str());
    sql_print_information("    write_buffer_size=%ld",    opts.write_buffer_size);
    sql_print_information("    target_file_size_base=%d", opts.target_file_size_base);

    rocksdb::Status s= rdb->CreateColumnFamily(opts, name, &cf_handle);
    if (s.ok())
      cf_map[cf_name]= cf_handle;
    else
      cf_handle= NULL;
  }
  mysql_mutex_unlock(&cfm_mutex);

  return cf_handle;
}


rocksdb::ColumnFamilyHandle*
Column_family_manager::get_cf(const char *name)
{
  rocksdb::ColumnFamilyHandle* cf_handle;
  ColumnFamilyHandleMap::iterator it;

  mysql_mutex_lock(&cfm_mutex);
  if (name == NULL)
    cf_handle= default_cf;
  else if ((it= cf_map.find(name)) != cf_map.end())
    cf_handle= it->second;
  else
    cf_handle= NULL;
  mysql_mutex_unlock(&cfm_mutex);

  return cf_handle;
}
