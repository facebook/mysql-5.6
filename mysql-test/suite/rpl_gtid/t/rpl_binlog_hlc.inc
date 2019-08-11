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

--echo Case 1: Single statement txn. Commit ts needs to be updated at cmt time
connection master;
use test;

create table t1 (a int primary key, b char(8)) engine=InnoDB;
insert into t1 values(1, 'a');
insert into t1 values(2, 'b');
--source include/show_binlog_events.inc

--source include/sync_slave_sql_with_master.inc
connection slave;
--source include/show_binlog_events.inc

--echo Case 2: Enable binlog_hlc in master and disable it in slave
connection master;
SET @@global.enable_binlog_hlc = true;
connection slave;
SET @@global.enable_binlog_hlc = false;

connection master;
insert into t1 values(6, 'f');
insert into t1 values(7, 'g');

--source include/show_binlog_events.inc

--source include/sync_slave_sql_with_master.inc
connection slave;
--source include/show_binlog_events.inc

--echo Case 3: Disable binlog_hlc in master and enable it in slave
connection master;
SET @@global.enable_binlog_hlc = false;
connection slave;
SET @@global.enable_binlog_hlc = true;

connection master;
insert into t1 values(8, 'h');
insert into t1 values(9, 'i');

--source include/show_binlog_events.inc

--source include/sync_slave_sql_with_master.inc
connection slave;
--source include/show_binlog_events.inc

--echo Case 4: Enable binlog_hlc in master again
connection master;
SET @@global.enable_binlog_hlc = true;

connection master;
insert into t1 values(10, 'j');
insert into t1 values(11, 'k');

--source include/show_binlog_events.inc

--source include/sync_slave_sql_with_master.inc
connection slave;
--source include/show_binlog_events.inc

--echo Case 5: Setting a lower HLC is a no-op. HLC should continue to increase
--echo monotonically
connection master;
SET @@global.enable_binlog_hlc = true;
SET SESSION DEBUG="+d,allow_long_hlc_drift_for_tests";
SET @@global.minimum_hlc_ns = 1907478000000000000; # ~2030 AD

connection slave;
SET @@global.enable_binlog_hlc = true;

connection master;
insert into t1 values(12, 'j');
insert into t1 values(13, 'k');

--source include/show_binlog_events.inc

--source include/sync_slave_sql_with_master.inc
connection slave;
--source include/show_binlog_events.inc

# Cleanup
connection master;
drop table if exists t1;
SET @@global.minimum_hlc_ns = default;
SET @@global.enable_binlog_hlc = default;

--source include/sync_slave_sql_with_master.inc
connection slave;
SET @@global.enable_binlog_hlc = default;

connection master;
source include/rpl_end.inc;
