# Setup
# Set minimum_hlc_ns to a high value. Subsequent txn's should see monotonically
# increasing timestamp from this point
connection master;
SET SESSION DEBUG="+d,allow_long_hlc_drift_for_tests";
SET @@global.minimum_hlc_ns = 2538630000000000000; # ~2050 AD

# Enable binlog_hlc in both master and slave
connection master;
SET @@global.enable_binlog_hlc = true;
connection slave;
SET @@global.enable_binlog_hlc = true;

# Generate some transactions
connection master;
use test;

create table t1 (a int primary key, b char(8));
insert into t1 values(1, 'a');
insert into t1 values(2, 'b');
--source include/show_binlog_events.inc

connection slave;
--source include/sync_slave_sql_with_master.inc
--source include/show_binlog_events.inc
--source include/show_relaylog_events.inc

--echo Case 1: Flush logs in master so that a new binlog file is generated with
--echo prev_hlc time in metadata event
--echo
connection master;
flush logs;
insert into t1 values(3, 'c');

--let $binlog_file= LAST
--source include/show_binlog_events.inc

connection slave;
--source include/sync_slave_sql_with_master.inc
--source include/show_binlog_events.inc

--echo Case 2: Flush logs in slave so that a new binlog file is generated with
--echo prev_hlc time in metadata event
--echo
connection slave;
flush logs;

connection master;
insert into t1 values(4, 'd');

--source include/show_binlog_events.inc

connection slave;
--source include/sync_slave_sql_with_master.inc
--source include/show_binlog_events.inc
--source include/show_relaylog_events.inc

# Cleanup
connection master;
use test;
drop table if exists t1;
SET SESSION DEBUG = "+d,reset_hlc_for_tests";
SET @@global.minimum_hlc_ns = default;
SET SESSION DEBUG = "-d,reset_hlc_for_tests";
SET @@global.enable_binlog_hlc = default;

connection slave;
--source include/sync_slave_sql_with_master.inc
SET @@global.enable_binlog_hlc = default;

connection master;
source include/rpl_end.inc;
