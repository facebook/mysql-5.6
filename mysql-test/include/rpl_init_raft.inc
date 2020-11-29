# In raft mode IO thread will never run, no need to check it
let $slave_io_running_check_disable= 1;

# TODO: check if the topology is supported in raft

--let $instances= "instances": "

--let $_rpl_server= $rpl_server_count
while ($_rpl_server)
{
  --source include/have_gtid.inc

  if (`SELECT COUNT(*) = 0 OR VARIABLE_VALUE = 'NONE' FROM performance_schema.global_variables WHERE VARIABLE_NAME = 'BINLOG_CHECKSUM'`)
  {
    --skip Mysql Raft cannot be enabled without binlog checksums
  }

  # Store the orginial values of these server variables as a string delimited by ':', one value for each server
  let $rpl_raft_orig_sync_binlog= `SELECT CONCAT((SELECT @@GLOBAL.SYNC_BINLOG), ':', '$rpl_raft_orig_sync_binlog')`;
  let $rpl_raft_orig_innodb_flush= `SELECT CONCAT((SELECT @@GLOBAL.INNODB_FLUSH_LOG_AT_TRX_COMMIT), ':', '$rpl_raft_orig_innodb_flush')`;
  let $rpl_raft_orig_innodb_doublewrite= `SELECT CONCAT((SELECT @@GLOBAL.INNODB_DOUBLEWRITE), ':', '$rpl_raft_orig_innodb_doublewrite')`;

  let $rpl_raft_orig_sync_binlog= `SELECT TRIM(BOTH ':' FROM '$rpl_raft_orig_sync_binlog')`;
  let $rpl_raft_orig_innodb_flush= `SELECT TRIM(BOTH ':' FROM '$rpl_raft_orig_innodb_flush')`;
  let $rpl_raft_orig_innodb_doublewrite= `SELECT TRIM(BOTH ':' FROM '$rpl_raft_orig_innodb_doublewrite')`;

  # Check if the plugin binary exists in the plugin dir
  --let $plugin_dir= `SELECT @@GLOBAL.PLUGIN_DIR`
  --file_exists $plugin_dir/mysql_raft_repl.so

  --let $rpl_connection_name= server_$_rpl_server
  --source include/rpl_connection.inc

  --let $ipv6_host_port= `SELECT CONCAT('[::1]', ':', (SELECT @@GLOBAL.PORT), ',')`
  --let $instances= `SELECT CONCAT('$instances', '$ipv6_host_port')`

  --dec $_rpl_server
}
--let $instances= `SELECT TRIM(TRAILING ',' FROM '$instances')`
--let $instances= `SELECT CONCAT('$instances', '"')`

--let $_rpl_server= $rpl_server_count
while ($_rpl_server)
{
  --let $rpl_connection_name= server_$_rpl_server
  --source include/rpl_connection.inc

  #--let $_is_slave= `SELECT SUBSTRING('$rpl_master_list', 1 + ($_rpl_server - 1) * $rpl_server_count_length, $rpl_server_count_length) != ''`

  let $fs_wal_path = `SELECT CONCAT((SELECT @@GLOBAL.RPL_RAFT_WAL_DIR), '/', (SELECT @@GLOBAL.PORT))`;

  exec rm -fr $fs_wal_path;
  exec mkdir -p $fs_wal_path;

  --let $config_json= `SELECT CONCAT('{', '$instances', '}')`

  if ($rpl_debug)
  {
    --echo raft config json: '$config_json'
  }

  --let $raft_plugin_early_installed= `SELECT COUNT(*) = 1 FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = 'RPL_RAFT'`

  # Install plugin
  if (!$raft_plugin_early_installed)
  {
    INSTALL PLUGIN RPL_RAFT SONAME 'mysql_raft_repl.so';
  }

  # Set config
  eval SET GLOBAL RPL_RAFT_CONFIG_JSON= '$config_json';

  if ($_rpl_server != $_rpl_master)
  {
    let $_slave_io_state= query_get_value(SHOW SLAVE STATUS, Slave_IO_Running, 1);
    if ($_slave_io_state!= No)
    {
      STOP SLAVE IO_THREAD;
    }
    # Disable failure detection on the slaves so that master becomes the leader
    SET GLOBAL RPL_RAFT_LEADER_FAILURE_DETECTION= OFF;
  }

  SET GLOBAL ENABLE_RAFT_PLUGIN= ON;

  # Enable raft plugin
  send SET GLOBAL RPL_RAFT_ON= ON;

  --dec $_rpl_server
}

--let $_rpl_server= $rpl_server_count
while ($_rpl_server)
{
  --let $rpl_connection_name= server_$_rpl_server
  --source include/rpl_connection.inc

  reap;
  --dec $_rpl_server
}

# Wait for the master to become the leader
--let $rpl_connection_name= server_$_rpl_master
--source include/rpl_connection.inc
--let $status_var= RPL_RAFT_ROLE
--let $status_var_value= LEADER
--source include/wait_for_status_var.inc

# The master should be the leader in term 1
--let $assert_cond= VARIABLE_VALUE = 1 FROM performance_schema.global_status WHERE VARIABLE_NAME = "RPL_RAFT_TERM"
--let $assert_text= Term should be 1
--source include/assert.inc

# Enable failure detection
let $_rpl_server= $rpl_server_count;
while ($_rpl_server)
{
  let $rpl_connection_name= server_$_rpl_server;
  source include/rpl_connection.inc;
  SET GLOBAL RPL_RAFT_LEADER_FAILURE_DETECTION= ON;
  if ($_rpl_server != $_rpl_master)
  {
    source include/wait_for_slave_sql_to_start.inc;
  }
  if ($_rpl_server == $_rpl_master)
  {
    let $max_sleep= 300;
    while (!$_leader_writeable) {
      let $_leader_writeable= `SELECT @@GLOBAL.READ_ONLY = 0`;
      sleep 1;
      let $max_sleep= $max_sleep - 1;
      if ($max_sleep == 0) {
        die "Timeout while waiting for read_only = 0 on leader";
      }
    }
  }
  dec $_rpl_server;
}

if ($rpl_debug)
{
  --echo "Raft leadership transferred to true master"
}
