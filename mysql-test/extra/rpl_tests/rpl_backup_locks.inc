########################################################################
# Replication-related tests for backup locks
#
# Variables to control test execution:
#   $engine - specifies additional transactional storage engine to test
########################################################################

--source include/have_myisam.inc
--source include/master-slave.inc

eval CREATE TABLE t_$engine (a INT) ENGINE=$engine;
CREATE TABLE t_myisam (a INT) ENGINE=MyISAM;

--source include/sync_slave_sql_with_master.inc

--source include/stop_slave.inc

--connection master
--echo # connection master

eval INSERT INTO t_$engine VALUES (0);

--connection slave
--echo # connection slave

LOCK TABLES FOR BACKUP;

--source include/start_slave.inc

--connection master
--source include/sync_slave_sql_with_master.inc

UNLOCK TABLES;

--source include/stop_slave.inc

--connection master
--echo # connection master

INSERT INTO t_myisam VALUES (0);

--connection slave
--echo # connection slave

--let $master_log_pos= query_get_value(SHOW SLAVE STATUS, Exec_Master_Log_Pos, 1)

LOCK TABLES FOR BACKUP;

--source include/start_slave.inc

let $wait_condition=
    SELECT COUNT(*) = 1 FROM INFORMATION_SCHEMA.PROCESSLIST
    WHERE STATE = "Waiting for table backup lock" AND
    (INFO = "INSERT INTO t_myisam VALUES (0)" OR INFO IS NULL);
--source include/wait_condition.inc

UNLOCK TABLES;

--connection master
--echo # connection master

############################################################################
# PS-4758: A sequence of LOCK TABLES FOR BACKUP and STOP SLAVE SQL_THREAD
#          can cause replication to be blocked and cannot be restarted
#          normally
############################################################################

--connection slave
--echo # connection slave

LOCK TABLES FOR BACKUP;

--connection master
--echo # connection master

INSERT INTO t_myisam VALUES (0);

--connection slave
--echo # connection slave

let $wait_condition=
    SELECT COUNT(*) = 1 FROM INFORMATION_SCHEMA.PROCESSLIST
    WHERE STATE = "Waiting for table backup lock" AND
    (INFO = "INSERT INTO t_myisam VALUES (0)" OR INFO IS NULL);
--source include/wait_condition.inc

--error ER_LOCK_OR_ACTIVE_TRANSACTION
STOP SLAVE SQL_THREAD;
UNLOCK TABLES;

--connection master
--echo # connection master

--source include/sync_slave_sql_with_master.inc

--connection slave
--echo # connection slave

LOCK INSTANCE FOR BACKUP;

--connection master
--echo # connection master

CREATE TABLE t (a INT);

--connection slave
--echo # connection slave

let $wait_condition=
    SELECT COUNT(*) = 1 FROM INFORMATION_SCHEMA.PROCESSLIST
    WHERE STATE = "Waiting for backup lock" AND
    (INFO = "CREATE TABLE t (a INT)" OR INFO IS NULL);
--source include/wait_condition.inc

--error ER_LOCK_OR_ACTIVE_TRANSACTION
STOP SLAVE SQL_THREAD;
UNLOCK INSTANCE;

--connection master
--echo # connection master

DROP TABLE t;
eval DROP TABLE t_$engine, t_myisam;
--source include/sync_slave_sql_with_master.inc

--source include/rpl_end.inc
