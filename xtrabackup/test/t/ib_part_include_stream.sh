########################################################################
# Bug #711166: Partitioned tables are not correctly handled by the
#              --databases and --tables-file options of innobackupex,
#              and by the --tables option of xtrabackup.
#              Testcase covers using --include option with InnoDB
#              database and --stream mode
########################################################################

. inc/common.sh
. inc/ib_part.sh

start_server --innodb_file_per_table

require_partitioning

# Create MyISAM partitioned table
ib_part_init $topdir MyISAM

# Saving the checksum of original table
checksum_a=`checksum_table test test`

# Take a backup
mkdir -p $topdir/backup
innobackupex --stream=tar --include='test.test$' $topdir/backup > $topdir/backup/backup.tar
$TAR ixvf $topdir/backup/backup.tar -C $topdir/backup 
$TAR cvhf $topdir/backup/backup11.tar $mysql_datadir/test/*

innobackupex --apply-log $topdir/backup

vlog "Backup taken"

stop_server

# Restore partial backup
ib_part_restore $topdir $mysql_datadir

start_server

ib_part_assert_checksum $checksum_a
