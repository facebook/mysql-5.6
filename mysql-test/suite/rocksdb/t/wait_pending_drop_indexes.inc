--disable_query_log
--source include/sync_slave_sql_with_master.inc
connection slave;
let $i = 0;
while ($i < 5000)
{
  --sleep 0.1
  --let $drop_index_count = query_get_value("SELECT count(*) as COUNT FROM information_schema.rocksdb_global_info where TYPE = 'DDL_DROP_INDEX_ONGOING'", COUNT, 1)
  if ($drop_index_count == 0)
  {
    let $i = 5000;
  }
  inc $i;
}
connection master;
let $i = 0;
while ($i < 5000)
{
  --sleep 0.1
  --let $drop_index_count = query_get_value("SELECT count(*) as COUNT FROM information_schema.rocksdb_global_info where TYPE = 'DDL_DROP_INDEX_ONGOING'", COUNT, 1)
  if ($drop_index_count == 0)
  {
    let $i = 5000;
  }
  inc $i;
}
--enable_query_log
