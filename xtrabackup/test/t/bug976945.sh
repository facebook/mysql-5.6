############################################################################
# Bug #976945: innodb_log_block_size=4096 is not supported
############################################################################
. inc/common.sh

if [ -z "$XTRADB_VERSION" ]; then
    echo "Requires XtraDB" > $SKIPPED_REASON
    exit $SKIPPED_EXIT_CODE
fi

start_server --innodb_log_block_size=4096
echo innodb_log_block_size=4096 >> ${MYSQLD_VARDIR}/my.cnf
load_sakila

# Full backup
vlog "Starting backup"

full_backup_dir=${MYSQLD_VARDIR}/full_backup
innobackupex  --no-timestamp $full_backup_dir

vlog "Preparing backup"
innobackupex --apply-log --redo-only $full_backup_dir
vlog "Log applied to full backup"

# Destroying mysql data
stop_server
rm -rf $mysql_datadir/*
vlog "Data destroyed"

# Restore backup
vlog "Copying files to their original locations"
innobackupex --copy-back $full_backup_dir
vlog "Data restored"

start_server --innodb_log_block_size=4096
