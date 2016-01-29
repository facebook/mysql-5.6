set -e

binlog_line=($(grep -m 1 -o "Last MySQL binlog file position 0 [0-9]*, file name .*\.[0-9]*" ${MYSQLTEST_VARDIR}/log/xtrabackup_restore_log))
binlog_pos=${binlog_line[6]%?}
binlog_file=${binlog_line[9]}
sql="change master to master_host='127.0.0.1', master_port=${MASTER_MYPORT}, master_user='root', master_log_file='$binlog_file', master_log_pos=$binlog_pos, master_connect_retry=1; start slave"
$MYSQL --defaults-group-suffix=.2 -e "$sql"
