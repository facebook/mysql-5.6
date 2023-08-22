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
  SET GLOBAL THRIFT_SERVER_SQUIFT=ON;
}

disable_warnings;
DROP TABLE IF EXISTS THRIFT_SERVER_PLUGIN_OUTPUT;
DROP USER IF EXISTS 'scriptrw:sys.xdb/xdb.dbclient_test';
DROP USER IF EXISTS 'admin:sys.database';
enable_warnings;

CREATE TABLE THRIFT_SERVER_PLUGIN_OUTPUT(
    ID BIGINT NOT NULL AUTO_INCREMENT, 
    SEQ_NUMBER INT NOT NULL, 
    TYPE VARCHAR(64), 
    VALUE VARCHAR(10240), 
    PRIMARY KEY (ID));

CREATE USER 'scriptrw:sys.xdb/xdb.dbclient_test';
GRANT ALL ON *.* TO 'scriptrw:sys.xdb/xdb.dbclient_test'@'%';

CREATE USER 'admin:sys.database';
GRANT ALL ON *.* TO 'admin:sys.database'@'%';

FLUSH PRIVILEGES;

enable_query_log;
