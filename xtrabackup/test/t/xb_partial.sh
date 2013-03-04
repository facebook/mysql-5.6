. inc/common.sh

start_server --innodb_file_per_table

load_dbase_schema incremental_sample

# Adding 10k rows
vlog "Adding initial rows to database..."
numrow=1000
count=0
while [ "$numrow" -gt "$count" ]
do
	${MYSQL} ${MYSQL_ARGS} -e "insert into test values ($count, $numrow);" incremental_sample
	let "count=count+1"
done
vlog "Initial rows added"
checksum_a=`checksum_table incremental_sample test`
vlog "Table checksum is $checksum_a"

# Backup directory
mkdir -p $topdir/data/parted

vlog "Starting backup"
xtrabackup --datadir=$mysql_datadir --backup --target-dir=$topdir/data/parted \
    --tables="^incremental_sample[.]test"
vlog "Partial backup done"

# Prepare backup
xtrabackup --datadir=$mysql_datadir --prepare --target-dir=$topdir/data/parted
vlog "Data prepared for restore"

# removing rows
run_cmd ${MYSQL} ${MYSQL_ARGS} -e "delete from test;" incremental_sample
vlog "Table cleared"

# Restore backup
stop_server
vlog "Copying files"
cd $topdir/data/parted/
cp -r * $mysql_datadir
cd $topdir
vlog "Data restored"
start_server --innodb_file_per_table
vlog "Checking checksums"
checksum_b=`checksum_table incremental_sample test`

if [ "$checksum_a" != "$checksum_b"  ]
then 
	vlog "Checksums are not equal"
	exit -1
fi

vlog "Checksums are OK"
