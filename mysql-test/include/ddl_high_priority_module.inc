###############################################################################
# This file plays as a function/module for ddl_high_priority test
#
# Usage: set the following variables before including
#
#         $slave_ddl: whether test high priority ddl on slave instance
#              value: 0/1
#
#          $con_init: a connection to initialize
#              value: master/default
#
#         $con_block: a blocking connection name
#              value: con1/slave2/default
#
#    $con_block_host: a blocking connection host
#              value: localhost/127.0.0.1
#
#    $con_block_user: user to create a blocking connection
#              value: test_user1/root
#
#    $con_block_port: blocking connection port
#              value: none/$SLAVE_MYPORT/$MASTER_MYPORT
#
#          $con_kill: a connection that will attempt to kill $con_blocking
#              value: con2/slave/default
#
#        $con_status: a connection to show processlist/lock data
#              value: slave1/default
#
#               $cmd: a regular command to evaluate (to use with sys var)
#              value: sql command
#
#       $should_kill: Expect the con_block to be killed or not
#              value: 0/1
#
#    $recreate_table: Should recreate the test table or not
#              value: 0/1
#
#       $throw_error: whether a command will throw lock_wait_timeout error.
#                     Note, optimize table catches all errors.
#              value: 0/1
###############################################################################

##
## Print out the parameters of the test set
## (useful for debugging)
##
--echo
--echo ## Test parameters:
--echo ## con_init          = $con_init
--echo ## con_block         = $con_block
--echo ## con_kill          = $con_kill
--echo ## con_status        = $con_status
--echo ## cmd               = $cmd
--echo ## blocking_sql      = $blocking_sql
--echo ## should_kill       = $should_kill
--echo ## recreate_table    = $recreate_table
--echo ## throw_error       = $throw_error
--echo


##
## Setup
##

echo connection $con_init;
connection $con_init;
# Save the initial number of concurrent sessions
--source include/count_sessions.inc

if ($recreate_table) {
  # create t1
  --disable_warnings
  drop table if exists t1;
  --enable_warnings
  create table t1 (i int);
  show create table t1;
  insert into t1 values (1), (2), (3);
}
# sync master with slave
if ($slave_ddl) {
  sync_slave_with_master;
}

##
## Testing
##


--echo connection: $con_block
connect ($con_block,$con_block_host, $con_block_user,,test,$con_block_port,);
--eval $blocking_sql

--connection $con_kill
set @m_lock_wait_timeout = @@lock_wait_timeout;
set lock_wait_timeout = 0.02;
set @m_high_priority_lock_wait_timout = @@high_priority_lock_wait_timeout;
set high_priority_lock_wait_timeout = 0.02;
if ($slave_ddl) {
  SET @m_slave_high_priority_lock_wait_timeout =
  @@global.slave_high_priority_lock_wait_timeout;
  set global slave_high_priority_lock_wait_timeout = 0.02;
}
describe t1;

--echo connection: $con_status (for show locks);
connection $con_status;
--echo # check metadata_locks
SELECT (COUNT(*) > 0) AS should_be_non_zero
FROM performance_schema.metadata_locks
WHERE object_schema = "test" AND source <> "";

--echo connection: $con_status (for show processlist)
connection $con_status;
--echo # both $con_block and $con_kill exist
--sorted_result
--replace_column 1 # 3 # 5 # 6 # 7 # 8 # 9 # 10 # 11 #
show processlist;


--echo connection: $con_kill
--connection $con_kill
# command will fail without high_priority_dll/slave_high_priority_ddl set
if ($throw_error) {
  --error ER_LOCK_WAIT_TIMEOUT
  --eval $cmd
}
if (!$throw_error) {
  --eval $cmd
}

eval set $high_priority_ddl = 1;

if (!$slave_ddl) {
  --echo connection: $con_kill
  --connection $con_kill
  
  if (!$should_kill) {
    # regular user ddl will fail regardless of $high_priority_ddl being on
    --error ER_LOCK_WAIT_TIMEOUT
    --eval $cmd
   }

   if ($should_kill) {
     --echo eval $cmd
     --eval $cmd
   }
}

if ($slave_ddl) {
  # on slave
  --echo connection : $con_status([slave] : show status)
  connection $con_status;
  let $before_slave_high_priority_ddl_executed = query_get_value(
      SHOW STATUS LIKE 'slave_high_priority_ddl_executed', Value, 1);
  let $before_slave_high_priority_ddl_killed_connections = query_get_value(
      SHOW STATUS LIKE 'slave_high_priority_ddl_killed_connections', Value, 1);

  --echo # execute DDL command on master
  echo connection $con_init;
  connection $con_init;
  eval $cmd;

  --echo # sync slave with master
  if($should_kill) {
    # on master
    --source include/sync_slave_sql_with_master.inc

    # on slave
    --echo connection: $con_status ([slave]: show locks)
    connection $con_status;
    LET $wait_condition= SELECT COUNT(*) = 0
    FROM performance_schema.metadata_locks
    WHERE object_type LIKE 'TABLE' AND object_name LIKE 't1';
    --source include/wait_condition.inc

    --echo connection: $con_status (for show processlist on slave)
    connection $con_status;
    --sorted_result
    --replace_column 1 # 3 # 5 # 6 # 7 # 8 # 9 # 10 # 11 #
    show processlist;

    let $after_slave_high_priority_ddl_executed = query_get_value(
        SHOW STATUS LIKE 'slave_high_priority_ddl_executed', Value, 1);
    let $assert_text = high priority DDL should issue one kill;
    let $assert_cond = $after_slave_high_priority_ddl_executed - $before_slave_high_priority_ddl_executed = 1;
    source include/assert.inc;

    let $after_slave_high_priority_ddl_killed_connections = query_get_value(
        SHOW STATUS LIKE 'slave_high_priority_ddl_killed_connections', Value, 1);
    let $assert_text = high priority DDL should be kill at least one connection;
    let $assert_cond = ($after_slave_high_priority_ddl_killed_connections >=
      $before_slave_high_priority_ddl_killed_connections + 1);
    source include/assert.inc;
  }
  if (!$should_kill) {
    --echo connection: $con_kill
    connection $con_kill;
    let $slave_sql_errno= 1205;
    source include/wait_for_slave_sql_error.inc;
  }

  --echo connection: $con_kill
  connection $con_kill;
  if ($should_kill) {
     if ($should_table_exist) {
      eval show create table $tablename;
      eval select * from $tablename;
    }
    if (!$should_table_exist) {
      --error 1146
      eval show create table $tablename;
      --error 1146
      eval select * from $tablename;
    }
  }
}

# reset high_priority_ddl/slave_high_priority_ddl
echo connection: $con_kill (for reset variable);
connection $con_kill;
eval set $high_priority_ddl = 0;

# enumerate locks
echo connection: $con_status (for show locks);
connection $con_status;
echo # check metadata_locks
SELECT COUNT(*) as should_be_zero FROM performance_schema.metadata_locks
WHERE object_schema = "test" AND source <> "";

echo connection: $con_status (for show processlist);
connection $con_status;
--sorted_result
--replace_column 1 # 3 # 5 # 6 # 7 # 8 # 9 # 10 # 11 #
show processlist;

disconnect $con_block;

if ($slave_ddl) {
  if (!$should_kill) {
    echo connection: $con_status (for show locks);
    connection $con_status;
    START REPLICA SQL_THREAD;
    source include/wait_for_slave_sql_to_start.inc;
  }
}

--source include/wait_until_count_sessions.inc

# restore global variable
--echo connection: $con_kill
connection $con_kill;
set @@high_priority_lock_wait_timeout = @m_high_priority_lock_wait_timout;
set @@lock_wait_timeout = @m_lock_wait_timeout;
if ($slave_ddl) {
  --echo connection: $con_kill (for restore)
  connection $con_kill;
  set global slave_high_priority_lock_wait_timeout =
  @m_slave_high_priority_lock_wait_timeout;

  echo connection $con_init;
  connection $con_init;
}
