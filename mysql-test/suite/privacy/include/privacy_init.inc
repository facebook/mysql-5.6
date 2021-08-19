# Check if the plugin binary exists in the plugin dir
--let $plugin_dir= `SELECT @@GLOBAL.PLUGIN_DIR`
--file_exists $plugin_dir/mysql_privacy_plugin.so
let $privacy_plugin_early_installed= `SELECT COUNT(*) = 1
FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = 'privacy_plugin'`;
let $sql_privacy_log_early_installed= `SELECT COUNT(*) = 1
FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = 'SQL_PRIVACY_LOG'`;

# Install plugin
disable_query_log;
if (!$privacy_plugin_early_installed)
{
  INSTALL PLUGIN privacy_plugin SONAME 'mysql_privacy_plugin.so';
}
if (!$sql_privacy_log_early_installed)
{
  INSTALL PLUGIN SQL_PRIVACY_LOG SONAME 'mysql_privacy_plugin.so';
}
enable_query_log;

# Enable session_track_response_attributes to enable using response attributes
set @original_session_track_response_attributes=@@session.session_track_response_attributes;
set session session_track_response_attributes = 1;
