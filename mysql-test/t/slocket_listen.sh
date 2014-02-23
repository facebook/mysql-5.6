#!/bin/sh

###########################################################################

# NOTE: this script returns 0 (success) even in case of failure (except for
# usage-error). This is because this script is executed under
# mysql-test-run[.pl] and it's better to examine particular problem in log
# file, than just having said that the test case has failed.

###########################################################################

basename=`basename "$0"`
dirname=`dirname "$0"`

###########################################################################

function log_debug {
  echo $1 >> $log_file
}

###########################################################################

if [ $# -ne 3 ]; then
  echo "Usage: $0 <client> <socket path> <test id>"
  exit 1
fi

client_exe="$1"
datadir_path="$2"
test_id="$3"
log_file="$MYSQLTEST_VARDIR/log/$test_id.script.log"
out_file="$MYSQLTEST_VARDIR/log/$test_id.listen.log"

log_debug "-- $basename: starting --"
log_debug "client_exe: '$client_exe'"
log_debug "datadir_path: '$datadir_path'"
log_debug "test_id: '$test_id'"
log_debug "log_file: '$log_file'"
log_debug "out_file: '$out_file'"

###########################################################################

if [ -z "$client_exe" ]; then
  log_debug "Invalid path to client executable ($client_exe)."
  exit 0;
fi

if [ ! -x "$client_exe" ]; then
  log_debug "Client by path '$client_exe' is not available."
  exit 0;
fi

###########################################################################

log_debug "--- running slocket listener --- "
$client_exe $datadir_path > $out_file &

# perhaps the most important sleep of my life... without this, the test
# fails because the socket isn't opened before the test continues
sleep 1
