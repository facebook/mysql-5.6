set -e

# Takes a full backup from server_1 to server_2

ibbackup_opt="--ibbackup=${MYSQL_XTRABACKUP}"
defaults_file_opt="--defaults-file=${MYSQLTEST_VARDIR}/my.cnf"
backup_dir="${MYSQLTEST_VARDIR}/backup"
dest_data_dir="${MYSQLTEST_VARDIR}/mysqld.2/data/"

mkdir -p $backup_dir
rm -rf $backup_dir/*
# delete and recreate the dest dir to make sure all hidden files and directories (such as .rocksdb) are blown away
rm -rf $dest_data_dir/
mkdir $dest_data_dir

mysql_dir=$(echo $MYSQL | awk '{print $1}' | xargs dirname)
PATH=$mysql_dir:$PATH

echo "innobackupex copy phase"
if ! $MYSQL_INNOBACKUPEX $defaults_file_opt --defaults-group=mysqld.1 $ibbackup_opt $backup_dir > ${MYSQL_TMP_DIR}/xtrabackup_copy_log 2>&1
then
  tail ${MYSQL_TMP_DIR}/xtrabackup_copy_log
  exit 1
fi
backup_dir=($(grep "innobackupex: Backup created in directory" ${MYSQL_TMP_DIR}/xtrabackup_copy_log | awk -F\' '{ print $2}'))

echo "innobackupex apply-log phase"
if ! $MYSQL_INNOBACKUPEX $ibbackup_opt $backup_dir --apply-log > ${MYSQL_TMP_DIR}/xtrabackup_restore_log 2>&1
then
  tail ${MYSQL_TMP_DIR}/xtrabackup_restore_log
  exit 1
fi

echo "innobackupex move-back phase"
if ! $MYSQL_INNOBACKUPEX $defaults_file_opt --defaults-group=mysqld.2 $ibbackup_opt $backup_dir --move-back > ${MYSQL_TMP_DIR}/xtrabackup_moveback_log 2>&1
then
  tail ${MYSQL_TMP_DIR}/xtrabackup_moveback_log
  exit 1
fi
cp $backup_dir/ib_logfile* $dest_data_dir
