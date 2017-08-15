###############################################################################
# Test replication of the high_priority syntax
###############################################################################


--source include/master-slave.inc

--let $rpl_connection_name= slave_block
--let $rpl_server_number= 2
--source include/rpl_connect.inc

connection master;

# create t1
--disable_warnings
drop table if exists t1;
--enable_warnings
create table t1 (i int);
show create table t1;
insert into t1 values (1), (2), (3);

sync_slave_with_master;

--echo connection slave
connection slave;
select * from t1;

let $sql_thread=`select id from information_schema.processlist where state
 like 'Slave has read all relay log; waiting for the slave I/O thread%'`;
let $worker_thread=`select id from information_schema.processlist where state
 like 'Waiting for%'`;

--echo # set slave thread's lock_wait_timeout to a small value

--replace_regex /session [0-9]*/session sql_thread/
--eval set session $sql_thread lock_wait_timeout = 0.2

--replace_regex /session [0-9]*/session sql_thread/
--eval show session $sql_thread variables like 'lock_wait_timeout'

--echo connection slave_block
connection slave_block;

# issue a bloking read from slave_block connection
lock tables t1 read;

--echo connection master;
connection master;

# issue a high_pri command from master
create high_priority index idx1 on t1 (i);
show create table t1;

# sync
sync_slave_with_master;

--echo connection slave;
connection slave;

# the high_pri ddl replication should go through
show create table t1;


##
## clean up
##

connection master;
drop table t1;

source include/rpl_end.inc;
