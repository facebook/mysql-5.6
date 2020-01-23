let $repl_status_semisync=`SELECT info FROM information_schema.processlist WHERE info like 'Semisync%' limit 1`;
if (!$repl_status_semisync) {
   let $repl_status_semisync=CANNOTFIND;
}
let $repl_status_async=`SELECT info FROM information_schema.processlist WHERE info like 'Async%' limit 1`;
if (!$repl_status_async) {
   let $repl_status_async=CANNOTFIND;
}

--replace_result $SLAVE_MYPORT SLAVE_PORT $DEFAULT_MASTER_PORT DEFAULT_PORT $repl_status_semisync REPL_STATUS_SEMISYNC $repl_status_async REPL_STATUS_ASYNC
--replace_regex /[0-9, a-f]{8}-[0-9, a-f]{4}-[0-9, a-f]{4}-[0-9, a-f]{4}-[0-9, a-f]{12}/SLAVE_UUID/
SHOW SLAVE HOSTS;
