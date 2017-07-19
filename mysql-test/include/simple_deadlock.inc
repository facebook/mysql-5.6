connection con1;
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
