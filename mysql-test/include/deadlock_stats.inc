eval set @prior_lock_wait_timeout = $engine_lwt;
eval set @prior_deadlock_detect = $engine_dld;
eval set global $raw_engine_lwt = 100000;
eval set global $raw_engine_dld = ON;

--source include/count_sessions.inc
connect (con1,localhost,root,,);
let $con1= `SELECT CONNECTION_ID()`;

connect (con2,localhost,root,,);
let $con2= `SELECT CONNECTION_ID()`;

connection default;
eval create table t (i int primary key) engine=$engine;
insert into t values (1), (2), (3);

--source include/simple_deadlock.inc
connection default;
select row_lock_deadlocks from information_schema.table_statistics where
table_name = "t";

select row_lock_deadlocks from information_schema.table_statistics where
table_name = "t";
--source include/simple_deadlock.inc
connection default;
select row_lock_deadlocks from information_schema.table_statistics where
table_name = "t";

select row_lock_deadlocks from information_schema.table_statistics where
table_name = "t";

disconnect con1;
disconnect con2;

eval set global $raw_engine_lwt = @prior_lock_wait_timeout;
eval set global $raw_engine_dld = @prior_deadlock_detect;
drop table t;
--source include/wait_until_count_sessions.inc
