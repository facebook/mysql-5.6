--echo Donor:
--connection default

SET GLOBAL clone_donor_timeout_after_network_failure=0;

--eval SET GLOBAL DEBUG="+d,$donor_crash_point"

--source include/expect_crash.inc

--connect(donor_conn_2,127.0.0.1,root,,test,$SERVER_PORT_1)

--connection clone_conn_1

--echo Client:

--let $HOST = 127.0.0.1
--let $PORT = $SERVER_PORT_1
--let $USER = root
--let clone_err = ER_CLONE_DONOR
--let remote_clone = 1

--source ../mysql-test/suite/clone/include/clone_command.inc

--connection donor_conn_2
--disable_reconnect
--source include/wait_until_disconnected.inc
--source include/start_mysqld.inc

--connection default
--enable_reconnect
--source include/wait_until_connected_again.inc

--disconnect donor_conn_2
--connection default

# Shouldn't be needed, but https://bugs.mysql.com/bug.php?id=108522 exists
# (A failed clone cleanup is incomplete)
--force-rmdir $CLONE_DATADIR
