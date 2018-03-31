#
# Bug 1461735: database directories not removed when drop database happened during backup
#

. inc/common.sh

if ! $XB_BIN --help 2>&1 | grep -q debug-sync; then
    skip_test "Requires --debug-sync support"
fi

XB_EXTRA_MY_CNF_OPTS="
debug-sync=xtrabackup_load_tablespaces_pause
"

start_server

function backup_local() {
	innobackupex --no-timestamp $topdir/backup
}

function prepare_local() {
	innobackupex --apply-log $topdir/backup
}

function backup_xbstream() {
	innobackupex --no-timestamp --stream=xbstream $topdir/backup > $topdir/backup.xbs
}

function prepare_xbstream() {
	mkdir $topdir/backup
	xbstream -x -C $topdir/backup < $topdir/backup.xbs
	innobackupex --apply-log $topdir/backup
}

function backup_rsync() {
	innobackupex --no-timestamp --rsync $topdir/backup
}

function prepare_rsync() {
	innobackupex --apply-log $topdir/backup
}

function do_test() {

	cat <<EOF | $MYSQL $MYSQL_ARGS
CREATE DATABASE sakila;
use sakila;
CREATE TABLE t (a INT);
INSERT INTO t (a) VALUES (1), (2), (3);
EOF

	(eval $backup_cmd) &
	job_pid=$!

	wait_for_xb_to_suspend $pid_file

	xb_pid=`cat $pid_file`

	$MYSQL $MYSQL_ARGS -Ns -e "DROP DATABASE sakila"

	# Resume the xtrabackup process
	vlog "Resuming xtrabackup"
	kill -SIGCONT $xb_pid

	# wait's return code will be the code returned by the background process
	run_cmd wait $job_pid

	eval $prepare_cmd

	if [ ! -d $topdir/backup/test ] ; then
		vlog "Database directory test is removed"
		exit 1
	fi

	if [ -d $topdir/backup/sakila ] ; then
		vlog "Database directory is not removed"
		exit 1
	fi

	rm -rf $topdir/backup
	rm -rf $pid_file

}

vlog "##############################"
vlog "# Streaming backup"
vlog "##############################"

export backup_cmd=backup_xbstream
export prepare_cmd=prepare_xbstream
export pid_file=$topdir/tmp/xtrabackup_debug_sync
do_test

vlog "##############################"
vlog "# Local backup"
vlog "##############################"

export backup_cmd=backup_local
export prepare_cmd=prepare_local
export pid_file=$topdir/backup/xtrabackup_debug_sync
do_test

vlog "##############################"
vlog "# Backup using rsync"
vlog "##############################"

export backup_cmd=backup_rsync
export prepare_cmd=prepare_rsync
export pid_file=$topdir/backup/xtrabackup_debug_sync
do_test
