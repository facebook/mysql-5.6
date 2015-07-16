set global rocksdb_force_flush_memtable_now = true;
select cast(variable_value as unsigned) into @a from information_schema.global_status where variable_name='rocksdb_number_stat_computes';
let $wait_condition= select variable_value>@a from information_schema.global_status where variable_name='rocksdb_number_stat_computes';
let $wait_timeout= 120;
--source include/wait_condition.inc
