set -e

# Takes a full backup from server_1 to server_2 using myrockshotbackup streaming

checkpoint_dir="${MYSQLTEST_VARDIR}/checkpoint"
backup_dir="${MYSQLTEST_VARDIR}/backup"
dest_data_dir="${MYSQLTEST_VARDIR}/mysqld.2/data/"
stream_opt="--stream"

mysql_dir=$(echo $MYSQL | awk '{print $1}' | xargs dirname)
PATH=$mysql_dir:$PATH

mkdir -p $backup_dir
rm -rf $backup_dir/*
# delete and recreate the dest dir to make sure all hidden files and directories (such as .rocksdb) are blown away
rm -rf $dest_data_dir/
mkdir $dest_data_dir

echo "myrockshotbackup copy phase"
if ! $MYSQL_MYROCKSHOTBACKUP --user='root' --port=${MASTER_MYPORT} $stream_opt --checkpoint_dir=$backup_dir 2> ${MYSQL_TMP_DIR}/myrockshotbackup_copy_log | $MYSQL_MYROCKSSTREAM $backup_dir
then
  tail ${MYSQL_TMP_DIR}/myrockshotbackup_copy_log
  exit 1
fi
mkdir ${backup_dir}/test      # TODO: Fix skipping empty directories

echo "myrockshotbackup move-back phase"
if ! $MYSQL_MYROCKSHOTBACKUP --move_back --datadir=$dest_data_dir --rocksdb_datadir="$dest_data_dir/.rocksdb" --rocksdb_waldir="$dest_data_dir/.rocksdb" --backup_dir=$backup_dir > ${MYSQL_TMP_DIR}/myrockshotbackup_moveback_log 2>&1
then
  tail ${MYSQL_TMP_DIR}/myrockshotbackup_moveback_log
  exit 1
fi
