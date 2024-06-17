#!/bin/bash
#
# Usage: concurWriteI124.sh testBuild testDB numSessions
#
# --------------------------------------------------------------- #
# initialize test parameters
# --------------------------------------------------------------- #
function initTest() {
   tableRowCnt=`cat $MYRHOME/concurrency/concurWriteI124/data/testSetup.txt|grep tableRowCnt|awk '{print $2}'`
   incrVal=`cat $MYRHOME/concurrency/concurWriteI124/data/testSetup.txt|grep incrVal|awk '{print $2}'`
   numStmtsPerScript=`cat $MYRHOME/concurrency/concurWriteI124/data/testSetup.txt|grep numStmtsPerScript|awk '{print $2}'`
   order1RowCnt=`cat $MYRHOME/concurrency/concurWriteI124/data/testSetup.txt|grep order1RowCnt|awk '{print $2}'`
}
# --------------------------------------------------------------- #
# Prepare table
# --------------------------------------------------------------- #
function prepareTable() {
#   echo Creating lineitem table and loading $tableRowCnt rows of data......
   $MYRCLIENT $testDB -vvv -f < $MYRHOME/databases/tpch/sql/createLineitemMyrocks.sql > createTable.log 2>&1 
   head -n $tableRowCnt $MYRHOME/data/source/tpch/1g/lineitem.tbl > /tmp/lineitem.tbl
   $MYRCLIENT $testDB -vvv -f < $MYRHOME/concurrency/concurWriteI124/sql/loadData.sql > loadoadData.log 2>&1 
}
# --------------------------------------------------------------- #
# Function to execute a test
# --------------------------------------------------------------- #
function execTest() {
   testName=$1
#
   $MYRCLIENT $testDB < $MYRHOME/concurrency/concurWriteI124/sql/getTableStats.sql > $testName.Stats1.txt 2>&1 
   $MYRHOME/common/sh/testExecConcur.sh $testDB $testName.sql $numSessions
# Capture test result
   $MYRCLIENT $testDB < $MYRHOME/concurrency/concurWriteI124/sql/getTableStats.sql > $testName.Stats2.txt 2>&1 
#   $MYRHOME/concurrency/concurWriteI124/sh/analyzeResults.sh $testName $rowCnt $valFactor $numSessions 
}
# --------------------------------------------------------------- #
# Function to verify test results
# --------------------------------------------------------------- #
function verifyResults {
   testName=$1
#
   queryCnt=`cat $testName.sql|wc -l`
   totalQueryCnt=`expr $queryCnt \\* $numSessions`
#
   successCnt=`grep -i aff $testName.sql.*.log |wc -l`
   timeoutCnt=`grep -i timeout $testName.sql.*.err|wc -l`
   processedCnt=`expr $successCnt + $timeoutCnt`
#
   colIdx=`cat $MYRHOME/concurrency/concurWriteI124/data/testSetup.txt|grep $testName|awk '{print $2}'`
   affectRowCnt=`cat $MYRHOME/concurrency/concurWriteI124/data/testSetup.txt|grep $testName|awk '{print $3}'`
   processedOverWrite=`cat $MYRHOME/concurrency/concurWriteI124/data/testSetup.txt|grep $testName|awk '{print $4}'`
   if [ "$processedOverWrite" != "" ]; then
      successCnt=$processedOverWrite
   fi
   initVals=`tail -n 1 $testName.Stats1.txt|awk -v i=$colIdx '{print $i}'`
   actVals=`tail -n 1 $testName.Stats2.txt|awk -v i=$colIdx '{print $i}'`
   actVals=`expr $actVals - $initVals`
   expVals=`expr $affectRowCnt \\* $successCnt \\* $incrVal`
# echo affectedCnt=$affectRowCnt successCnt=$successCnt  incrVal=$incrVal
#
# echo actvals=$actVals expVals=$expVals
   if [ "$actVals" = "$expVals" ] ; then
      status=Passed
   else
      status=Failed
   fi
   echo testName=$testName Status=$status rowCnt=$tableRowCnt queryCnt=$queryCnt sessionCnt=$numSessions totalQueryCnt=$totalQueryCnt successCnt=$successCnt timeoutCnt=$timeoutCnt actVals=$actVals expVals=$expVals >> testStatus.txt
}
# --------------------------------------------------------------- #
# main
# --------------------------------------------------------------- #
   if [ "$#" -ne 3 ]; then
      echo Missing one or more parameters
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
   rm -rf *.log *.sql *.txt
   initTest
# Generate DML SQL scripts
   $MYRHOME/concurrency/concurWriteI124/sh/genDMLScripts.sh $numStmtsPerScript
#
# reset database
   cat $MYRHOME/concurrency/concurWriteI124/data/sqlStatements.txt|grep -v "#" |
   while read testName statement ; do
      prepareTable
      execTest $testName
      verifyResults $testName
   done
# drop test table
   $MYRCLIENT $testDB -vvv -f < $MYRHOME/concurrency/concurWriteI124/sql/dropTable.sql > dropTable.log 2>&1
#
   errCnt=`cat testStatus.txt |grep -i Failed |wc -l`
   exit $errCnt
#
# End of script
