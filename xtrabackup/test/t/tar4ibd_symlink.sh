########################################################################
# Tests for tar4ibd + symlinks (bug #387587)
########################################################################

. inc/common.sh

start_server --innodb_file_per_table

load_dbase_schema sakila
load_dbase_data sakila

# Force a checkpoint
stop_server
start_server

# Copy some .ibd files to a temporary location and replace them with symlinks

mv $MYSQLD_DATADIR/sakila/actor.ibd $MYSQLD_VARDIR/
ln -s $MYSQLD_VARDIR/actor.ibd $MYSQLD_DATADIR/sakila/actor.ibd

mv $MYSQLD_DATADIR/sakila/customer.ibd $MYSQLD_VARDIR/customer.ibd
ln -s $MYSQLD_VARDIR/customer.ibd $MYSQLD_VARDIR/customer_link.ibd
ln -s $MYSQLD_VARDIR/customer_link.ibd $MYSQLD_DATADIR/sakila/customer.ibd

# Take backup
mkdir -p $MYSQLD_VARDIR/backup
innobackupex --stream=tar $MYSQLD_VARDIR/backup > $MYSQLD_VARDIR/backup/out.tar 

stop_server

# Remove datadir
rm -r $mysql_datadir

# Remove the temporary files referenced by symlinks
rm -f $MYSQLD_VARDIR/actor.ibd
rm -f $MYSQLD_VARDIR/customer.ibd
rm -f $MYSQLD_VARDIR/customer_link.ibd

# Restore sakila
vlog "Applying log"
backup_dir=$MYSQLD_VARDIR/backup
cd $backup_dir
run_cmd $TAR -ixvf out.tar
cd - >/dev/null 2>&1 

innobackupex --apply-log $backup_dir

vlog "Restoring MySQL datadir"
mkdir -p $mysql_datadir

innobackupex --copy-back $backup_dir

start_server

# Check sakila
run_cmd ${MYSQL} ${MYSQL_ARGS} -e "SELECT COUNT(*) FROM actor" sakila
run_cmd ${MYSQL} ${MYSQL_ARGS} -e "SELECT COUNT(*) FROM customer" sakila
