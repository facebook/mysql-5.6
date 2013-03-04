########################################################################
# Bug #722638: xtrabackup: taking backup while tables are droped and 
#              created breaks backup
########################################################################

. inc/common.sh

if ! $XB_BIN --help 2>&1 | grep -q debug-sync; then
    echo "Requires --debug-sync support" > $SKIPPED_REASON
    exit $SKIPPED_EXIT_CODE
fi

start_server --innodb_file_per_table

run_cmd $MYSQL $MYSQL_ARGS test <<EOF

CREATE TABLE t1(a INT) ENGINE=InnoDB;
INSERT INTO t1 VALUES (1), (2), (3);

CREATE TABLE t2(a INT) ENGINE=InnoDB;
INSERT INTO t2 VALUES (1), (2), (3);

CREATE TABLE t3(a INT) ENGINE=InnoDB;
INSERT INTO t3 VALUES (1), (2), (3);

CREATE TABLE t4_old(a INT) ENGINE=InnoDB;
INSERT INTO t4_old VALUES (1), (2), (3);

EOF

mkdir -p $topdir/backup

# Backup
xtrabackup --datadir=$mysql_datadir --backup --target-dir=$topdir/backup \
    --debug-sync="data_copy_thread_func" &

job_pid=$!

pid_file=$topdir/backup/xtrabackup_debug_sync

# Wait for xtrabackup to suspend
i=0
while [ ! -r "$pid_file" ]
do
    sleep 1
    i=$((i+1))
    echo "Waited $i seconds for $pid_file to be created"
done

xb_pid=`cat $pid_file`

# Modify the original tables, then change spaces ids by running DDL

run_cmd $MYSQL $MYSQL_ARGS test <<EOF

INSERT INTO t1 VALUES (4), (5), (6);
DROP TABLE t1;
CREATE TABLE t1(a CHAR(1)) ENGINE=InnoDB;
INSERT INTO t1 VALUES ("1"), ("2"), ("3");

INSERT INTO t2 VALUES (4), (5), (6);
ALTER TABLE t2 MODIFY a BIGINT;
INSERT INTO t2 VALUES (7), (8), (9);

INSERT INTO t3 VALUES (4), (5), (6);
TRUNCATE t3;
INSERT INTO t3 VALUES (7), (8), (9);

INSERT INTO t4_old VALUES (4), (5), (6);
ALTER TABLE t4_old RENAME t4;
INSERT INTO t4 VALUES (7), (8), (9);

EOF

# Calculate checksums
checksum_t1=`checksum_table test t1`
checksum_t2=`checksum_table test t2`
checksum_t3=`checksum_table test t3`
checksum_t4=`checksum_table test t4`

# Resume xtrabackup
vlog "Resuming xtrabackup"
kill -SIGCONT $xb_pid

run_cmd wait $job_pid

# Prepare
xtrabackup --datadir=$mysql_datadir --prepare --target-dir=$topdir/backup

stop_server

# Restore
rm -rf $mysql_datadir/ibdata1 $mysql_datadir/ib_logfile* \
    $mysql_datadir/test/*.ibd
cp -r $topdir/backup/* $mysql_datadir

start_server --innodb_file_per_table

# Verify checksums
checksum_t1_new=`checksum_table test t1`
checksum_t2_new=`checksum_table test t2`
checksum_t3_new=`checksum_table test t3`
checksum_t4_new=`checksum_table test t4`
vlog "Checksums (old/new):"
vlog "t1: $checksum_t1/$checksum_t1_new"
vlog "t2: $checksum_t2/$checksum_t2_new"
vlog "t3: $checksum_t3/$checksum_t3_new"
vlog "t4: $checksum_t4/$checksum_t4_new"

if [ "$checksum_t1" = "$checksum_t1_new" -a \
     "$checksum_t2" = "$checksum_t2_new" -a \
     "$checksum_t3" = "$checksum_t3_new" -a \
     "$checksum_t4" = "$checksum_t4_new" ]; then
    exit 0
fi

vlog "Checksums do not match"
exit -1
