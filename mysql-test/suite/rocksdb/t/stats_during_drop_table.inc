--source include/count_sessions.inc

--disable_query_log
#call mtr.add_suppression("Cannot mark Column family.*because it is in use");
--enable_query_log

set @old_debug = @@global.debug;

--disable_warnings
drop table if exists t1;
--enable_warnings

connect (conn1,localhost,root,,);
connect (conn2,localhost,root,,);
connect (conn3,localhost,root,,);

## test1 ##
--connection conn1
set @@global.debug = '+d,rocksdb_before_delete_table';
create table t1 (
  id1 int(10) unsigned not null default '0',
  primary key (id1) comment 'cf_primary_key'
  ) engine=rocksdb;

send drop table t1;

--connection conn2
set debug_sync = "now wait_for ready_to_mark_cf_dropped_before_delete_table";

# replace_column is in reap
send SHOW TABLE STATUS LIKE 't1';

--connection conn3
let $wait_condition= SELECT count(*) = 1 FROM INFORMATION_SCHEMA.PROCESSLIST WHERE State = 'Waiting for table metadata lock';
--source include/wait_condition.inc
set debug_sync = "now signal mark_cf_dropped_done_before_delete_table";

--connection conn1
reap;
show tables;

--connection conn2
--replace_column 12 # 13 # 15 #
reap;

set @@global.debug = @old_debug;

## test2 ##
--connection conn1

set @@global.debug = '+d,rocksdb_after_delete_table';
create table t1 (
  id1 int(10) unsigned not null default '0',
  primary key (id1) comment 'cf_primary_key'
  ) engine=rocksdb;

send drop table t1;

--connection conn2
set debug_sync = "now wait_for ready_to_mark_cf_dropped_after_delete_table";

# replace_column is in reap
send SHOW TABLE STATUS LIKE 't1';

--connection conn3
let $wait_condition= SELECT count(*) = 1 FROM INFORMATION_SCHEMA.PROCESSLIST WHERE State = 'Waiting for table metadata lock';
--source include/wait_condition.inc

set debug_sync = "now signal mark_cf_dropped_done_after_delete_table";

--connection conn1
reap;
show tables;

--connection conn2
--replace_column 12 # 13 # 15 #
reap;

set @@global.debug = @old_debug;

--connection default
--disconnect conn1
--disconnect conn2
--disconnect conn3
--source include/wait_until_count_sessions.inc
