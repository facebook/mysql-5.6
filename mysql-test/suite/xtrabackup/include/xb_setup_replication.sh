slave_socket=$1; shift
master_port=$1; shift
binlog_line=($(grep -m 1 -o "Last MySQL binlog file position 0 [0-9]*, file name .*\.[0-9]*" ${MYSQL_TMP_DIR}/xtrabackup_restore_log))
binlog_pos=${binlog_line[6]%?}
binlog_file=${binlog_line[9]}
sql="change master to master_host='127.0.0.1', master_port=$master_port, master_user='root', master_log_file='$binlog_file', master_log_pos=$binlog_pos, master_connect_retry=1; start slave"
mysql --user=root --socket=$slave_socket -e "$sql"
