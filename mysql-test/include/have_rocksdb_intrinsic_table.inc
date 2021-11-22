if (`select count(*) = 0 from performance_schema.global_variables where variable_name = 'enable_rocksdb_intrinsic_tmp_table' and variable_value = "ON";`) {
  --skip Test requires enable_rocksdb_intrinsic_tmp_table to ON
}
