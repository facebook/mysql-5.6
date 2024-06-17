#! /bin/bash
#
   if [ "$#" -ne 3 ]; then
      echo Missing required parameters
      echo Usage: $0 testDB sqlScriptName numSessions
      exit 
   fi
#
   testDB=$1
   scriptName=$2
   numSessions=$3
#
   source $MYRHOME/common/env/myrclient.sh
#   echo Running $scriptName test with $numSessions concurrent sessions......
   rm -f sesspids.txt
   for ((i=1; $i<=$numSessions; i++)); do
      $MYRCLIENT $testDB -vvv -f <$scriptName > $scriptName.$i.log 2>$scriptName.$i.err &
      echo $! >> sesspids.txt
   done
#
   $MYRHOME/common/sh/waitForExecDone.sh sesspids.txt
#  
