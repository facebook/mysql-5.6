# Test for bug https://bugs.launchpad.net/percona-xtrabackup/+bug/737569
. inc/common.sh

start_server

innobackupex --no-timestamp $topdir/backup
backup_dir=$topdir/backup
vlog "Backup created in directory $backup_dir"

stop_server
# Remove datadir
rm -r $mysql_datadir
# Restore backup
vlog "Applying log"
vlog "###########"
vlog "# PREPARE #"
vlog "###########"
innobackupex --apply-log $backup_dir
vlog "Restoring MySQL datadir"
mkdir -p $mysql_datadir
touch $mysql_datadir/anyfile
vlog "###########"
vlog "# RESTORE #"
vlog "###########"
run_cmd_expect_failure $IB_BIN $IB_ARGS --copy-back $backup_dir

if grep -q "is not empty!" $OUTFILE
then
    vlog "innobackupex reported error about non-empty dirrectory correctly"
    exit 0
else
    vlog "innobackupex did not report an error about non-empty dir"
    exit 1
fi
