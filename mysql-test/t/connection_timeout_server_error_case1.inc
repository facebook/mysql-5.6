select @@session.wait_timeout;
select @@session.interactive_timeout;

--echo ### Connection gets timed out without executing any queries

--echo sleeping 18 sec to make sure the connection get timed out and closed by server...
sleep 18;
--echo waking up...

--echo dump and verify the error 2006 from socket buffer...
dump_timed_out_connection_socket_buffer;
