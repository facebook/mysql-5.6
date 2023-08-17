disable_query_log;

let $plugin_installed= `SELECT COUNT(*) = 1 FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = 'THRIFT_SERVER'`;
if ($plugin_installed)
{
  uninstall plugin THRIFT_SERVER;
}

enable_query_log;
