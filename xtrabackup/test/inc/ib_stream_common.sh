############################################################################
# Common code for ib_stream_*.sh tests.
# Expects the following variables to be set appropriately before 
# including:
#   stream_format: passed to the --stream innobackupex option
#   stream_extract_cmd: shell command to be used to extract the stream
#
# Optionally the following variables may be set:
#   innobackupex_option:  additional options to be passed to innobackupex.
#   stream_uncompress_cmd: command used to uncompress data streamed data
############################################################################

. inc/common.sh

start_server --innodb_file_per_table

load_dbase_schema sakila
load_dbase_data sakila

innobackupex_options=${innobackupex_options:-""}

# Take backup
mkdir -p $topdir/backup
innobackupex --stream=$stream_format $innobackupex_options $topdir/backup > $topdir/backup/out

stop_server

# Remove datadir
rm -r $mysql_datadir

# Restore sakila
vlog "Applying log"
backup_dir=$topdir/backup
cd $backup_dir
run_cmd bash -c "$stream_extract_cmd out"
test -n "${stream_uncompress_cmd:-""}" && run_cmd bash -c "$stream_uncompress_cmd";
cd - >/dev/null 2>&1 
vlog "###########"
vlog "# PREPARE #"
vlog "###########"
innobackupex --apply-log $backup_dir
vlog "Restoring MySQL datadir"
mkdir -p $mysql_datadir
vlog "###########"
vlog "# RESTORE #"
vlog "###########"
innobackupex --copy-back $backup_dir

start_server
# Check sakila
run_cmd ${MYSQL} ${MYSQL_ARGS} -e "SELECT count(*) from actor" sakila
