. inc/common.sh

start_server

load_dbase_schema sakila
load_dbase_data sakila

# Take backup
mkdir -p $topdir/backup
mkdir -p $topdir/backup/stream
innobackupex --stream=tar $topdir/backup > $topdir/backup/stream/out.tar 

stop_server

cd $topdir/backup/stream/
$TAR -ixvf out.tar

if [ -f $topdir/backup/stream/xtrabackup_binary ]
then
	vlog "File xtrabackup_binary was copied"
else
	vlog "File xtrabackup_binary was not copied"
	exit -1
fi

cd - >/dev/null 2>&1 
