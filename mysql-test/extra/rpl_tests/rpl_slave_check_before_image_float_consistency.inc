# create schema and insert rows
connection master;
eval create table t1 (a int primary key, b int, c float) engine = $engine; # pk
eval create table t2 (a int unique, b int, c float) engine = $engine; # unique key
eval create table t3 (a int primary key, b int not null, c float not null) engine = $engine; # not null columns
let $uuid = `select @@global.server_uuid`;

# insert high precision floating point values
insert into t1 values(1, 1, 0.7983204567);
insert into t2 values(1, 1, 0.7983204567);
insert into t3 values(1, 1, 0.7983204567);

source include/sync_slave_sql_with_master.inc;
let $initial_value = query_get_value(show global status like "Slave_before_image_inconsistencies", Value, 1);

# create inconsistencies on the slave
connection slave;
set @@sql_log_bin = 0;
update t1 set c = 0.798321;
update t2 set c = 0.798321;
update t3 set c = 0.798321;
set @@sql_log_bin = 1;

--echo # Test inconsistencies for table with primary key
connection master;
update t1 set b = 2;
connection slave;
let $slave_sql_errno= 1032; # HA_ERR_END_OF_FILE
source include/wait_for_slave_sql_error.inc;
let $current_value = query_get_value(show global status like "Slave_before_image_inconsistencies", Value, 1);
--disable_query_log
eval select $current_value - $initial_value as "inconsistency_count";
--enable_query_log
replace_result $uuid uuid;
select * from information_schema.RBR_BI_INCONSISTENCIES ORDER BY SOURCE_LOG_POS;
set @@sql_log_bin = 0;
update t1 set c = 0.798320;
set @@sql_log_bin = 1;
source include/stop_slave.inc;
source include/start_slave.inc;
connection master;
source include/sync_slave_sql_with_master.inc;
connection slave;
let $current_value = query_get_value(show global status like "Slave_before_image_inconsistencies", Value, 1);
--disable_query_log
eval select $current_value - $initial_value as "inconsistency_count";
--enable_query_log

--echo # Test inconsistencies for table with unique key
connection master;
update t2 set b = 2;
connection slave;
let $slave_sql_errno= 1032; # HA_ERR_END_OF_FILE
source include/wait_for_slave_sql_error.inc;
let $current_value = query_get_value(show global status like "Slave_before_image_inconsistencies", Value, 1);
--disable_query_log
eval select $current_value - $initial_value as "inconsistency_count";
--enable_query_log
replace_result $uuid uuid;
select * from information_schema.RBR_BI_INCONSISTENCIES ORDER BY SOURCE_LOG_POS;
set @@sql_log_bin = 0;
update t2 set c = 0.798320;
set @@sql_log_bin = 1;
source include/stop_slave.inc;
source include/start_slave.inc;
connection master;
source include/sync_slave_sql_with_master.inc;
connection slave;
let $current_value = query_get_value(show global status like "Slave_before_image_inconsistencies", Value, 1);
--disable_query_log
eval select $current_value - $initial_value as "inconsistency_count";
--enable_query_log

--echo # Test inconsistencies for table with not null keys
connection master;
update t3 set b = 2;
connection slave;
let $slave_sql_errno= 1032; # HA_ERR_END_OF_FILE
source include/wait_for_slave_sql_error.inc;
let $current_value = query_get_value(show global status like "Slave_before_image_inconsistencies", Value, 1);
--disable_query_log
eval select $current_value - $initial_value as "inconsistency_count";
--enable_query_log
replace_result $uuid uuid;
select * from information_schema.RBR_BI_INCONSISTENCIES ORDER BY SOURCE_LOG_POS;
set @@sql_log_bin = 0;
update t3 set c = 0.798320;
set @@sql_log_bin = 1;
source include/stop_slave.inc;
source include/start_slave.inc;
connection master;
source include/sync_slave_sql_with_master.inc;
connection slave;
let $current_value = query_get_value(show global status like "Slave_before_image_inconsistencies", Value, 1);
--disable_query_log
eval select $current_value - $initial_value as "inconsistency_count";
--enable_query_log

connection master;
select * from t1;
select * from t2;
select * from t3;

connection slave;
select * from t1;
select * from t2;
select * from t3;

# cleanup
connection master;
drop table t1;
drop table t2;
drop table t3;
source include/sync_slave_sql_with_master.inc;
