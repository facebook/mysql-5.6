--echo ### Query results and timeout error will sit in client socket buffer in order

send select * from t1;

--echo sleeping 18 sec to make sure the connection get timed out and closed by server...
sleep 18;
--echo waking up...

--echo con2 has been closed on server side but the results of 'send show processlist'
--echo should be in cilent buffer already and can be received by 'reap'
reap;

--echo dump and verify the error 2006 from socket buffer...
dump_timed_out_connection_socket_buffer;

connection default;

set @@global.wait_timeout = @orig_wait_timeout;
set @@global.interactive_timeout = @orig_interactive_timeout;

drop table t1;
