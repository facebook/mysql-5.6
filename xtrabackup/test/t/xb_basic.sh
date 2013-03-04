. inc/common.sh

start_server

load_dbase_schema sakila
load_dbase_data sakila

mkdir -p $topdir/backup
innobackupex $topdir/backup
backup_dir=`grep "innobackupex: Backup created in directory" $OUTFILE | awk -F\' '{ print $2}'`
vlog "Backup created in directory $backup_dir"

stop_server
# Remove datadir
rm -r $mysql_datadir
#init_mysql_dir
# Restore sakila
vlog "Applying log"
vlog "###########"
vlog "# PREPARE #"
vlog "###########"
innobackupex --apply-log $backup_dir
vlog "Restoring MySQL datadir"
mkdir -p $mysql_datadir
vlog "###########"
vlog "# RESTORE #"
vlog "###########"
innobackupex --copy-back $backup_dir

start_server
# Check sakila
run_cmd ${MYSQL} ${MYSQL_ARGS} -e "SELECT count(*) from actor" sakila
