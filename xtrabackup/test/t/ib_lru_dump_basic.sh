########################################################################
# Bug #543134: Missing support for LRU dump/restore
########################################################################

. inc/common.sh

if [ -z "$XTRADB_VERSION" ]; then
    echo "Requires XtraDB" > $SKIPPED_REASON
    exit $SKIPPED_EXIT_CODE
fi

start_server

# produce ib_lru_dump
${MYSQL} ${MYSQL_ARGS} -e "select * from information_schema.XTRADB_ADMIN_COMMAND /*!XTRA_LRU_DUMP*/;"

# take a backup
innobackupex --no-timestamp $topdir/backup

if [ -f $topdir/backup/ib_lru_dump ] ; then
    vlog "LRU dump has been backed up"
else
    vlog "LRU dump has not been backed up"
    exit -1
fi

# restore from backup
rm -rf $mysql_datadir/*
innobackupex --copy-back $topdir/backup

if [ -f $mysql_datadir/ib_lru_dump ] ; then
    vlog "LRU dump has been restored"
else
    vlog "LRU dump has not been restored"
    exit -1
fi
