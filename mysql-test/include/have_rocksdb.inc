if (`SELECT COUNT(*) = 0 FROM INFORMATION_SCHEMA.ENGINES WHERE engine = 'rocksdb' AND support IN ('DEFAULT')`)
{
  --skip Test requires default engine RocksDB
}

if (`select count(*) = 0 from information_schema.tables where engine='rocksdb' and table_name='slave_gtid_info'`)
{
  --skip Test requires default engine RocksDB
}

--disable_query_log
set global rocksdb_force_flush_memtable_now = true;
--enable_query_log

