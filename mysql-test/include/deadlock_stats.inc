eval $engine_deadlock_detection;
eval $engine_lock_wait_timeout;
eval create table t1 (id int primary key) engine=$engine;
insert into t1 values (1),(2);
begin;
select * from t1 where id=1 for update;

--connect (con1,localhost,root,,)
--connection con1
eval $engine_deadlock_detection;
eval $engine_lock_wait_timeout;
select row_lock_deadlocks from information_schema.table_statistics where
table_name="t1";
begin;
select * from t1 where id=2 for update;
send select * from t1 where id=1 for update;

--connection default
--error ER_LOCK_DEADLOCK
select * from t1 where id=2 for update;
rollback;

--connection con1
reap;
commit;

--connection default
select row_lock_deadlocks from information_schema.table_statistics where
table_name="t1"; 
select row_lock_deadlocks from information_schema.table_statistics where
table_name="t1";

begin;
select * from t1 where id=1 for update;

--connection con1
begin;
select * from t1 where id=2 for update;

--connection default
send select * from t1 where id=2 for update;

--connection con1
--error ER_LOCK_DEADLOCK
select * from t1 where id=1 for update;
rollback;

--connection default
reap;
select row_lock_deadlocks from information_schema.table_statistics where
table_name="t1"; 
select row_lock_deadlocks from information_schema.table_statistics where
table_name="t1";
disconnect con1;
drop table t1;
