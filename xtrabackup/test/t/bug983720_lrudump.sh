############################################################################
# Bug983720: ib_lru_dump and --galera-info fail with --stream=xbstream
############################################################################

. inc/common.sh

if [ -z "$XTRADB_VERSION" ]; then
    echo "Requires XtraDB" > $SKIPPED_REASON
    exit $SKIPPED_EXIT_CODE
fi

start_server

# produce ib_lru_dump
${MYSQL} ${MYSQL_ARGS} -e "select * from information_schema.XTRADB_ADMIN_COMMAND /*!XTRA_LRU_DUMP*/;"

# take a backup with stream mode
mkdir -p $topdir/backup
innobackupex --stream=xbstream $topdir/backup > $topdir/backup/stream.xbs

xbstream -xv -C $topdir/backup < $topdir/backup/stream.xbs
if [ -f $topdir/backup/ib_lru_dump ] ; then
    vlog "LRU dump has been backed up"
else
    vlog "LRU dump has not been backed up"
    exit -1
fi
