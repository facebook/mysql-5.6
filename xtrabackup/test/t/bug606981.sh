. inc/common.sh

if test "`uname -s`" != "Linux"
then
    echo "Requires Linux" > $SKIPPED_REASON
    exit $SKIPPED_EXIT_CODE
fi

start_server --innodb_file_per_table

load_sakila

# Take backup
echo "innodb_flush_method=O_DIRECT" >> $topdir/my.cnf
mkdir -p $topdir/backup
innobackupex --stream=tar $topdir/backup > $topdir/backup/out.tar
stop_server

# See if xtrabackup was using O_DIRECT
if ! grep -q "xtrabackup: using O_DIRECT" $OUTFILE ;
then
  vlog "xtrabackup was not using O_DIRECT for the input file."
  exit -1
fi

# Remove datadir
rm -r $mysql_datadir
# Restore sakila
vlog "Applying log"
backup_dir=$topdir/backup
cd $backup_dir
$TAR -ixvf out.tar
cd - >/dev/null 2>&1 
innobackupex --apply-log --defaults-file=$topdir/my.cnf $backup_dir
vlog "Restoring MySQL datadir"
mkdir -p $mysql_datadir
innobackupex --copy-back --defaults-file=$topdir/my.cnf $backup_dir

start_server
# Check sakila
${MYSQL} ${MYSQL_ARGS} -e "SELECT count(*) from actor" sakila
