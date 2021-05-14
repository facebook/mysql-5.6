disable_query_log;
let $privacy_plugin_installed= `SELECT COUNT(*) = 1
FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = 'privacy_plugin'`;
if (!$privacy_plugin_installed)
{
  skip privacy_plugin plugin not installed.
  Please check mysql_privacy_plugin.so is loaded;
}

let $sql_privacy_log_installed= `SELECT COUNT(*) = 1
FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = 'SQL_PRIVACY_LOG'`;
if (!$privacy_plugin_installed)
{
  skip SQL_PRIVACY_LOG plugin not installed.
  Please check mysql_privacy_plugin.so is loaded;
}

enable_query_log;
