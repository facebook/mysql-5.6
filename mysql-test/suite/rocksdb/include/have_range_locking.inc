if (`select count(*) = 0 from performance_schema.session_variables where variable_name = 'rocksdb_use_range_locking' and variable_value = 'ON';`) {
  --skip Test requires range locking
}
