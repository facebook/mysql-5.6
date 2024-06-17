#!/bin/bash
#
#=========================================================================================
# Generate unique test run ID
#=========================================================================================
function genTestRunID {
#
   testRunID="notthereyet"
   dirName=""
   while [ $testRunID = "notthereyet" ]
   do
      dirName=$(date +%Y%m%d%H%M%S)
      if [ ! -d ~/pf/testResult/$dirName ]
      then
         mkdir -p ~/pf/testResult/$dirName
         sync
         testRunID=$dirName
      fi
   done
}
#=========================================================================================
# Create unique test ID, create a directory for it, and cd into the test directory
#=========================================================================================
function prepareTestDir {
#
   genTestRunID
   cd ~/pf/testResult/$testRunID
   echo TestResultDir=`pwd`
#
}
#=========================================================================================
# get test info
#=========================================================================================
function getTestInfo {
#
   testID=`cat testInfo.txt | grep testID |awk -F"=" '{print $2}'`
   testDB=`cat testInfo.txt | grep testDB |awk -F"=" '{print $2}'`
   fileName=`cat testInfo.txt | grep scriptName |awk -F"=" '{print $2}'`
   sessions=`cat testInfo.txt | grep sessions |awk -F"=" '{print $2}'`
   iterations=`cat testInfo.txt | grep iterations |awk -F"=" '{print $2}'`
   timeoutVal=`cat testInfo.txt | grep timeoutVal |awk -F"=" '{print $2}'`
   testType=`cat testInfo.txt | grep grpTestType |awk -F"=" '{print $2}'`
   dbmsType=`cat testInfo.txt | grep dbmsType |awk -F"=" '{print $2}'`
}
#=========================================================================================
# log test info
#=========================================================================================
function logTestInfo {
#
   cp $execDir/testInfo.txt .
   cp $fileName $logFileNamePrefix
   execServer=`hostname -s`
   echo execServer=$execServer >>testInfo.txt
}
#=========================================================================================
# log environment info
#=========================================================================================
function breakScriptFile {
#
   numUsers=$1
   idx=1
   cat $logFileNamePrefix |
   while read lineText; do
      i=`expr index "$lineText" ^`
      if [ $i -gt 0 ]; then
         ((idx++))
         if [ $idx -gt $numUsers ]; then
            idx=1
         fi
      else
         echo "$lineText" >> $idx.sql
      fi
   done
}

#=========================================================================================
# Extract and output start and ending times
#=========================================================================================
function logTimes {
#
   local dirName=""

   if [ $1 -eq 0 ]
   then
      dirName="."
   elif [ $2 -eq 0 ]
      then
         dirName=$1
   else
      dirName=$1/$2
   fi
   date "+%Y-%m-%d %H:%M:%S" > $dirName/$3time.log

}
#=========================================================================================
# Extract and output start and ending times
#=========================================================================================
function getTimes {
#
   if [ $1 -eq 0 ]
   then
      st=`cat starttime.log`
      et=`cat stoptime.log`
   elif [ $2 -eq 0 ]
      then
         st=`cat $1\/starttime.log`
         et=`cat $1\/stoptime.log`
   else
      st=`cat $1\/$2\/starttime.log`
      et=`cat $1\/$2\/stoptime.log`
   fi
   echo $testRunID $1 $2 $st $et >>testResultSummary.txt
}
#=========================================================================================
# Waiting for sessions to finish.....
#=========================================================================================
function waitForDone {
#
# $1 = Test run number
# 
#  assume at least one session is not done
#
   stillGoing=1
#
#  Assuem all sesions are done.
#  Keep checking all N sessions.  If any one session is not done yet, keep going
#
   while [ $stillGoing -gt 0 ]
   do
      stillGoing=0
      for (( sess=1; sess<=sessions; sess++ ))
      do
         if [ ${pids[sess]} -ne 0 ]
         then
            lines=`ps -p ${pids[sess]} |wc -l`
            if [ $lines -eq 1 ]
            then
               logTimes $1 $sess stop
               pids[$sess]=0
#               wc -l $1\/$sess\/$logFileNamePrefix.log > $1\/$sess\/rowCnt.txt
            else
               stillGoing=1
               break
            fi
         fi
      done
      if [ $stillGoing -eq 1 ]
      then
         sleep 1
      fi
   done
}
#=========================================================================================
# Execute a single test run
#=========================================================================================
function execOneTestRun {
# $1 = Test run Number
#
   logTimes $1 0 start
#
   for (( sess=1; sess<=$sessions; sess++ ))
   do
      mkdir $1\/$sess
      logTimes $1 $sess start
      if [ $fileExt = "sh" ]; then
         $fileName $1\/$sess\/$logFileNamePrefix.log &
      else
         if [ $testType = "M" ]; then
            sfn=$sess.sql
         else
            sfn=`basename $fileName`
         fi
         if [ $dbmsType = "M" ]; then
            $MYRCLIENT $testDB -f <$sfn > $1\/$sess\/$sfn.log &
         else
            su - oracle -c "sqlplus /nolog @/home/qa/srv/common/script/callogin.sql $testDB $testDB xe srvqaperf2 <$sfn" |grep Calpont > $1\/$sess\/$sfn.log &
         fi
      fi
      pids[$sess]=$!
   done
#check for back ground status here
   waitForDone $1
#
   logTimes $1 0 stop
}
#=========================================================================================
# Execute a all test run
#=========================================================================================
function execAllTestRuns {
# $1 = Test run Number
#
   cp $fileName $logFileNamePrefix
   logTimes 0 0 start
   for (( iter=1; iter<=$iterations; iter++ ))
   do
      echo iteration=$iter
      mkdir $iter
      execOneTestRun $iter
   done
   logTimes 0 0 stop
}
#=========================================================================================
# Main
#=========================================================================================
#
   execDir=$autopilotExecDir
   rm -f /tmp/autopilotExecDir.txt
   prepareTestDir
   cp $execDir/testInfo.txt .
   getTestInfo
#
   source $MYRHOME/common/env/myrclient.sh
#
   fileExt=${fileName##*.}
   logFileNamePrefix=`basename $fileName`
#
   if [ $fileExt != "sql" ] && [ $fileExt != "sh" ]; then
      echo Unsupported file.  Only .sql and .sh files are currently supported.
      exit
   fi
#
   echo filename=$fileName
   logTestInfo
   if [ "$testType" = "M" ]; then
      breakScriptFile $sessions 
   fi
   execAllTestRuns
   echo testResultDir=$testRunID >>testInfo.txt
   cp testInfo.txt $execDir
 
   
