--source include/master-slave.inc

# Save the initial number of concurrent sessions
--source include/count_sessions.inc

connection master;
select @@global.enable_user_tables_engine_check;
connection slave;
select @@global.enable_user_tables_engine_check;

--echo # 
--echo # Create table on master where engine is substituted
--echo # with enable_user_tables_engine_check=ON on both master and slave
--echo #
connection master;
set @@session.sql_mode=NO_UNSIGNED_SUBTRACTION;
create table t0(a int primary key) engine=InnoDB charset=latin1;
--source include/sync_slave_sql_with_master.inc

--echo # 
--echo # Create table on master where engine is not substituted
--echo # with enable_user_tables_engine_check=OFF on both master and slave
--echo #
connection slave;
stop replica sql_thread;
SET @@global.enable_user_tables_engine_check=OFF;
start replica sql_thread;
connection master;
SET @@session.enable_user_tables_engine_check=OFF;
create table t1(a int primary key) engine=InnoDB charset=latin1;

--echo # Print storage engines of both tables created on master
show create table t0;
show create table t1;
--source include/sync_slave_sql_with_master.inc
connection slave;
--echo # Print storage engines of both tables created on slave
show create table t0;
show create table t1;
SET @@global.enable_user_tables_engine_check=ON;
connection master;
drop table t0;
drop table t1;
--source include/sync_slave_sql_with_master.inc
disconnect master;
disconnect slave;
--source include/rpl_end.inc

--source include/wait_until_count_sessions.inc
