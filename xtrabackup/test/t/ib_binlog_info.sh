##########################################################################
# Bug #907147: 1.6.4-313 loses master log info                           #
##########################################################################

. inc/common.sh

start_server
load_sakila

innobackupex --no-timestamp $topdir/backup

run_cmd_expect_failure grep "xtrabackup ping" \
    $topdir/backup/xtrabackup_binlog_info
