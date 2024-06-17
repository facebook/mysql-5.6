#!/bin/bash
# $1 = session ID
# $2 = caltraceon() value. 9, 264 etc
# $3 = sendPlan hex file name
#
# jsfn  = job step file name
# jsrfn = job step result file name
#
fn=`basename $3`
jsfn=$fn'_js.dot'
jsrfn=$fn'_jsr.dot'
#
echo ----------------------------------------------------------------------------------------
echo sendPlan start: $3
date
echo
/usr/local/Calpont/bin/sendPlan -v -s$1 -t$2 $3
echo
date
echo sendPlan end: $3
echo 
sleep 1
echo Copying dot files......
/bin/cp -f /mnt/tmp/jobstep.dot $jsfn
/bin/cp -f /mnt/tmp/jobstep_results.dot $jsrfn
echo Finish copying dot files.
date
echo ----------------------------------------------------------------------------------------
echo
