#! /bin/bash
#
# Usage: concurTrans.sh testBuild testDB numSessions
#
   if [ "$#" -ne 3 ]; then
      echo Usage: $0 testBuild testDB numSessions
      exit 
   fi
#
   testBuild=$1
   testDB=$2   
   numSessions=$3
#
   MYRBUILD=$testBuild   
   source $MYRHOME/common/env/myrclient.sh
   $MYRHOME/myrocks/sh/resetEnvSingle.sh $MYRBUILD > resetEnvSingle.log 2>&1
#
   rm -rf sesspids.txt
#
# Create TPCH tables and load 1gb data
   $MYRHOME/databases/tpch/sh/buildTPCHDBMYR.sh $testDB 1g
# Initialize o_custkey column value to 2000000
   $MYRCLIENT test -e 'update orders set o_custkey = 20000;'
#
# create n SQL scripts, one for each concurrent user
# The last statement in the first transaction update the column with a value that starts with a 2
# before commit takes place.  At the end of the test, the distinct value in the column
# should be 2000nn, where nn is the session that had the last updated the column 
#
  rm -rf concurTrans*.* > /dev/null 2>&1
  for i in $(seq 1 $numSessions)
  do
     echo use test\; > concurTrans.$i.sql
     echo SET AUTOCOMMIT=0\; >> concurTrans.$i.sql     
     echo START TRANSACTION\; >> concurTrans.$i.sql
     echo SELECT DISTINCT o_custkey from orders\;  >> concurTrans.$i.sql
     echo UPDATE orders set o_custkey = $i\; >> concurTrans.$i.sql
     echo UPDATE orders set o_custkey = 100$i\; >> concurTrans.$i.sql
     echo UPDATE orders set o_custkey = 2000$i\; >> concurTrans.$i.sql
     echo COMMIT\; >> concurTrans.$i.sql
     echo START TRANSACTION\; >> concurTrans.$i.sql     
     echo SELECT DISTINCT o_custkey from orders\;  >> concurTrans.$i.sql
     echo UPDATE orders set o_custkey = 10000$i\; >> concurTrans.$i.sql
     echo UPDATE orders set o_custkey = 100000$i\; >> concurTrans.$i.sql
     echo UPDATE orders set o_custkey = 1000000$i\; >> concurTrans.$i.sql
     echo ROLLBACK\; >> concurTrans.$i.sql
     echo SELECT DISTINCT o_custkey from orders\;  >> concurTrans.$i.sql
#
     $MYRCLIENT test -f -vvv < concurTrans.$i.sql > concurTrans.$i.log 2>&1 &
      echo $! >> sesspids.txt
     sleep 2
#
# query and save unique column value
     $MYRCLIENT test -e 'SELECT DISTINCT o_custkey from orders;' >> concurTrans.res 2>&1
  done
#
# Wait for all SQL sessions to finish
  $MYRHOME/common/sh/waitForExecDone.sh sesspids.txt
#
# query and save unique column value one more time
   $MYRCLIENT test -e 'SELECT DISTINCT o_custkey from orders;' >> concurTrans.res 2>&1
   numLines=`cat concurTrans.res |grep -v 'custkey'|wc -l`
   numUpdatedLines=`cat concurTrans.res |grep -v 'custkey'|grep -v '20000'|wc -l`
   numMatchedLines=`cat concurTrans.res|grep -v custkey|grep 2000|wc -l`
#
# get test status
   if [[ "$numMatchedLines" -eq "$numLines"  && "$numUpdatedLines" -ne 0 ]]; then
      errCode=0
      status=Passed
   else
      errCode=1
      status=Failed
   fi
   echo concurTrans=$status > testStatus.txt
   exit $errCode    
#
# end of script
