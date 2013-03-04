########################################################################
# Bug #483827: support for mysqld_multi
########################################################################

function modify_args()
{
  XB_ARGS=`echo $XB_ARGS | sed -e 's/my.cnf/my_multi.cnf/'`
  IB_ARGS=`echo $IB_ARGS | sed -e 's/my.cnf/my_multi.cnf/'`
}

. inc/common.sh

start_server

backup_dir=$topdir/backup

# change defaults file from my.cnf to my_multi.cnf
modify_args

# make my_multi.cnf
echo "
[mysqld1]
datadir=${mysql_datadir}
tmpdir=$mysql_tmpdir" > $topdir/my_multi.cnf

# Backup
innobackupex --no-timestamp --defaults-group=mysqld1 $backup_dir
innobackupex --apply-log $backup_dir

stop_server

# clean datadir
rm -rf ${mysql_datadir}/*

# restore backup
innobackupex --copy-back --defaults-group=mysqld1 $backup_dir

# make sure that data are in correct place
if [ ! -f ${mysql_datadir}/ibdata1 ] ; then
  vlog "Data not found in ${mysql_datadir}"
  exit -1
fi
