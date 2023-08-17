# Don't run under valgrind.
# remove this after we fixed the timeout issue.
source include/not_valgrind.inc;
disable_query_log;

# Install plugin
if (!$plugin_installed)
{
  install plugin THRIFT_SERVER soname 'mysql_thrift_server.so';
  set global thrift_server_mysql_thrift_acl='mysql_thrift.test';
  set global thrift_server_vlog=on;
  set global thrift_server_on=on;
}

enable_query_log;
