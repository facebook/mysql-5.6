# Verify that HLC continues to increase monotonically after server crash
# recovery

# Set deterministic hlc for the tests. Should increase monotonically from here
set session debug="+d,allow_long_hlc_drift_for_tests";
set @@global.minimum_hlc_ns = 2538630000000000000; # ~2050 AD

-- echo Generate some trx to move HLC forward
create table t1(a int primary key, b char(8)) engine=InnoDB;
insert into t1 values(1, 'a');
insert into t1 values(2, 'b');
insert into t1 values(3, 'c');

let $binlog_file= query_get_value(SHOW MASTER STATUS, File, 1);
--source include/show_binlog_events.inc

-- echo Crash and restart the master
--exec echo "wait" > $MYSQLTEST_VARDIR/tmp/mysqld.1.expect
SET SESSION DEBUG="+d,crash_after_flush_engine_log";
begin;
insert into t1 values(4, 'd');
# 2013 - CR_SERVER_LOST
--error 2013
commit;
--source include/wait_until_disconnected.inc

--enable_reconnect
--echo # Restart the master server
--exec echo "restart" > $MYSQLTEST_VARDIR/tmp/mysqld.1.expect
--source include/wait_until_connected_again.inc
--disable_reconnect

-- echo Generate some trx on the restarted, recovered master.
-- echo HLC should continue to increase monotonically.
insert into t1 values(4, 'd');
insert into t1 values(5, 'e');
insert into t1 values(6, 'f');

select * from t1;

let $binlog_file= query_get_value(SHOW MASTER STATUS, File, 1);
--source include/show_binlog_events.inc

# Cleanup
drop table t1;
SET SESSION DEBUG = "+d,reset_hlc_for_tests";
set @@global.minimum_hlc_ns = default;
SET SESSION DEBUG = "-d,reset_hlc_for_tests";
