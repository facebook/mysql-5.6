--let $old_debug = `SELECT @@global.debug`

--source include/rpl_connection_master.inc
--let $uuid = `SELECT @@server_uuid`
--eval CREATE TABLE t1(a INT, PRIMARY KEY(a)) ENGINE=$engine
INSERT INTO t1 VALUES(1);
INSERT INTO t1 VALUES(2);
--source include/sync_slave_sql_with_master.inc
--replace_result $uuid uuid
SELECT @@global.gtid_executed;

--source include/rpl_connection_slave.inc
--eval SET GLOBAL debug = '+d,$debug_option'
--exec echo "restart" > $MYSQLTEST_VARDIR/tmp/mysqld.2.expect

--source include/rpl_connection_master.inc
INSERT INTO t1 VALUES(3);

--source include/rpl_connection_slave.inc
--source include/wait_until_disconnected.inc
--let $rpl_server_number = 2
--source include/rpl_reconnect.inc

--eval SET GLOBAL debug = `$old_debug`
--replace_result $uuid uuid
SELECT @@global.gtid_executed;
SELECT * FROM t1;

CHANGE REPLICATION SOURCE TO SOURCE_AUTO_POSITION = 1;
--source include/start_slave.inc

--source include/rpl_connection_master.inc
RENAME TABLE t1 TO test1;
--source include/sync_slave_sql_with_master.inc
SELECT * FROM test1;
--source include/rpl_connection_master.inc
SELECT * FROM test1;
DROP TABLE test1;
--source include/sync_slave_sql_with_master.inc
--source include/stop_slave.inc
CHANGE REPLICATION SOURCE TO SOURCE_AUTO_POSITION = 0;
--source include/start_slave.inc
--replace_result $uuid uuid
SELECT @@global.gtid_executed;
