--source include/count_sessions.inc

--echo # In connection con1 - Running Insert Random [100 - 200 Key range]
connect (con1,localhost,root,,);
--send call execute_dml(0, 100, 100, 100, 20, 1)

--echo # In connection clone_conn_1 - Cloning database
--source ../../clone/include/clone_connection_begin.inc
--connection clone_conn_1
--source ../../clone/include/clone_command.inc

--echo # In connection con1 - Waiting
connection con1;
--reap

disconnect con1;

--echo # In connection default
connection default;
--source ../../clone/include/clone_connection_end.inc

--source include/wait_until_count_sessions.inc

--echo # Restart cloned database
--replace_result $CLONE_DATADIR CLONE_DATADIR
--let restart_parameters="restart: --datadir=$CLONE_DATADIR"
--source include/restart_mysqld.inc

SHOW CREATE TABLE t1;

--let restart_parameters="restart:"
--source include/restart_mysqld.inc

TRUNCATE TABLE t1;

--force-rmdir $CLONE_DATADIR
