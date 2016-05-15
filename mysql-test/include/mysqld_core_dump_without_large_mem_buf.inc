--source include/not_valgrind.inc
--source include/have_innodb.inc

# Embedded mode doesn't support restart
--source include/not_embedded.inc

--echo # Shutdown server
--source include/shutdown_mysqld.inc

--echo # Restart server with --log-error
--exec echo "restart:--log-error=$MYSQLTEST_VARDIR/log/core_dump.err" > $MYSQLTEST_VARDIR/tmp/mysqld.1.expect
--enable_reconnect
--source include/wait_until_connected_again.inc

--let $expected_max_core_size = 3584
--source include/mysqld_core_dump.inc
