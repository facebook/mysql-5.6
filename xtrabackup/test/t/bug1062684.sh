MYSQLD_EXTRA_ARGS=--innodb-data-file-path="ibdata1:${DEFAULT_IBDATA_SIZE};ibdata2:5M:autoextend"
. inc/common.sh

start_server --innodb-data-file-path="ibdata1:${DEFAULT_IBDATA_SIZE};ibdata2:5M:autoextend"
load_dbase_schema incremental_sample

echo "innodb-data-file-path=ibdata1:${DEFAULT_IBDATA_SIZE};ibdata2:5M:autoextend" >>$topdir/my.cnf

# Adding initial rows
vlog "Adding initial rows to database..."
${MYSQL} ${MYSQL_ARGS} -e "insert into test values (1, 1);" incremental_sample

# Full backup
# backup root directory
mkdir -p $topdir/backup

vlog "Starting backup"
innobackupex  $topdir/backup
full_backup_dir=`grep "innobackupex: Backup created in directory" $OUTFILE | awk -F\' '{print $2}'`
vlog "Full backup done to directory $full_backup_dir"

# Changing data

vlog "Making changes to database"
${MYSQL} ${MYSQL_ARGS} -e "create table t2 (a int(11) default null, number int(11) default null) engine=innodb" incremental_sample
${MYSQL} ${MYSQL_ARGS} -e "insert into test values (10, 1);" incremental_sample
${MYSQL} ${MYSQL_ARGS} -e "insert into t2 values (10, 1);" incremental_sample
vlog "Changes done"

# Saving the checksum of original table
checksum_test_a=`checksum_table incremental_sample test`
checksum_t2_a=`checksum_table incremental_sample t2`
vlog "Table 'test' checksum is $checksum_test_a"
vlog "Table 't2' checksum is $checksum_t2_a"

vlog "Making incremental backup"

vlog "###############"
vlog "# INCREMENTAL #"
vlog "###############"

# Incremental backup
innobackupex --incremental --incremental-basedir=$full_backup_dir \
    $topdir/backup
inc_backup_dir=`grep "innobackupex: Backup created in directory" $OUTFILE | tail -n 1 | awk -F\' '{print $2}'`
vlog "Incremental backup done to directory $inc_backup_dir"

vlog "Preparing backup"
# Prepare backup
vlog "##############"
vlog "# PREPARE #1 #"
vlog "##############"
innobackupex --apply-log --redo-only $full_backup_dir
vlog "Log applied to full backup"
vlog "##############"
vlog "# PREPARE #2 #"
vlog "##############"
innobackupex --apply-log --redo-only --incremental-dir=$inc_backup_dir \
    $full_backup_dir
vlog "Delta applied to full backup"
vlog "##############"
vlog "# PREPARE #3 #"
vlog "##############"
innobackupex --apply-log $full_backup_dir
vlog "Data prepared for restore"

# Destroying mysql data
stop_server
rm -rf $mysql_datadir/*
vlog "Data destroyed"

# Restore backup
vlog "Copying files"
vlog "###########"
vlog "# RESTORE #"
vlog "###########"
innobackupex --copy-back $full_backup_dir
vlog "Data restored"

start_server --innodb-data-file-path="ibdata1:${DEFAULT_IBDATA_SIZE};ibdata2:5M:autoextend"

vlog "Checking checksums"
checksum_test_b=`checksum_table incremental_sample test`
checksum_t2_b=`checksum_table incremental_sample t2`

if [ "$checksum_test_a" != "$checksum_test_b"  ]
then 
	vlog "Checksums for table 'test' are not equal"
	exit -1
fi

if [ "$checksum_t2_a" != "$checksum_t2_b"  ]
then 
	vlog "Checksums for table 't2' are not equal"
	exit -1
fi

vlog "Checksums are OK"
