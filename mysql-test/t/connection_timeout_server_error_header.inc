--echo ### mysqld writes error 2006 to the socket before closing a connection
--echo ### due to timeout so that client can find out the cause of the lost
--echo ### connection by reading the message.

connection default;

--disable_warnings
drop table if exists t1;
--enable_warnings

create table t1 (a1 int primary key, x1 int) engine = innodb;
insert into t1 values (1, 1);

select @@wait_timeout;
select @@interactive_timeout;

set @orig_wait_timeout = @@wait_timeout;
set @orig_interactive_timeout = @@interactive_timeout;

--echo set @@global.wait_timeout to 3 seconds
set @@global.wait_timeout = 3;

--echo set @@global.interactive_timeout to 3 seconds
set @@global.interactive_timeout = 3;
