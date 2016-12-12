if (`SELECT COUNT(*) = 0
       FROM INFORMATION_SCHEMA.ENGINES
       WHERE engine = 'innodb'
         AND support IN ('YES', 'DEFAULT', 'ENABLED')`)
{
  --skip Test requires both InnoDB and RocksDB to be enabled (InnoDB not found)
}

if (`SELECT COUNT(*) = 0
       FROM INFORMATION_SCHEMA.ENGINES
       WHERE engine = 'rocksdb'
         AND support IN ('YES', 'DEFAULT', 'ENABLED')`)
{
  --skip Test requires both InnoDB and RocksDB to be enabled (RocksDB not found)
}

--disable_query_log
# Table statistics can vary depending on when the memtables are flushed, so
# flush them at the beginning of the test to ensure the test runs consistently.
set global rocksdb_force_flush_memtable_now = true;
--enable_query_log

