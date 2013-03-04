########################################################################
# Bug #996493: innobackupex --apply-log doesn't read config from
# backup-my.cnf
########################################################################

. inc/common.sh

start_server

backup_dir=$topdir/backup
innobackupex --no-timestamp $backup_dir
vlog "Backup created in directory $backup_dir"

vlog "Stop mysqld"
stop_server

vlog "Remove datadir"
rm -r $mysql_datadir/*

vlog "Remove my.cnf"
mv $topdir/my.cnf $topdir/my.cnf.bak

trap "vlog restoring $topdir/my.cnf ; \
mv $topdir/my.cnf.bak $topdir/my.cnf" INT TERM EXIT

vlog "Apply log"
# Do not run innobackupex, because it pass option --defaults-file
# which we should avoid. Our goal is to test that backup-my.cnf
# will be read by default when apply-log is run.
run_cmd $IB_BIN --ibbackup=$XB_BIN --apply-log $backup_dir

vlog "Get my.cnf back"
mv $topdir/my.cnf.bak $topdir/my.cnf
trap "vlog test exit" INT TERM EXIT

vlog "Copy back"
innobackupex --copy-back $backup_dir
