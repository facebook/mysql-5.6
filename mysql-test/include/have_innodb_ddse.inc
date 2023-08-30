if (`select count(*) = 1 from performance_schema.global_variables where variable_name = 'default_dd_storage_engine' and variable_value = 'RocksDB';`) {
  --skip Test requires non Rocksdb DD
}
