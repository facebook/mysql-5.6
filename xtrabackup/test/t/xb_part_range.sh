. inc/common.sh

start_server

function check_partitioning()
{
   $MYSQL $MYSQL_ARGS -Ns -e "show variables like 'have_partitioning'"
}

PARTITION_CHECK=`check_partitioning`

if [ -z "$PARTITION_CHECK" ]; then
    echo "Requires Partitioning." > $SKIPPED_REASON
    stop_server
    exit $SKIPPED_EXIT_CODE
fi

run_cmd $MYSQL $MYSQL_ARGS test <<EOF
CREATE TABLE test (
  a int(11) DEFAULT NULL
) ENGINE=InnoDB DEFAULT CHARSET=latin1
PARTITION BY RANGE (a)
(PARTITION p0 VALUES LESS THAN (100) ENGINE = InnoDB,
 PARTITION P1 VALUES LESS THAN (200) ENGINE = InnoDB,
 PARTITION p2 VALUES LESS THAN (300) ENGINE = InnoDB,
 PARTITION p3 VALUES LESS THAN (400) ENGINE = InnoDB,
 PARTITION p4 VALUES LESS THAN MAXVALUE ENGINE = InnoDB);
EOF

# Adding 10k rows

vlog "Adding initial rows to database..."

numrow=500
count=0
while [ "$numrow" -gt "$count" ]
do
	${MYSQL} ${MYSQL_ARGS} -e "insert into test values ($count);" test
	let "count=count+1"
done


vlog "Initial rows added"

# Full backup

# Full backup folder
mkdir -p $topdir/data/full
# Incremental data
mkdir -p $topdir/data/delta

vlog "Starting backup"

xtrabackup --no-defaults --datadir=$mysql_datadir --backup \
    --target-dir=$topdir/data/full

vlog "Full backup done"

# Changing data in sakila

vlog "Making changes to database"

numrow=500
count=0
while [ "$numrow" -gt "$count" ]
do
	${MYSQL} ${MYSQL_ARGS} -e "insert into test values ($count);" test
	let "count=count+1"
done

vlog "Changes done"

# Saving the checksum of original table
checksum_a=`checksum_table test test`

vlog "Table checksum is $checksum_a - before backup"

vlog "Making incremental backup"

# Incremental backup
xtrabackup --no-defaults --datadir=$mysql_datadir --backup \
    --target-dir=$topdir/data/delta --incremental-basedir=$topdir/data/full

vlog "Incremental backup done"
vlog "Preparing backup"

# Prepare backup
xtrabackup --no-defaults --datadir=$mysql_datadir --prepare --apply-log-only \
    --target-dir=$topdir/data/full
vlog "Log applied to backup"
xtrabackup --no-defaults --datadir=$mysql_datadir --prepare --apply-log-only \
    --target-dir=$topdir/data/full --incremental-dir=$topdir/data/delta
vlog "Delta applied to backup"
xtrabackup --no-defaults --datadir=$mysql_datadir --prepare \
    --target-dir=$topdir/data/full
vlog "Data prepared for restore"

# removing rows
vlog "Table cleared"
${MYSQL} ${MYSQL_ARGS} -e "delete from test" test

# Restore backup

stop_server

vlog "Copying files"

cd $topdir/data/full/
cp -r * $mysql_datadir
cd $topdir

vlog "Data restored"

start_server

vlog "Cheking checksums"
checksum_b=`checksum_table test test`

if [ "$checksum_a" != "$checksum_b"  ]
then 
	vlog "Checksums are not equal"
	exit -1
fi

vlog "Checksums are OK"
