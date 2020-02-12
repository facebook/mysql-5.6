# ==== Purpose ====
#
# Configure three servers in mysql raft with a single leader
# Connection server1 and server1_1 will be for the initial leader
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
--let $rpl_topology= 1->2,1->3
--source include/rpl_init.inc

# rpl_end.inc requires a connection called "master"
--let $rpl_connection_name= master
--let $rpl_server_number= 1
--source include/rpl_connect.inc

--let $rpl_connection_name= server1
--let $rpl_server_number= 1
--source include/rpl_connect.inc

--let $rpl_connection_name= server1_1
--let $rpl_server_number= 1
--source include/rpl_connect.inc

--let $rpl_connection_name= server2
--let $rpl_server_number= 2
--source include/rpl_connect.inc

--let $rpl_connection_name= server2_1
--let $rpl_server_number= 2
--source include/rpl_connect.inc

--let $rpl_connection_name= server3
--let $rpl_server_number= 3
--source include/rpl_connect.inc

--let $rpl_connection_name= server3_1
--let $rpl_server_number= 3
--source include/rpl_connect.inc

# Set this variable to include metadata event in show_binlog_events.inc
# See: show_events.inc
let $show_metadata_event= 1;


--let $include_filename= raft_3_node.inc
--source include/end_include_file.inc


# Set the default connection to 'master'. Do this after
# end_include_file.inc, so that it gets printed to the query log.
--let $rpl_connection_name= master
--source include/rpl_connection.inc
