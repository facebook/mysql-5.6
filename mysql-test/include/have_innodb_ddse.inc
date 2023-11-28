if (`select count(*) = 0 from performance_schema.global_variables where variable_name = 'default_dd_system_storage_engine' and variable_value = 'InnoDB';`) {
  --skip Test requires InnoDB DDSE
}
