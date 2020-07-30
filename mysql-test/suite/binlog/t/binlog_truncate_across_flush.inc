#
# verify that binlog truncation to match engine position works correctly across
# binlog file rotation. On binlog file rotation which is followed by a crash
# before committing new trx to engine, we should trim everything from the latest
# binlog file. Without this, gtid_executed could show additional trxs that were
# not committed to engine
#
# For example:
# 1. To start with master-bin.000001 is the binlog file which gets rotated
# 2. New trxs are started andmysqld crashes after flushing the trxs to binlog,
# but before committing to engine
# 3. On recovery wngine will say that the binlog file it last saw was
# master-bin.000001. The current binlog file being recovered is
# master-bin.000002 which should be truncated to the beginin of the first gtid
# event in this file
#
--source include/not_valgrind.inc
--source include/have_log_bin.inc
--source include/have_debug.inc

CALL mtr.add_suppression("Taking backup from .*");

CREATE TABLE t1(c1 INT);
CREATE TABLE blackhole (c1 INT PRIMARY KEY) ENGINE=BLACKHOLE;

INSERT INTO t1 VALUES(1);
INSERT INTO blackhole VALUES(1);
COMMIT;

INSERT INTO t1 VALUES(2);
COMMIT;

--let $first_binlog_file= query_get_value(SHOW MASTER STATUS, File, 1)

# Flush logs to rotate binlog and make trx durable in engine
FLUSH LOGS;

--exec echo "wait" > $MYSQLTEST_VARDIR/tmp/mysqld.1.expect
--echo # Crash right after flushing binary log

--let $second_binlog_file= query_get_value(SHOW MASTER STATUS, File, 1)
SET SESSION DEBUG="+d,crash_after_flush_binlog";
BEGIN;
INSERT INTO t1 VALUES(3);
--error CR_SERVER_LOST
COMMIT;
--source include/wait_until_disconnected.inc

--enable_reconnect
--echo # Restart the master server
--exec echo "restart" > $MYSQLTEST_VARDIR/tmp/mysqld.1.expect
--source include/wait_until_connected_again.inc
--disable_reconnect

--echo #
--echo # Verify that a transaction cannot be recovered during server
--echo # recovery from a crash, which happened after flushing it
--echo # to binary log. This is because the transaction is still marked
--echo # as prepared in engine and will be rollbacked when
--echo # trim-binlog-to-recover is set
--echo #
--let $assert_text= There should be 2 rows in table t1
--let $assert_cond= [SELECT COUNT(*) FROM t1] = 2
--source include/assert.inc

--let $third_binlog_file= query_get_value(SHOW MASTER STATUS, File, 1)

INSERT INTO t1 VALUES(4);
COMMIT;
FLUSH ENGINE LOGS;

--echo #
--echo # verify that the latest binlog file is trimmed to the starting position
--echo # of the first gtid event
--echo #
--let $binlog_file= $first_binlog_file
--source include/show_binlog_events.inc

--let $binlog_file= $second_binlog_file
--source include/show_binlog_events.inc

--let $binlog_file= $third_binlog_file
--source include/show_binlog_events.inc

--exec echo "wait" > $MYSQLTEST_VARDIR/tmp/mysqld.1.expect
--echo # Crash right after flushing binary log

# Now write to blackhole table and simulate a crash
SET SESSION DEBUG="+d,crash_after_flush_binlog";
BEGIN;
INSERT INTO blackhole VALUES(2);
--error CR_SERVER_LOST
COMMIT;
--source include/wait_until_disconnected.inc

--enable_reconnect
--echo # Restart the master server
--exec echo "restart" > $MYSQLTEST_VARDIR/tmp/mysqld.1.expect
--source include/wait_until_connected_again.inc
--disable_reconnect

INSERT INTO t1 VALUES(5);
COMMIT;
FLUSH ENGINE LOGS;

--let $assert_text= There should be 4 rows in table t1
--let $assert_cond= [SELECT COUNT(*) FROM t1] = 4
--source include/assert.inc

--echo #
--echo # verify that the latest binlog file is trimmed to the starting position
--echo # of the first gtid event
--echo #
--let $binlog_file= $third_binlog_file
--source include/show_binlog_events.inc

--let $fourth_binlog_file= query_get_value(SHOW MASTER STATUS, File, 1)
--let $binlog_file= $fourth_binlog_file
--source include/show_binlog_events.inc

# Cleanup
DROP TABLE t1;
DROP TABLE blackhole;
