. inc/common.sh

start_server --innodb_file_per_table
run_cmd ${MYSQL} ${MYSQL_ARGS} -e "create database csv"
run_cmd ${MYSQL} ${MYSQL_ARGS} -e "create table csm (a int NOT NULL ) ENGINE=CSV" csv
# Adding initial rows
vlog "Adding initial rows to database..."
numrow=100
count=0
while [ "$numrow" -gt "$count" ]
do
	${MYSQL} ${MYSQL_ARGS} -e "insert into csm values ($count);" csv
	let "count=count+1"
done
vlog "Initial rows added"

# Full backup
# backup root directory
mkdir -p $topdir/backup

vlog "Starting backup"
innobackupex $topdir/backup
full_backup_dir=`grep "innobackupex: Backup created in directory" $OUTFILE | awk -F\' '{print $2}'`
vlog "Full backup done to directory $full_backup_dir"

# Saving the checksum of original table
checksum_a=`checksum_table csv csm`
vlog "Table checksum is $checksum_a"

vlog "Preparing backup"
# Prepare backup
vlog "###########"
vlog "# PREPARE #"
vlog "###########"
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

start_server --innodb_file_per_table
checksum_b=`checksum_table csv csm`
vlog "Checking checksums: $checksum_a/$checksum_b"

if [ "$checksum_a" != "$checksum_b"  ]
then 
	vlog "Checksums are not equal"
	exit -1
fi
vlog "Checksums are OK"
