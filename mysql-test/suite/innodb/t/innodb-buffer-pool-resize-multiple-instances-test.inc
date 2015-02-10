create or replace view view0 as select 1 union all select 1;

set @`v_id` := 0;
set @`v_val` := 0;

# 2^18 == 262144 records
replace into t1 select (@`v_id` := (@`v_id` + 4) mod 4294967296) as id, (@`v_val` := (@`v_val` + 4) mod 4294967296) as val from view0 v0, view0 v1, view0 v2, view0 v3, view0 v4, view0 v5, view0 v6, view0 v7, view0 v8, view0 v9, view0 v10, view0 v11, view0 v12, view0 v13, view0 v14, view0 v15, view0 v16, view0 v17;

select count(val) from t1;

# Shrink buffer pool size more than half to trigger page hash rebuild
# From 8GB to 4000000000
set global innodb_buffer_pool_size = 4000000000;

--source include/wait_condition.inc

select @@innodb_buffer_pool_size;
select @@innodb_buffer_pool_instances;
select @@innodb_buffer_pool_chunk_size;

# Enlarge buffer pool size more than double to trigger page hash rebuild
# From 4000000000 to 9000000000
set global innodb_buffer_pool_size = 9000000000;

--source include/wait_condition.inc

select count(val) from t1;

select @@innodb_buffer_pool_size;
select @@innodb_buffer_pool_instances;
select @@innodb_buffer_pool_chunk_size;

drop table t1;
drop view view0;

--disable_query_log
set global innodb_buffer_pool_size = @old_innodb_buffer_pool_size;
set global innodb_file_format = @old_innodb_file_format;
set global innodb_file_per_table = @old_innodb_file_per_table;
set global innodb_adaptive_hash_index = @old_innodb_adaptive_hash_index;
--enable_query_log
--source include/wait_condition.inc
