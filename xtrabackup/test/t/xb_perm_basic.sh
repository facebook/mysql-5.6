# Test for fix of https://bugs.launchpad.net/percona-xtrabackup/+bug/691090
. inc/common.sh

start_server

chmod -R 500 $mysql_datadir
trap "vlog restoring permissions on datadir $mysql_datadir ; \
chmod -R 700 $mysql_datadir" INT TERM EXIT
innobackupex  --no-timestamp $topdir/backup
