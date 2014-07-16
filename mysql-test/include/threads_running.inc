
--source include/master-slave.inc

let $status_var= Threads_binlog_client;
let $status_var_value= 1;
--source include/wait_for_status_var.inc

let $status_var= Threads_running;
let $status_var_value= 2;
--source include/wait_for_status_var.inc

connection slave;
--source include/stop_slave.inc

connection master;

FLUSH LOGS;
CREATE TABLE t1 (a INT);
INSERT INTO t1 VALUES (1);
DROP TABLE t1;
FLUSH LOGS;

# Binlog dump thread may be in the process of cleanup, so we need
# to wait here.
let $status_var= Threads_binlog_client;
let $status_var_value= 0;
--source include/wait_for_status_var.inc

let $status_var= Threads_running;
let $status_var_value= 2;
--source include/wait_for_status_var.inc

connection slave;
source include/start_slave.inc;

--source include/rpl_end.inc
