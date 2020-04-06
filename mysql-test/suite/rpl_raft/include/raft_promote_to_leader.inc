# Promotes the server specified in $rpl_raft_leader_number to leader

let $include_filename= raft_promote_to_leader.inc;
source include/begin_include_file.inc;

disable_query_log;

# get uuid of the peer which needs to be promoted
let $rpl_connection_name= server_$rpl_raft_leader_number;
source include/rpl_connection.inc;
let $uuid= `SELECT VARIABLE_VALUE FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = "Rpl_raft_peer_uuid"`;

# connect to current leader and start election;
source raft_connect_leader.inc;
eval SET @@GLOBAL.RPL_RAFT_NEW_LEADER_UUID="$uuid";

enable_query_log;

source include/end_include_file.inc;
