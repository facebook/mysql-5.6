. inc/common.sh

if ! $XB_BIN --help 2>&1 | grep -q debug-sync; then
    echo "Requires --debug-sync support" > $SKIPPED_REASON
    exit $SKIPPED_EXIT_CODE
fi

start_server --innodb_log_file_size=1M --innodb_thread_concurrency=1 \
    --innodb_log_buffer_size=1M

load_dbase_schema sakila
load_dbase_data sakila
mkdir $topdir/backup

run_cmd_expect_failure $XB_BIN $XB_ARGS --datadir=$mysql_datadir --backup \
    --innodb_log_file_size=1M --target-dir=$topdir/backup \
    --debug-sync="xtrabackup_copy_logfile_pause" &

job_pid=$!

pid_file=$topdir/backup/xtrabackup_debug_sync

# Wait for xtrabackup to suspend
i=0
while [ ! -r "$pid_file" ]
do
    sleep 1
    i=$((i+1))
    echo "Waited $i seconds for $pid_file to be created"
done

xb_pid=`cat $pid_file`

# Create 4M+ of log data

$MYSQL $MYSQL_ARGS -Ns -e "CREATE TABLE tmp1 ENGINE=InnoDB SELECT * FROM payment" sakila
$MYSQL $MYSQL_ARGS -Ns -e "CREATE TABLE tmp2 ENGINE=InnoDB SELECT * FROM payment" sakila
$MYSQL $MYSQL_ARGS -Ns -e "CREATE TABLE tmp3 ENGINE=InnoDB SELECT * FROM payment" sakila

# Resume the xtrabackup process
vlog "Resuming xtrabackup"
kill -SIGCONT $xb_pid

# wait's return code will be the code returned by the background process
run_cmd wait $job_pid
