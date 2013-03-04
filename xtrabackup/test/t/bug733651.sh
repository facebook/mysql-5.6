########################################################################
# Bug #733651: innobackupex not stores some critical
# innodb options in backup-my.cnf
########################################################################

. inc/common.sh

options="innodb_log_files_in_group innodb_log_file_size"
if [ ! -z "$XTRADB_VERSION" ]; then
    options="$options innodb_page_size innodb_fast_checksum innodb_log_block_size"
fi

start_server

mkdir -p $topdir/backup
innobackupex  $topdir/backup
backup_dir=`grep "innobackupex: Backup created in directory" $OUTFILE | awk -F\' '{ print $2}'`
vlog "Backup created in directory $backup_dir"

# test presence of options
for option in $options ; do

	if [ "`cat $backup_dir/backup-my.cnf | grep $option | wc -l`" == "0" ] ; then
		vlog "Option $option is absent"
		exit -1
	else
		vlog "Option $option is present"
	fi

done
