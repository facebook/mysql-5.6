########################################################################
# Bug #665210: tar4ibd does not support innodb row_format=compressed
# Bug #810269: tar4ibd does not check for doublewrite buffer pages
########################################################################

. inc/common.sh

if [ -z "$INNODB_VERSION" ]; then
    echo "Requires InnoDB plugin or XtraDB" >$SKIPPED_REASON
    exit $SKIPPED_EXIT_CODE
fi

start_server "--innodb_strict_mode --innodb_file_per_table \
--innodb_file_format=Barracuda"

load_dbase_schema incremental_sample

vlog "Compressing the table"

run_cmd $MYSQL $MYSQL_ARGS -e \
    "ALTER TABLE test ENGINE=InnoDB ROW_FORMAT=compressed \
KEY_BLOCK_SIZE=4" incremental_sample

vlog "Adding initial rows to table"

numrow=10000
count=0
while [ "$numrow" -gt "$count" ]; do
    sql="INSERT INTO test VALUES ($count, $numrow)"
    let "count=count+1"
    for ((i=0; $i<99; i++)); do
	sql="$sql,($count, $numrow)"
	let "count=count+1"
    done
    ${MYSQL} ${MYSQL_ARGS} -e "$sql" incremental_sample
done

rows=`${MYSQL} ${MYSQL_ARGS} -Ns -e "SELECT COUNT(*) FROM test" \
    incremental_sample`
if [ "$rows" != "10000" ]; then
    vlog "Failed to add initial rows"
    exit -1
fi

vlog "Initial rows added"

checksum_a=`checksum_table incremental_sample test`

vlog "Starting streaming backup"

mkdir -p $topdir/backup

innobackupex --stream=tar $topdir/backup > $topdir/backup/out.tar

stop_server
rm -rf $mysql_datadir

vlog "Applying log"

cd $topdir/backup
$TAR -ixvf out.tar
cd -
innobackupex --apply-log $topdir/backup

vlog "Restoring MySQL datadir"
mkdir -p $mysql_datadir
innobackupex --copy-back $topdir/backup

start_server

checksum_b=`checksum_table incremental_sample test`

if [ "$checksum_a" != "$checksum_b" ]; then
    vlog "Checksums do not match"
    exit -1
fi

vlog "Checksums are OK"
