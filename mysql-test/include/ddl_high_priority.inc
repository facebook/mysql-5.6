###############################################################################
# Common test file for high priority DDL
###############################################################################
# in slave instance, DDL commands are executed on slave sql threads.
# Thus, these users aren't used for slave_high_priority_ddl
if (!$slave_ddl) {
  echo connection $con_init;
  connection $con_init;
  create user test_user1@localhost;
  grant all on test to test_user1@localhost;
  create user test_user2@localhost;
  grant all on test to test_user2@localhost;
}

--echo ##
--echo ## killing conflicting shared locks by alter table
--echo ##
--let $blocking_sql = lock tables t1 read;
--let $cmd = alter table t1 modify i bigint;
--source include/ddl_high_priority_module.inc


--echo ##
--echo ## killing conflicting shared lock in a transaction
--echo ## transaction will rollback
--echo ##
--let $blocking_sql = begin; insert into t1 values (4); select i from t1;
--let $cmd = alter table t1 rename t1_new;
--let $tablename = t1_new
--source include/ddl_high_priority_module.inc
select * from t1_new;
drop table t1_new;
# restore tablename
--let $tablename = t1

--echo ##
--echo ## rename table, rename table doesn't support high_priority keyword
--echo ##
--let $saved_use_sys_var=$use_sys_var
--let $use_sys_var=1
--let $blocking_sql = begin; insert into t1 values (5); select i from t1;
--let $cmd = rename table t1 to t1_new;
--let $high_priority_cmd = rename high_priority table t1 to t1_new;
--let $tablename = t1_new
--source include/ddl_high_priority_module.inc
select * from t1_new;
drop table t1_new;
# restore tablename
--let $tablename = t1
--let $use_sys_var = $saved_use_sys_var

--echo ##
--echo ## simulate conflicting DDL which will not be killed
--echo ##
# Simulate conflicting DDL
# This will hold MDL_SHARED_NO_READ_WRITE, which may be upgraded to exclusive
# locks to run DDLs like ALTER TABLE
# the upgradable/exclusive lock should not be killed
--let $should_kill = 0
--let $should_table_exist = 0
--let $blocking_sql = lock tables t1 write;
--let $cmd = drop table t1;
--source include/ddl_high_priority_module.inc
# restore $should_kill/$should_table_exist
--let $should_kill = 1
--let $should_table_exist = 1


--echo ##
--echo ## killing conflicting transaction by drop table DDL
--echo ##
--let $blocking_sql = lock tables t1 read; begin; insert into t1 values (4);
--let $should_table_exist = 0
--let $cmd = drop table t1;
--source include/ddl_high_priority_module.inc
# restore $should_table_exist
--let $should_table_exist = 1

if (!$slave_ddl) {
  --echo ##
  --echo ## no effect for regular users
  --echo ##
  # save connected session count
  --source include/count_sessions.inc
  --let $begin_session_count =$count_sessions
  connect (con2, $con_block_host,test_user2,,test,$con_block_port,);
  # $con_kill is regular user
  --let $con_kill_orginal = $con_kill
  --let $con_kill = con2
  --let $should_kill = 0
  --let $blocking_sql = lock tables t1 read
  --let $cmd = alter table t1 modify i bigint

  --source include/ddl_high_priority_module.inc
  connection default;
  disconnect con2;

  # Ensure con2 is closed
  --let $count_sessions = $begin_session_count
  --source include/wait_until_count_sessions.inc

  # restore $con_kill
  --let $con_kill = $con_kill_orginal
  # restore $should_kill
  --let $should_kill = 1
}


--echo ##
--echo ## create index
--echo ##
--let $blocking_sql = lock tables t1 read;
--let $cmd = create index idx1 on t1 (i);
--source include/ddl_high_priority_module.inc


--echo ##
--echo ## drop index (use the previously created table)
--echo ##
--let $recreate_table = 0
--let $cmd = drop index idx1 on t1;
--source include/ddl_high_priority_module.inc
# restore $recreate_table
--let $recreate_table = 1


--echo ##
--echo ## high_priority truncate table
--echo ##
--let $blocking_sql = lock tables t1 read
--let $cmd = truncate t1
--source include/ddl_high_priority_module.inc


--echo ##
--echo ## high_priority create trigger
--echo ##
--let $blocking_sql = lock tables t1 read
let $cmd = create trigger ins_sum before insert on t1
for each row set @sum = @sum + new.i;
--source include/ddl_high_priority_module.inc


--echo ##
--echo ## high_priority drop trigger
--echo ##
# drop trigger (use the previously created table)
--let $recreate_table = 0
--let $cmd = drop trigger ins_sum
--source include/ddl_high_priority_module.inc
# restore $recreate_table
--let $recreate_table = 1


--echo ##
--echo ## drop database 
--echo ##
create database menagerie;
use menagerie;
create table t1(a int);
if (!$slave_ddl) {
  echo connection $con_init;
  connection $con_init;
  grant all on t1 to test_user1@localhost;
  grant all on t1 to test_user2@localhost;
}
--let $recreate_table = 0
--let $blocking_sql = begin;use menagerie; select * from t1;
--let $cmd = drop database menagerie;
--let $high_priority_cmd = drop high_priority database menagerie;
--source include/ddl_high_priority_module.inc
# restore $recreate_table
--let $recreate_table = 1
use test;

--echo ##
--echo ## high_priority optimize table
--echo ##
--echo ## "optimize table" doesn't throw errors. It catches all errors, and
--echo ## returns a result set in a table
--echo ##
--let $throw_error = 0
--let $blocking_sql = lock tables t1 read
--let $cmd = optimize table t1
--source include/ddl_high_priority_module.inc
# restore throw_error
--let $throw_error = 1

# LOCK TABLE COMMAND isn't replicate to slave
if (!$slave_ddl)
{
  --echo ##
  --echo ## lock table read vs write
  --echo ##
  --let $blocking_sql = lock tables t1 read
  --let $cmd = lock tables t1 write
  --source include/ddl_high_priority_module.inc
}


--echo ##
--echo ## clean up
--echo ##
echo connection $con_init;
connection $con_init;
unlock tables;
if (!$slave_ddl) {
  drop user test_user1@localhost;
  drop user test_user2@localhost;
}
drop table if exists t1_new;
drop table t1;
