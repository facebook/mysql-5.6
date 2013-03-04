########################################################################
# Bug #711166: Partitioned tables are not correctly handled by the
#              --databases and --tables-file options of innobackupex,
#              and by the --tables option of xtrabackup.
#              Testcase covers using --tables-file option with MyISAM
#              database
########################################################################

. inc/common.sh
. inc/ib_part.sh

start_server

require_partitioning

# Create MyISAM partitioned table with some partitions in
# different location
ib_part_init $topdir MyISAM

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

stop_server

# Restore partial backup
ib_part_restore $topdir $mysql_datadir

start_server

ib_part_assert_checksum $checksum_a
