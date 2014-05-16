if (`SELECT COUNT(*) = 1 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE LOWER(variable_name) = 'innodb_page_size' AND variable_value = 32768`)
{
  --skip Test cannot run with InnoDB with 32k Page size.
}
