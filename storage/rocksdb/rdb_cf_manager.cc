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

#include "./rdb_cf_manager.h"

#include "./ha_rocksdb.h"

/* Check if ColumnFamily name says it's a reverse-ordered CF */
bool is_cf_name_reverse(const char *name)
{
  /* NULL means the default CF is used.. (TODO: can the default CF be reverse?) */
  if (name && !strncmp(name, "rev:", 4))
    return true;
  else
    return false;
}

#ifdef HAVE_PSI_INTERFACE
static PSI_mutex_key ex_key_cfm;
#endif

void Column_family_manager::init(std::vector<rocksdb::ColumnFamilyHandle*> *handles)
{
  mysql_mutex_init(ex_key_cfm, &cfm_mutex, MY_MUTEX_INIT_FAST);
  DBUG_ASSERT(handles->size() > 0);

  for (auto cfh : *handles) {
    cf_name_map[cfh->GetName()] = cfh;
    cf_id_map[cfh->GetID()] = cfh;
  }
}


void Column_family_manager::cleanup()
{
  for (auto it : cf_name_map) {
    delete it.second;
  }
  mysql_mutex_destroy(&cfm_mutex);
}


/*
  Generate Column Family name for per-index column families

  @param res  OUT  Column Family name
*/

void get_per_index_cf_name(const char *db_table_name, const char *index_name,
                           std::string *res)
{
  res->assign(db_table_name);
  res->append(".");
  res->append(index_name);
}


/*
  @brief
  Find column family by name. If it doesn't exist, create it

  @detail
    See Column_family_manager::get_cf
*/
rocksdb::ColumnFamilyHandle*
Column_family_manager::get_or_create_cf(rocksdb::DB *rdb, const char *cf_name,
                                        const char *db_table_name,
                                        const char *index_name,
                                        bool *is_automatic)
{
  rocksdb::ColumnFamilyHandle* cf_handle;

  mysql_mutex_lock(&cfm_mutex);
  *is_automatic= false;
  if (cf_name == NULL)
    cf_name= DEFAULT_CF_NAME;

  std::string per_index_name;
  if (!strcmp(cf_name, PER_INDEX_CF_NAME))
  {
    get_per_index_cf_name(db_table_name, index_name, &per_index_name);
    cf_name= per_index_name.c_str();
    *is_automatic= true;
  }

  auto it = cf_name_map.find(cf_name);
  if (it != cf_name_map.end())
    cf_handle= it->second;
  else
  {
    /* Create a Column Family. */
    std::string cf_name_str(cf_name);
    rocksdb::ColumnFamilyOptions opts;
    get_cf_options(cf_name_str, &opts);

    sql_print_information("RocksDB: creating column family %s", cf_name_str.c_str());
    sql_print_information("    write_buffer_size=%ld",    opts.write_buffer_size);
    sql_print_information("    target_file_size_base=%" PRIu64,
                          opts.target_file_size_base);

    rocksdb::Status s= rdb->CreateColumnFamily(opts, cf_name_str, &cf_handle);
    if (s.ok()) {
      cf_name_map[cf_handle->GetName()] = cf_handle;
      cf_id_map[cf_handle->GetID()] = cf_handle;
    } else {
      cf_handle= NULL;
    }
  }
  mysql_mutex_unlock(&cfm_mutex);

  return cf_handle;
}


/*
  Find column family by its cf_name.

  @detail
  dbname.tablename  and index_name are also parameters, because
  cf_name=PER_INDEX_CF_NAME means that column family name is a function
  of table/index name.

  @param out is_automatic  TRUE<=> column family name is auto-assigned based on
                           db_table_name and index_name.
*/

rocksdb::ColumnFamilyHandle*
Column_family_manager::get_cf(const char *cf_name,
                              const char *db_table_name,
                              const char *index_name,
                              bool *is_automatic)
{
  rocksdb::ColumnFamilyHandle* cf_handle;

  *is_automatic= false;
  mysql_mutex_lock(&cfm_mutex);
  if (cf_name == NULL)
    cf_name= DEFAULT_CF_NAME;

  std::string per_index_name;
  if (!strcmp(cf_name, PER_INDEX_CF_NAME))
  {
    get_per_index_cf_name(db_table_name, index_name, &per_index_name);
    cf_name= per_index_name.c_str();
    *is_automatic= true;
  }

  auto it = cf_name_map.find(cf_name);
  cf_handle = (it != cf_name_map.end()) ? it->second : nullptr;

  mysql_mutex_unlock(&cfm_mutex);

  return cf_handle;
}

rocksdb::ColumnFamilyHandle* Column_family_manager::get_cf(uint32_t id)
{
  rocksdb::ColumnFamilyHandle* cf_handle = nullptr;

  mysql_mutex_lock(&cfm_mutex);
  auto it = cf_id_map.find(id);
  if (it != cf_id_map.end())
    cf_handle = it->second;
  mysql_mutex_unlock(&cfm_mutex);

  return cf_handle;
}

std::vector<std::string>
Column_family_manager::get_cf_names(void)
{
  std::vector<std::string> names;

  mysql_mutex_lock(&cfm_mutex);
  for (auto it : cf_name_map) {
    names.push_back(it.first);
  }
  mysql_mutex_unlock(&cfm_mutex);
  return names;
}

std::vector<rocksdb::ColumnFamilyHandle*>
Column_family_manager::get_all_cf(void)
{
  std::vector<rocksdb::ColumnFamilyHandle*> list;

  mysql_mutex_lock(&cfm_mutex);
  for (auto it : cf_id_map) {
    list.push_back(it.second);
  }
  mysql_mutex_unlock(&cfm_mutex);

  return list;
}
