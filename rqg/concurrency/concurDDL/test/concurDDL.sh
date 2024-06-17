#!/bin/bash
#
# Usage: concurDDL.sh testBuild testDB numTables numColumns numSessions
#
   if [ "$#" -ne 5 ]; then
      echo Usage: $0 testBuild testDB tableCnt columnCnt numSessions
      exit 
   fi
#
   testBuild=$1   
   testDB=$2
   tableCnt=$3
   columnCnt=$4
   numSessions=$5
#
   MYRBUILD=$testBuild  
   source $MYRHOME/common/env/myrclient.sh
   $MYRHOME/myrocks/sh/resetEnvSingle.sh $MYRBUILD > resetEnvSingle.log 2>&1
#
   $MYRHOME/concurrency/concurDDL/sh/genDDLScript.sh $numSessions $tableCnt $columnCnt
#
   rm -f sesspids.txt
   for ((i=1; $i<=$numSessions; i++)); do
      $MYRCLIENT $testDB -vvv -f <ddlTest$i.sql > ddlTest$i.log 2>&1 &
      echo $! >> sesspids.txt
   done
#
   $MYRHOME/common/sh/waitForExecDone.sh sesspids.txt
#
# drop test tables created during the test
   $MYRCLIENT $testDB -vvv -f < ddlTestDropTables.sql > ddlTestDropTables.log 2>&1 &
#
# get test status
   grep -i error ddlTest*.log > errors.txt
   errCnt=`cat errors.txt | wc -l`
   if [ $errCnt -eq 0 ]; then
      errCode=0
      status=Passed
   else
      errCode=1
      status=Failed
   fi   
   echo concurDDL=$status > testStatus.txt
   exit $errCode    
#
# End of script
