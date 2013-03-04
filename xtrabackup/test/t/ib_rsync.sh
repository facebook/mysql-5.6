. inc/common.sh

if ! which rsync > /dev/null 2>&1
then
    echo "Requires rsync to be installed" > $SKIPPED_REASON
    exit $SKIPPED_EXIT_CODE
fi

start_server --innodb_file_per_table
load_sakila

innobackupex --rsync --no-timestamp $topdir/backup

stop_server

run_cmd rm -r $mysql_datadir

innobackupex --apply-log $topdir/backup

run_cmd mkdir -p $mysql_datadir

innobackupex --copy-back $topdir/backup

start_server
run_cmd ${MYSQL} ${MYSQL_ARGS} -e "SELECT COUNT(*) FROM actor" sakila
