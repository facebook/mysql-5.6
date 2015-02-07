# This .inc file generates innodb load on master, deletes a binary
# log on slave and tests the functionality of slave_gtid_info
# table i.e; checks the missing binlog transactions are logged
# again without committing in InnoDB. Finally checks the validity of
# binlogs by replaying them on slave and comparing checksums of
# master and slave databases.

-- source include/have_gtid.inc
-- source include/master-slave.inc
-- source include/not_valgrind.inc

call mtr.add_suppression("Recovery from master pos");

-- connection master
-- let $MASTER_UUID = `SELECT @@SERVER_UUID;`
-- let $skip_cleanup=1
-- source extra/rpl_tests/rpl_parallel_load_innodb.test

-- let $MYSQLD_DATADIR = `select @@datadir`
-- let $slave_binlog_file = query_get_value("SHOW MASTER STATUS", "File", 1)
-- remove_file $MYSQLD_DATADIR/$slave_binlog_file

-- let $sed = /$slave_binlog_file/,\$d

-- exec sed -i '$sed' $MYSQLD_DATADIR/slave-bin.index

-- let $rpl_server_number = 2
-- let $rpl_start_with_gtids = 1
-- source include/rpl_restart_server.inc

change master to master_auto_position=1;
-- source include/start_slave.inc

connection master;
-- let $use_gtids=1
-- source include/sync_slave_sql_with_master.inc

# Test slave has no missing binlog events by first resetting slave and
# taking backup of slave binlogs and re-executing the binlogs.
-- exec $MYSQL_BINLOG $MYSQLD_DATADIR/slave-bin.0* > $MYSQLTEST_VARDIR/tmp/fulldump.sql
reset master;
-- exec $MYSQL --user=root --host=127.0.0.1 --port=$SLAVE_MYPORT < $MYSQLTEST_VARDIR/tmp/fulldump.sql
-- remove_file $MYSQLTEST_VARDIR/tmp/fulldump.sql

# This consistency check verifies slave doesn't have missing binlog events.
-- let $include_silent=1
-- source extra/rpl_tests/rpl_parallel_load_innodb_consistency.test
-- source extra/rpl_tests/rpl_parallel_load_innodb_cleanup.test
-- connection master
-- let $include_silent =

-- let $use_gtids=0
-- source include/rpl_end.inc
