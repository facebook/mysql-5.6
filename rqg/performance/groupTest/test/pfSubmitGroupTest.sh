#!/bin/bash
#
   grpNum=$1
   testDB=$2
   numConCur=$3
   numRepeat=$4
   testType=$5
   timeoutVal=$6
   dbmsType=$7
  
#
   testID=1
#
   if [ $# -lt 6 ]; then
      echo Syntax: pfSubmitGroupTest.sh grpNum testDB numOfConcurrentUsers numOfRuns testType timeoutValue [dbmsType]
      echo Exit.....
      exit 1
   fi
#
   testType=`echo $testType |tr "[:lower:]" "[:upper:]"`
   dbmsType=`echo $dbmsType |tr "[:lower:]" "[:upper:]"`
#
   if [ $testType = "M" ]; then
      scriptFileName=GroupQueryMixed.sql
   else
      scriptFileName=GroupQuery$grpNum.sql
   fi
#
   if [ $dbmsType <> "O" ]; then
      dbmsType=M
   fi
   $MYRHOME/performance/groupTest/sh/pfGetGroupQueries.sh $grpNum $testType $dbmsType

#
#append current directory path to to script file name
   scriptFileName=`pwd`\/$scriptFileName
#
#  Create test info file for the execution engine
#
   echo testID=$testID >testInfo.txt
   echo testDB=$testDB >>testInfo.txt
   echo scriptName=$scriptFileName >>testInfo.txt
   echo sessions=$numConCur >>testInfo.txt
   echo iterations=$numRepeat >>testInfo.txt
   echo timeoutVal=$timeoutVal >>testInfo.txt
   echo grpTestType=$testType >>testInfo.txt
   echo grpTestNum=$grpNum >>testInfo.txt
   echo dbmsType=$dbmsType >>testInfo.txt
#
   autopilotExecDir=`pwd`
   export autopilotExecDir
#
   $MYRHOME/common/sh/testExecEngine.sh > $scriptFileName.log
   testRunID=`cat testInfo.txt |grep testResultDir |awk -F"=" '{print $2}'`
   $MYRHOME/common/sh/collExecResult.sh $testRunID > collExecResult.log
#   $MYRHOME/common/sh/insertExecResult.sh $testRunID > insertExecResult.log
#   $MYRHOME/performance/groupTest/sh/pfCopyResults.sh $testRunID >copyResults.log
 



