set -e

slave_socket=$1; shift
master_port=$1; shift
master_socket=$1; shift
binlog_line=($(grep -m 1 -o "Last MySQL binlog file position 0 [0-9]*, file name .*\.[0-9]*" ${MYSQL_TMP_DIR}/xtrabackup_restore_log))
binlog_pos=${binlog_line[6]%?}
binlog_file=${binlog_line[9]}

sql="show gtid_executed in '$binlog_file' from $binlog_pos"
result=($(mysql --user=root --socket=$master_socket -e "$sql"))
gtid_executed=${result[1]}

sql="reset master;"
sql="$sql reset slave;"
sql="$sql change master to master_host='127.0.0.1', master_port=$master_port, master_user='root', master_auto_position=1, master_connect_retry=1;"
sql="$sql set global gtid_purged='$gtid_executed';"
sql="$sql start slave;"
sql="$sql stop slave;"
sql="$sql change master to master_auto_position=0;"
sql="$sql start slave;"
mysql --user=root --socket=$slave_socket -e "$sql"
echo "$sql" > ${MYSQL_TMP_DIR}/gtid_stmt
