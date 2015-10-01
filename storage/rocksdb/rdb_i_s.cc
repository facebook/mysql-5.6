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

/* MySQL header files */
#include <sql_show.h>

/* RocksDB header files */
#include "rocksdb/compaction_filter.h"
#include "rocksdb/filter_policy.h"
#include "rocksdb/memtablerep.h"
#include "rocksdb/merge_operator.h"
#include "rocksdb/slice_transform.h"

/* MyRocks header files */
#include "./ha_rocksdb.h"
#include "./ha_rocksdb_proto.h"
#include "./rdb_cf_manager.h"
#include "./rdb_datadic.h"

#define ROCKSDB_FIELD_INFO(_name_, _len_, _type_, _flag_) \
        { _name_, _len_, _type_, 0, _flag_, NULL, 0 }

#define ROCKSDB_FIELD_INFO_END ROCKSDB_FIELD_INFO(NULL, 0, MYSQL_TYPE_NULL, 0)

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
  ROCKSDB_FIELD_INFO("CF_NAME", NAME_LEN+1, MYSQL_TYPE_STRING, 0),
  ROCKSDB_FIELD_INFO("STAT_TYPE", NAME_LEN+1, MYSQL_TYPE_STRING, 0),
  ROCKSDB_FIELD_INFO("VALUE", sizeof(uint64_t), MYSQL_TYPE_LONGLONG, 0),
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
  ROCKSDB_FIELD_INFO("STAT_TYPE", NAME_LEN+1, MYSQL_TYPE_STRING, 0),
  ROCKSDB_FIELD_INFO("VALUE", sizeof(uint64_t), MYSQL_TYPE_LONGLONG, 0),
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

/*
  Support for INFORMATION_SCHEMA.ROCKSDB_PERF_CONTEXT dynamic table
 */

static int i_s_rocksdb_perf_context_fill_table(THD *thd,
                                               TABLE_LIST *tables,
                                               Item *cond)
{
  int ret= 0;
  Field** field = tables->table->field;

  DBUG_ENTER("i_s_rocksdb_perf_context_fill_table");

  std::vector<std::string> tablenames= get_share_names();
  for (auto it : tablenames)
  {
    StringBuffer<256> buf, dbname, tablename, partname;
    SHARE_PERF_COUNTERS counters;

    rocksdb_normalize_tablename(it.c_str(), &buf);
    if (rocksdb_split_normalized_tablename(buf.c_ptr(), &dbname, &tablename,
                                           &partname))
      continue;

    if (rocksdb_get_share_perf_counters(it.c_str(), &counters))
      continue;

    field[0]->store(dbname.c_ptr(), dbname.length(), system_charset_info);
    field[1]->store(tablename.c_ptr(), tablename.length(), system_charset_info);
    if (partname.length() == 0)
      field[2]->set_null();
    else
    {
      field[2]->set_notnull();
      field[2]->store(partname.c_ptr(), partname.length(),
                                     system_charset_info);
    }

    for (int i= 0; i < PC_MAX_IDX; i++) {
      field[3]->store(pc_stat_types[i].c_str(), pc_stat_types[i].size(),
                      system_charset_info);
      field[4]->store(counters.value[i], true);

      ret= schema_table_store_record(thd, tables->table);
      if (ret)
        DBUG_RETURN(ret);
    }
  }

  DBUG_RETURN(0);
}

static ST_FIELD_INFO i_s_rocksdb_perf_context_fields_info[]=
{
  ROCKSDB_FIELD_INFO("TABLE_SCHEMA", NAME_LEN+1, MYSQL_TYPE_STRING, 0),
  ROCKSDB_FIELD_INFO("TABLE_NAME", NAME_LEN+1, MYSQL_TYPE_STRING, 0),
  ROCKSDB_FIELD_INFO("PARTITION_NAME", NAME_LEN+1, MYSQL_TYPE_STRING,
                     MY_I_S_MAYBE_NULL),
  ROCKSDB_FIELD_INFO("STAT_TYPE", NAME_LEN+1, MYSQL_TYPE_STRING, 0),
  ROCKSDB_FIELD_INFO("VALUE", sizeof(uint64_t), MYSQL_TYPE_LONGLONG,
                     0),
  ROCKSDB_FIELD_INFO_END
};

static int i_s_rocksdb_perf_context_init(void *p)
{
  ST_SCHEMA_TABLE *schema;

  DBUG_ENTER("i_s_rocksdb_perf_context_init");

  schema= (ST_SCHEMA_TABLE*) p;

  schema->fields_info= i_s_rocksdb_perf_context_fields_info;
  schema->fill_table= i_s_rocksdb_perf_context_fill_table;

  DBUG_RETURN(0);
}

static int i_s_rocksdb_perf_context_global_fill_table(THD *thd,
                                                      TABLE_LIST *tables,
                                                      Item *cond)
{
  int ret= 0;
  DBUG_ENTER("i_s_rocksdb_perf_context_global_fill_table");

  SHARE_PERF_COUNTERS counters;
  if (rocksdb_get_share_perf_counters(NULL, &counters))
    DBUG_RETURN(0);

  for (int i= 0; i < PC_MAX_IDX; i++) {
    tables->table->field[0]->store(pc_stat_types[i].c_str(),
                                   pc_stat_types[i].size(),
                                   system_charset_info);
    tables->table->field[1]->store(counters.value[i], true);

    ret= schema_table_store_record(thd, tables->table);
    if (ret)
      DBUG_RETURN(ret);
  }

  DBUG_RETURN(0);
}

static ST_FIELD_INFO i_s_rocksdb_perf_context_global_fields_info[]=
{
  ROCKSDB_FIELD_INFO("STAT_TYPE", NAME_LEN+1, MYSQL_TYPE_STRING, 0),
  ROCKSDB_FIELD_INFO("VALUE", sizeof(uint64_t), MYSQL_TYPE_LONGLONG, 0),
  ROCKSDB_FIELD_INFO_END
};

static int i_s_rocksdb_perf_context_global_init(void *p)
{
  ST_SCHEMA_TABLE *schema;

  DBUG_ENTER("i_s_rocksdb_perf_context_global_init");

  schema= (ST_SCHEMA_TABLE*) p;

  schema->fields_info= i_s_rocksdb_perf_context_global_fields_info;
  schema->fill_table= i_s_rocksdb_perf_context_global_fill_table;

  DBUG_RETURN(0);
}

/*
  Support for INFORMATION_SCHEMA.ROCKSDB_CFOPTIONS dynamic table
 */
static int i_s_rocksdb_cfoptions_fill_table(THD *thd,
                                            TABLE_LIST *tables,
                                            Item *cond)
{
  bool ret;

  DBUG_ENTER("i_s_rocksdb_cfoptions_fill_table");

  Column_family_manager cf_manager = rocksdb_get_cf_manager();

  for (auto cf_name : cf_manager.get_cf_names())
  {
    std::string val;
    rocksdb::ColumnFamilyOptions opts;
    get_cf_options(cf_name, &opts);

    std::vector<std::pair<std::string, std::string>> cf_option_types = {
      {"COMPARATOR", opts.comparator == nullptr ? "NULL" :
          std::string(opts.comparator->Name())},
      {"MERGE_OPERATOR", opts.merge_operator == nullptr ? "NULL" :
          std::string(opts.merge_operator->Name())},
      {"COMPACTION_FILTER", opts.compaction_filter == nullptr ? "NULL" :
          std::string(opts.compaction_filter->Name())},
      {"COMPACTION_FILTER_FACTORY",
          opts.compaction_filter_factory == nullptr ? "NULL" :
          std::string(opts.compaction_filter_factory->Name())},
      {"WRITE_BUFFER_SIZE", std::to_string(opts.write_buffer_size)},
      {"MAX_WRITE_BUFFER_NUMBER", std::to_string(opts.max_write_buffer_number)},
      {"MIN_WRITE_BUFFER_NUMBER_TO_MERGE",
          std::to_string(opts.min_write_buffer_number_to_merge)},
      {"NUM_LEVELS", std::to_string(opts.num_levels)},
      {"LEVEL0_FILE_NUM_COMPACTION_TRIGGER",
          std::to_string(opts.level0_file_num_compaction_trigger)},
      {"LEVEL0_SLOWDOWN_WRITES_TRIGGER",
          std::to_string(opts.level0_slowdown_writes_trigger)},
      {"LEVEL0_STOP_WRITES_TRIGGER",
          std::to_string(opts.level0_stop_writes_trigger)},
      {"MAX_MEM_COMPACTION_LEVEL", std::to_string(opts.max_mem_compaction_level)},
      {"TARGET_FILE_SIZE_BASE", std::to_string(opts.target_file_size_base)},
      {"TARGET_FILE_SIZE_MULTIPLIER", std::to_string(opts.target_file_size_multiplier)},
      {"MAX_BYTES_FOR_LEVEL_BASE", std::to_string(opts.max_bytes_for_level_base)},
      {"LEVEL_COMPACTION_DYNAMIC_LEVEL_BYTES",
          opts.level_compaction_dynamic_level_bytes ? "ON" : "OFF"},
      {"MAX_BYTES_FOR_LEVEL_MULTIPLIER",
          std::to_string(opts.max_bytes_for_level_multiplier)},
      {"EXPANDED_COMPACTION_FACTOR",
          std::to_string(opts.expanded_compaction_factor)},
      {"SOURCE_COMPACTION_FACTOR",
          std::to_string(opts.source_compaction_factor)},
      {"MAX_GRANDPARENT_OVERLAP_FACTOR",
          std::to_string(opts.max_grandparent_overlap_factor)},
      {"SOFT_RATE_LIMIT", std::to_string(opts.soft_rate_limit)},
      {"HARD_RATE_LIMIT", std::to_string(opts.hard_rate_limit)},
      {"RATE_LIMIT_DELAY_MAX_MILLISECONDS",
          std::to_string(opts.rate_limit_delay_max_milliseconds)},
      {"ARENA_BLOCK_SIZE", std::to_string(opts.arena_block_size)},
      {"DISABLE_AUTO_COMPACTIONS",
          opts.disable_auto_compactions ? "ON" : "OFF"},
      {"PURGE_REDUNDANT_KVS_WHILE_FLUSH",
          opts.purge_redundant_kvs_while_flush ? "ON" : "OFF"},
      {"VERIFY_CHECKSUM_IN_COMPACTION",
          opts.verify_checksums_in_compaction ? "ON" : "OFF"},
      {"FILTER_DELETES",
          opts.filter_deletes ? "ON" : "OFF"},
      {"MAX_SEQUENTIAL_SKIP_IN_ITERATIONS",
          std::to_string(opts.max_sequential_skip_in_iterations)},
      {"MEMTABLE_FACTORY",
          opts.memtable_factory == nullptr ? "NULL" :
            opts.memtable_factory->Name()},
      {"INPLACE_UPDATE_SUPPORT",
          opts.inplace_update_support ? "ON" : "OFF"},
      {"INPLACE_UPDATE_NUM_LOCKS",
          opts.inplace_update_num_locks ? "ON" : "OFF"},
      {"MEMTABLE_PREFIX_BLOOM_BITS",
          std::to_string(opts.memtable_prefix_bloom_bits)},
      {"MEMTABLE_PREFIX_BLOOM_PROBES",
          std::to_string(opts.memtable_prefix_bloom_probes)},
      {"MEMTABLE_PREFIX_BLOOM_HUGE_PAGE_TLB_SIZE",
          std::to_string(opts.memtable_prefix_bloom_huge_page_tlb_size)},
      {"BLOOM_LOCALITY", std::to_string(opts.bloom_locality)},
      {"MAX_SUCCESSIVE_MERGES",
          std::to_string(opts.max_successive_merges)},
      {"MIN_PARTIAL_MERGE_OPERANDS",
          std::to_string(opts.min_partial_merge_operands)},
      {"OPTIMIZE_FILTERS_FOR_HITS",
          (opts.optimize_filters_for_hits ? "ON" : "OFF")},
    };

    // get MAX_BYTES_FOR_LEVEL_MULTIPLIER_ADDITIONAL option value
    val = opts.max_bytes_for_level_multiplier_additional.empty() ? "NULL" : "";
    for (auto level : opts.max_bytes_for_level_multiplier_additional)
    {
      val.append(std::to_string(level) + ":");
    }
    val.pop_back();
    cf_option_types.push_back({"MAX_BYTES_FOR_LEVEL_MULTIPLIER_ADDITIONAL", val});

    // get COMPRESSION_TYPE option value
    switch (opts.compression)
    {
      case rocksdb::kNoCompression: val = "kNoCompression"; break;
      case rocksdb::kSnappyCompression: val = "kSnappyCompression"; break;
      case rocksdb::kZlibCompression: val = "kZlibCompression"; break;
      case rocksdb::kBZip2Compression: val = "kBZip2Compression"; break;
      case rocksdb::kLZ4Compression: val = "kLZ4Compression"; break;
      case rocksdb::kLZ4HCCompression: val = "kLZ4HCCompression"; break;
      default: val = "NULL";
    }
    cf_option_types.push_back({"COMPRESSION_TYPE", val});

    // get COMPRESSION_PER_LEVEL option value
    val = opts.compression_per_level.empty() ? "NULL" : "";
    for (auto compression_type : opts.compression_per_level)
    {
      switch (compression_type)
      {
        case rocksdb::kNoCompression: val.append("kNoCompression:"); break;
        case rocksdb::kSnappyCompression: val.append("kSnappyCompression:"); break;
        case rocksdb::kZlibCompression: val.append("kZlibCompression:"); break;
        case rocksdb::kBZip2Compression: val.append("kBZip2Compression:"); break;
        case rocksdb::kLZ4Compression: val.append("kLZ4Compression:"); break;
        case rocksdb::kLZ4HCCompression: val.append("kLZ4HCCompression:"); break;
        case rocksdb::kZSTDNotFinalCompression: val.append("kZSTDNotFinalCompression"); break;
      }
    }
    val.pop_back();
    cf_option_types.push_back({"COMPRESSION_PER_LEVEL", val});

    // get compression_opts value
    val = std::to_string(opts.compression_opts.window_bits) + ":";
    val.append(std::to_string(opts.compression_opts.level) + ":");
    val.append(std::to_string(opts.compression_opts.strategy));
    cf_option_types.push_back({"COMPRESSION_OPTS", val});

    // get PREFIX_EXTRACTOR option
    cf_option_types.push_back({"PREFIX_EXTRACTOR",
        opts.prefix_extractor == nullptr ? "NULL" :
        std::string(opts.prefix_extractor->Name())});

    // get COMPACTION_STYLE option
    switch (opts.compaction_style)
    {
      case rocksdb::kCompactionStyleLevel: val = "kCompactionStyleLevel"; break;
      case rocksdb::kCompactionStyleUniversal: val = "kCompactionStyleUniversal"; break;
      case rocksdb:: kCompactionStyleFIFO: val = "kCompactionStyleFIFO"; break;
      case rocksdb:: kCompactionStyleNone: val = "kCompactionStyleNone"; break;
      default: val = "NULL";
    }
    cf_option_types.push_back({"COMPACTION_STYLE", val});

    // get COMPACTION_OPTIONS_UNIVERSAL related options
    rocksdb::CompactionOptionsUniversal compac_opts = opts.compaction_options_universal;
    val = "{SIZE_RATIO=";
    val.append(std::to_string(compac_opts.size_ratio));
    val.append("; MIN_MERGE_WIDTH=");
    val.append(std::to_string(compac_opts.min_merge_width));
    val.append("; MAX_MERGE_WIDTH=");
    val.append(std::to_string(compac_opts.max_merge_width));
    val.append("; MAX_SIZE_AMPLIFICATION_PERCENT=");
    val.append(std::to_string(compac_opts.max_size_amplification_percent));
    val.append("; COMPRESSION_SIZE_PERCENT=");
    val.append(std::to_string(compac_opts.compression_size_percent));
    val.append("; STOP_STYLE=");
    switch (compac_opts.stop_style)
    {
      case rocksdb::kCompactionStopStyleSimilarSize:
        val.append("kCompactionStopStyleSimilarSize}"); break;
      case rocksdb::kCompactionStopStyleTotalSize:
        val.append("kCompactionStopStyleTotalSize}"); break;
      default: val.append("}");
    }
    cf_option_types.push_back({"COMPACTION_OPTIONS_UNIVERSAL", val});

    // get COMPACTION_OPTION_FIFO option
    cf_option_types.push_back({"COMPACTION_OPTION_FIFO::MAX_TABLE_FILES_SIZE",
        std::to_string(opts.compaction_options_fifo.max_table_files_size)});

    // get block-based table related options
    rocksdb::BlockBasedTableOptions& table_options = rocksdb_get_table_options();

    // get BLOCK_BASED_TABLE_FACTORY::CACHE_INDEX_AND_FILTER_BLOCKS option
    cf_option_types.push_back(
        {"BLOCK_BASED_TABLE_FACTORY::CACHE_INDEX_AND_FILTER_BLOCKS",
        table_options.cache_index_and_filter_blocks ? "1" : "0"});

    // get BLOCK_BASED_TABLE_FACTORY::INDEX_TYPE option value
    switch (table_options.index_type)
    {
      case rocksdb::BlockBasedTableOptions::kBinarySearch: val = "kBinarySearch"; break;
      case rocksdb::BlockBasedTableOptions::kHashSearch: val = "kHashSearch"; break;
      default: val = "NULL";
    }
    cf_option_types.push_back({"BLOCK_BASED_TABLE_FACTORY::INDEX_TYPE", val});

    // get BLOCK_BASED_TABLE_FACTORY::HASH_INDEX_ALLOW_COLLISION option value
    cf_option_types.push_back({"BLOCK_BASED_TABLE_FACTORY::HASH_INDEX_ALLOW_COLLISION",
        table_options.hash_index_allow_collision ? "ON" : "OFF"});

    // get BLOCK_BASED_TABLE_FACTORY::CHECKSUM option value
    switch (table_options.checksum)
    {
      case rocksdb::kNoChecksum: val = "kNoChecksum"; break;
      case rocksdb::kCRC32c: val = "kCRC32c"; break;
      case rocksdb::kxxHash: val = "kxxHash"; break;
      default: val = "NULL";
    }
    cf_option_types.push_back({"BLOCK_BASED_TABLE_FACTORY::CHECKSUM", val});

    // get BLOCK_BASED_TABLE_FACTORY::NO_BLOCK_CACHE option value
    cf_option_types.push_back({"BLOCK_BASED_TABLE_FACTORY::NO_BLOCK_CACHE",
        table_options.no_block_cache ? "ON" : "OFF"});

    // get BLOCK_BASED_TABLE_FACTORY::FILTER_POLICY option
    cf_option_types.push_back({"BLOCK_BASED_TABLE_FACTORY::FILTER_POLICY",
        table_options.filter_policy == nullptr ? "NULL" :
          std::string(table_options.filter_policy->Name())});

    // get BLOCK_BASED_TABLE_FACTORY::WHOLE_KEY_FILTERING option
    cf_option_types.push_back({"BLOCK_BASED_TABLE_FACTORY::WHOLE_KEY_FILTERING",
        table_options.whole_key_filtering ? "1" : "0"});

    // get BLOCK_BASED_TABLE_FACTORY::BLOCK_CACHE option
    cf_option_types.push_back({"BLOCK_BASED_TABLE_FACTORY::BLOCK_CACHE",
        table_options.block_cache == nullptr ? "NULL" :
          std::to_string(table_options.block_cache->GetUsage())});

    // get BLOCK_BASED_TABLE_FACTORY::BLOCK_CACHE_COMPRESSED option
    cf_option_types.push_back({"BLOCK_BASED_TABLE_FACTORY::BLOCK_CACHE_COMPRESSED",
        table_options.block_cache_compressed == nullptr ? "NULL" :
          std::to_string(table_options.block_cache_compressed->GetUsage())});

    // get BLOCK_BASED_TABLE_FACTORY::BLOCK_SIZE option
    cf_option_types.push_back({"BLOCK_BASED_TABLE_FACTORY::BLOCK_SIZE",
        std::to_string(table_options.block_size)});

    // get BLOCK_BASED_TABLE_FACTORY::BLOCK_SIZE_DEVIATION option
    cf_option_types.push_back({"BLOCK_BASED_TABLE_FACTORY::BLOCK_SIZE_DEVIATION",
        std::to_string(table_options.block_size_deviation)});

    // get BLOCK_BASED_TABLE_FACTORY::BLOCK_RESTART_INTERVAL option
    cf_option_types.push_back({"BLOCK_BASED_TABLE_FACTORY::BLOCK_RESTART_INTERVAL",
        std::to_string(table_options.block_restart_interval)});

    // get BLOCK_BASED_TABLE_FACTORY::FORMAT_VERSION option
    cf_option_types.push_back({"BLOCK_BASED_TABLE_FACTORY::FORMAT_VERSION",
        std::to_string(table_options.format_version)});

    for (auto cf_option_type : cf_option_types)
    {
      tables->table->field[0]->store(cf_name.c_str(), cf_name.size(),
                                     system_charset_info);
      tables->table->field[1]->store(cf_option_type.first.c_str(),
                                     cf_option_type.first.size(),
                                     system_charset_info);
      tables->table->field[2]->store(cf_option_type.second.c_str(),
                                     cf_option_type.second.size(),
                                     system_charset_info);

      ret = schema_table_store_record(thd, tables->table);

      if (ret)
        DBUG_RETURN(ret);
    }
  }
  DBUG_RETURN(0);
}

static ST_FIELD_INFO i_s_rocksdb_cfoptions_fields_info[] =
{
  ROCKSDB_FIELD_INFO("CF_NAME", NAME_LEN+1, MYSQL_TYPE_STRING, 0),
  ROCKSDB_FIELD_INFO("OPTION_TYPE", NAME_LEN+1, MYSQL_TYPE_STRING, 0),
  ROCKSDB_FIELD_INFO("VALUE", NAME_LEN+1, MYSQL_TYPE_STRING, 0),
  ROCKSDB_FIELD_INFO_END
};

struct i_s_rocksdb_ddl {
  THD *thd;
  TABLE_LIST *tables;
  Item *cond;
};

static int i_s_rocksdb_ddl_callback(void *cb_arg, RDBSE_TABLE_DEF *rec)
{
  struct i_s_rocksdb_ddl *ddl_arg= (struct i_s_rocksdb_ddl*)cb_arg;
  int ret= 0;
  THD *thd= ddl_arg->thd;
  TABLE_LIST *tables= ddl_arg->tables;

  StringBuffer<256> dbname, tablename, partname;

  /* Some special tables such as drop_index have different names, ignore them */
  if (rocksdb_split_normalized_tablename(rec->dbname_tablename.c_ptr(),
                                         &dbname, &tablename, &partname))
    return 0;

  for (uint i= 0; i < rec->n_keys; i++) {
    RDBSE_KEYDEF* key_descr= rec->key_descr[i];

    tables->table->field[0]->store(dbname.c_ptr(), dbname.length(),
                                   system_charset_info);
    tables->table->field[1]->store(tablename.c_ptr(), tablename.length(),
                                   system_charset_info);
    if (partname.length() == 0)
      tables->table->field[2]->set_null();
    else
    {
      tables->table->field[2]->set_notnull();
      tables->table->field[2]->store(partname.c_ptr(), partname.length(),
                                     system_charset_info);
    }

    tables->table->field[3]->store(key_descr->name.c_str(),
                                   key_descr->name.size(),
                                   system_charset_info);

    tables->table->field[4]->store(key_descr->get_index_number(), true);
    tables->table->field[5]->store(key_descr->index_type, true);
    tables->table->field[6]->store(key_descr->kv_format_version, true);

    std::string cf_name= key_descr->get_cf()->GetName();
    tables->table->field[7]->store(cf_name.c_str(), cf_name.size(),
                                   system_charset_info);

    ret= schema_table_store_record(thd, tables->table);
    if (ret)
      return ret;
  }
  return 0;
}

static int i_s_rocksdb_ddl_fill_table(THD *thd, TABLE_LIST *tables, Item *cond)
{
  int ret;
  Table_ddl_manager *ddl_manager= get_ddl_manager();
  struct i_s_rocksdb_ddl ddl_arg= { thd, tables, cond };

  DBUG_ENTER("i_s_rocksdb_ddl_fill_table");

  ret= ddl_manager->scan((void*)&ddl_arg, i_s_rocksdb_ddl_callback);

  DBUG_RETURN(ret);
}

static ST_FIELD_INFO i_s_rocksdb_ddl_fields_info[] =
{
  ROCKSDB_FIELD_INFO("TABLE_SCHEMA", NAME_LEN+1, MYSQL_TYPE_STRING, 0),
  ROCKSDB_FIELD_INFO("TABLE_NAME", NAME_LEN+1, MYSQL_TYPE_STRING, 0),
  ROCKSDB_FIELD_INFO("PARTITION_NAME", NAME_LEN+1, MYSQL_TYPE_STRING,
                     MY_I_S_MAYBE_NULL),
  ROCKSDB_FIELD_INFO("INDEX_NAME", NAME_LEN+1, MYSQL_TYPE_STRING, 0),
  ROCKSDB_FIELD_INFO("INDEX_NUMBER", sizeof(uint32_t), MYSQL_TYPE_LONG, 0),
  ROCKSDB_FIELD_INFO("INDEX_TYPE", sizeof(uint16_t), MYSQL_TYPE_SHORT, 0),
  ROCKSDB_FIELD_INFO("KV_FORMAT_VERSION", sizeof(uint16_t),
                     MYSQL_TYPE_SHORT, 0),
  ROCKSDB_FIELD_INFO("CF", NAME_LEN+1, MYSQL_TYPE_STRING, 0),
  ROCKSDB_FIELD_INFO_END
};

static int i_s_rocksdb_ddl_init(void *p)
{
  ST_SCHEMA_TABLE *schema;

  DBUG_ENTER("i_s_rocksdb_ddl_init");

  schema= (ST_SCHEMA_TABLE*) p;

  schema->fields_info= i_s_rocksdb_ddl_fields_info;
  schema->fill_table= i_s_rocksdb_ddl_fill_table;

  DBUG_RETURN(0);
}

static int i_s_rocksdb_cfoptions_init(void *p)
{
  ST_SCHEMA_TABLE *schema;

  DBUG_ENTER("i_s_rocksdb_cfoptions_init");

  schema= (ST_SCHEMA_TABLE*) p;

  schema->fields_info= i_s_rocksdb_cfoptions_fields_info;
  schema->fill_table= i_s_rocksdb_cfoptions_fill_table;

  DBUG_RETURN(0);
}

/* Given a path to a file return just the filename portion. */
static std::string filename_without_path(
    const std::string& path)
{
  /* Find last slash in path */
  size_t pos = path.rfind('/');

  /* None found?  Just return the original string */
  if (pos == std::string::npos) {
    return std::string(path);
  }

  /* Return everything after the slash (or backslash) */
  return path.substr(pos + 1);
}

/* Fill the information_schema.rocksdb_index_file_map virtual table */
static int i_s_rocksdb_index_file_map_fill_table(
    THD        *thd,
    TABLE_LIST *tables,
    Item       *cond)
{
  int     ret = 0;
  Field **field = tables->table->field;

  DBUG_ENTER("i_s_rocksdb_index_file_map_fill_table");

  /* Iterate over all the column families */
  rocksdb::DB *rdb= rocksdb_get_rdb();
  Column_family_manager& cf_manager = rocksdb_get_cf_manager();
  for (auto cf_handle : cf_manager.get_all_cf()) {
    /* Grab the the properties of all the tables in the column family */
    rocksdb::TablePropertiesCollection table_props_collection;
    rocksdb::Status s = rdb->GetPropertiesOfAllTables(cf_handle,
        &table_props_collection);
    if (!s.ok()) {
      continue;
    }

    /* Iterate over all the items in the collection, each of which contains a
     * name and the actual properties */
    for (auto props : table_props_collection) {
      /* Add the SST name into the output */
      std::string sst_name = filename_without_path(props.first);
      field[1]->store(sst_name.data(), sst_name.size(), system_charset_info);

      /* Get the __indexstats__ data out of the table property */
      std::vector<MyRocksTablePropertiesCollector::IndexStats> stats =
          MyRocksTablePropertiesCollector::GetStatsFromTableProperties(props.second);
      if (stats.empty()) {
        field[0]->store(-1, true);
        field[2]->store(-1, true);
        field[3]->store(-1, true);
      }
      else {
        for (auto it : stats) {
          /* Add the index number, the number of rows, and data size to the output */
          field[0]->store(it.index_number, true);
          field[2]->store(it.rows, true);
          field[3]->store(it.data_size, true);

          /* Tell MySQL about this row in the virtual table */
          ret= schema_table_store_record(thd, tables->table);
          if (ret != 0) {
            break;
          }
        }
      }
    }
  }

  DBUG_RETURN(ret);
}

static ST_FIELD_INFO i_s_rocksdb_index_file_map_fields_info[] =
{
  /* The information_schema.rocksdb_index_file_map virtual table has four fields:
   *   INDEX_NUMBER => the index id contained in the SST file
   *   SST_NAME => the name of the SST file containing some indexes
   *   NUM_ROWS => the number of entries of this index id in this SST file
   *   DATA_SIZE => the data size stored in this SST file for this index id */
  ROCKSDB_FIELD_INFO("INDEX_NUMBER", sizeof(uint32_t), MYSQL_TYPE_LONG, 0),
  ROCKSDB_FIELD_INFO("SST_NAME", NAME_LEN+1, MYSQL_TYPE_STRING, 0),
  ROCKSDB_FIELD_INFO("NUM_ROWS", sizeof(int64_t), MYSQL_TYPE_LONGLONG, 0),
  ROCKSDB_FIELD_INFO("DATA_SIZE", sizeof(int64_t), MYSQL_TYPE_LONGLONG, 0),
  ROCKSDB_FIELD_INFO_END
};

/* Initialize the information_schema.rocksdb_index_file_map virtual table */
static int i_s_rocksdb_index_file_map_init(void *p)
{
  ST_SCHEMA_TABLE *schema;

  DBUG_ENTER("i_s_rocksdb_index_file_map_init");

  schema= (ST_SCHEMA_TABLE*) p;

  schema->fields_info=i_s_rocksdb_index_file_map_fields_info;
  schema->fill_table=i_s_rocksdb_index_file_map_fill_table;

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
  "Facebook",
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
  "Facebook",
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

struct st_mysql_plugin i_s_rocksdb_perf_context=
{
  MYSQL_INFORMATION_SCHEMA_PLUGIN,
  &i_s_rocksdb_info,
  "ROCKSDB_PERF_CONTEXT",
  "Facebook",
  "RocksDB perf context stats",
  PLUGIN_LICENSE_GPL,
  i_s_rocksdb_perf_context_init,
  i_s_rocksdb_deinit,
  0x0001,                             /* version number (0.1) */
  NULL,                               /* status variables */
  NULL,                               /* system variables */
  NULL,                               /* config options */
  0,                                  /* flags */
};

struct st_mysql_plugin i_s_rocksdb_perf_context_global=
{
  MYSQL_INFORMATION_SCHEMA_PLUGIN,
  &i_s_rocksdb_info,
  "ROCKSDB_PERF_CONTEXT_GLOBAL",
  "Facebook",
  "RocksDB perf context stats (all)",
  PLUGIN_LICENSE_GPL,
  i_s_rocksdb_perf_context_global_init,
  i_s_rocksdb_deinit,
  0x0001,                             /* version number (0.1) */
  NULL,                               /* status variables */
  NULL,                               /* system variables */
  NULL,                               /* config options */
  0,                                  /* flags */
};

struct st_mysql_plugin i_s_rocksdb_cfoptions=
{
  MYSQL_INFORMATION_SCHEMA_PLUGIN,
  &i_s_rocksdb_info,
  "ROCKSDB_CF_OPTIONS",
  "Facebook",
  "RocksDB column family options",
  PLUGIN_LICENSE_GPL,
  i_s_rocksdb_cfoptions_init,
  i_s_rocksdb_deinit,
  0x0001,                             /* version number (0.1) */
  NULL,                               /* status variables */
  NULL,                               /* system variables */
  NULL,                               /* config options */
  0,                                  /* flags */
};

struct st_mysql_plugin i_s_rocksdb_ddl=
{
  MYSQL_INFORMATION_SCHEMA_PLUGIN,
  &i_s_rocksdb_info,
  "ROCKSDB_DDL",
  "Facebook",
  "RocksDB Data Dictionary",
  PLUGIN_LICENSE_GPL,
  i_s_rocksdb_ddl_init,
  i_s_rocksdb_deinit,
  0x0001,                             /* version number (0.1) */
  NULL,                               /* status variables */
  NULL,                               /* system variables */
  NULL,                               /* config options */
  0,                                  /* flags */
};

struct st_mysql_plugin i_s_rocksdb_index_file_map=
{
  MYSQL_INFORMATION_SCHEMA_PLUGIN,
  &i_s_rocksdb_info,
  "ROCKSDB_INDEX_FILE_MAP",
  "Facebook",
  "RocksDB index file map",
  PLUGIN_LICENSE_GPL,
  i_s_rocksdb_index_file_map_init,
  i_s_rocksdb_deinit,
  0x0001,                             /* version number (0.1) */
  NULL,                               /* status variables */
  NULL,                               /* system variables */
  NULL,                               /* config options */
  0,                                  /* flags */
};
