############################################################################
# Bug #1007446: innobackupex should remove stale xtrabackup_suspended file
############################################################################

. inc/common.sh

start_server

echo "" > $MYSQLD_TMPDIR/xtrabackup_suspended

mkdir -p $topdir/backup
innobackupex --stream=tar $topdir/backup > /dev/null

grep -q "A left over instance of suspend file" $OUTFILE
