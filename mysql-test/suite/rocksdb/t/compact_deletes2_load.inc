select 'loading data';

--disable_query_log
let $i=0;
while ($i<1000)
{
  inc $i;
  eval insert r1(id1, id2, type, value, value2, value3)
  values($i,$i,$i, 'xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx',$i,$i);
}
--enable_query_log

set global rocksdb_force_flush_memtable_now=1;
optimize table r1;

