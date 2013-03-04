############################################################################
# Bug983720: ib_lru_dump and --galera-info fail with --stream=xbstream
############################################################################

. inc/common.sh

set +e
${MYSQLD} --basedir=$MYSQL_BASEDIR --user=$USER --help --verbose --wsrep-sst-method=rsync| grep -q wsrep
probe_result=$?
if [[ "$probe_result" == "0" ]]
    then
        vlog "Server supports wsrep"
    else
        echo "Requires WSREP enabled" > $SKIPPED_REASON
        exit $SKIPPED_EXIT_CODE
fi
set -e

galera_port=`get_free_port 2`

start_server --log-bin=`hostname`-bin --binlog-format=ROW --wsrep-provider=${MYSQL_BASEDIR}/lib/libgalera_smm.so --wsrep_cluster_address=gcomm:// --wsrep_provider_options="base_port=${galera_port}"

# take a backup with stream mode
mkdir -p $topdir/backup
innobackupex --galera-info --stream=xbstream $topdir/backup > $topdir/backup/stream.xbs

xbstream -xv -C $topdir/backup < $topdir/backup/stream.xbs
if [ -f $topdir/backup/xtrabackup_galera_info ] ; then
    vlog "galera info has been backed up"
else
    vlog "galera info has not been backed up"
    exit -1
fi

stop_server
free_reserved_port ${galera_port}
