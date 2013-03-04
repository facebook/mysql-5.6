############################################################################
# Bug #972169: --compress in 2.0 should be incompatible with --stream=tar
############################################################################

. inc/common.sh

start_server

run_cmd_expect_failure $XB_BIN $XB_ARGS --datadir=$mysql_datadir --backup \
    --compress --stream=tar
grep -q "xtrabackup: error: compressed backups are incompatible with the" \
    $OUTFILE
