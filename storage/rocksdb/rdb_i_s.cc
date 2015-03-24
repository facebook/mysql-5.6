/*
   Copyright (c) 2012, Monty Program Ab

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

#include <mysql/plugin.h>
#include <table.h>
#include <field.h>
#include <sql_show.h>

#include "ha_rocksdb.h"
#include "ha_rocksdb_proto.h"
#include "rdb_cf_manager.h"
#include "rdb_i_s.h"

#define ROCKSDB_FIELD_INFO(_name_, _len_, _type_) \
        { _name_, _len_, _type_, 0, 0, NULL, 0 }

#define ROCKSDB_FIELD_INFO_END ROCKSDB_FIELD_INFO(NULL, 0, MYSQL_TYPE_NULL)

/*
  Support for INFORMATION_SCHEMA.ROCKSDB_CFSTATS dynamic table
 */
static int i_s_rocksdb_cfstats_fill_table(THD *thd,
                                          TABLE_LIST *tables,
                                          Item *cond)
{
  bool ret;
  uint64_t val;

  DBUG_ENTER("i_s_rocksdb_cfstats_fill_table");

  std::vector<std::pair<const std::string, std::string>> cf_properties = {
    {rocksdb::DB::Properties::kNumImmutableMemTable, "NUM_IMMUTABLE_MEM_TABLE"},
    {rocksdb::DB::Properties::kMemTableFlushPending,
        "MEM_TABLE_FLUSH_PENDING"},
    {rocksdb::DB::Properties::kCompactionPending, "COMPACTION_PENDING"},
    {rocksdb::DB::Properties::kCurSizeActiveMemTable,
        "CUR_SIZE_ACTIVE_MEM_TABLE"},
    {rocksdb::DB::Properties::kCurSizeAllMemTables, "CUR_SIZE_ALL_MEM_TABLES"},
    {rocksdb::DB::Properties::kNumEntriesActiveMemTable,
        "NUM_ENTRIES_ACTIVE_MEM_TABLE"},
    {rocksdb::DB::Properties::kNumEntriesImmMemTables,
        "NUM_ENTRIES_IMM_MEM_TABLES"},
    {rocksdb::DB::Properties::kEstimateTableReadersMem,
        "NON_BLOCK_CACHE_SST_MEM_USAGE"},
    {rocksdb::DB::Properties::kNumLiveVersions, "NUM_LIVE_VERSIONS"}
  };

  rocksdb::DB *rdb= rocksdb_get_rdb();
  Column_family_manager cf_manager= rocksdb_get_cf_manager();

  for (auto cf_name : cf_manager.get_cf_names())
  {
    rocksdb::ColumnFamilyHandle* cfh;
    bool is_automatic;

    /*
      Only the cf name is important. Whether it was generated automatically
      does not matter, so is_automatic is ignored.
    */
    cfh= cf_manager.get_cf(cf_name.c_str(), nullptr, nullptr, &is_automatic);
    if (cfh == nullptr)
      continue;

    for (auto property : cf_properties)
    {
      if (!rdb->GetIntProperty(cfh, property.first, &val))
        continue;

      tables->table->field[0]->store(cf_name.c_str(), cf_name.size(),
                                     system_charset_info);
      tables->table->field[1]->store(property.second.c_str(),
                                     property.second.size(),
                                     system_charset_info);
      tables->table->field[2]->store(val, true);

      ret= schema_table_store_record(thd, tables->table);

      if (ret)
        DBUG_RETURN(ret);
    }
  }
  DBUG_RETURN(0);
}

static ST_FIELD_INFO i_s_rocksdb_cfstats_fields_info[]=
{
  ROCKSDB_FIELD_INFO("CF_NAME", NAME_LEN+1, MYSQL_TYPE_STRING),
  ROCKSDB_FIELD_INFO("STAT_TYPE", NAME_LEN+1, MYSQL_TYPE_STRING),
  ROCKSDB_FIELD_INFO("VALUE", sizeof(uint64_t), MYSQL_TYPE_LONG),
  ROCKSDB_FIELD_INFO_END
};

static int i_s_rocksdb_cfstats_init(void *p)
{
  ST_SCHEMA_TABLE *schema;

  DBUG_ENTER("i_s_rocksdb_cfstats_init");

  schema= (ST_SCHEMA_TABLE*) p;

  schema->fields_info= i_s_rocksdb_cfstats_fields_info;
  schema->fill_table= i_s_rocksdb_cfstats_fill_table;

  DBUG_RETURN(0);
}

/*
  Support for INFORMATION_SCHEMA.ROCKSDB_DBSTATS dynamic table
 */
static int i_s_rocksdb_dbstats_fill_table(THD *thd,
                                          TABLE_LIST *tables,
                                          Item *cond)
{
  bool ret;
  uint64_t val;

  DBUG_ENTER("i_s_rocksdb_dbstats_fill_table");

  std::vector<std::pair<std::string, std::string>> db_properties = {
    {rocksdb::DB::Properties::kBackgroundErrors, "DB_BACKGROUND_ERRORS"},
    {rocksdb::DB::Properties::kNumSnapshots, "DB_NUM_SNAPSHOTS"},
    {rocksdb::DB::Properties::kOldestSnapshotTime, "DB_OLDEST_SNAPSHOT_TIME"}
  };

  rocksdb::DB *rdb= rocksdb_get_rdb();
  rocksdb::BlockBasedTableOptions table_options= rocksdb_get_table_options();

  for (auto property : db_properties)
  {
    if (!rdb->GetIntProperty(property.first, &val))
      continue;

    tables->table->field[0]->store(property.second.c_str(),
                                   property.second.size(),
                                   system_charset_info);
    tables->table->field[1]->store(val, true);

    ret= schema_table_store_record(thd, tables->table);

    if (ret)
      DBUG_RETURN(ret);
  }

  /*
    Currently, this can only show the usage of a block cache allocated
    directly by the handlerton. If the column family config specifies a block
    cache (i.e. the column family option has a parameter such as
    block_based_table_factory={block_cache=1G}), then the block cache is
    allocated within the rocksdb::GetColumnFamilyOptionsFromString().

    There is no interface to retrieve this block cache, nor fetch the usage
    information from the column family.
   */
  val= (table_options.block_cache ? table_options.block_cache->GetUsage() : 0);
  tables->table->field[0]->store(STRING_WITH_LEN("DB_BLOCK_CACHE_USAGE"),
                                 system_charset_info);
  tables->table->field[1]->store(val, true);

  ret= schema_table_store_record(thd, tables->table);

  DBUG_RETURN(ret);
}

static ST_FIELD_INFO i_s_rocksdb_dbstats_fields_info[]=
{
  ROCKSDB_FIELD_INFO("STAT_TYPE", NAME_LEN+1, MYSQL_TYPE_STRING),
  ROCKSDB_FIELD_INFO("VALUE", sizeof(uint64_t), MYSQL_TYPE_LONG),
  ROCKSDB_FIELD_INFO_END
};

static int i_s_rocksdb_dbstats_init(void *p)
{
  ST_SCHEMA_TABLE *schema;

  DBUG_ENTER("i_s_rocksdb_dbstats_init");

  schema= (ST_SCHEMA_TABLE*) p;

  schema->fields_info= i_s_rocksdb_dbstats_fields_info;
  schema->fill_table= i_s_rocksdb_dbstats_fill_table;

  DBUG_RETURN(0);
}

static int i_s_rocksdb_deinit(void *p)
{
  DBUG_ENTER("i_s_rocksdb_deinit");
  DBUG_RETURN(0);
}

static struct st_mysql_information_schema i_s_rocksdb_info=
{ MYSQL_INFORMATION_SCHEMA_INTERFACE_VERSION };

struct st_mysql_plugin i_s_rocksdb_cfstats=
{
  MYSQL_INFORMATION_SCHEMA_PLUGIN,
  &i_s_rocksdb_info,
  "ROCKSDB_CFSTATS",
  "Monty Program Ab",
  "RocksDB column family stats",
  PLUGIN_LICENSE_GPL,
  i_s_rocksdb_cfstats_init,
  i_s_rocksdb_deinit,
  0x0001,                             /* version number (0.1) */
  NULL,                               /* status variables */
  NULL,                               /* system variables */
  NULL,                               /* config options */
  0,                                  /* flags */
};

struct st_mysql_plugin i_s_rocksdb_dbstats=
{
  MYSQL_INFORMATION_SCHEMA_PLUGIN,
  &i_s_rocksdb_info,
  "ROCKSDB_DBSTATS",
  "Monty Program Ab",
  "RocksDB database stats",
  PLUGIN_LICENSE_GPL,
  i_s_rocksdb_dbstats_init,
  i_s_rocksdb_deinit,
  0x0001,                             /* version number (0.1) */
  NULL,                               /* status variables */
  NULL,                               /* system variables */
  NULL,                               /* config options */
  0,                                  /* flags */
};
