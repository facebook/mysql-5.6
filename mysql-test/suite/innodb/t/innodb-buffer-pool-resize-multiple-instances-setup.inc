#
# Shrinking and enlarging buffer pool with multiple instances and small chunks
#
--source include/have_innodb.inc
--source include/not_valgrind.inc

let $wait_timeout = 600;
let $wait_condition =
  SELECT SUBSTR(variable_value, 1, 34) = 'Completed resizing buffer pool at '
  FROM information_schema.global_status
  WHERE LOWER(variable_name) = 'innodb_buffer_pool_resize_status';

--disable_query_log
set @old_innodb_buffer_pool_size = @@innodb_buffer_pool_size;
set @old_innodb_file_format = @@innodb_file_format;
set @old_innodb_file_per_table = @@innodb_file_per_table;
set @old_innodb_adaptive_hash_index = @@innodb_adaptive_hash_index;
--enable_query_log

set global innodb_file_format=`Barracuda`;
set global innodb_file_per_table=ON;
set global innodb_adaptive_hash_index=ON;

select @@innodb_buffer_pool_size;
select @@innodb_buffer_pool_instances;
select @@innodb_buffer_pool_chunk_size;

# fill buffer pool
