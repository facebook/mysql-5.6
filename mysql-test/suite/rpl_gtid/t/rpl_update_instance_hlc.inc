# Setup
# Set minimum_hlc_ns to a high value. Subsequent txn's should see monotonically
# increasing timestamp from this point
connection master;
SET SESSION DEBUG="+d,allow_long_hlc_drift_for_tests";
SET @@global.minimum_hlc_ns = 2538630000000000000; # ~2050 AD

# Generate some transactions
connection master;
use test;
create table t1 (a int primary key, b char(8));
insert into t1 values(1, 'a');
insert into t1 values(2, 'b');
--source include/show_binlog_events.inc
--source include/sync_slave_sql_with_master.inc

connection slave;
--source include/show_binlog_events.inc

# Restarting master will rotate master's binlog file. The new binlog file should
# have previous hlc updated correctly based on the hlc timestamps of all
# previous trx. New trxs in the master should get monotonically increasing
# timestamp
--echo Case 1: Stop slave and restart master. HLC monotonicity should not break
connection slave;
--source include/stop_slave.inc
connection master;
--let $rpl_server_number=1
source include/rpl_restart_server.inc;

# Generate some more trx in master. Should get monotonically increasing HLC
# timestamps
connection master;
insert into t1 values(3, 'c');
insert into t1 values(4, 'd');
insert into t1 values(5, 'e');

--let $binlog_file= LAST
--source include/show_binlog_events.inc

# Start slave and verify that HLC timestamps are preserved
connection slave;
--source include/start_slave.inc

connection master;
--source include/sync_slave_sql_with_master.inc

connection slave;
--source include/show_binlog_events.inc

--echo Case 2: Restart slave. HLC monotonicity should not break
connection slave;
--source include/stop_slave.inc
--let $rpl_server_number=2
source include/rpl_restart_server.inc;
connection slave;
--source include/start_slave.inc

# Generate some more trx in master. Should get monotonically increasing
# timestamp
connection master;
--source include/sync_slave_sql_with_master.inc
connection master;
insert into t1 values(6, 'c');
insert into t1 values(7, 'd');
insert into t1 values(8, 'e');

--let $binlog_file= LAST
--source include/show_binlog_events.inc

--source include/sync_slave_sql_with_master.inc
connection slave;
--source include/show_binlog_events.inc

--echo Case 3: Reset master should not break HLC monotonocity
# let slave catch up and delete binlogs on master (reset master)
connection slave;
--source include/stop_slave.inc
connection master;
reset master;
source include/show_binary_logs.inc;

# Generate some more trx in master. Should get monotonically increasing
# timestamp
connection master;
insert into t1 values(9, 'c');
insert into t1 values(10, 'd');
insert into t1 values(11, 'e');

--let $binlog_file= LAST
--source include/show_binlog_events.inc

# Start slave and verify that HLC timestamps are preserved
connection slave;
reset replica;
reset master;
--source include/start_slave.inc
connection master;
--source include/sync_slave_sql_with_master.inc
connection slave;
--source include/show_binlog_events.inc

# Cleanup
connection master;
use test;
drop table if exists t1;
SET @@global.minimum_hlc_ns = default;
--source include/sync_slave_sql_with_master.inc
source include/rpl_end.inc;
