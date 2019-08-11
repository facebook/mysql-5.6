# Setup
# Set minimum_hlc_ns to a high value. Subsequent txn's should see monotonically
# increasing timestamp from this point
--source include/rpl_connection_master.inc
SET @@session.debug = "+d,allow_long_hlc_drift_for_tests";
--let $master_saved_minimum_hlc_ns = `SELECT @@global.minimum_hlc_ns`
SET @@global.minimum_hlc_ns = 2538630000000000000; # ~2050 AD

# Enable binlog_hlc in both master and slave
--let $master_saved_enable_binlog_hlc = `SELECT @@global.enable_binlog_hlc`
SET @@global.enable_binlog_hlc = true;
--source include/rpl_connection_slave.inc
--let $slave_saved_minimum_hlc_ns = `SELECT @@global.minimum_hlc_ns`
--let $slave_saved_enable_binlog_hlc = `SELECT @@global.enable_binlog_hlc`
SET @@global.enable_binlog_hlc = true;

--echo Commit some trxs to move HLC forward
--source include/rpl_connection_master.inc
CREATE TABLE t1 (a INT PRIMARY KEY, b CHAR(8)) ENGINE=InnoDB;
INSERT INTO t1 VALUES (1, 'a');
INSERT INTO t1 VALUES (2, 'b');

--source include/sync_slave_sql_with_master.inc

--echo Now start a trx and roll it back
--source include/rpl_connection_master.inc
BEGIN;
INSERT INTO t1 VALUES (3, 'c');
INSERT INTO t1 VALUES (4, 'd');
ROLLBACK;

SELECT * FROM t1;
--source include/show_binlog_events.inc

--source include/sync_slave_sql_with_master.inc
--source include/show_binlog_events.inc

--echo Now disable binlog HLC and start a new trx. The new trx should not
--echo generate HLC and should not corrupt binlog
--source include/rpl_connection_master.inc
SET @@global.enable_binlog_hlc = false;

INSERT INTO t1 VALUES (5, 'e');
INSERT INTO t1 VALUES (6, 'f');

SELECT * FROM t1;
--source include/show_binlog_events.inc

--source include/sync_slave_sql_with_master.inc
SELECT * FROM t1;
--source include/show_binlog_events.inc

# Cleanup
--source include/rpl_connection_master.inc
DROP TABLE IF EXISTS t1;
SET @@session.debug = "-d,allow_long_hlc_drift_for_tests";
--eval SET @@global.minimum_hlc_ns = $master_saved_minimum_hlc_ns
--eval SET @@global.enable_binlog_hlc = $master_saved_enable_binlog_hlc
--source include/sync_slave_sql_with_master.inc
--eval SET @@global.minimum_hlc_ns = $slave_saved_minimum_hlc_ns
--eval SET @@global.enable_binlog_hlc = $slave_saved_enable_binlog_hlc

--source include/rpl_end.inc
