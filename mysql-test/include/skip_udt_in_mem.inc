if (`select count(*) != 0 from performance_schema.global_variables where variable_name = 'rocksdb_enable_udt_in_mem' and variable_value = 'ON';`) {
  --skip Test doesn't enable HLC
}
