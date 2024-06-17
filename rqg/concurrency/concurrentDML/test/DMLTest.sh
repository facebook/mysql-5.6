#!/bin/bash
#
   testBuild=$1
   testDB=$2
   dmlCommand=$3
   testMode=$4
   numIter=$5
#
   MYRBUILD=$testBuild  
   source $MYRHOME/common/env/myrclient.sh
   echo Executing $dmlCommand $testMode
#
# Generate SQL script for DML command
   $MYRHOME/concurrency/concurrentDML/sh/genDMLScript.sh $testDB $dmlCommand $testMode
#
#  Execute concurrent DML statements
   for ((i=1; $i<=$numIter; i++)); do
# Prepare tables tables
#      echo iteration $i Preparing test tables.....
      ./tablesPrep.sh > $dmlCommand$testMode$i.tablePrep.log 2>&1
# Execute tests
#      echo iteration $i Executing tests.....
      rm -f $dmlCommand.pids.txt
      cat $MYRHOME/concurrency/concurrentDML/data/tpchTableList.txt |grep -v "#" |
      while read tableName restoftheline; do
         $MYRCLIENT $testDB -vvv -f <$dmlCommand$testMode$tableName.sql > $dmlCommand$testMode$tableName$i.log 2>&1 &
         echo $! >> $dmlCommand.pids.txt
      done
      $MYRHOME/common/sh/waitForExecDone.sh $dmlCommand.pids.txt
#
      $MYRHOME/concurrency/concurrentDML/sh/validateTestResults.sh $testDB $dmlCommand $testMode $i
#
   done
#
# Check for status for all tests
   wc -l *.status.txt > testStatus.txt
   $MYRHOME/concurrency/concurrentDML/sh/getStatus.sh
   exit $?

