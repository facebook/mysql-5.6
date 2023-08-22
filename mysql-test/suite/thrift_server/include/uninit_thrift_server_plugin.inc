disable_query_log;

let $plugin_installed= `SELECT COUNT(*) = 1 FROM INFORMATION_SCHEMA.PLUGINS WHERE PLUGIN_NAME = 'THRIFT_SERVER'`;
if ($plugin_installed)
{
  uninstall plugin THRIFT_SERVER;
}

DROP TABLE THRIFT_SERVER_PLUGIN_OUTPUT;
DROP USER 'scriptrw:sys.xdb/xdb.dbclient_test';
DROP USER 'admin:sys.database';

enable_query_log;
