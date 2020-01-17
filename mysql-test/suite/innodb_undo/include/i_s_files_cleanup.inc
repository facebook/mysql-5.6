--disable_query_log
call mtr.add_suppression("Doublewrite page .* could not be restored");

# Do a slow shutdown and restart to clear out the undo logs
SET GLOBAL innodb_fast_shutdown = 0;
--let $shutdown_server_timeout = 300
--source include/restart_mysqld.inc
--enable_query_log
