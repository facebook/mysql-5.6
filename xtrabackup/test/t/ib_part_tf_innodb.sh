########################################################################
# Bug #711166: Partitioned tables are not correctly handled by the
#              --databases and --tables-file options of innobackupex,
#              and by the --tables option of xtrabackup.
#              Testcase covers using --tables-file option with InnoDB
#              database
########################################################################

. inc/common.sh
. inc/ib_part.sh

start_server --innodb_file_per_table

require_partitioning

# Create InnoDB partitioned table
ib_part_init $topdir InnoDB

# Saving the checksum of original table
checksum_a=`checksum_table test test`

# Take a backup
# Only backup of test.test table will be taken
cat >$topdir/tables <<EOF
test.test
EOF
innobackupex --no-timestamp --tables-file=$topdir/tables $topdir/backup
innobackupex --apply-log $topdir/backup
vlog "Backup taken"

COUNT=`xtrabackup --stats --tables-file=$topdir/tables --datadir=$topdir/backup \
       | grep table: | awk '{print $2}' | sort -u | wc -l`
echo "COUNT = $COUNT"
if [ $COUNT != 7 ] ; then
	vlog "xtrabackup --stats does not work"
	exit -1
fi

stop_server

# Restore partial backup
ib_part_restore $topdir $mysql_datadir

start_server

ib_part_assert_checksum $checksum_a
