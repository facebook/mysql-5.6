##################################################################
# Bug #514068: Output to STDOUT and STDERR is not conventional	 #
# Bug #741021: xtrabackup --prepare prints a few lines to stdout #
##################################################################

. inc/common.sh

start_server

innobackupex  --no-timestamp $topdir/backup >$topdir/stdout 2>$topdir/stderr

stop_server
# Remove datadir
rm -r $mysql_datadir

# Restore sakila
vlog "Applying log"
innobackupex --apply-log $topdir/backup >>$topdir/stdout 2>>$topdir/stderr

vlog "Restoring MySQL datadir"
mkdir -p $mysql_datadir
innobackupex --copy-back $topdir/backup >>$topdir/stdout 2>>$topdir/stderr

if [ "`cat $topdir/stdout | wc -l`" -gt 0 ]
then
    vlog "Got the following output on stdout:"
    cat $topdir/stdout
    exit -1
fi
