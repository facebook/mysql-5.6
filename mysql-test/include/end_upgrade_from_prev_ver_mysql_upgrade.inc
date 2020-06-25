--echo #########
--echo # End upgrade testing with mysql_upgrade
--echo ###

--echo # Stop the server
--source include/shutdown_mysqld.inc

--let $restart_parameters =
--let $restart_hide_args =

--echo # Remove temp datadir
--force-rmdir $MYSQLD_DATADIR1

--echo # Restart the server with default options.
--source include/start_mysqld.inc
