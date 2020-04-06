let $include_filename= raft_connect_leader.inc;
source include/begin_include_file.inc;

disable_query_log;

let $_rpl_server= $rpl_server_count;
while ($_rpl_server)
{
  let $is_leader= `SELECT VARIABLE_VALUE='LEADER' FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = "RPL_RAFT_ROLE"`;
  if ($is_leader)
  {
    let $rpl_connection_name= server_$_rpl_server;
    source include/rpl_connection.inc;
  }
  dec $_rpl_server;
}

enable_query_log;

source include/end_include_file.inc;
