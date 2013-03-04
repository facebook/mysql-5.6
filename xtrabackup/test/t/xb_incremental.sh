. inc/common.sh

start_server --innodb_file_per_table

load_dbase_schema incremental_sample

# Adding 10k rows

vlog "Adding initial rows to database..."

numrow=100
count=0
while [ "$numrow" -gt "$count" ]
do
	${MYSQL} ${MYSQL_ARGS} -e "insert into test values ($count, $numrow);" incremental_sample
	let "count=count+1"
done


vlog "Initial rows added"

# Full backup

# Full backup folder
mkdir -p $topdir/data/full
# Incremental data
mkdir -p $topdir/data/delta

vlog "Starting backup"

xtrabackup --datadir=$mysql_datadir --backup --target-dir=$topdir/data/full

vlog "Full backup done"

# Changing data in sakila

vlog "Making changes to database"

${MYSQL} ${MYSQL_ARGS} -e "create table t2 (a int(11) default null, number int(11) default null) engine=innodb" incremental_sample
let "count=numrow+1"
let "numrow=1000"
while [ "$numrow" -gt "$count" ]
do
	${MYSQL} ${MYSQL_ARGS} -e "insert into test values ($count, $numrow);" incremental_sample
        ${MYSQL} ${MYSQL_ARGS} -e "insert into t2 values ($count, $numrow);" incremental_sample
	let "count=count+1"
done

vlog "Changes done"

# Saving the checksum of original table
checksum_test_a=`checksum_table incremental_sample test`
checksum_t2_a=`checksum_table incremental_sample t2`

vlog "Table 'test' checksum is $checksum_test_a"
vlog "Table 't2' checksum is $checksum_t2_a"
vlog "Making incremental backup"

# Incremental backup
xtrabackup --datadir=$mysql_datadir --backup --target-dir=$topdir/data/delta --incremental-basedir=$topdir/data/full

vlog "Incremental backup done"
vlog "Preparing backup"

# Prepare backup
xtrabackup --datadir=$mysql_datadir --prepare --apply-log-only \
    --target-dir=$topdir/data/full
vlog "Log applied to backup"
xtrabackup --datadir=$mysql_datadir --prepare --apply-log-only \
    --target-dir=$topdir/data/full --incremental-dir=$topdir/data/delta
vlog "Delta applied to backup"
xtrabackup --datadir=$mysql_datadir --prepare --target-dir=$topdir/data/full
vlog "Data prepared for restore"

# removing rows
run_cmd ${MYSQL} ${MYSQL_ARGS} -e "delete from test;" incremental_sample
run_cmd ${MYSQL} ${MYSQL_ARGS} -e "delete from t2;" incremental_sample
vlog "Tables cleared"

# Restore backup
stop_server
vlog "Copying files"
cd $topdir/data/full/
cp -r * $mysql_datadir
cd $topdir

vlog "Data restored"
start_server --innodb_file_per_table

vlog "Checking checksums"
checksum_test_b=`checksum_table incremental_sample test`
checksum_t2_b=`checksum_table incremental_sample t2`

if [ "$checksum_test_a" != "$checksum_test_b"  ]
then 
	vlog "Checksums of table 'test' are not equal"
	exit -1
fi

if [ "$checksum_t2_a" != "$checksum_t2_b"  ]
then 
	vlog "Checksums of table 't2' are not equal"
	exit -1
fi

vlog "Checksums are OK"
