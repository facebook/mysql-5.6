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

--echo Commit some trxs to move HLC forward
connection master;
use test;

create table t1 (a int primary key, b char(8)) engine=InnoDB;
insert into t1 values(1, 'a');
insert into t1 values(2, 'b');

connection slave;
--source include/sync_slave_sql_with_master.inc

--echo Now start a trx and roll it back
connection master;

begin;
insert into t1 values(3, 'c');
insert into t1 values(4, 'd');
rollback;

select * from t1;
--source include/show_binlog_events.inc

connection slave;
--source include/sync_slave_sql_with_master.inc
--source include/show_binlog_events.inc

--echo Now disable binlog HLC and start a new trx. The new trx should not
--echo generate HLC and should not corrupt binlog
connection master;
SET @@global.enable_binlog_hlc = false;

insert into t1 values(5, 'e');
insert into t1 values(6, 'f');

select * from t1;
--source include/show_binlog_events.inc

connection slave;
--source include/sync_slave_sql_with_master.inc
select * from t1;
--source include/show_binlog_events.inc

# Cleanup
connection master;
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
