########################################################################
# Bug #817132: innobackupex copy-back doesn't work without ibbackup
########################################################################

. inc/common.sh

# innobackupex --copy-back without explicit --ibbackup specification
# defaults to 'xtrabackup'. So any build configurations other than xtradb51
# would fail in Jenkins.
if [ "`basename $XB_BIN`" != "xtrabackup" ]; then
    echo "Requires xtradb51" > $SKIPPED_REASON
    exit $SKIPPED_EXIT_CODE
fi

start_server
load_dbase_schema sakila
load_dbase_data sakila

mkdir -p $topdir/backup
innobackupex  $topdir/backup
backup_dir=`grep "innobackupex: Backup created in directory" $OUTFILE | awk -F\' '{ print $2}'`
vlog "Backup created in directory $backup_dir"

stop_server
# Remove datadir
rm -r $mysql_datadir

# Restore sakila
vlog "Applying log"
innobackupex --apply-log $backup_dir
vlog "Restoring MySQL datadir"
mkdir -p $mysql_datadir

# The following would fail before #817132 was fixed.
# Cannot use innobackupex here, because that would specify
# --ibbackup explicitly (see $IB_ARGS).
run_cmd $IB_BIN --defaults-file=$topdir/my.cnf --user=root --socket=$mysql_socket --copy-back $backup_dir

start_server
# Check sakila
run_cmd ${MYSQL} ${MYSQL_ARGS} -e "SELECT count(*) from actor" sakila
