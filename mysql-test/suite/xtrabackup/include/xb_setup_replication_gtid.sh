set -e

binlog_line=($(grep -m 1 -o "Last MySQL binlog file position 0 [0-9]*, file name .*\.[0-9]*" ${MYSQLTEST_VARDIR}/log/xtrabackup_restore_log))
binlog_pos=${binlog_line[6]%?}
binlog_file=${binlog_line[9]}

sql="show gtid_executed in '$binlog_file' from $binlog_pos"
result=($($MYSQL --defaults-group-suffix=.1 -e "$sql"))
gtid_executed=${result[1]}

sql="reset master;"
sql="$sql reset slave;"
sql="$sql change master to master_host='127.0.0.1', master_port=${MASTER_MYPORT}, master_user='root', master_auto_position=1, master_connect_retry=1;"
sql="$sql set global gtid_purged='$gtid_executed';"
sql="$sql start slave;"
sql="$sql stop slave;"
sql="$sql change master to master_auto_position=0;"
sql="$sql start slave;"
$MYSQL --defaults-group-suffix=.2 -e "$sql"
echo "$sql" > ${MYSQLTEST_VARDIR}/log/gtid_stmt
