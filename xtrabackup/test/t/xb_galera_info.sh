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

start_server --log-bin=`hostname`-bin --binlog-format=ROW --wsrep-provider=${MYSQL_BASEDIR}/lib/libgalera_smm.so --wsrep_cluster_address=gcomm://

innobackupex --no-timestamp --galera-info $topdir/backup 
backup_dir=$topdir/backup
vlog "Backup created in directory $backup_dir"
if [[ "`${MYSQL} ${MYSQL_ARGS} -Ns -e 'SHOW STATUS LIKE "wsrep_local_state_uuid"'|awk {'print $2'}`" == "`sed  -re 's/:.+$//' $topdir/backup/xtrabackup_galera_info`" && "`${MYSQL} ${MYSQL_ARGS} -Ns -e 'SHOW STATUS LIKE "wsrep_last_committed"'|awk {'print $2'}`" == "`sed  -re 's/^.+://' $topdir/backup/xtrabackup_galera_info`" ]]
then
	vlog "File is written correctly"
else
	vlog "File incorrect"
	exit 1
fi

stop_server
