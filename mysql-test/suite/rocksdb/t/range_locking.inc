#
#  Range locking tests.
#

--source include/have_rocksdb.inc
--source suite/rocksdb/include/have_range_locking.inc

--enable_connect_log


show variables like 'rocksdb_use_range_locking';

eval create table t1 (
  pk int,
  a int,
  primary key (pk) comment '$pk_cf'
) engine=rocksdb;

insert into t1 values
(10,10),(20,20),(30,30);

connect (con1,localhost,root,,);
connect (con2,localhost,root,,);

--echo ### Test: check that range lock inhibits a point lock
connection con1;
begin;
select * from t1 where pk between 5 and 25 for update;

connection con2;
--error ER_LOCK_WAIT_TIMEOUT
insert into t1 values (15,15);

connection con1;
rollback;

--echo ## Test: check that range lock inhibits another range lock
connection con1;
begin;
select * from t1 where pk between 5 and 25 for update;

connection con2;
begin;
--error ER_LOCK_WAIT_TIMEOUT
select * from t1 where pk between 15 and 35 for update;
rollback;

connection con1;
rollback;

--echo ## Test: check that regular read does not get a range lock
connection con1;
begin;
select * from t1 where pk between 5 and 25;

connection con2;
begin;
# This must not block
select * from t1 where pk between 15 and 35 for update;
rollback;

connection con1;
rollback;

--echo ## Test that locks are not released when a statement inside 
--echo ## a transaction is rolled back
eval
create table t2 (
  pk int,
  a int,
  primary key (pk) comment '$pk_cf',
  unique key(a) comment '$sk_cf'
) engine=rocksdb;

insert into t2 values (1,1),(2,2);

begin;
insert into t2 values (3,3);
--error ER_DUP_ENTRY
insert into t2 values (10,2);

connection con2;
begin;
# This must time out:
--error ER_LOCK_WAIT_TIMEOUT
select * from t2 where pk=3 for update;

rollback;
connection con1;
rollback;
drop table t2;

# cleanup
connection default;
disconnect con1;
disconnect con2;
drop table t1;

--echo #
--echo # Test INFORMATION_SCHEMA.lock_info in range-locking mode
--echo #

connect (con1,localhost,root,,);
connection con1;
eval create table t0 (a int primary key);
begin;
insert into t0 values (1);
connection default;


eval
create table t1 (
  pk int,
  a int,
  primary key (pk) comment '$pk_cf'
) engine=rocksdb;

insert into t1 values
(10,10),(20,20),(30,30);

begin;
select * from t1 where pk=10 for update;

#let TRX1_ID=`(select transaction_id from information_schema.rocksdb_trx where thread_id=connection_id())` ;
let $select_from_is_rowlocks_current_trx_only=1;
--source suite/rocksdb/include/select_from_is_rowlocks.inc

delete from t1 where pk between 25 and 40;

--source suite/rocksdb/include/select_from_is_rowlocks.inc

rollback;
begin;
--echo # The following will show a range lock on 2-9 and also a point lock on 10.
--echo # This is how things currently work. (after MDEV-21314, not anymore)
select * from t1 where pk between 2 and 9 for update;
--source suite/rocksdb/include/select_from_is_rowlocks.inc
rollback;

drop table t1;
connection con1;
rollback;
drop table t0;
connection default;
disconnect con1;

--echo #
--echo # MDEV-18104: MyRocks-Gap-Lock: range locking bounds are incorrect for multi-part keys
--echo #

create table t0(a int);
insert into t0 values (0),(1),(2),(3),(4),(5),(6),(7),(8),(9);
eval
create table t1 (
  kp1 int not null,
  kp2 int not null,
  a int,
  primary key(kp1, kp2) comment '$pk_cf'
) engine=rocksdb;

insert into t1 select 1, a, 1234 from t0;
insert into t1 select 2, a, 1234 from t0;
insert into t1 select 3, a, 1234 from t0;

connect (con1,localhost,root,,);
connection con1;

begin;
select * from t1 where kp1=2 for update;

connection default;
--echo # The lock on kp1=2 should inhibit the following INSERT:
--error ER_LOCK_WAIT_TIMEOUT
insert into t1 values ( 2,5,9999); 
rollback;

connection con1;
rollback;
connection default;
disconnect con1;
drop table t0,t1;

--echo #
--echo # Test that locks on ranges on non-unique secondary keys inhibit
--echo # modifications of the contents of these ranges
--echo #

create table t0(a int);
insert into t0 values (0),(1),(2),(3),(4),(5),(6),(7),(8),(9);
eval
create table t1 (
  kp1 int not null,
  kp2 int not null,
  a int,
  key(kp1, kp2) comment '$pk_cf'
) engine=rocksdb;

insert into t1 select  1, a, 1234 from t0;
insert into t1 values (2, 3, 1234);
insert into t1 values (2, 5, 1234);
insert into t1 values (2, 7, 1234);
insert into t1 select  3, a, 1234 from t0;

connect (con1,localhost,root,,);
connection con1;
begin;
--replace_column 10 #
explain
select * from t1 where kp1=2 for update;
select * from t1 where kp1=2 for update;

connection default;
begin;
--error ER_LOCK_WAIT_TIMEOUT
insert into t1 values (2, 9, 9999);

--error ER_LOCK_WAIT_TIMEOUT
delete from t1 where kp1=2 and kp2=5;

# Update that "moves a row away" from the locked range
--error ER_LOCK_WAIT_TIMEOUT
update t1 set kp1=333 where kp1=2 and kp2=3;

# Update that "moves a row into" the locked range
--error ER_LOCK_WAIT_TIMEOUT
update t1 set kp1=2 where kp1=1 and kp2=8;

rollback;

connection con1;
rollback;
disconnect con1;
connection default;
drop table t0,t1;

--echo #
--echo # Transaction isolation test
--echo #

create table t1 (pk int primary key, a int) engine=rocksdb;
insert into t1 values (1,1),(2,2),(3,3);

connect (con1,localhost,root,,);

--echo # TRX1: Start, Allocate a snapshot
connection con1;
begin;
select * from t1;

--echo # TRX2: Make a change that TRX1 will not see
connection default;
update t1 set a=2222 where pk=2;

--echo # TRX1: Now, make a change that would overwrite TRX2'x change and commit
connection con1;
update t1 set a=a+1 where pk=2;
commit;

--echo # Examine the result:
--echo #   pk=2, a=2223 means UPDATE in TRX1 used "read committed" (InnoDB-like isolation)
--echo #   pk=2, a=3 means UPDATE in TRX1 silently overwrote TRX2
--echo #   (and with key tracking, one would get an error on the second UPDATE)
connection default;
select * from t1;

disconnect con1;
connection default;
drop table t1;

--echo #
--echo # Same test as above, but check the range scan
--echo #

eval
create table t1 (
  pk int,
  a int,
  primary key (pk) comment '$pk_cf'
) engine=rocksdb;
insert into t1 values (1,1),(2,2),(3,3),(4,4),(5,5),(6,6);

connect (con1,localhost,root,,);

--echo # TRX1: Start, Allocate a snapshot
connection con1;
begin;
select * from t1;

--echo # TRX2: Make a change that TRX1 will not see
connection default;
update t1 set a=2222 where pk between 3 and 5;

--echo # TRX1: Now, make a change that would overwrite TRX2'x change and commit
connection con1;
update t1 set a=a+1 where pk between 3 and 5;
commit;

--echo # Examine the result:
--echo #   pk={3,4,5} a=2223 means UPDATE in TRX1 used "read committed" (InnoDB-like isolation)
connection default;
select * from t1;

disconnect con1;
connection default;
drop table t1;

--echo #
--echo # Same as above, but test SELECT FOR UPDATE.
--echo #
eval
create table t1 (
  pk int,
  a int,
  primary key (pk) comment '$pk_cf'
) engine=rocksdb;
insert into t1 values (1,1),(2,2),(3,3),(4,4),(5,5),(6,6);

connect (con1,localhost,root,,);

--echo # TRX1: Start, Allocate a snapshot
connection con1;
begin;
select * from t1;

--echo # TRX2: Make a change that TRX1 will not see
connection default;
update t1 set a=222 where pk=2;
update t1 set a=333 where pk=3;

--echo # TRX1: Check what select [FOR UPDATE] sees
connection con1;
select * from t1 where pk in (2,3);
select * from t1 where pk=2 for update;
select * from t1 where pk=2;

commit;

disconnect con1;
connection default;
drop table t1;

if (!$PK_USES_REVERSE_CF) {
--echo #
--echo # Another no-snapshot-checking test, this time for single-statement
--echo # transaction
--echo #
eval
create table t1 (
  pk int,
  a int,
  name varchar(16),
  primary key(pk) comment '$pk_cf'
) engine=rocksdb;
insert into t1 values (1,1, 'row1'), (2,2,'row2');

connect (con1,localhost,root,,);
connection con1;
select get_lock('row1', 100);

connection default;

--echo # The following will read the first row (1,1,'row1'), and stop.

send update t1 set a=a+100 where get_lock(name, 1000)=1;

# Wait till the default connection has stopped:
connection con1;

let $wait_condition=
  SELECT COUNT(*) = 1 FROM INFORMATION_SCHEMA.PROCESSLIST WHERE STATE = "User lock"
  AND INFO = "update t1 set a=a+100 where get_lock(name, 1000)=1";
--source include/wait_condition.inc

# Update the second row
update t1 set a=5 where pk=2;

select release_lock('row1');

connection default;
reap;

--echo # Look at the row with pk=2:
--echo #  2, 105, row2 - means the UPDATE was reading current data (Correct)
--echo #  2, 102, row - means the UPDATE read the snapshot (incorrect)
select * from t1;

--echo # Try releasing both locks (in 5.6, we will be holding only the second one)
select release_lock(name) from t1;

disconnect con1;
connection default;
drop table t1;
}

--echo #
--echo # Check that I_S.processlist.state is set correctly now.
--echo #
eval
create table t1(
  pk int,
  a int,
  primary key(pk) comment '$pk_cf'
) engine=rocksdb;
insert into t1 values (1,1),(2,2),(3,3);

begin;
select * from t1 where pk=2 for update;

--connect (con1,localhost,root,,)
begin;
set rocksdb_lock_wait_timeout=300;
send select * from t1 where pk=2 for update;

connection default;
--echo # Now, will wait until we see con1 have state="Waiting for row lock"
let $wait_condition=
  SELECT COUNT(*) = 1 FROM INFORMATION_SCHEMA.PROCESSLIST WHERE STATE = "Waiting for row lock"
  AND INFO = "select * from t1 where pk=2 for update";
--source include/wait_condition.inc

rollback;
connection con1;
--reap
rollback;

disconnect con1;
connection default;
drop table t1;

--echo #
--echo # Test range locking for ranges with HA_READ_PREFIX_LAST
--echo #
create table t0(a int) engine=rocksdb;
insert into t0 values (0),(1),(2),(3),(4),(5),(6),(7),(8),(9);

eval
create table t1 (
  pk1 int,
  pk2 int,
  a int,
  primary key(pk1, pk2) comment '$pk_cf'
) engine=rocksdb;

insert into t1 
select 
  A.a, B.a, A.a*10+B.a
from
  t0 A, t0 B;


# Get a lock in another connection so that the primary transaction is not using
# STO optimization, and its locks can be seen in I_S.rocksdb_locks
--connect (con1,localhost,root,,)
connection con1;
begin;
insert into t1 values (0x1112222,0x1112222,0);

connection default;
begin;
--echo # Should use ref access w/o filesort:
--replace_column 10 #
explain
select * from t1
where pk1=3
order by pk1 desc, pk2 desc
for update;

select * from t1
where pk1=3
order by pk1 desc, pk2 desc
for update;

let $select_from_is_rowlocks_current_trx_only=1;
--source suite/rocksdb/include/select_from_is_rowlocks.inc
rollback;

--echo #
--echo # Test range locking for ranges with HA_READ_PREFIX_LAST_OR_PREV
--echo #

begin;
--echo # Should use range access with 2 keyparts and w/o filesort:
--replace_column 10 #
explain 
select * from t1 
where pk1=4 and pk2 between 5 and 8 
order by pk1 desc, pk2 desc
for update;

select * from t1
where pk1=4  and pk2 between 5 and 8 
order by pk1 desc, pk2 desc
for update;

let $select_from_is_rowlocks_current_trx_only=1;
--source suite/rocksdb/include/select_from_is_rowlocks.inc
rollback;

connection con1;
rollback;

connection default;
drop table t0, t1;

--echo #
--echo # A bug: range locking was not used when scan started at table start or end
--echo #
create table t0(a int);
insert into t0 values (0),(1),(2),(3),(4),(5),(6),(7),(8),(9);
create table t10(a int);
insert into t10 select A.a + B.a* 10 + C.a * 100 from t0 A, t0 B, t0 C;

create table t1 (
  pk int not null,
  a int,
  primary key(pk)
) engine=rocksdb;

insert into t1 select a*2,a*2 from t10;

connection con1;
begin;
select * from t1 where pk=500 for update;
connection default;

begin;
select * from t1 where pk<10 order by pk limit 10 for update;

let $select_from_is_rowlocks_current_trx_only=1;
--source suite/rocksdb/include/select_from_is_rowlocks.inc
rollback;

begin;
select * from t1 where pk>1990 order by pk desc limit 10 for update;
let $select_from_is_rowlocks_current_trx_only=1;
--source suite/rocksdb/include/select_from_is_rowlocks.inc
rollback;

connection con1;
rollback;
disconnect con1;

connection default;
drop table t0,t10,t1;

--echo #
--echo # Range locking and READ-COMMITTED isolation level
--echo #
connect (con1,localhost,root,,);
connection con1;
set session transaction isolation level read committed;
create table t1 (
  pk int not null,
  a int,
  primary key(pk)
) engine=rocksdb;

insert into t1(pk) values (0),(1),(2),(3),(4),(5),(6),(7),(8),(9);

begin;
select * from t1 where pk between 2 and 5 for update;
let $select_from_is_rowlocks_current_trx_only=1;
--echo # Below should show individual row locks, not locked range:
--source suite/rocksdb/include/select_from_is_rowlocks.inc
rollback;

begin;
update t1 set a=a+1 where pk between 2 and 5;
let $select_from_is_rowlocks_current_trx_only=1;
--echo # Below should show individual row locks, not locked range:
--source suite/rocksdb/include/select_from_is_rowlocks.inc
rollback;

drop table t1;
disconnect con1;
connection default;

