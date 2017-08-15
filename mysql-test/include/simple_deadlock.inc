connection con1;
begin;
select * from t where i=1 for update;

connection con2;
begin;
select * from t where i=2 for update;

connection con1;
--send select * from t where i=2 for update

connection con2;
if ($engine == "rocksdb"){
  let $wait_condition = select count(*) = 1 from information_schema.rocksdb_trx
  where thread_id = $con1 and waiting_key != "";
}
if ($engine == "innodb"){
  let $wait_condition=
  SELECT COUNT(*) = 1 FROM information_schema.processlist
  WHERE info = "select * from t where i=2 for update" and
  state = "statistics";
}
--source include/wait_condition.inc

--error ER_LOCK_DEADLOCK
select * from t where i=1 for update;
rollback;

connection con1;
--reap
rollback;
