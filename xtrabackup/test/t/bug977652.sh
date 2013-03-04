#############################################################################
# Bug #977652: Compressed/Uncompressed incremental backup fails on compressed
#              full backup in Xtrabackup 2.0.0
#############################################################################

if ! which qpress > /dev/null 2>&1 ; then
  echo "Requires qpress to be installed" > $SKIPPED_REASON
  exit $SKIPPED_EXIT_CODE
fi

. inc/common.sh

start_server --innodb_file_per_table

load_sakila

# Take a full compressed backup
innobackupex --compress --no-timestamp $topdir/full

# Test that incremental backups work without uncompressing the full one
innobackupex --compress --no-timestamp --incremental \
    --incremental-basedir=$topdir/full $topdir/incremental
