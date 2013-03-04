########################################################################
# Basic test for local compressed backups
########################################################################

. inc/common.sh

if ! which qpress > /dev/null 2>&1 ; then
  echo "Requires qpress to be installed" > $SKIPPED_REASON
  exit $SKIPPED_EXIT_CODE
fi

start_server --innodb_file_per_table
load_sakila

innobackupex --compress --no-timestamp $topdir/backup

stop_server
rm -rf ${MYSQLD_DATADIR}/*

cd $topdir/backup

for i in *.qp;  do qpress -d $i ./; done; \
for i in sakila/*.qp; do qpress -d $i sakila/; done

innobackupex --apply-log $topdir/backup

innobackupex --copy-back $topdir/backup

start_server

# TODO: proper validation
run_cmd ${MYSQL} ${MYSQL_ARGS} -e "SELECT count(*) from actor" sakila
