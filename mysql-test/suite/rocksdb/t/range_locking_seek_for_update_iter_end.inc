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

