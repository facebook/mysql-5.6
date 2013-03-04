#
# Bug #798488: xtrabackup ignores defaults-file in apply-log-only prepare mode 
#
# Test xtrabackup fails with an error when --defaults-file is not the first argument
# on the command line (and thus would be ignored).

. inc/common.sh

start_server

# The following should succeed (can't use xtrabackup directly as it uses
# --no-defaults)
run_cmd $XB_BIN --defaults-file=$topdir/my.cnf --backup \
    --datadir=$mysql_datadir --target-dir=$topdir/backup

rm -rf $topdir/backup

# The following should fail (can't use xtrabackup directly as that would make
# the test fail)

run_cmd_expect_failure $XB_BIN --backup --defaults-file=$topdir/my.cnf \
    --datadir=$mysql_datadir --target-dir=$topdir/backup

exit 0
