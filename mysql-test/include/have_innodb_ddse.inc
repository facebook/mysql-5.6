if (`SELECT COUNT(*) = 0 FROM PERFORMANCE_SCHEMA.GLOBAL_VARIABLES WHERE variable_name = 'default_dd_storage_engine' and variable_value = 'InnoDB';`) {
  --skip Test requires InnoDB DDSE
}
