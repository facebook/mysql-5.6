--source include/have_rocksdb.inc

--let $EXTRA_DIR_ROOT=$MYSQL_TMP_DIR/extra_dir

--replace_result $CLONE_PLUGIN CLONE_PLUGIN
--eval INSTALL PLUGIN clone SONAME '$CLONE_PLUGIN'

CREATE TABLE t1(col1 INT PRIMARY KEY, col2 CHAR(64)) ENGINE=ROCKSDB;

INSERT INTO t1 VALUES (10, 'clone row 1');

--echo 1. Clone to an instance with non-default rocksdb_wal_dir

--source ../../clone/include/clone_connection_begin.inc

--let $CLONE_DATADIR=$EXTRA_DIR_ROOT/data_new
--let $CLONE_WALDIR=$EXTRA_DIR_ROOT/rdb_wal

--connection clone_conn_1
--source ../mysql-test/suite/clone/include/clone_command.inc

--source ../../clone/include/clone_connection_end.inc
--connection default
--replace_result $MYSQL_TMP_DIR MYSQL_TMP_DIR
--let restart_parameters="restart:--datadir=$CLONE_DATADIR --rocksdb-wal-dir=$CLONE_WALDIR $extra_restart_params"
--source include/restart_mysqld.inc

--connection default

--replace_result $MYSQL_TMP_DIR MYSQL_TMP_DIR
SELECT @@rocksdb_wal_dir;

SELECT * FROM t1 ORDER BY col1;

--echo 2. Clone between two instances with non-default rocksdb_wal_dir

--source ../../clone/include/clone_connection_begin.inc

--let $CLONE_DATADIR=$EXTRA_DIR_ROOT/data_new2
--let $CLONE_WALDIR=$EXTRA_DIR_ROOT/rdb_wal2

--connection clone_conn_1
--source ../mysql-test/suite/clone/include/clone_command.inc

--source ../../clone/include/clone_connection_end.inc
--connection default
--replace_result $MYSQL_TMP_DIR MYSQL_TMP_DIR
--let restart_parameters="restart:--datadir=$CLONE_DATADIR --rocksdb-wal-dir=$CLONE_WALDIR $extra_restart_params"
--source include/restart_mysqld.inc

--connection default

--let restart_parameters="restart:"

--replace_result $MYSQL_TMP_DIR MYSQL_TMP_DIR
SELECT @@rocksdb_wal_dir;

SELECT * FROM t1 ORDER BY col1;

--echo 3. Clone from an instance with non-default rocksdb_wal_dir

--source ../../clone/include/clone_connection_begin.inc

--let $CLONE_DATADIR=$EXTRA_DIR_ROOT/data_new3
--let $CLONE_WALDIR=

--connection clone_conn_1
--source ../mysql-test/suite/clone/include/clone_command.inc

--source ../../clone/include/clone_connection_end.inc
--connection default
--replace_result $MYSQL_TMP_DIR MYSQL_TMP_DIR
--let restart_parameters="restart:--datadir=$CLONE_DATADIR $extra_restart_params"
--source include/restart_mysqld.inc

--replace_result $MYSQL_TMP_DIR MYSQL_TMP_DIR
SELECT @@rocksdb_wal_dir;

SELECT * FROM t1 ORDER BY col1;

--echo 4. Clone with rocksdb_wal_dir == rocksdb_data_dir

--source ../../clone/include/clone_connection_begin.inc

--let $CLONE_DATADIR=$EXTRA_DIR_ROOT/data_new4

--connection clone_conn_1
--source ../mysql-test/suite/clone/include/clone_command.inc

--source ../../clone/include/clone_connection_end.inc
--connection default
--replace_result $MYSQL_TMP_DIR MYSQL_TMP_DIR
--let restart_parameters="restart:--datadir=$CLONE_DATADIR --rocksdb-datadir=.rocksdb --rocksdb-wal-dir=.rocksdb $extra_restart_params"
--source include/restart_mysqld.inc

--let $assert_text = rocksdb_datadir must be identical to rocksdb_wal_dir
--let $assert_cond = @@rocksdb_datadir = @@rocksdb_wal_dir
--source include/assert.inc

SELECT * FROM t1 ORDER BY col1;

--let $restart_parameters=
--source include/restart_mysqld.inc

DROP TABLE t1;

UNINSTALL PLUGIN clone;

--force-rmdir $EXTRA_DIR_ROOT
