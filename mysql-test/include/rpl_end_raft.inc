let $_rpl_server= $rpl_server_count;
while ($_rpl_server)
{
  let $rpl_connection_name= server_$_rpl_server;
  source include/rpl_connection.inc;

  let $plugin_was_early_installed = `SELECT COUNT(*) = 0 FROM mysql.plugin WHERE NAME = 'RPL_RAFT'`;
  let $fs_wal_path = `SELECT CONCAT((SELECT @@GLOBAL.RPL_RAFT_WAL_DIR), '/', (SELECT @@GLOBAL.PORT))`;

  disable_query_log;
  SET GLOBAL RPL_RAFT_ON= 0;
  SET GLOBAL RPL_RAFT_CONFIG_JSON= default;
  SET GLOBAL RPL_RAFT_NEW_LEADER_UUID= default;
  SET GLOBAL READ_ONLY_ERROR_MSG_EXTRA= default;
  exec rm -fr $fs_wal_path;
  if (!$plugin_was_early_installed)
  {
    UNINSTALL PLUGIN RPL_RAFT;
  }
  SET GLOBAL READ_ONLY= 1;

  # Extract original values for these variables (they are stored as a string seprated by ':', one value for each server)
  let $sync_binlog= `SELECT SUBSTRING_INDEX(SUBSTRING_INDEX('$rpl_raft_orig_sync_binlog', ':', $rpl_server_count - $_rpl_server + 1), ':', -1)`;
  let $innodb_flush= `SELECT SUBSTRING_INDEX(SUBSTRING_INDEX('$rpl_raft_orig_innodb_flush', ':', $rpl_server_count - $_rpl_server + 1), ':', -1)`;
  let $innodb_doublewrite= `SELECT SUBSTRING_INDEX(SUBSTRING_INDEX('$rpl_raft_orig_innodb_doublewrite', ':', $rpl_server_count - $_rpl_server + 1), ':', -1)`;
  eval SET GLOBAL SYNC_BINLOG= $sync_binlog;
  eval SET GLOBAL INNODB_FLUSH_LOG_AT_TRX_COMMIT= $innodb_flush;
  eval SET GLOBAL INNODB_DOUBLEWRITE= $innodb_doublewrite;
  enable_query_log;


  dec $_rpl_server;
}
source include/rpl_change_topology.inc;
