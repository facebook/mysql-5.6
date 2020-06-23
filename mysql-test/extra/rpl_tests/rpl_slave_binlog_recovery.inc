# This .inc file generates innodb load on master, truncates a binary
# log on slave and checks if the missing binlog transactions are logged
# again without committing in InnoDB. Finally checks the validity of
# binlogs by replaying them on slave and comparing checksums of
# master and slave databases.

-- source include/have_gtid.inc
-- source include/master-slave.inc
-- source include/not_valgrind.inc

call mtr.add_suppression("Recovery from master pos");
call mtr.add_suppression("Error in Log_event::read_log_event()");
call mtr.add_suppression("handler error HA_ERR_KEY_NOT_FOUND");
call mtr.add_suppression("Duplicate entry");

-- connection master
-- let $MASTER_UUID = `SELECT @@SERVER_UUID;`
-- let $skip_cleanup=1
-- source extra/rpl_tests/rpl_parallel_simple_load.test

-- connection slave
-- let $MYSQLD_DATADIR = `select @@datadir`
-- let $slave_binlog_file = query_get_value("SHOW MASTER STATUS", "File", 1)
-- let $slave_binlog_size = query_get_value("SHOW MASTER STATUS", "Position", 1)
-- let $slave_log_prefix= `SELECT SUBSTRING_INDEX('$slave_binlog_file', '.', 1)`

# Truncate quarter of the binlog
-- let $three_fourth = `select ROUND($slave_binlog_size / 4 * 3)`
-- exec truncate -s $three_fourth $MYSQLD_DATADIR/$slave_binlog_file

-- let $rpl_server_number = 2
-- let $rpl_start_with_gtids = 1
-- let $rpl_force_stop = 1
-- source include/rpl_restart_server.inc

change master to master_auto_position=1, master_heartbeat_period=0.25;
-- source include/start_slave.inc

-- connection master
-- let $use_gtids=1
-- source include/sync_slave_sql_with_master.inc

# Test slave has no missing binlog events by first resetting slave and
# taking backup of slave binlogs and re-executing the binlogs.
-- exec $MYSQL_BINLOG -v -v $MYSQLD_DATADIR/$slave_log_prefix.0* > $MYSQLTEST_VARDIR/tmp/fulldump.sql
reset master;
-- exec $MYSQL --user=root --host=127.0.0.1 --port=$SLAVE_MYPORT < $MYSQLTEST_VARDIR/tmp/fulldump.sql
-- remove_file $MYSQLTEST_VARDIR/tmp/fulldump.sql

# This consistency check verifies slave doesn't have missing binlog events.
-- let $include_silent=1
-- source extra/rpl_tests/rpl_parallel_simple_load_consistency.test
-- source extra/rpl_tests/rpl_parallel_simple_load_cleanup.test
-- connection master
-- let $include_silent =

-- let $use_gtids=0
-- source include/rpl_end.inc
