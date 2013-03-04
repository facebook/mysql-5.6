########################################################################
# Tests for streaming incremental backups
########################################################################

. inc/common.sh

#
# Test streaming incremental backup with a specified stream format
#
# Expects the following variables to be set before calling:
# stream_format - current can be either 'xbstream' or 'tar'.
# stream_extract_cmd - command to extract an archive of the format
#                      specified with $stream_format
#
function test_streaming_incremental()
{
    vlog "************************************************************************"
    vlog "Testing a streaming incremental backup with the '$stream_format' format"
    vlog "************************************************************************"

    start_server
    load_dbase_schema incremental_sample

# Adding initial rows
    vlog "Adding initial rows to database..."
    numrow=100
    count=0
    while [ "$numrow" -gt "$count" ]
    do
	${MYSQL} ${MYSQL_ARGS} -e "insert into test values ($count, $numrow);" incremental_sample
	let "count=count+1"
    done
    vlog "Initial rows added"

    full_backup_dir=$topdir/full_backup

    vlog "Starting backup"
    innobackupex --no-timestamp $full_backup_dir

# Changing data

    vlog "Making changes to database"
    let "count=numrow+1"
    let "numrow=500"
    while [ "$numrow" -gt "$count" ]
    do
	${MYSQL} ${MYSQL_ARGS} -e "insert into test values ($count, $numrow);" incremental_sample
	let "count=count+1"
    done
    vlog "Changes done"

# Saving the checksum of original table
    checksum_a=`checksum_table incremental_sample test`
    vlog "Table checksum is $checksum_a"

    vlog "Making incremental backup"

# Incremental backup
    inc_backup_dir=$topdir/inc_backup
    mkdir $inc_backup_dir
    innobackupex --stream=$stream_format --incremental \
	--incremental-basedir=$full_backup_dir ./ > \
	$inc_backup_dir/inc_backup_archive

    vlog "Preparing full backup"
    innobackupex --apply-log --redo-only $full_backup_dir
    vlog "Log applied to full backup"

    vlog "Unpacking incremental backup archive"
    cd $inc_backup_dir
    run_cmd bash -c "$stream_extract_cmd inc_backup_archive"
    cd - >/dev/null 2>&1 
    innobackupex --apply-log --redo-only --incremental-dir=$inc_backup_dir \
	$full_backup_dir
    vlog "Delta applied to full backup"

    innobackupex --apply-log $full_backup_dir
    vlog "Data prepared for restore"

# Destroying mysql data
    stop_server
    rm -rf $mysql_datadir/*
    vlog "Data destroyed"

# Restore backup
    vlog "Copying files back to data directory"
    innobackupex --copy-back $full_backup_dir
    vlog "Data restored"

    start_server --innodb_file_per_table

    vlog "Checking checksums"
    checksum_b=`checksum_table incremental_sample test`

    if [ "$checksum_a" != "$checksum_b"  ]
    then 
	vlog "Checksums do not matc"
	exit -1
    fi
    
    clean
}

stream_format="xbstream"
stream_extract_cmd="xbstream -xv <"
test_streaming_incremental
