# Promotes the server specified in $rpl_raft_leader_number to leader

let $include_filename = raft_promote_to_leader.inc;
source include/begin_include_file.inc;

disable_query_log;

let $rpl_connection_name = server_$rpl_raft_leader_number;
source include/rpl_connection.inc;
let $uuid =
  `SELECT VARIABLE_VALUE
   FROM performance_schema.global_status
   WHERE VARIABLE_NAME = "Rpl_raft_peer_uuid"`;

# Connect to current leader and start election
source raft_connect_leader.inc;
let $current_leader_uuid =
  `SELECT VARIABLE_VALUE
   FROM performance_schema.global_status
   WHERE VARIABLE_NAME = "Rpl_raft_peer_uuid"`;
if ($current_leader_uuid != $uuid) {
  eval SET @@GLOBAL.RPL_RAFT_NEW_LEADER_UUID = "$uuid";
}

# Wait for leader to become writable
let $rpl_connection_name = server_$rpl_raft_leader_number;
source include/rpl_connection.inc;
let $wait_condition = SELECT @@GLOBAL.READ_ONLY = 0 AND @@GLOBAL.SUPER_READ_ONLY = 0;
source include/wait_condition.inc;

# Wait for SQL threads to start on followers
let $_rpl_server = $rpl_server_count;
while ($_rpl_server) {
  if ($_rpl_server != $rpl_raft_leader_number) {
    let $rpl_connection_name = server_$_rpl_server;
    source include/rpl_connection.inc;
    let $_slave_check_configured = query_get_value("SHOW REPLICA STATUS", Replica_SQL_Running, 1);
    let $max_sleep = 300;
    while ($_slave_check_configured == 'No such row') {
      let $_slave_check_configured = query_get_value("SHOW REPLICA STATUS", Replica_SQL_Running, 1);
      sleep 1;
      let $max_sleep = $max_sleep - 1;
      if ($max_sleep == 0) {
        die "Timeout while waiting for SQL thread to start on $_rpl_server";
      }
    }
    source include/wait_for_slave_sql_to_start.inc;
  }
  dec $_rpl_server;
}

if ($rpl_debug) {
  let $rpl_connection_name = server_$rpl_raft_leader_number;
  source include/rpl_connection.inc;
  SHOW STATUS LIKE 'Rpl_raft_role';
  SELECT @@GLOBAL.READ_ONLY;
  SELECT @@GLOBAL.SUPER_READ_ONLY;
}

enable_query_log;

source include/end_include_file.inc;
