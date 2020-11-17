let $include_filename = raft_connect_leader.inc;
source include/begin_include_file.inc;

disable_query_log;

let $skip_restore_connection = 1;
let $_rpl_server = $rpl_server_count;

while ($_rpl_server) {
  let $rpl_connection_name = server_$_rpl_server;
  source include/rpl_connection.inc;

  let $role = `SELECT VARIABLE_VALUE FROM performance_schema.global_status WHERE
                  VARIABLE_NAME = "RPL_RAFT_ROLE"`;
  if ($role == 'LEADER') {
    let $_leader_server = $_rpl_server;
  }
  dec $_rpl_server;
}

let $rpl_connection_name = server_$_leader_server;
source include/rpl_connection.inc;

enable_query_log;

source include/end_include_file.inc;
