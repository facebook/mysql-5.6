########################################################################
# Bug #1066843: Fix for bug #932623 does not take separate doublewrite
#               tablespace into account
# Bug #1068470: XtraBackup handles separate doublewrite buffer file
#               incorrectly
# We testing full and incremental backup and restore to check that
# separate doublewrite buffer file handled correctly
########################################################################

. inc/common.sh

if [ -z "$XTRADB_VERSION" ]; then
    echo "Requires XtraDB" > $SKIPPED_REASON
    exit $SKIPPED_EXIT_CODE
fi

DBLWR=dblwr.ibd
start_server --innodb_file_per_table --innodb_doublewrite_file=${DBLWR}
load_dbase_schema incremental_sample

# Workaround for bug #1072695
IB_ARGS_NO_DEFAULTS_FILE=`echo $IB_ARGS | sed -e 's/--defaults-file=[^ ]* / /'`
function innobackupex_no_defaults_file ()
{
	run_cmd $IB_BIN $IB_ARGS_NO_DEFAULTS_FILE $*
}

echo "innodb_doublewrite_file=${DBLWR}" >>$topdir/my.cnf

# Adding initial rows
vlog "Adding initial rows to database..."
${MYSQL} ${MYSQL_ARGS} -e "insert into test values (1, 1);" incremental_sample

# Full backup
# backup root directory
mkdir -p $topdir/backup

vlog "Starting backup"
full_backup_dir=$topdir/backup/full
innobackupex  --no-timestamp $full_backup_dir
vlog "Full backup done to directory $full_backup_dir"
cat $full_backup_dir/backup-my.cnf

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
inc_backup_dir=$topdir/backup/inc
innobackupex --no-timestamp --incremental --incremental-basedir=$full_backup_dir \
    $inc_backup_dir
vlog "Incremental backup done to directory $inc_backup_dir"

vlog "Preparing backup"
# Prepare backup
vlog "##############"
vlog "# PREPARE #1 #"
vlog "##############"
innobackupex_no_defaults_file --apply-log --redo-only $full_backup_dir
vlog "Log applied to full backup"
vlog "##############"
vlog "# PREPARE #2 #"
vlog "##############"
innobackupex_no_defaults_file --apply-log --redo-only --incremental-dir=$inc_backup_dir \
    $full_backup_dir
vlog "Delta applied to full backup"
vlog "##############"
vlog "# PREPARE #3 #"
vlog "##############"
innobackupex_no_defaults_file --apply-log $full_backup_dir
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

start_server --innodb_file_per_table --innodb_doublewrite_file=${DBLWR}

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
