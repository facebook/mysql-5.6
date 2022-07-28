# ==== Purpose ====
#
# Configure three servers in mysql raft with a single leader
# Connection server_1 and server_1_1 will be for the initial leader
#
# ==== Usage ====
#
#   [--let $rpl_server_count= N]
#   [--let $rpl_check_server_ids= 1]
#   [--let $rpl_skip_reset_master_and_slave= 1]
#   [--let $rpl_skip_change_master= 1]
#   [--let $rpl_skip_start_slave= 1]
#   [--let $rpl_debug= 1]
#   [--let $slave_timeout= NUMBER]
#   [--let $use_gtids= 1]
#   [--let $rpl_gtid_utils= 1]
#   --source include/raft_3_node.inc
#
# Parameters:
#   $rpl_check_server_ids, $rpl_skip_reset_master_and_slave,
#   $rpl_skip_change_master, $rpl_skip_start_slave, $rpl_debug,
#   $slave_timeout, $use_gtids, $rpl_gtid_utils
#     See include/rpl_init.inc
#
#   $rpl_server_count
#     By default, three servers are configured. You can configure more
#     servers (servers 4, 5, etc are neither masters nor slaves) by
#     setting this variable. See also include/rpl_init.inc


--let $include_filename= raft_3_node.inc
if ($rpl_server_count)
{
  --let $include_filename= raft_3_node.inc [rpl_server_count=$rpl_server_count]
}
--source include/begin_include_file.inc


--let $rpl_enable_raft= 1
# In this test suite leader can move around
--let $rpl_raft_mobile_leader= 1
--let $rpl_topology= 1->2,1->3
--source include/rpl_init.inc

# Some places in mtr require the following connections:
# master
# master1
# slave
# slave1

--let $rpl_connection_name= master
--let $rpl_server_number= 1
--source include/rpl_connect.inc

--let $rpl_connection_name= master1
--let $rpl_server_number= 1
--source include/rpl_connect.inc

--let $rpl_connection_name= slave
--let $rpl_server_number= 2
--source include/rpl_connect.inc

--let $rpl_connection_name= slave1
--let $rpl_server_number= 2
--source include/rpl_connect.inc

# Set this variable to include metadata event in show_binlog_events.inc
# See: show_events.inc
let $show_metadata_event= 1;

# Set this variable to not decrement the binlog file name by 1 in
# show_binlog_events.inc
# See: show_events.inc
let $dont_decrement_filename= 1;

--let $include_filename= raft_3_node.inc
--source include/end_include_file.inc

# Set the default connection to 'master'. Do this after
# end_include_file.inc, so that it gets printed to the query log.
--let $rpl_connection_name= master
--source include/rpl_connection.inc

# Create connections to server 4 and 5 (these are not in the ring)
let $rpl_server_number= 4;
let $rpl_connection_name= server_4;
source include/rpl_connect.inc;

let $rpl_server_number= 5;
let $rpl_connection_name= server_5;
source include/rpl_connect.inc;

connection server_1;
show status like 'rpl_raft_role';

connection server_2;
show status like 'rpl_raft_role';
let $exec_gtid_set= query_get_value(SHOW MASTER STATUS, Executed_Gtid_Set, 1);
reset master;
--disable_query_log
eval set global gtid_purged='$exec_gtid_set';
--enable_query_log

connection server_3;
show status like 'rpl_raft_role';
let $exec_gtid_set= query_get_value(SHOW MASTER STATUS, Executed_Gtid_Set, 1);
reset master;
--disable_query_log
eval set global gtid_purged='$exec_gtid_set';
--enable_query_log

connection server_4;
let $exec_gtid_set= query_get_value(SHOW MASTER STATUS, Executed_Gtid_Set, 1);
--disable_query_log
reset master;
eval set global gtid_purged='$exec_gtid_set';
--enable_query_log

connection server_5;
let $exec_gtid_set= query_get_value(SHOW MASTER STATUS, Executed_Gtid_Set, 1);
--disable_query_log
reset master;
eval set global gtid_purged='$exec_gtid_set';
--enable_query_log

# switch back to server_1
connection server_1;

--disable_result_log
--source include/force_restart.inc
--enable_result_log
