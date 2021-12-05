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

# Enable plugin
set @original_privacy_plugin_on=@@privacy_plugin_on;
set global privacy_plugin_on = 1;

# Disable query log
disable_query_log;

# Increase PES Timeout
let $value = query_get_value(SHOW VARIABLES LIKE 'privacy_plugin_pes_overall_timeout', Value, 1);
if ($value !=  No such row)
{
  set @original_privacy_plugin_pes_overall_timeout=@@privacy_plugin_pes_overall_timeout;
  set global privacy_plugin_pes_overall_timeout = 60000;
}

# Check if the plugin has column level check implemented
let $value = query_get_value(SHOW VARIABLES LIKE 'privacy_plugin_purpose_policy_column_level_check', Value, 1);
if ($value ==  No such row)
{
  let $has_column_level_check = 0;
}
if ($value !=  No such row)
{
  let $has_column_level_check = 1;
}

# Check if the plugin has column level check implemented
let $value = query_get_value(SHOW VARIABLES LIKE 'purpose_policy_column_level_check_fine_grained', Value, 1);
if ($value ==  No such row)
{
  let $has_column_level_check_fine_grained = 0;
}
if ($value !=  No such row)
{
  let $has_column_level_check_fine_grained = 1;
}

# Enable query log
enable_query_log;
