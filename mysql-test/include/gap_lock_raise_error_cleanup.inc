
set global gap_lock_write_log= 0;
set global gap_lock_raise_error= 0;

drop table if exists t1, t2, t3;
drop table if exists x1, x2;
DROP DATABASE mysqlslap;

--exec grep "Got error" $MYSQLTEST_VARDIR/log/mysqld.1.err | wc -l

let $gap_lock_log_file_orig=`select concat(@@gap_lock_log_file, '_orig')`;
let $gap_lock_log_file_orig2=`select concat(@@gap_lock_log_file, '_orig2')`;

--move_file $gap_lock_log_file $gap_lock_log_file_orig
--replace_result $gap_lock_log_file_orig <GAP_LOCK_ORIG>
eval SET GLOBAL gap_lock_log_file='$gap_lock_log_file_orig';
--replace_result $gap_lock_log_file <GAP_LOCK>
eval SET GLOBAL gap_lock_log_file='$gap_lock_log_file';
--move_file $gap_lock_log_file $gap_lock_log_file_orig2
flush general logs;
