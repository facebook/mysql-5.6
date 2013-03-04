

function check_partitioning()
{
   $MYSQL $MYSQL_ARGS -Ns -e "show variables like 'have_partitioning'"
}

function require_partitioning()
{
	PARTITION_CHECK=`check_partitioning`

	if [ -z "$PARTITION_CHECK" ]; then
	    echo "Requires Partitioning." > $SKIPPED_REASON
	    exit $SKIPPED_EXIT_CODE
	fi
}

function ib_part_schema()
{
	topdir=$1
	engine=$2

	cat <<EOF
CREATE TABLE test (
  a int(11) DEFAULT NULL
) ENGINE=$engine DEFAULT CHARSET=latin1
PARTITION BY RANGE (a)
(PARTITION p0 VALUES LESS THAN (100) ENGINE = $engine,
 PARTITION P1 VALUES LESS THAN (200) ENGINE = $engine,
 PARTITION p2 VALUES LESS THAN (300)
   DATA DIRECTORY = '$topdir/ext' INDEX DIRECTORY = '$topdir/ext'
   ENGINE = $engine,
 PARTITION p3 VALUES LESS THAN (400)
   DATA DIRECTORY = '$topdir/ext' INDEX DIRECTORY = '$topdir/ext'
   ENGINE = $engine,
 PARTITION p4 VALUES LESS THAN MAXVALUE ENGINE = $engine);
EOF
}

function ib_part_data()
{
	echo 'INSERT INTO test VALUES (1), (101), (201), (301), (401);';
}

function ib_part_init()
{
	topdir=$1
	engine=$2

	if [ -d $topdir/ext ] ; then
		rm -rf $topdir/ext
	fi
	mkdir -p $topdir/ext

	ib_part_schema $topdir $engine | run_cmd $MYSQL $MYSQL_ARGS test
	ib_part_data $topdir $engine | run_cmd $MYSQL $MYSQL_ARGS test
}

function ib_part_restore()
{
	topdir=$1
	mysql_datadir=$2

	# Remove database
	rm -rf $mysql_datadir/test/*
	rm -rf $topdir/ext/*
	vlog "Original database removed"

	# Restore database from backup
	cp -rv $topdir/backup/test/* $mysql_datadir/test
	vlog "database restored from backup"

}

function ib_part_assert_checksum()
{
	checksum_a=$1

	vlog "Checking checksums"
	checksum_b=`checksum_table test test`

	vlog "Checksums are $checksum_a and $checksum_b"

	if [ "$checksum_a" != "$checksum_b" ]
	then 
		vlog "Checksums are not equal"
		exit -1
	fi

	vlog "Checksums are OK"

}
