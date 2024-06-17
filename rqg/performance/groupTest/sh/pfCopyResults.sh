#! /bin/sh
#
#$1 = testRunID
#
host=`hostname -s`
smbclient //calweb/perf -Wcalpont -Uoamuser%Calpont1 -D "/results/${host}" -c "lcd /home/pf/testResult;recurse;prompt OFF;mput $1"
