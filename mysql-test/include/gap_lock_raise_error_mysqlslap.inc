
set global gap_lock_write_log = 1;
--exec $MYSQL_SLAP --silent --concurrency=50 --number-of-queries=1000 --query="select ceil(rand()*100) into @id;begin; select * from test.gap1 where id1=@id for update; commit;"
set global gap_lock_write_log = 0;
--exec cat $gap_lock_log_file | grep 'select \* from test.gap1 where id1=@id for update' | wc -l

