
# TODO: check if the topology is supported in raft

# Check if SASL path is set in env
perl;
  my $sasl_path= $ENV{'SASL_PATH'} or die "[ERROR] SASL_PATH not set, set it using export SASL_PATH=/path/to/sasl/libs";
EOF

--let $instances= "instances": "

--let $_rpl_server= $rpl_server_count
while ($_rpl_server)
{
  --source include/have_gtid.inc

  if (`SELECT COUNT(*) = 0 OR VARIABLE_VALUE = 'NONE' FROM INFORMATION_SCHEMA.GLOBAL_VARIABLES WHERE VARIABLE_NAME = 'BINLOG_CHECKSUM'`)
  {
    --skip Mysql Raft cannot be enabled without binlog checksums
  }

  # Check if the plugin binary exists in the plugin dir
  --let $plugin_dir= `SELECT @@GLOBAL.PLUGIN_DIR`
  --file_exists $plugin_dir/mysql_raft_repl.so

  --let $rpl_connection_name= server_$_rpl_server
  --source include/rpl_connection.inc

  --let $ipv6_host_port= `SELECT CONCAT('::1', ':', (SELECT @@GLOBAL.PORT), ',')`
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

  --let $_is_slave= `SELECT SUBSTRING('$rpl_master_list', 1 + ($_rpl_server - 1) * $rpl_server_count_length, $rpl_server_count_length) != ''`

  let $fs_wal_path = `SELECT CONCAT((SELECT @@GLOBAL.TMPDIR), '/mysqlraft')`;
  exec rm -fr $fs_wal_path;

  --let $fs_wal_dir= `SELECT CONCAT('"fs_wal_dir": "', '$fs_wal_path', '"')`
  --let $rpc_port= `SELECT (SELECT @@GLOBAL.PORT) + 2000`
  --let $rpc_bind_addresses= `SELECT CONCAT('"rpc_bind_addresses": "[::]:', '$rpc_port', '"')`

  --let $config_json= `SELECT CONCAT('{', '$instances', ',', '$fs_wal_dir', ',', '$rpc_bind_addresses', '}')`

  if ($rpl_debug)
  {
    --echo raft config json: '$config_json'
  }

  --let $raft_plugin_early_installed= `SELECT COUNT(*) = 1 FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = 'MYSQL_RAFT_PLUGIN'`

  # Install plugin
  if (!$raft_plugin_early_installed)
  {
    INSTALL PLUGIN MYSQL_RAFT_PLUGIN SONAME 'mysql_raft_repl.so';
  }

  # Set config
  eval SET GLOBAL MYSQL_RAFT_PLUGIN_CONFIG_JSON= '$config_json';

  if ($_is_slave)
  {
    source include/stop_slave_io.inc;
  }

  # Enable raft plugin
  send SET GLOBAL MYSQL_RAFT_PLUGIN_ON= ON;

  --dec $_rpl_server
}

--let $_rpl_server= $rpl_server_count
while ($_rpl_server)
{
  --let $rpl_connection_name= server_$_rpl_server
  --source include/rpl_connection.inc

  reap;

  let $_is_mysql_slave= `SELECT SUBSTRING('$rpl_master_list', 1 + ($_rpl_server - 1) * $rpl_server_count_length, $rpl_server_count_length) != ''`;

  # Stash master's connection and raft uuid
  if (!$_is_mysql_slave)
  {
    let $master_raft_uuid= `SELECT VARIABLE_VALUE FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = "RPL_RAFT_PEER_UUID"`;
    let $master_server_num= $_rpl_server;
  }

  # Enable raft
  SET GLOBAL ENABLE_RAFT_PLUGIN= ON;

  --dec $_rpl_server
}

# Promote the mysql master to leader in the raft world
let $rpl_connection_name= server_$master_server_num;
source include/rpl_connection.inc;

let $master_raft_role= `SELECT VARIABLE_VALUE FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = "RPL_RAFT_ROLE"`;

while ($master_raft_role != 'LEADER')
{
  # Find the current raft leader
  let $_rpl_server= $rpl_server_count;
  while ($_rpl_server)
  {
    let $rpl_connection_name= server_$_rpl_server;
    source include/rpl_connection.inc;
    let $_is_raft_leader= `SELECT VARIABLE_VALUE = "LEADER" FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = "RPL_RAFT_ROLE"`;
    if ($_is_raft_leader)
    {
      eval set @@global.mysql_raft_plugin_new_leader_uuid= '$master_raft_uuid';
    }
    dec $_rpl_server;
  }
  let $rpl_connection_name= server_$master_server_num;
  source include/rpl_connection.inc;
  let $master_raft_role= `SELECT VARIABLE_VALUE FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = "RPL_RAFT_ROLE"`;
}

