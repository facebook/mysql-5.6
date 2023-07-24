source include/have_debug.inc;

# resetting the counter by disabling and then enabling
connection slave;
set @save.slave_check_before_image_consistency = @@global.slave_check_before_image_consistency;
set @@global.slave_check_before_image_consistency = OFF;
set @@global.slave_check_before_image_consistency = @save.slave_check_before_image_consistency;

# create schema and insert rows
connection master;
eval create table t1 (a int primary key, b int, c int) engine = $engine; # pk
eval create table t2 (a int unique, b int, c int) engine = $engine; # unique key
eval create table t3 (a int, b int, c int, key(a)) engine = $engine; # sec key
eval create table t4 (a int, b int, c int) engine = $engine; # no key
let $uuid = `select @@global.server_uuid`;

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
let $slave_sql_errno= convert_error(ER_RBR_BEFORE_IMAGE_INCONSISTENT);
source include/wait_for_slave_sql_error.inc;
show status like 'Slave_before_image_inconsistencies';
replace_result $uuid uuid;
select * from information_schema.RBR_BI_INCONSISTENCIES;
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
let $slave_sql_errno= 1032; # HA_ERR_KEY_NOT_FOUND;
source include/wait_for_slave_sql_error.inc;
show status like 'Slave_before_image_inconsistencies';
replace_result $uuid uuid;
select * from information_schema.RBR_BI_INCONSISTENCIES;
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
let $slave_sql_errno= 1032; # HA_ERR_KEY_NOT_FOUND;
source include/wait_for_slave_sql_error.inc;
show status like 'Slave_before_image_inconsistencies';
replace_result $uuid uuid;
select * from information_schema.RBR_BI_INCONSISTENCIES;
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
let $slave_sql_errno= 1032; # HA_ERR_KEY_NOT_FOUND;
show status like 'Slave_before_image_inconsistencies';
replace_result $uuid uuid;
select * from information_schema.RBR_BI_INCONSISTENCIES;
source include/wait_for_slave_sql_error.inc;
set @@sql_log_bin = 0;
update t4 set c = 1;
set @@sql_log_bin = 1;
source include/stop_slave.inc;
source include/start_slave.inc;
connection master;
source include/sync_slave_sql_with_master.inc;

# check if we detect false positive inconsistencies from record_compare()
connection slave;
# resetting the counter by disabling and then enabling
set @save.slave_check_before_image_consistency = @@global.slave_check_before_image_consistency;
set @@global.slave_check_before_image_consistency = OFF;
set @@global.slave_check_before_image_consistency = @save.slave_check_before_image_consistency;
set @@global.debug = "+d,dbg.fire_bi_inconsistency";

connection master;
update t1 set c = 3;
update t2 set c = 3;
update t3 set c = 3;
update t4 set c = 3;
source include/sync_slave_sql_with_master.inc;

connection slave;
show status like 'Slave_before_image_inconsistencies';
replace_result $uuid uuid;
select * from information_schema.RBR_BI_INCONSISTENCIES;
set @@global.debug = "-d,dbg.fire_bi_inconsistency";

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
