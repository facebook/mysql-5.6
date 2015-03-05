--echo connection_net_errors_stats_test (user= $user, ER_NET_READ_ERROR= $ER_NET_READ_ERROR, ER_NET_READ_INTERRUPTED= $ER_NET_READ_INTERRUPTED)

connect (conn_stat,127.0.0.1,root);

--let $mode=baseline
--source include/get_user_net_error_stats.inc

flush status;

# Test ER_NET_READ_ERROR
if ($ER_NET_READ_ERROR == 1)
{
  connect (conn1,127.0.0.1,$user);
  select "disconnect the connection";
  dirty_close conn1;

  --source include/wait_until_disconnected.inc
}

# Test ER_NET_READ_INTERRUPTED
if ($ER_NET_READ_INTERRUPTED == 1)
{
  connect (conn2,127.0.0.1,$user);
  set session wait_timeout = 1;
  --real_sleep 2
  --error 2006, 2013
  # Why do we check for two error codes ?
  # The client fails with very related error codes in different builds. The
  # intention of this test is to test that the server stats reflect correctly.
  # This is a known client side behavior and other tests like kill.test
  # and wait_until_connected already does the same.
  select "connection should have been closed by server";
  disconnect conn2;
}

# We don't have a mechanism to trigger the following errors
# ER_NET_WRITE_INTERRUPTED
# ER_NET_ERROR_ON_WRITE
# ER_NET_PACKET_TOO_LARGE
# ER_NET_UNCOMPRESS_ERROR
# ER_NET_PACKETS_OUT_OF_ORDER

--connection conn_stat

# Verify the counters
show status like "Connection_errors_net_%";

--let $mode=compute
--source include/get_user_net_error_stats.inc

# Run flush status
flush status;
show status like "Connection_errors_net_%";

disconnect conn_stat;
