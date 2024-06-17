#!/bin/bash
#
# Verify test results
   testName=$1
   rowCnt=$2
   valFactor=$3
   numSessions=$4
#
   queryCnt=`cat $testName.sql|wc -l`
   totalQueryCnt=`expr $queryCnt \\* $numSessions`
#
   successCnt=`grep -i aff $testName.sql.*.log |wc -l`
   timeoutCnt=`grep -i timeout $testName.sql.*.log|wc -l`
   processedCnt=`expr $successCnt + $timeoutCnt`
#
   actVals=`tail -n 1 result$testName.txt`
   expVals=`expr $rowCnt \\* $successCnt \\* $valFactor`
#   
   if [ $actVals -eq $expVals ] ; then
      status=Passed
   else
      status=Failed
   fi
   echo testName=$testName Status=$status rowCnt=$rowCnt queryCnt=$queryCnt sessionCnt=$numSessions totalQueryCnt=$totalQueryCnt successCnt=$successCnt timeoutCnt=$timeoutCnt actVals=$actVals expVals=$expVals >> testStatus.txt
#   cat testStatus.txt
