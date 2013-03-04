########################################################################
# Bug #489290: xtrabackup defaults to mysql datadir
########################################################################

. inc/common.sh

start_server

backup_dir=$topdir/backup
mkdir -p $backup_dir
vlog "Backup created in directory $backup_dir"

cwd=`pwd`
cd $topdir

# Backup
xtrabackup --datadir=$mysql_datadir --backup --target-dir=backup

# Assure that directory is correct
if [ ! -f $backup_dir/ibdata1 ] ; then
	vlog "Backup not found in $backup_dir"
	exit -1
fi

cd $cwd
