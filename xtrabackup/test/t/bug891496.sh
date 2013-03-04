##########################################################################
# Bug #891496: tar4ibd fails on datafiles generated on 5.0               #
##########################################################################
MYSQLD_EXTRA_ARGS=--innodb-data-file-path="ibdata1:3M;ibdata2:10M:autoextend"

. inc/common.sh

innodb_data_file_path="ibdata1:3M;ibdata2:10M:autoextend"

start_server --innodb_data_file_path=$innodb_data_file_path

cat >> $topdir/my.cnf <<EOF
innodb_data_file_path=$innodb_data_file_path
EOF

load_dbase_schema sakila
load_dbase_data sakila

# Take backup
mkdir -p $topdir/backup
innobackupex --stream=tar $topdir/backup > $topdir/backup/out.tar

stop_server
# Remove datadir
rm -r $mysql_datadir
# Restore sakila
vlog "Applying log"
backup_dir=$topdir/backup
cd $backup_dir
$TAR -ixvf out.tar
cd - >/dev/null 2>&1 
vlog "###########"
vlog "# PREPARE #"
vlog "###########"
innobackupex --apply-log $backup_dir
vlog "Restoring MySQL datadir"
mkdir -p $mysql_datadir
vlog "###########"
vlog "# RESTORE #"
vlog "###########"
innobackupex  --copy-back $backup_dir

start_server --innodb_data_file_path=$innodb_data_file_path
# Check sakila
run_cmd ${MYSQL} ${MYSQL_ARGS} -e "SELECT count(*) from actor" sakila
