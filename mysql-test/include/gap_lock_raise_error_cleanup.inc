
set global gap_lock_write_log= 0;
set global gap_lock_raise_error= 0;

drop table if exists t1, t2, t3;
DROP DATABASE mysqlslap;

let $gap_lock_log_file_orig=`select concat(@@gap_lock_log_file, '_orig')`;

--move_file $gap_lock_log_file $gap_lock_log_file_orig
--replace_result $gap_lock_log_file_orig <GAP_LOCK_ORIG>
eval SET GLOBAL gap_lock_log_file='$gap_lock_log_file_orig';
--replace_result $gap_lock_log_file <GAP_LOCK>
eval SET GLOBAL gap_lock_log_file='$gap_lock_log_file';

