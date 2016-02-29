
set @save_gap_lock_write_log = @@gap_lock_write_log;
set @save_gap_lock_raise_error = @@gap_lock_raise_error;

set gap_lock_write_log = 1;
set gap_lock_raise_error = 0;

begin;
# log should be written exactly once
update gap4 set a= (select 1+max(a) from gap5 where gap5.pk between 1 and 3 and gap5.b=gap4.b);
--exec cat $gap_lock_log_file | grep 'select 1' | wc -l

update gap4 set a= (select 2+max(a) from gap5 where gap5.pk between 1 and 3 and gap5.b=gap4.b);update gap4 set a= (select 3+max(a) from gap5 where gap5.pk between 1 and 3 and gap5.b=gap4.b);
--exec cat $gap_lock_log_file | grep 'select 2' | wc -l
--exec cat $gap_lock_log_file | grep 'select 3' | wc -l

# No bogus logs
--exec cat $gap_lock_log_file | grep -e 'started with' -e 'Tcp port:' -e 'Argument' | wc -l
flush logs;
--exec cat $gap_lock_log_file | grep -e 'started with' -e 'Tcp port:' -e 'Argument' | wc -l

rollback;


set gap_lock_write_log = @save_gap_lock_write_log;
set gap_lock_raise_error = @save_gap_lock_raise_error;
