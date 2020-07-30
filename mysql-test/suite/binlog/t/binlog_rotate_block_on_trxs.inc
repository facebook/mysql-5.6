--source include/count_sessions.inc

CALL mtr.add_suppression("Timeout waiting for reply of binlog *");

SET @save.rpl_semi_sync_master_timeout = @@global.rpl_semi_sync_master_timeout;
SET @save.rpl_semi_sync_master_enabled = @@global.rpl_semi_sync_master_enabled;

--connect(conn1,localhost,root,,test)
--connect(conn2,localhost,root,,test)

--let $rpl_connection_name= default
--source include/rpl_connection.inc
CREATE TABLE t1(c1 INT PRIMARY KEY);
CREATE TABLE blackhole(c1 INT PRIMARY KEY) ENGINE=blackhole;

INSERT INTO t1 VALUES(1);
COMMIT;


--let $first_binlog_file= query_get_value(SHOW MASTER STATUS, File, 1)

-- echo # Create a 20 sec semisync timeout
SET global rpl_semi_sync_master_timeout=20000;
SET global rpl_semi_sync_master_enabled=1;

--let $rpl_connection_name= conn1
--source include/rpl_connection.inc
--send INSERT INTO blackhole VALUES (1);

--let $rpl_connection_name= conn2
--source include/rpl_connection.inc

# Rotate the binary log
--send FLUSH LOGS

# Wait until the server reaches the debug sync point while rotating the
# binary log
--let $rpl_connection_name= conn2
--source include/rpl_connection.inc
--reap

--let $rpl_connection_name= conn1
--source include/rpl_connection.inc
--reap

--let $rpl_connection_name= default
--source include/rpl_connection.inc
INSERT INTO t1 VALUES (2);

--let $second_binlog_file= query_get_value(SHOW MASTER STATUS, File, 1)

--echo # The first binlog file should contain the trx on blackhole table since
--echo # the flush was blocked by the trx. The insert (of value 2) into t1
--echo # should be in the rotated file (second binlog file)
--let $binlog_file= $first_binlog_file
--source include/show_binlog_events.inc

--let $binlog_file= $second_binlog_file
--source include/show_binlog_events.inc

# Cleanup
DROP TABLE t1;
DROP TABLE blackhole;

SET @@global.rpl_semi_sync_master_timeout = @save.rpl_semi_sync_master_timeout;
SET @@global.rpl_semi_sync_master_enabled = @save.rpl_semi_sync_master_enabled;

# Disconnect the additional connections
--disconnect conn1
--disconnect conn2
--source include/wait_until_count_sessions.inc
