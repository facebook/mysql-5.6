
set @save_gap_lock_write_log = @@gap_lock_write_log;
set @save_gap_lock_raise_error = @@gap_lock_raise_error;

set gap_lock_write_log = 1;
set gap_lock_raise_error = 0;

begin;
# log should be written exactly once
update x1 set a= (select 1+max(a) from x2 where x2.pk between 1 and 3 and x2.b=x1.b);
--exec cat $gap_lock_log_file | grep 'select 1' | wc -l

update x1 set a= (select 2+max(a) from x2 where x2.pk between 1 and 3 and x2.b=x1.b);update x1 set a= (select 3+max(a) from x2 where x2.pk between 1 and 3 and x2.b=x1.b);
--exec cat $gap_lock_log_file | grep 'select 2' | wc -l
--exec cat $gap_lock_log_file | grep 'select 3' | wc -l

rollback;


set gap_lock_write_log = @save_gap_lock_write_log;
set gap_lock_raise_error = @save_gap_lock_raise_error;
