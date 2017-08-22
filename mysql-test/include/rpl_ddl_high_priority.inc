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

--echo connection slave_block
connection slave_block;

# issue a bloking read from slave_block connection
lock tables t1 read;

--echo connection master;
connection master;

# issue a high_pri command from master
create high_priority index idx1 on t1 (i);
show create table t1;

# sync. high_priority_lock_wait_timeout is 1 second by default
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
