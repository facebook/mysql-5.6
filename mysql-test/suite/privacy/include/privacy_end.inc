# Uninstall plugin
let $privacy_plugin_was_early_installed = `SELECT COUNT(*) = 0
FROM mysql.plugin WHERE NAME = 'privacy_plugin'`;
let $sql_privacy_log_was_early_installed = `SELECT COUNT(*) = 0
FROM mysql.plugin WHERE NAME = 'SQL_PRIVACY_LOG'`;

disable_query_log;
disable_warnings;
# $privacy_plugin_was_early_installed = 1 means the privacy plugin was
# installed at the startup time instead of being installed dynamically.
# If so, we don't want to uninstall them.
if (!$privacy_plugin_was_early_installed)
{
  UNINSTALL PLUGIN privacy_plugin;
}
if (!$sql_privacy_log_was_early_installed)
{
  UNINSTALL PLUGIN SQL_PRIVACY_LOG;
}
enable_warnings;
enable_query_log;

# Restore session_track_response_attributes
set session session_track_response_attributes=@original_session_track_response_attributes;

#Restore privacy_plugin_on setting
set global privacy_plugin_on=@original_privacy_plugin_on;

# Restore privacy_plugin_pes_overall_timeout setting
disable_query_log;
let $value = query_get_value(SHOW VARIABLES LIKE 'privacy_plugin_pes_overall_timeout', Value, 1);
if ($value != No such row)
{
  set global privacy_plugin_pes_overall_timeout = @original_privacy_plugin_pes_overall_timeout;
}
enable_query_log;
