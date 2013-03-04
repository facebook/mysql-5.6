############################################################################
# Bug #740489: Add --defaults-extra-file param to innobackupex
############################################################################
. inc/common.sh

start_server --innodb_file_per_table
load_sakila

run_cmd ${MYSQL} ${MYSQL_ARGS} -e "UPDATE mysql.user SET Password=PASSWORD('password') WHERE User='root'; FLUSH PRIVILEGES;"

defaults_extra_file=${TEST_BASEDIR}/740489.cnf

echo "[client]" > $defaults_extra_file
echo "user=root" >> $defaults_extra_file
echo "password=password" >> $defaults_extra_file

mkdir -p $topdir/backup
cat ${MYSQLD_VARDIR}/my.cnf
run_cmd $IB_BIN --defaults-extra-file=$defaults_extra_file --socket=${MYSQLD_SOCKET} --ibbackup=$XB_BIN $topdir/backup
backup_dir=`grep "innobackupex: Backup created in directory" $OUTFILE | awk -F\' '{ print $2}'`
vlog "Backup created in directory $backup_dir"

run_cmd ${MYSQL} ${MYSQL_ARGS} --password=password -e "UPDATE mysql.user SET Password=PASSWORD('') WHERE User='root'; FLUSH PRIVILEGES;"

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
run_cmd ${MYSQL} ${MYSQL_ARGS} --password=password -e "SELECT count(*) from actor" sakila

rm -f $defaults_extra_file
