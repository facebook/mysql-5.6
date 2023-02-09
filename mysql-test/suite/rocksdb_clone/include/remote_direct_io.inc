--let restart_parameters="restart: $clone_donor_params"
--source include/restart_mysqld.inc

SELECT @@rocksdb_use_direct_reads,
       @@rocksdb_use_direct_io_for_flush_and_compaction;

--connection clone_conn_1

--let restart_parameters="restart: $clone_client_params"
--source include/restart_mysqld.inc

--echo Client:
SELECT @@rocksdb_use_direct_reads,
       @@rocksdb_use_direct_io_for_flush_and_compaction;

--let $HOST=127.0.0.1
--let $PORT=$SERVER_PORT_1
--let $USER=root
--let remote_clone=1
--source ../mysql-test/suite/clone/include/clone_command.inc

--replace_result $CLONE_DATADIR CLONE_DATADIR
--let restart_parameters="restart: --datadir=$CLONE_DATADIR $clone_client_params"
# --let $mysqld_name=mysqld.2
--source include/restart_mysqld.inc

SELECT * FROM t1 ORDER BY col1;

--let restart_parameters="restart: $clone_client_params"
# --let $mysqld_name=mysqld.2
--source include/restart_mysqld.inc

--force-rmdir $CLONE_DATADIR

--connection default
