# Setup
--source include/rpl_connection_master.inc
call mtr.add_suppression("Databases were empty for this trx");
CREATE DATABASE test1;
CREATE DATABASE test2;

CREATE TABLE test1.t1(a INT PRIMARY KEY, b CHAR(8)) ENGINE=InnoDB;
CREATE TABLE test2.t1(a INT PRIMARY KEY, b CHAR(8)) ENGINE=InnoDB;
--source include/sync_slave_sql_with_master.inc

# Set minimum_hlc_ns to a high value. Subsequent txn's should see monotonically
# increasing timestamp from this point
--source include/rpl_connection_master.inc
SET SESSION DEBUG = "+d,allow_long_hlc_drift_for_tests";
--let $saved_minimum_hlc_ns = `SELECT @@global.minimum_hlc_ns`
SET @@global.minimum_hlc_ns = 2538630000000000000; # ~2050 AD

# Enable binlog_hlc and maintain_database_hlc in both master and slave
--source include/rpl_connection_master.inc
--let $saved_master_enable_binlog_hlc = `SELECT @@global.enable_binlog_hlc`
--let $saved_master_maintain_database_hlc = `SELECT @@global.maintain_database_hlc`
SET @@global.enable_binlog_hlc = TRUE;
SET @@global.maintain_database_hlc = TRUE;
--source include/rpl_connection_slave.inc
--let $saved_slave_enable_binlog_hlc= `SELECT @@global.enable_binlog_hlc`
--let $saved_slave_maintain_database_hlc = `SELECT @@global.maintain_database_hlc`
SET @@global.enable_binlog_hlc = TRUE;
SET @@global.maintain_database_hlc = TRUE;

--echo Case 1: Single statement txn
--source include/rpl_connection_master.inc
USE test1;
INSERT INTO t1 VALUES(1, 'a');
INSERT INTO t1 VALUES(2, 'b');
--source include/show_binlog_events.inc
SELECT * FROM test1.t1;
SELECT * FROM information_schema.database_applied_hlc ORDER BY 2, 1;

--source include/sync_slave_sql_with_master.inc
--source include/rpl_connection_slave.inc
--source include/show_binlog_events.inc
SELECT * FROM test1.t1;
SELECT * FROM information_schema.database_applied_hlc ORDER BY 2, 1;

--echo Case 2: Multi statement txn
--source include/rpl_connection_master.inc
SET autocommit = OFF;

USE test1;
INSERT INTO t1 VALUES(3, 'c');
INSERT INTO t1 VALUES(4, 'd');
COMMIT;
--source include/show_binlog_events.inc
SELECT * FROM test1.t1;
SELECT * FROM information_schema.database_applied_hlc ORDER BY 2, 1;

--source include/sync_slave_sql_with_master.inc
--source include/rpl_connection_slave.inc
--source include/show_binlog_events.inc
SELECT * FROM test1.t1;
SELECT * FROM information_schema.database_applied_hlc ORDER BY 2, 1;

--echo Case 3: Multi statement txn spanning multiple database
--source include/rpl_connection_master.inc
SET autocommit = OFF;

INSERT INTO test1.t1 VALUES(5, 'e');
INSERT INTO test2.t1 VALUES(5, 'e');
COMMIT;
--source include/show_binlog_events.inc
SELECT * FROM test1.t1;
SELECT * FROM test2.t1;
SELECT * FROM information_schema.database_applied_hlc ORDER BY 2, 1;

--source include/sync_slave_sql_with_master.inc
--source include/rpl_connection_slave.inc
--source include/show_binlog_events.inc
SELECT * FROM test1.t1;
SELECT * FROM test2.t1;
SELECT * FROM information_schema.database_applied_hlc ORDER BY 2, 1;

--echo Case 4: Multiple single stmt txns txn spanning multiple database
--source include/rpl_connection_master.inc
SET autocommit = OFF;

INSERT INTO test1.t1 VALUES(6, 'f');
COMMIT;

INSERT INTO test2.t1 VALUES(8, 'g');
COMMIT;

--source include/show_binlog_events.inc
SELECT * FROM test1.t1;
SELECT * FROM test2.t1;
SELECT * FROM information_schema.database_applied_hlc ORDER BY 2, 1;

--source include/sync_slave_sql_with_master.inc
--source include/rpl_connection_slave.inc
--source include/show_binlog_events.inc
SELECT * FROM test1.t1;
SELECT * FROM test2.t1;
SELECT * FROM information_schema.database_applied_hlc ORDER BY 2, 1;


--echo Case 5: Pure DDL should also update and maintain per database HLC
--source include/rpl_connection_master.inc
SET autocommit = OFF;

CREATE TABLE test1.t2(a INT);
CREATE TABLE test2.t2(a INT);
CREATE DATABASE test3;
SELECT * FROM information_schema.database_applied_hlc ORDER BY 2, 1;

--source include/sync_slave_sql_with_master.inc
--source include/rpl_connection_slave.inc
SELECT * FROM information_schema.database_applied_hlc ORDER BY 2, 1;

# Cleanup
--source include/rpl_connection_master.inc
--disable_query_log
--eval SET SESSION DEBUG = "-d,allow_long_hlc_drift_for_tests"
--eval SET GLOBAL minimum_hlc_ns = $saved_minimum_hlc_ns
--eval SET GLOBAL enable_binlog_hlc = $saved_master_enable_binlog_hlc
--eval SET GLOBAL maintain_database_hlc = $saved_master_maintain_database_hlc
--enable_query_log

DROP TABLE test1.t1;
DROP TABLE test2.t1;
DROP TABLE test1.t2;
DROP TABLE test2.t2;
DROP DATABASE test1;
DROP DATABASE test2;
DROP DATABASE test3;

--source include/sync_slave_sql_with_master.inc
--source include/rpl_connection_slave.inc
--disable_query_log
--eval SET GLOBAL enable_binlog_hlc = $saved_slave_enable_binlog_hlc
--eval SET GLOBAL maintain_database_hlc = $saved_slave_maintain_database_hlc
--enable_query_log

--source include/rpl_end.inc
