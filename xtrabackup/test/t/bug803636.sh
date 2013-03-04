########################################################################
# Bug #803636: "moves files" option needed with --copy-back
########################################################################

. inc/common.sh

start_server --innodb_file_per_table

load_sakila

# Backup
innobackupex --no-timestamp $topdir/backup

stop_server

rm -r $mysql_datadir

# Prepare
innobackupex --apply-log $topdir/backup

# Restore
mkdir -p $mysql_datadir
innobackupex --move-back $topdir/backup

# Check that there are no data files in the backup directory
run_cmd_expect_failure ls -R $topdir/backup/*/*.{ibd,MYD,MYI,frm}

start_server

# Verify data
run_cmd ${MYSQL} ${MYSQL_ARGS} -e "SELECT count(*) from actor" sakila
