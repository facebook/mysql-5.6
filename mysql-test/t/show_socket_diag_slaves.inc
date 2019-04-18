--let $repl_status_semisync = `SELECT info FROM information_schema.processlist WHERE info LIKE 'Semisync%' LIMIT 1`
if (!$repl_status_semisync) {
   --let $repl_status_semisync = CANNOTFIND
}
--let $repl_status_async = `SELECT info FROM information_schema.processlist WHERE info LIKE 'Async%' LIMIT 1`
if (!$repl_status_async) {
   --let $repl_status_async = CANNOTFIND
}

--replace_result $SLAVE_MYPORT SLAVE_PORT $MASTER_MYPORT MASTER_PORT $repl_status_semisync REPL_STATUS_SEMISYNC $repl_status_async REPL_STATUS_ASYNC
--replace_regex /[[:xdigit:]]{8}-[[:xdigit:]]{4}-[[:xdigit:]]{4}-[[:xdigit:]]{4}-[[:xdigit:]]{12}/SLAVE_UUID/
--replace_column 1 <id> 3 <state> 4 <local_ip> 5 <remote_ip> 6 <uid> 7 <inode> 8 <rqueue> 9 <wqueue> 10 <retrans> 11 <lost> 12 <total_retrans> 15 <host>
SELECT * FROM information_schema.socket_diag_slaves;
