. inc/common.sh

start_server

load_dbase_schema sakila
load_dbase_data sakila

# Take backup
mkdir -p $topdir/backup
xtrabackup --datadir=$mysql_datadir --backup --target-dir=$topdir/backup
vlog "Backup taken, trying stats"
xtrabackup --datadir=$mysql_datadir --prepare --target-dir=$topdir/backup

# First check that xtrabackup fails with the correct error message
# when trying to get stats before creating the log files

vlog "===> xtrabackup --stats --datadir=$topdir/backup"
run_cmd_expect_failure $XB_BIN $XB_ARGS --stats --datadir=$topdir/backup

if ! grep -q "Cannot find log file ib_logfile0" $OUTFILE
then
    die "Cannot find the expected error message from xtrabackup --stats"
fi

# Now create the log files in the backup and try xtrabackup --stats again

xtrabackup --datadir=$mysql_datadir --prepare --target-dir=$topdir/backup

xtrabackup --stats --datadir=$topdir/backup

vlog "stats did not fail"
