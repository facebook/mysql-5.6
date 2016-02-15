--echo # Get the full path name of the PID file
--let $pid_file= query_get_value(SELECT @@pid_file, @@pid_file, 1)

--echo # Expecting a "crash", but don't restart the server until it is told to
--let $server_id= `SELECT @@server_id`
--let $expect_file_name= $MYSQLTEST_VARDIR/tmp/mysqld.$server_id.expect
--exec echo "wait" > $expect_file_name

--echo # Expected max core size is $expected_max_core_size MB

--echo # Call Perl script to test if a core can be dumped
--exec perl include/mysqld_core_dump_perl.inc $MYSQLTEST_VARDIR $pid_file $expected_max_core_size

--echo # Clear error log to make sure there are no complains about it
--exec echo "" > $MYSQLTEST_VARDIR/log/mysqld.1.err

--echo # Make server restart
--exec echo "restart" > $expect_file_name

--enable_reconnect

--echo # Wait for server to be back online
--source include/wait_until_connected_again.inc

--disable_reconnect
