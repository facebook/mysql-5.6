
let $gap_lock_log_file=`select @@gap_lock_log_file`;
--let $warning_or_error= ERROR
--source include/gap_lock_raise_error_init.inc

--source include/gap_lock_raise_error_log.inc
--source include/gap_lock_raise_error_mysqlslap.inc

let $select_lock=for update;
let $autocommit = 0;
--source include/gap_lock_raise_error_select.inc
let $autocommit = 1;
--source include/gap_lock_raise_error_select.inc

let $select_lock=lock in share mode;
let $autocommit = 0;
--source include/gap_lock_raise_error_select.inc
let $autocommit = 1;
--source include/gap_lock_raise_error_select.inc

let $select_lock=;
let $autocommit = 0;
--source include/gap_lock_raise_error_select.inc
let $autocommit = 1;
--source include/gap_lock_raise_error_select.inc

--source include/gap_lock_raise_error_update.inc
--source include/gap_lock_raise_error_cleanup.inc

