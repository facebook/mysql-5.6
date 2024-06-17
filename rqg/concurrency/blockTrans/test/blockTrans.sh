#! /bin/bash
#
# Usage: blockTrans.sh testBuild testDB
#
   if [ "$#" -ne 2 ]; then
      echo Usage: $0 testBuild testDB
      exit 
   fi
#
   testBuild=$1
   testDB=$2   
   MYRBUILD=$testBuild
   source $MYRHOME/common/env/myrclient.sh
   $MYRHOME/myrocks/sh/resetEnvSingle.sh $MYRBUILD > resetEnvSingle.log 2>&1
#
# Remove existing log files
   rm -f blockTrans.*
   rm -rf sesspids.txt
#   
   $MYRCLIENT -f -vvv $testDB <$MYRHOME/concurrency/blockTrans/sql/createTable.sql > blockTrans.createTable.log 2>&1
   $MYRCLIENT -f -vvv $testDB <$MYRHOME/concurrency/blockTrans/sql/t1.sql > blockTrans.1.log 2>&1 & 
   echo $! >> sesspids.txt
   $MYRCLIENT -f -vvv $testDB <$MYRHOME/concurrency/blockTrans/sql/t2.sql > blockTrans.2.log 2>&1 &
   echo $! >> sesspids.txt
   $MYRCLIENT -f -vvv $testDB <$MYRHOME/concurrency/blockTrans/sql/t3.sql > blockTrans.3.log 2>&1 &
   echo $! >> sesspids.txt
#
# Wait for all SQL sessions to finish
   $MYRHOME/common/sh/waitForExecDone.sh sesspids.txt
#
#
# Extra results    
   tail -n 16 blockTrans.1.log|head -n 7 > blockTrans.1.res
   tail -n 10 blockTrans.2.log|head -n 7 > blockTrans.2.res
   head -n 11 blockTrans.3.log|tail -n 7 > blockTrans.3.res
   tail -n 10 blockTrans.3.log|head -n 7 >> blockTrans.3.res
#
   diff blockTrans.1.res $MYRHOME/concurrency/blockTrans/result/blockTrans.1.res > blockTrans.diff.txt
   diff blockTrans.2.res $MYRHOME/concurrency/blockTrans/result/blockTrans.2.res >> blockTrans.diff.txt
   diff blockTrans.3.res $MYRHOME/concurrency/blockTrans/result/blockTrans.3.res >> blockTrans.diff.txt
#
# get test status
   numDiff=`cat blockTrans.diff.txt|wc -l`
   if [ "$numDiff" -ne 0 ]
   then
      errCode=1
      status=Failed
   else
      errCode=0
      status=Passed
   fi
#
   echo blockTrans=$status > testStatus.txt
   exit $errCode

#
# end of script


         
   

