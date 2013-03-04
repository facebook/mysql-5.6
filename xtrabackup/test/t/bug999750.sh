############################################################################
# Bug #999750: incremental backups should be incompatible with --stream=tar
############################################################################

. inc/common.sh

# Exclude the built-in InnoDB configuration as it requires a different format
# for --incremental-lsn.
if [ -z "$INNODB_VERSION" ]; then
    echo "Requires InnoDB plugin or XtraDB" >$SKIPPED_REASON
    exit $SKIPPED_EXIT_CODE
fi

start_server

run_cmd_expect_failure $XB_BIN $XB_ARGS --datadir=$mysql_datadir --backup \
    --incremental-lsn=0 --stream=tar
grep -q "xtrabackup: error: streaming incremental backups are incompatible with the " \
    $OUTFILE
