--let $_use_range_locking= `select @@rocksdb_use_range_locking`
if ($_use_range_locking == 1)
{
  --skip Test doesn't support range locking
}
