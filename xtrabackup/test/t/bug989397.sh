############################################################################
# Bug989397: InnoDB tables with names *opt*, *par*, *CSV*, *MYD* and so on
# backed up twice
############################################################################

. inc/common.sh

start_server --innodb_file_per_table

# create table which name ends with opt
${MYSQL} ${MYSQL_ARGS} -e "create table test.topt (a int auto_increment primary key);"

# take a backup with stream mode
mkdir -p $topdir/backup
innobackupex --stream=xbstream $topdir/backup > $topdir/backup/stream.xbs

# will fail if table topt backed up twice
xbstream -xv -C $topdir/backup < $topdir/backup/stream.xbs
