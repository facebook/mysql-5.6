

--source include/have_rocksdb.inc
--source include/have_debug_sync.inc
--source suite/rocksdb/include/have_range_locking.inc
--enable_connect_log
show variables like 'rocksdb_use_range_locking';


create table t0(a int primary key);
insert into t0 values (0),(1),(2),(3),(4),(5),(6),(7),(8),(9);

eval create table t1 (
  pk int,
  a int,
  primary key (pk) comment '$cf'
) engine=rocksdb;

insert into t1 (pk)
select
  A.a + B.a*10 + C.a*100
from 
  t0 A, t0 B, t0 C;
delete from t1 where pk<100;

connect (con1,localhost,root,,);
connection con1;

begin;
set debug_sync='rocksdb.locking_iter_scan SIGNAL about_to_lock_range WAIT_FOR spoiler_inserted';
send
select * from t1 where pk >=5 order by pk limit 5 for update;

connection default;
set debug_sync='now WAIT_FOR about_to_lock_range';
insert into t1 (pk) values 
(10),(20),(30),(40),(50);
set debug_sync='now SIGNAL spoiler_inserted';

connection con1;
reap;
--echo # This must return 1, no 5:
select lock_count from information_schema.rocksdb_trx
where thread_id=CONNECTION_ID();

rollback;
disconnect con1;
connection default;
drop table t0, t1;

--echo #
--echo # A testcase for locking at the end of the scan
--echo #
eval create table t1 (
  pk int,
  primary key (pk) comment '$cf'
) engine=rocksdb;

connect (con1,localhost,root,,);
connection con1;

insert into t1 values (1), (10), (100);

begin;
select * from t1 for update;

connection default;
select * from t1;

--error ER_LOCK_WAIT_TIMEOUT
insert into t1 values (150);

connection con1;
rollback;

begin;
--replace_column 10 #
explain
select * from t1 order by pk desc for update;
select * from t1 order by pk desc for update;

connection default;
select * from t1;

--error ER_LOCK_WAIT_TIMEOUT
insert into t1 values (0);

disconnect con1;
connection default;
drop table t1;
