# Test relies on input variable(s) to be set :
#   $ARCH_DATA       - Archived datadir in a .zip file to be used as a starting
#                      point for the upgrade test.  Pass the raw filename only,
#                      no path nor extension.  Expects the file to be located
#                      exactly as $MYSQLTEST_VARDIR/std_data/$ARCH_DATA.zip
#   $ADDITIONAL_OPTS - Additional options to be passed to mysqld when restarted
#                      on the upgrade (old dversion) datadir.
#
# Upon return, the following variables will be available for the test validation
# but _MUST_NOT_BE_ALTERED_ or the cleanup stage will fail :
#   #MYSQLD_DATADIR1  - new datadir the the server is running on.
#
# Once Test speficic validation has been completed, test must --source
# include/end_upgrade_from_prev_ver_mysql_upgrade.inc to perform proper cleanup.

--echo #########
--echo # Begin upgrade testing with mysql_upgrade
--echo ###

# Set different paths for --datadir
--let $MYSQLD_DATADIR1=$MYSQL_TMP_DIR/$ARCH_DATA
--let $DATA_ARCH_PATH=$MYSQL_TMP_DIR/$ARCH_DATA.zip

--echo # Copy the remote tablespace & DB zip files from suite location to working location.
--copy_file $MYSQLTEST_VARDIR/std_data/$ARCH_DATA.zip $DATA_ARCH_PATH

--echo # Check that the file exists in the working folder.
--file_exists $DATA_ARCH_PATH

--echo # Stop DB server which was created by MTR default
--source include/shutdown_mysqld.inc

--echo # Unzip the zip file.
--file_exists $DATA_ARCH_PATH
--exec unzip -qo $DATA_ARCH_PATH -d $MYSQL_TMP_DIR

--echo # Remove copied files
--remove_file $DATA_ARCH_PATH

--let $restart_parameters = "restart: --loose-skip-log-bin --skip-log-slave-updates --datadir=$MYSQLD_DATADIR1 $ADDITIONAL_OPTS"
--let $restart_hide_args = 1
--source include/start_mysqld.inc

--echo #########
--echo # Performing test specific validation
--echo ###
