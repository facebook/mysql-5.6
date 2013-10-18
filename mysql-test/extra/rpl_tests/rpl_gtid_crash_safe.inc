-- let $old_debug = `select @@global.debug;`

connection master;
-- let $uuid = `select @@server_uuid;`
create table t1(a int, PRIMARY KEY(a)) ENGINE=INNODB;
insert into t1 values(1);
insert into t1 values(2);
sync_slave_with_master;
use mysql;
-- replace_result $uuid uuid
select * from slave_gtid_info;

connection slave;
-- eval SET GLOBAL debug = '+d,$debug_option'
-- exec echo "restart" > $MYSQLTEST_VARDIR/tmp/mysqld.2.expect

connection master;
insert into t1 values(3);

connection slave;
-- source include/wait_until_disconnected.inc
-- let $rpl_server_number = 2
-- source include/rpl_reconnect.inc

-- eval SET GLOBAL debug = `$old_debug`
use mysql;
-- replace_result $uuid uuid
select * from slave_gtid_info;
use test;
select * from t1;

change master to master_auto_position = 1;
-- source include/start_slave.inc

connection master;
rename table t1 to test1;
sync_slave_with_master;
use test;
select * from test1;
connection master;
use test;
select * from test1;
drop table test1;
sync_slave_with_master;
-- source include/stop_slave.inc
change master to master_auto_position = 0;
-- source include/start_slave.inc
use mysql;
-- replace_result $uuid uuid
select * from slave_gtid_info;
