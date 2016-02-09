select @@session.wait_timeout;
select @@session.interactive_timeout;

--echo set wait_timeout to 2 seconds
set wait_timeout = 2;

--echo set interactive_timeout to 2 seconds
set interactive_timeout = 2;

select @@session.wait_timeout;
select @@session.interactive_timeout;

--echo sleeping 12 sec to make sure the connection get timed out and closed by server...
sleep 12;
--echo waking up...

--echo dump and verify the error 2006 from socket buffer
--echo the timeout value in error message should refect
--echo the latest session variable overrides
dump_timed_out_connection_socket_buffer;

connection default;
let $i=1;
while($i<=3) {
  disconnect con$i;
  inc $i;
}

set @@global.wait_timeout = @orig_wait_timeout;
set @@global.interactive_timeout = @orig_interactive_timeout;

drop table t1;
