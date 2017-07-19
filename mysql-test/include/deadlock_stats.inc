eval set $prior_engine_lwt = $engine_lwt;
eval set $prior_engine_dld= $engine_dld;
eval set global $raw_engine_lwt = 100000;
eval set global $raw_engine_dld = ON;

connect (con1,localhost,root,,);
let $con1= `SELECT CONNECTION_ID()`;

connect (con2,localhost,root,,);
let $con2= `SELECT CONNECTION_ID()`;

connect (con3,localhost,root,,);
let $con3= `SELECT CONNECTION_ID()`;


connection default;
select row_lock_deadlocks from information_schema.table_statistics where
table_name = "t";

eval create table t (i int primary key) engine=$engine;
insert into t values (1), (2), (3);

connection con1;
eval $engine_start_txn;
select * from t where i=1 for update;

connection con2;
eval $engine_start_txn;
select * from t where i=2 for update;

connection con3;
eval $engine_start_txn;
select * from t where i=3 for update;

connection con1;
--send select * from t where i=2 for update

connection con2;
if ($engine == "rocksdb"){
  let $wait_condition=
  `select concat('select count(*) = 1 from information_schema.rocksdb_trx
    where THREAD_ID = ', '$con1', ' and WAITING_KEY != ""')`;
}
if ($engine == "innodb"){
  let $wait_condition=
  `select concat('select count(*) = 1 from information_schema.innodb_trx where
    trx_mysql_thread_id = $con1 and trx_state="LOCK WAIT"')`;
}
--source include/wait_condition.inc

--send select * from t where i=3 for update

connection con3;
if ($engine == "rocksdb"){
  let $wait_condition=
  `select concat('select count(*) = 1 from information_schema.rocksdb_trx
    where THREAD_ID = ', '$con2', ' and WAITING_KEY != ""')`;
}
if ($engine == "innodb"){
  let $wait_condition=
  `select concat('select count(*) = 1 from information_schema.innodb_trx where
    trx_mysql_thread_id = $con2 and trx_state="LOCK WAIT"')`;
}
--source include/wait_condition.inc

--error ER_LOCK_DEADLOCK
select * from t where i=1 for update;
rollback;

connection con2;
--reap
rollback;

connection con1;
--reap
rollback;

connection con1;
select row_lock_deadlocks from information_schema.table_statistics where
table_name = "t";

select row_lock_deadlocks from information_schema.table_statistics where
table_name = "t";

eval $engine_start_txn;
select * from t where i=1 for update;

connection con2;
eval $engine_start_txn;
select * from t where i=2 for update;

connection con1;
--send select * from t where i=2 for update

connection con2;
if ($engine == "rocksdb"){
  let $wait_condition=
  `select concat('select count(*) = 1 from information_schema.rocksdb_trx
    where THREAD_ID = ', '$con1', ' and WAITING_KEY != ""')`;
}
if ($engine == "innodb"){
  let $wait_condition=
  `select concat('select count(*) = 1 from information_schema.innodb_trx where
    trx_mysql_thread_id = $con1 and trx_state="LOCK WAIT"')`;
}
--source include/wait_condition.inc

--error ER_LOCK_DEADLOCK
select * from t where i=1 for update;
rollback;

connection con1;
--reap
rollback;

connection default;
select row_lock_deadlocks from information_schema.table_statistics where
table_name = "t";

select row_lock_deadlocks from information_schema.table_statistics where
table_name = "t";

disconnect con1;
disconnect con2;
disconnect con3;

eval set global $raw_engine_lwt = $prior_engine_lwt;
eval set global $raw_engine_dld = $prior_engine_dld;
drop table t;
