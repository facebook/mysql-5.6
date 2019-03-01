--echo connection_net_errors_stats_test (user= $user, ER_NET_READ_ERROR= $ER_NET_READ_ERROR, ER_NET_READ_INTERRUPTED= $ER_NET_READ_INTERRUPTED)

connect (conn_stat,127.0.0.1,root);

# Run flush status and clear counters
flush status;

# Get counters at the start
--let $start_READ_ERROR = query_get_value(show status like 'Connection_errors_net_ER_NET_READ_ERROR', Value, 1)
--let $start_READ_INTERRUPTED = query_get_value(show status like 'Connection_errors_net_ER_NET_READ_INTERRUPTED', Value, 1)
--let $start_ERROR_ON_WRITE = query_get_value(show status like 'Connection_errors_net_ER_NET_ERROR_ON_WRITE', Value, 1)
--let $start_PACKETS_OUT_OF_ORDER = query_get_value(show status like 'Connection_errors_net_ER_NET_PACKETS_OUT_OF_ORDER', Value, 1)
--let $start_PACKET_TOO_LARGE = query_get_value(show status like 'Connection_errors_net_ER_NET_PACKET_TOO_LARGE', Value, 1)
--let $start_UNCOMPRESS_ERROR = query_get_value(show status like 'Connection_errors_net_ER_NET_UNCOMPRESS_ERROR', Value, 1)
--let $start_WRITE_INTERRUPTED = query_get_value(show status like 'Connection_errors_net_ER_NET_WRITE_INTERRUPTED', Value, 1)

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
  --sleep 2
  --error CR_SERVER_GONE_ERROR, CR_SERVER_LOST
  # Why do we check for multiple error codes ?
  # The client fails with very related error codes in different builds. The
  # intention of this test is to test that the server stats reflect correctly.
  # This is a known client side behavior and other tests like kill.test
  # and wait_until_connected already does the same.
  select "connection should have been closed by server";
  disconnect conn2;
}

# We don't have a mechanism to trigger the following errors
--let $ER_NET_ERROR_ON_WRITE= 0
--let $ER_NET_PACKETS_OUT_OF_ORDER= 0
--let $ER_NET_PACKET_TOO_LARGE= 0
--let $ER_NET_UNCOMPRESS_ERROR= 0
--let $ER_NET_WRITE_INTERRUPTED= 0

--connection conn_stat

# Get counters at the end
--let $end_READ_ERROR = query_get_value(show status like 'Connection_errors_net_ER_NET_READ_ERROR', Value, 1)
--let $end_READ_INTERRUPTED = query_get_value(show status like 'Connection_errors_net_ER_NET_READ_INTERRUPTED', Value, 1)
--let $end_ERROR_ON_WRITE = query_get_value(show status like 'Connection_errors_net_ER_NET_ERROR_ON_WRITE', Value, 1)
--let $end_PACKETS_OUT_OF_ORDER = query_get_value(show status like 'Connection_errors_net_ER_NET_PACKETS_OUT_OF_ORDER', Value, 1)
--let $end_PACKET_TOO_LARGE = query_get_value(show status like 'Connection_errors_net_ER_NET_PACKET_TOO_LARGE', Value, 1)
--let $end_UNCOMPRESS_ERROR = query_get_value(show status like 'Connection_errors_net_ER_NET_UNCOMPRESS_ERROR', Value, 1)
--let $end_WRITE_INTERRUPTED = query_get_value(show status like 'Connection_errors_net_ER_NET_WRITE_INTERRUPTED', Value, 1)

# Verify the counters
--let $assert_text= Connection_errors_net_ER_NET_READ_ERROR shall be $ER_NET_READ_ERROR
--let $assert_cond= $end_READ_ERROR - $start_READ_ERROR = $ER_NET_READ_ERROR
--source include/assert.inc

--let $assert_text= Connection_errors_net_ER_NET_READ_INTERRUPTED shall be $ER_NET_READ_INTERRUPTED
--let $assert_cond= $end_READ_INTERRUPTED - $start_READ_INTERRUPTED = $ER_NET_READ_INTERRUPTED
--source include/assert.inc

--let $assert_text= Connection_errors_net_ER_NET_ERROR_ON_WRITE shall be $ER_NET_ERROR_ON_WRITE
--let $assert_cond= $end_ERROR_ON_WRITE - $start_ERROR_ON_WRITE = $ER_NET_ERROR_ON_WRITE
--source include/assert.inc

--let $assert_text= Connection_errors_net_ER_NET_PACKETS_OUT_OF_ORDER shall be $ER_NET_PACKETS_OUT_OF_ORDER
--let $assert_cond= $end_PACKETS_OUT_OF_ORDER - $start_PACKETS_OUT_OF_ORDER = $ER_NET_PACKETS_OUT_OF_ORDER
--source include/assert.inc

--let $assert_text= Connection_errors_net_ER_NET_PACKET_TOO_LARGE shall be $ER_NET_PACKET_TOO_LARGE
--let $assert_cond= $end_PACKET_TOO_LARGE - $start_PACKET_TOO_LARGE = $ER_NET_PACKET_TOO_LARGE
--source include/assert.inc

--let $assert_text= Connection_errors_net_ER_NET_UNCOMPRESS_ERROR shall be $ER_NET_UNCOMPRESS_ERROR
--let $assert_cond= $end_UNCOMPRESS_ERROR - $start_UNCOMPRESS_ERROR = $ER_NET_UNCOMPRESS_ERROR
--source include/assert.inc

--let $assert_text= Connection_errors_net_ER_NET_WRITE_INTERRUPTED shall be $ER_NET_WRITE_INTERRUPTED
--let $assert_cond= $end_WRITE_INTERRUPTED - $start_WRITE_INTERRUPTED = $ER_NET_WRITE_INTERRUPTED
--source include/assert.inc

flush status;

disconnect conn_stat;
