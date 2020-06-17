# ==== Purpose ====
#
# Promote mysql master to raft leader
#
let $include_filename= rpl_raft_make_master_leader.inc;
source include/begin_include_file.inc;

if ($rpl_enable_raft) {
  let $_master= 0;
  let $_leader= 0;
  let $_master_uuid= '';

  while ($_leader == 0) {
    let $_rpl_server= $rpl_server_count;

    while ($_rpl_server) {
      let $rpl_connection_name= server_$_rpl_server;
      if ($_rpl_server == 1) {
        let $rpl_connection_name= master;
      }
      if ($_rpl_server == 2) {
        let $rpl_connection_name= slave;
      }
      source include/rpl_connection.inc;

      let $_is_master= `SELECT SUBSTRING('$rpl_master_list', 1 + ($_rpl_server - 1) * $rpl_server_count_length, $rpl_server_count_length) = ''`;
      let $_is_leader= `SELECT VARIABLE_VALUE = 'LEADER' FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'RPL_RAFT_ROLE'`;

      if ($_is_master) {
        let $_master= $_rpl_server;
        let $_master_uuid = `SELECT VARIABLE_VALUE FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'RPL_RAFT_PEER_UUID'`;
      }
      if ($_is_leader) {
        let $_leader= $_rpl_server;
      }

      dec $_rpl_server;
    }
  }

  if ($rpl_debug) {
    echo "Leader: server_$_leader, Master: server_$_master";
  }

  if ($_master != $_leader) {
    if ($rpl_debug) {
      echo "Starting election";
    }
    let $rpl_connection_name= server_$_leader;
    if ($_leader == 1) {
      let $rpl_connection_name= master;
    }
    if ($_leader == 2) {
      let $rpl_connection_name= slave;
    }
    source include/rpl_connection.inc;
    disable_query_log;
    eval set @@global.rpl_raft_new_leader_uuid = '$_master_uuid';
    enable_query_log;
  }


  # Wait for master to be writable
  if ($rpl_debug) {
    echo "Waiting for master to be writable";
  }
  let $_leader_writeable= 0;
  let $rpl_connection_name= server_$_master;
  if ($_master == 1) {
    let $rpl_connection_name= master;
  }
  if ($_master == 2) {
    let $rpl_connection_name= slave;
  }
  source include/rpl_connection.inc;
  let $max_sleep= 300;
  while (!$_leader_writeable) {
    let $_leader_writeable= `SELECT @@GLOBAL.READ_ONLY = 0`;
    sleep 1;
    let $max_sleep= $max_sleep - 1;
    if ($max_sleep == 0) {
      die "Timeout while waiting for read_only = 0 on leader";
    }
  }

  # Wait for SQL thread to start on all followers
  let $_rpl_server= $rpl_server_count;
  while ($_rpl_server) {
    let $rpl_connection_name= server_$_rpl_server;
    if ($_rpl_server == 1) {
      let $rpl_connection_name= master;
    }
    if ($_rpl_server == 2) {
      let $rpl_connection_name= slave;
    }
    source include/rpl_connection.inc;

    let $_is_master= `SELECT SUBSTRING('$rpl_master_list', 1 + ($_rpl_server - 1) * $rpl_server_count_length, $rpl_server_count_length) = ''`;

    if (!$_is_master) {
      source include/wait_for_slave_sql_to_start.inc;
    }
    dec $_rpl_server;
  }
}

if ($rpl_debug) {
  echo "rpl_raft_make_master_leader.inc is done";
}

source include/end_include_file.inc;
