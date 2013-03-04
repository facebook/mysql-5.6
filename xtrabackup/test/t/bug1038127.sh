############################################################################
# Bug #1038127: XtraBackup 2.0.2 is not backwards compatible
#               if no space_id found in .meta file, applying delta
#               to full backup failing
############################################################################

. inc/common.sh

start_server --innodb_file_per_table

run_cmd $MYSQL $MYSQL_ARGS test <<EOF
CREATE TABLE t1(a INT) ENGINE=InnoDB;
INSERT INTO t1 VALUES (1), (2), (3);
EOF

# Full backup
# backup root directory
vlog "Starting backup"
innobackupex  --no-timestamp $topdir/full

vlog "Creating incremental backup"

innobackupex --incremental --no-timestamp \
    --incremental-basedir=$topdir/full $topdir/inc

# remove space_id = something line from .meta file
sed -ie '/space_id/ d' $topdir/inc/test/t1.ibd.meta

vlog "Preparing backup"

innobackupex --apply-log --redo-only $topdir/full
vlog "Log applied to full backup"

innobackupex --apply-log --redo-only --incremental-dir=$topdir/inc \
    $topdir/full
vlog "Delta applied to full backup"

innobackupex --apply-log $topdir/full
vlog "Data prepared for restore"

grep -q "This backup was taken with XtraBackup 2.0.1" $OUTFILE
