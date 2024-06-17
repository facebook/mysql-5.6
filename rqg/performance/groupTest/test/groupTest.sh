#!/bin/bash
#
#   testBuild = build of MyRocks to be tested
#   grpNum=		group number 
#   testDB=		database to be used
#   numConcur		number of concurrent user
#   numRepeat		number of iterations to repeat the test
#   testType		D = disk run.   Flush disk catch before running a query
#			C = cache run.  Run each query twice, one disk and one cache
#			S = stream run. Do not flush cache and run all queries in the group test
#                    M = mixed run.  Use query groups 1 to 5 as one group.  all users will pick queries from the group.
#                                    Each query will be executed only once.
#   timeoutVal	Timeout value to abort the test.  Not yet implemented.
#   dbmsType		DBMS type, M for mySQL. 
#   loadDBFlag	Create TPCH tables and load 1gb data
#
# This test runs for one group only.
#
   if [ "$#" -ne 4 ]; then
      echo Usage: $0 testBuild testDB groupNum loadDBFlag
      exit 
   fi
#
   testBuild=$1   
   testDB=$2
   grpNum=$3
   loadDBFlag=$4
#
# The following parameters are not applicable for RocksDB so setting them to default values
   testType=S
   timeoutVal=0
   dbmsType=M
   numIter=1
   numConcur=3
#
   MYRBUILD=$testBuild
   source $MYRHOME/common/env/myrclient.sh
#
# Create TPCH tables and load 1gb data if specified
   if [ "$loadDBFlag" = 1 ]; then
      $MYRHOME/databases/tpch/sh/buildTPCHDBMYR.sh $testDB 1g
   fi
#
# Execute requested group test
   $MYRHOME/performance/groupTest/test/pfSubmitGroupTest.sh $grpNum $testDB $numConcur $numIter $testType $timeoutVal $dbmsType
#
# copy test results from the central result directory and extract timing info
   resultDir=`cat testInfo.txt |grep testResultDir |awk -F= '{print $2}'`
   cp -r ~/pf/testResult/$resultDir .
   startTime=`cat $resultDir/starttime.log`
   stopTime=`cat $resultDir/stoptime.log`
   testCompleted=`cat $resultDir/testResultSummary.txt |awk -F\| '{print $18}'`
#
   if [ "$testCompleted" = "Y" ]; then
      if [ "$startTime" != "" ] && [ "$stopTime" != "" ]; then
         startTimeInSec=`date -d "$startTime" +%s`
         stopTimeInSec=`date -d "$stopTime" +%s`
         elapsedTimeInSec=`expr $stopTimeInSec - $startTimeInSec`
         echo $startTime $stopTime $startTimeInSec $stopTimeInSec $elapsedTimeInSec >timingInfo.txt
         status=Passed
         msg=$elapsedTimeInSec
      else
         status=Failed
         msg="Missing timestamps. Test may have failed"
      fi
   else
      status=Failed
      msg="Test failed due to some of the statements were not executed"
   fi   
   echo $status $msg > testStatus.txt
   if [ $status = "Passed" ]; then
      exit 0
   else
      exit 1
   fi 
#   



