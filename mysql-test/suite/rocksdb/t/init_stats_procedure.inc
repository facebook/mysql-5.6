# This inc script creates two procedures -- save_read_stats() and
# get_read_stats(). get_read_stats() prints differential rocksdb_rows_read,
# rocksdb_rows_updated, and rocksdb_rows_deleted values since calling
# save_read_stats().

delimiter //;
create procedure save_read_stats()
begin
  select rows_fetched into @rf from sys.schema_table_statistics
    where table_schema=database() and table_name='t1';
  select variable_value into @rr from performance_schema.global_status
    where variable_name='rocksdb_rows_read';
  select variable_value into @ru from performance_schema.global_status
    where variable_name='rocksdb_rows_updated';
  select variable_value into @rd from performance_schema.global_status
    where variable_name='rocksdb_rows_deleted';
end//

create procedure get_read_stats()
begin
  select rows_fetched - @rf from sys.schema_table_statistics
    where table_schema=database() and table_name='t1';
  select variable_value - @rr as rows_read from
    performance_schema.global_status
    where variable_name='rocksdb_rows_read';
  select variable_value - @ru as rows_updated from
    performance_schema.global_status
    where variable_name='rocksdb_rows_updated';
  select variable_value - @rd as rows_deleted from
    performance_schema.global_status
    where variable_name='rocksdb_rows_deleted';
end//
delimiter ;//

