. inc/common.sh

start_server --innodb_file_per_table

load_dbase_schema sakila
load_dbase_data sakila

# Take backup
vlog "Creating the backup directory: $topdir/backup"
mkdir -p $topdir/backup
innobackupex $topdir/backup --parallel=8
backup_dir=`grep "innobackupex: Backup created in directory" $OUTFILE | awk -F\' '{ print $2}'`

#echo "Backup dir in $backup_dir"

stop_server
# Remove datadir
rm -r $mysql_datadir
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
