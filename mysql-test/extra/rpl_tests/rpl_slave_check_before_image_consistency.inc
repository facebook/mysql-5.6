# create schema and insert rows
connection master;
eval create table t1 (a int primary key, b int, c int) engine = $engine; # pk
eval create table t2 (a int unique, b int, c int) engine = $engine; # unique key
eval create table t3 (a int, b int, c int, key(a)) engine = $engine; # sec key
eval create table t4 (a int, b int, c int) engine = $engine; # no key

insert into t1 values(1, 1, 1);
insert into t2 values(1, 1, 1);
insert into t3 values(1, 1, 1);
insert into t4 values(1, 1, 1);

source include/sync_slave_sql_with_master.inc;

# create inconsistencies on the slave
connection slave;
set @@sql_log_bin = 0;
update t1 set c = 2;
update t2 set c = 2;
update t3 set c = 2;
update t4 set c = 2;
set @@sql_log_bin = 1;

# check if we detect inconsistencies for different schemas
connection master;
update t1 set b = 2;
connection slave;
let $slave_sql_errno= 1032; # HA_ERR_END_OF_FILE
source include/wait_for_slave_sql_error.inc;
set @@sql_log_bin = 0;
update t1 set c = 1;
set @@sql_log_bin = 1;
source include/stop_slave.inc;
source include/start_slave.inc;
connection master;
source include/sync_slave_sql_with_master.inc;

connection master;
update t2 set b = 2;
connection slave;
let $slave_sql_errno= 1032; # HA_ERR_END_OF_FILE
source include/wait_for_slave_sql_error.inc;
set @@sql_log_bin = 0;
update t2 set c = 1;
set @@sql_log_bin = 1;
source include/stop_slave.inc;
source include/start_slave.inc;
connection master;
source include/sync_slave_sql_with_master.inc;

connection master;
update t3 set b = 2;
connection slave;
let $slave_sql_errno= 1032; # HA_ERR_END_OF_FILE
source include/wait_for_slave_sql_error.inc;
set @@sql_log_bin = 0;
update t3 set c = 1;
set @@sql_log_bin = 1;
source include/stop_slave.inc;
source include/start_slave.inc;
connection master;
source include/sync_slave_sql_with_master.inc;

connection master;
update t4 set b = 2;
connection slave;
let $slave_sql_errno= 1032; # HA_ERR_END_OF_FILE
source include/wait_for_slave_sql_error.inc;
set @@sql_log_bin = 0;
update t4 set c = 1;
set @@sql_log_bin = 1;
source include/stop_slave.inc;
source include/start_slave.inc;
connection master;
source include/sync_slave_sql_with_master.inc;

connection master;
select * from t1;
select * from t2;
select * from t3;
select * from t4;

connection slave;
select * from t1;
select * from t2;
select * from t3;
select * from t4;

# cleanup
connection master;
drop table t1;
drop table t2;
drop table t3;
drop table t4;
source include/sync_slave_sql_with_master.inc;
