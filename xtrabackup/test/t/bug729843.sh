########################################################################
# Bug #729843: innobackupex logs plaintext password
########################################################################

. inc/common.sh

start_server

mkdir $topdir/backup
logfile=$topdir/backup/innobackupex_log

# Don't use run_cmd_* or innobackupex functions here to avoid logging
# the full command line (including the password in plaintext)
set +e
$IB_BIN $IB_ARGS --password=secret $topdir/backup 2>&1 | tee $logfile
set -e

# Check that the password was not logged in plaintext
run_cmd grep  -- "--password=xxxxxxxx" $logfile 
run_cmd_expect_failure grep -- "--password=secret" $logfile
