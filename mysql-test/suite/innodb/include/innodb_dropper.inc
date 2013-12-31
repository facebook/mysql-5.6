# Do inserts, updates, deletes concurrent with DROP TABLE

# create the directory for temporary log files.
--exec rm -rf $MYSQL_TMP_DIR/innodb_dropper
--exec mkdir $MYSQL_TMP_DIR/innodb_dropper

call mtr.add_suppression("InnoDB: Warning: Small buffer pool size.*");
call mtr.add_suppression("InnoDB: Error: trying to do i/o to a tablespace which does not exist");
call mtr.add_suppression("InnoDB: Error: trying to do ibuf merge to a dropped tablespace.*");

let $pid_file = `SELECT @@pid_file`;

let $exec =
python suite/innodb/t/table_dropper.py $pid_file $MYSQL_TMP_DIR/innodb_dropper
$min_records $max_records $num_workers $test_seconds $use_blob root 127.0.0.1 $MASTER_MYPORT;

exec $exec;

