. inc/common.sh

start_server --innodb_file_per_table
load_dbase_schema incremental_sample

# Full backup dir
mkdir -p $topdir/data/full
# Incremental backup dir
mkdir -p $topdir/data/delta

xtrabackup --datadir=$mysql_datadir --backup --target-dir=$topdir/data/full

vlog "Creating and filling a new table"
run_cmd $MYSQL $MYSQL_ARGS test <<EOF
CREATE TABLE t(a int) ENGINE=InnoDB;
INSERT INTO t VALUES (1), (2), (3);
FLUSH LOGS;
EOF

stop_server
start_server --innodb_file_per_table

vlog "Making incremental backup"
xtrabackup --datadir=$mysql_datadir --backup --target-dir=$topdir/data/delta --incremental-basedir=$topdir/data/full

vlog "Preparing full backup"
xtrabackup --datadir=$mysql_datadir --prepare --apply-log-only \
    --target-dir=$topdir/data/full

# The following would fail before the bugfix
vlog "Applying incremental delta"
xtrabackup --datadir=$mysql_datadir --prepare --apply-log-only \
    --target-dir=$topdir/data/full --incremental-dir=$topdir/data/delta

vlog "Preparing full backup"
xtrabackup --datadir=$mysql_datadir --prepare --target-dir=$topdir/data/full
vlog "Data prepared for restore"

stop_server

vlog "Copying files"

cd $topdir/data/full/
cp -r * $mysql_datadir
cd $topdir

vlog "Data restored"
start_server --innodb_file_per_table

run_cmd $MYSQL $MYSQL_ARGS -e "SELECT * FROM t" test
