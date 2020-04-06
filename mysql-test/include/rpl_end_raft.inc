let $_rpl_server= $rpl_server_count;
while ($_rpl_server)
{
  let $rpl_connection_name= server_$_rpl_server;
  source include/rpl_connection.inc;

  let $_is_slave= `SELECT SUBSTRING('$rpl_master_list', 1 + ($_rpl_server - 1) * $rpl_server_count_length, $rpl_server_count_length) != ''`;
  let $plugin_was_early_installed = `SELECT COUNT(*) = 0 FROM mysql.plugin WHERE NAME = 'RPL_RAFT'`;

  disable_query_log;
  SET GLOBAL RPL_RAFT_ON= 0;
  SET GLOBAL RPL_RAFT_CONFIG_JSON= default;
  SET GLOBAL RPL_RAFT_NEW_LEADER_UUID= default;
  if (!$plugin_was_early_installed)
  {
    UNINSTALL PLUGIN RPL_RAFT;
  }
  SET GLOBAL READ_ONLY= 1;
  enable_query_log;

  if ($_is_slave)
  {
    source include/start_slave_io.inc;
  }

  dec $_rpl_server;
}
