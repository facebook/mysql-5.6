--let $include_filename= raft_leadership.inc
--source include/begin_include_file.inc

connection server_1;
let $master_raft_uuid= `SELECT VARIABLE_VALUE FROM
  performance_schema.global_status WHERE VARIABLE_NAME = "RPL_RAFT_PEER_UUID"`;
let $master_raft_role= `SELECT VARIABLE_VALUE FROM
  performance_schema.global_status WHERE VARIABLE_NAME = "RPL_RAFT_ROLE"`;

let $num_times = 0;
while ($master_raft_role != 'LEADER')
{
  # Find the current raft leader
  let $_rpl_server= $rpl_server_count;
  while ($_rpl_server)
  {
    let $rpl_connection_name= server_$_rpl_server;
    source include/rpl_connection.inc;
    let $_is_raft_leader= `SELECT VARIABLE_VALUE FROM
      performance_schema.global_status WHERE VARIABLE_NAME = "RPL_RAFT_ROLE"`;
    if ($_is_raft_leader == 'LEADER')
    {
      if ($num_times == 0)
      {
        --disable_query_log
        --disable_abort_on_error
        eval SET @@global.rpl_raft_new_leader_uuid= '$master_raft_uuid';
        --enable_abort_on_error
        --enable_query_log
        inc $num_times;
      }
    }
    dec $_rpl_server;
  }

  connection server_1;
  let $master_raft_role= `SELECT VARIABLE_VALUE FROM
  performance_schema.global_status WHERE VARIABLE_NAME = "RPL_RAFT_ROLE"`;
}

while (`SELECT @@GLOBAL.READ_ONLY`) {
  sleep 1;
}

if ($rpl_debug)
{
  --echo "Raft leadership transferred to true master"
}
--source include/end_include_file.inc
