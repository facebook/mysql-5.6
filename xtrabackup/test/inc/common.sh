set -eu

function innobackupex()
{
    run_cmd $IB_BIN $IB_ARGS $*
}

function xtrabackup()
{
    run_cmd $XB_BIN $XB_ARGS $*
}

function vlog
{
    echo "`date +"%F %T"`: `basename "$0"`: $@" >&2
}


function clean()
{
    rm -rf ${TEST_BASEDIR}/var[0-9]
}

function die()
{
  vlog "$*" >&2
  exit 1
}

function init_mysql_dir()
{
    if [ ! -d "$MYSQLD_VARDIR" ]
    then
	vlog "Creating server root directory: $MYSQLD_VARDIR"
	mkdir "$MYSQLD_VARDIR"
    fi
    if [ ! -d "$MYSQLD_TMPDIR" ]
    then
	vlog "Creating server temporary directory: $MYSQLD_TMPDIR"
	mkdir "$MYSQLD_TMPDIR"
    fi
    if [ ! -d "$MYSQLD_DATADIR" ]
    then
	vlog "Creating server data directory: $MYSQLD_DATADIR"
	mkdir -p "$MYSQLD_DATADIR"
	vlog "Calling mysql_install_db"
	$MYSQL_INSTALL_DB --no-defaults --basedir=$MYSQL_BASEDIR --datadir="$MYSQLD_DATADIR" --tmpdir="$MYSQLD_TMPDIR" ${MYSQLD_EXTRA_ARGS}
    fi
}

########################################################################
# Checks whether MySQL is alive
########################################################################
function mysql_ping()
{
    $MYSQLADMIN $MYSQL_ARGS --wait=100 ping >/dev/null 2>&1 || return 1
}

function kill_leftovers()
{
    local file
    for file in ${TEST_BASEDIR}/mysqld*.pid
    do
	if [ -f $file ]
	then
	    vlog "Found a leftover mysqld processes with PID `cat $file`, stopping it"
	    kill -9 `cat $file` 2>/dev/null || true
	    rm -f $file
	fi
    done
}

function run_cmd()
{
  vlog "===> $@"
  set +e
  "$@"
  local rc=$?
  set -e
  if [ $rc -ne 0 ]
  then
      die "===> `basename $1` failed with exit code $rc"
  fi
}

function run_cmd_expect_failure()
{
  vlog "===> $@"
  set +e
  "$@"
  local rc=$?
  set -e
  if [ $rc -eq 0 ]
  then
      die "===> `basename $1` succeeded when it was expected to fail"
  fi
}

function load_sakila()
{
vlog "Loading sakila"
${MYSQL} ${MYSQL_ARGS} -e "create database sakila"
vlog "Loading sakila scheme"
${MYSQL} ${MYSQL_ARGS} sakila < inc/sakila-db/sakila-schema.sql
vlog "Loading sakila data"
${MYSQL} ${MYSQL_ARGS} sakila < inc/sakila-db/sakila-data.sql
}

function load_dbase_schema()
{
vlog "Loading $1 database schema"
${MYSQL} ${MYSQL_ARGS} -e "create database $1"
${MYSQL} ${MYSQL_ARGS} $1 < inc/$1-db/$1-schema.sql
}

function load_dbase_data()
{
vlog "Loading $1 database data"
${MYSQL} ${MYSQL_ARGS} $1 < inc/$1-db/$1-data.sql
}

function drop_dbase()
{
  vlog "Dropping database $1"
  run_cmd ${MYSQL} ${MYSQL_ARGS} -e "DROP DATABASE $1"
}

########################################################################
# Choose a free port for a MySQL server instance
########################################################################
function get_free_port()
{
    local id=$1
    local port
    local lockfile

    for (( port=PORT_BASE+id; port < 65535; port++))
    do
	lockfile="/tmp/xtrabackup_port_lock.$port"
	# Try to atomically lock the current port number
	if ! set -C > $lockfile
	then
	    set +C
	    # check if the process still exists
	    if kill -0 "`cat $lockfile`"
	    then
		continue;
	    fi
	    # stale file, overwrite it with the current PID
	fi
	set +C
	echo $$ > $lockfile
	if ! nc -z -w1 localhost $port >/dev/null 2>&1
	then
	    echo $port
	    return 0
	fi
	rm -f $lockfile
    done

    die "Could not find a free port for server id $id!"
}

function free_reserved_port()
{
   local port=$1

   rm -f /tmp/xtrabackup_port_lock.${port}
}


########################################################################
# Initialize server variables such as datadir, tmpdir, etc. and store
# them with the specified index in SRV_MYSQLD_* arrays to be used by
# switch_server() later
########################################################################
function init_server_variables()
{
    local id=$1

    if [ -n ${SRV_MYSQLD_IDS[$id]:-""} ]
    then
	die "Server with id $id has already been started"
    fi

    SRV_MYSQLD_IDS[$id]="$id"
    local vardir="${TEST_BASEDIR}/var${id}"
    SRV_MYSQLD_VARDIR[$id]="$vardir"
    SRV_MYSQLD_DATADIR[$id]="$vardir/data"
    SRV_MYSQLD_TMPDIR[$id]="$vardir/tmp"
    SRV_MYSQLD_PIDFILE[$id]="${TEST_BASEDIR}/mysqld${id}.pid"
    SRV_MYSQLD_PORT[$id]=`get_free_port $id`
    SRV_MYSQLD_SOCKET[$id]=`mktemp -t xtrabackup.mysql.sock.XXXXXX`
}

########################################################################
# Reset server variables
########################################################################
function reset_server_variables()
{
    local id=$1

    if [ -z ${SRV_MYSQLD_VARDIR[$id]:-""} ]
    then
	# Variables have already been reset
	return 0;
    fi

    SRV_MYSQLD_IDS[$id]=
    SRV_MYSQLD_VARDIR[$id]=
    SRV_MYSQLD_DATADIR[$id]=
    SRV_MYSQLD_TMPDIR[$id]=
    SRV_MYSQLD_PIDFILE[$id]=
    SRV_MYSQLD_PORT[$id]=
    SRV_MYSQLD_SOCKET[$id]=
}

##########################################################################
# Change the environment to make all utilities access the server with an
# id specified in the argument.
##########################################################################
function switch_server()
{
    local id=$1

    MYSQLD_VARDIR="${SRV_MYSQLD_VARDIR[$id]}"
    MYSQLD_DATADIR="${SRV_MYSQLD_DATADIR[$id]}"
    MYSQLD_TMPDIR="${SRV_MYSQLD_TMPDIR[$id]}"
    MYSQLD_PIDFILE="${SRV_MYSQLD_PIDFILE[$id]}"
    MYSQLD_PORT="${SRV_MYSQLD_PORT[$id]}"
    MYSQLD_SOCKET="${SRV_MYSQLD_SOCKET[$id]}"

    MYSQL_ARGS="--no-defaults --socket=${MYSQLD_SOCKET} --user=root"
    MYSQLD_ARGS="--no-defaults --basedir=${MYSQL_BASEDIR} \
--socket=${MYSQLD_SOCKET} --port=${MYSQLD_PORT} --server-id=$id \
--datadir=${MYSQLD_DATADIR} --tmpdir=${MYSQLD_TMPDIR} --log-bin=mysql-bin \
--relay-log=mysql-relay-bin --pid-file=${MYSQLD_PIDFILE} ${MYSQLD_EXTRA_ARGS}"
    if [ "`whoami`" = "root" ]
    then
	MYSQLD_ARGS="$MYSQLD_ARGS --user=root"
    fi

    export MYSQL_HOME=$MYSQLD_VARDIR

    IB_ARGS="--user=root --socket=${MYSQLD_SOCKET} --ibbackup=$XB_BIN"
    XB_ARGS="--no-defaults"

    # Some aliases for compatibility, as tests use the following names
    topdir="$MYSQLD_VARDIR"
    mysql_datadir="$MYSQLD_DATADIR"
    mysql_tmpdir="$MYSQLD_TMPDIR"
    mysql_socket="$MYSQLD_SOCKET"
}

########################################################################
# Start server with the id specified as the first argument
########################################################################
function start_server_with_id()
{
    local id=$1
    shift

    vlog "Starting server with id=$id..."

    init_server_variables $id
    switch_server $id

    init_mysql_dir
    cat > ${MYSQLD_VARDIR}/my.cnf <<EOF
[mysqld]
datadir=${MYSQLD_DATADIR}
tmpdir=${MYSQLD_TMPDIR}
EOF

    # Start the server
    ${MYSQLD} ${MYSQLD_ARGS} $* &
    if ! mysql_ping
    then
        die "Can't start the server!"
    fi
    vlog "Server with id=$id has been started on port $MYSQLD_PORT, \
socket $MYSQLD_SOCKET"
}

########################################################################
# Stop server with the id specified as the first argument and additional
# command line arguments (if specified)
########################################################################
function stop_server_with_id()
{
    local id=$1
    switch_server $id

    vlog "Stopping server with id=$id..."

    if [ -f "${MYSQLD_PIDFILE}" ]
    then
	${MYSQLADMIN} ${MYSQL_ARGS} --shutdown-timeout=60 shutdown
	if [ -f "${MYSQLD_PIDFILE}" ]
	then
	    vlog "Could not stop the server with id=$id, using kill -9"
	    kill -9 `cat ${MYSQLD_PIDFILE}`
	    rm -f ${MYSQLD_PIDFILE}
	fi
	vlog "Server with id=$id has been stopped"
    else
	vlog "Server pid file '${MYSQLD_PIDFILE}' doesn't exist!"
    fi

    # unlock the port number
    free_reserved_port $MYSQLD_PORT

    reset_server_variables $id
}

########################################################################
# Start server with id=1 and additional command line arguments
# (if specified)
########################################################################
function start_server()
{
    start_server_with_id 1 $*
}

########################################################################
# Stop server with id=1
########################################################################
function stop_server()
{
    stop_server_with_id 1
}

########################################################################
# Stop all running servers
########################################################################
function stop_all_servers()
{
    local id
    for id in ${SRV_MYSQLD_IDS[*]}
    do
	stop_server_with_id ${SRV_MYSQLD_IDS[$id]}
    done
}

########################################################################
# Configure a specified server as a slave
# Synopsis:
#   setup_slave <slave_id> <master_id>
#########################################################################
function setup_slave()
{
    local slave_id=$1
    local master_id=$2

    vlog "Setting up server #$slave_id as a slave of server #$master_id"

    switch_server $slave_id

    run_cmd $MYSQL $MYSQL_ARGS <<EOF
CHANGE MASTER TO
  MASTER_HOST='localhost',
  MASTER_USER='root',
  MASTER_PORT=${SRV_MYSQLD_PORT[$master_id]};

START SLAVE
EOF
}

########################################################################
# Wait until slave catches up with master.
# The timeout is hardcoded to 300 seconds
#
# Synopsis:
#   sync_slave_with_master <slave_id> <master_id>
#########################################################################
function sync_slave_with_master()
{
    local slave_id=$1
    local master_id=$2
    local count
    local master_file
    local master_pos

    vlog "Syncing slave (id=#$slave_id) with master (id=#$master_id)"

    # Get master log pos
    switch_server $master_id
    count=0
    while read line; do
      	if [ $count -eq 1 ] # File:
      	then
      	    master_file=`echo "$line" | sed s/File://`
      	elif [ $count -eq 2 ] # Position:
      	then
      	    master_pos=`echo "$line" | sed s/Position://`
      	fi
      	count=$((count+1))
    done <<< "`run_cmd $MYSQL $MYSQL_ARGS -Nse 'SHOW MASTER STATUS\G' mysql`"

    echo "master_file=$master_file, master_pos=$master_pos"

    # Wait for the slave SQL thread to catch up
    switch_server $slave_id

    run_cmd $MYSQL $MYSQL_ARGS <<EOF
SELECT MASTER_POS_WAIT('$master_file', $master_pos, 300);
EOF
}

########################################################################
# Prints checksum for a given table.
# Expects 2 arguments: $1 is the DB name and $2 is the table to checksum
########################################################################
function checksum_table()
{
    $MYSQL $MYSQL_ARGS -Ns -e "CHECKSUM TABLE $2 EXTENDED" $1 | awk {'print $2'}
}

########################################################################
# Workarounds for a bug in grep 2.10 when grep -q file > file would
# result in a failure.
########################################################################
function grep()
{
    command grep "$@" | cat
    return ${PIPESTATUS[0]}
}

function egrep()
{
    command egrep "$@" | cat
    return ${PIPESTATUS[0]}
}

# To avoid unbound variable error when no server have been started
SRV_MYSQLD_IDS=
