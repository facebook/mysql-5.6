################################################################################
# Testing the second behind master is correct with timestamp in HB event
################################################################################

source include/not_valgrind.inc;
source include/not_parallel.inc;

# Start a new master-slave
--source include/have_binlog_format_row.inc
--source include/master-slave.inc
--source include/rpl_set_gtid_mode.inc
--source include/have_debug.inc

# Enable MTS
--connection slave
--source include/stop_slave.inc
set @save.replica_parallel_workers= @@global.replica_parallel_workers;
eval SET @@global.replica_parallel_workers= $parallel_workers;
CHANGE REPLICATION SOURCE TO SOURCE_AUTO_POSITION=1;
--source include/start_slave.inc

# Run a few queries on the master to create skip scneario
--connection master
SET GLOBAL DEBUG="+d, send_zero_hb_event";
let $databases = 4;
let $iter = 10;
--source suite/rpl/include/rpl_heartbeat_zero_timestamp_input.inc
# Make sure that the slave has caught up to the master
--source include/sync_slave_sql_with_master.inc


# Stop the slave
--connection slave
--source include/stop_slave.inc


# Create a lag on the master by running many queries
--connection master
let $databases = 4;
let $iter = 10000;

let $i = $databases;
while ($i)
{
  eval drop database test$i;
  dec $i;
}

--source suite/rpl/include/rpl_heartbeat_zero_timestamp_input.inc


# Now start slave again so that we can get some HB during skipping
--connection slave
# Create some lag
let $lagging_sec = 5;
sleep $lagging_sec;
# Set HB event interval small enough
let $old_slave_heartbeat_period= query_get_value(select heartbeat_interval from performance_schema.replication_connection_configuration, heartbeat_interval, 1);
let $new_slave_heartbeat_period= 0.1;
# Since the skiping is really small, after this, for sure we should get the event
let $skipping_guarantee_sleep= 1;
eval CHANGE REPLICATION SOURCE TO SOURCE_HEARTBEAT_PERIOD=$new_slave_heartbeat_period;
--source include/start_slave.inc

# Sleep so that we for sure get the binlog events
sleep $skipping_guarantee_sleep;
let $sbm= query_get_value("SHOW SLAVE STATUS", Seconds_Behind_Master, 1);
# Assertion
--let $assert_text = Seconds behind master should be bigger than zero after creating the lag for MTS
--let $assert_cond = $sbm > $lagging_sec;
--source include/assert.inc


# Catch up
--connection master
--source include/sync_slave_sql_with_master.inc


# clean up
--connection master
let $i = $databases;
while ($i)
{
  eval drop database test$i;
  dec $i;
}

SET GLOBAL DEBUG="-d, send_zero_hb_event";
--source include/sync_slave_sql_with_master.inc

--connection slave
--source include/stop_slave.inc
set @@global.replica_parallel_workers= @save.replica_parallel_workers;
eval CHANGE REPLICATION SOURCE TO SOURCE_HEARTBEAT_PERIOD=$old_slave_heartbeat_period;
--source include/start_slave.inc

--source include/rpl_end.inc
