############################################################################
# Bug #1002688: innobackupex incremental apply-log copies to wrong directory
############################################################################
. inc/common.sh

start_server --innodb_file_per_table
load_sakila

# Full backup
# backup root directory
vlog "Starting backup"

full_backup_dir=$topdir/full_backup
innobackupex  --no-timestamp $full_backup_dir

# Changing data

run_cmd $MYSQL $MYSQL_ARGS -e "CREATE DATABASE newdb"
run_cmd $MYSQL $MYSQL_ARGS -e \
    "CREATE TABLE actor_copy ENGINE=MyISAM SELECT * FROM sakila.actor" newdb

# Saving the checksum of original table
checksum_a=`checksum_table newdb actor_copy`
test -n "$checksum_a" || die "Failed to checksum table actor_copy"

vlog "Making incremental backup"

# Incremental backup
inc_backup_dir=$topdir/incremental_backup
innobackupex --incremental --no-timestamp \
    --incremental-basedir=$full_backup_dir $inc_backup_dir
vlog "Incremental backup created in directory $inc_backup_dir"

vlog "Preparing backup"
innobackupex --apply-log --redo-only $full_backup_dir
vlog "Log applied to full backup"

innobackupex --apply-log --redo-only --incremental-dir=$inc_backup_dir/ \
    $full_backup_dir
vlog "Delta applied to full backup"

innobackupex --apply-log $full_backup_dir
vlog "Data prepared for restore"

# Destroying mysql data
stop_server
rm -rf $mysql_datadir/*
vlog "Data destroyed"

# Restore backup
vlog "Copying files to their original locations"
innobackupex --copy-back $full_backup_dir
vlog "Data restored"

start_server --innodb_file_per_table

vlog "Checking checksums"
checksum_b=`checksum_table newdb actor_copy`

vlog "Old checksum: $checksum_a"
vlog "New checksum: $checksum_b"

if [ "$checksum_a" != "$checksum_b"  ]
then 
    vlog "Checksums do not match"
    exit -1
fi
