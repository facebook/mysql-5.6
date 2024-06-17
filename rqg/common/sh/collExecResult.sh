#!/bin/bash
#
# $1 = testRunID
# $2 = testType, 1=SQL test, 2=Bulk test
#
# Check for correct number of command line parameters
#
#=========================================================================================
# get test information
#=========================================================================================
function getTestInfo {
#
   testID=`cat $dirName/testInfo.txt | grep testID |awk -F"=" '{print $2}'`
   calpontDB=`cat $dirName/testInfo.txt | grep testDB |awk -F"=" '{print $2}'`
   execServer=`cat testInfo.txt|grep execServer|awk -F"=" '{print $2}'`
   scriptName=`cat $dirName/testInfo.txt | grep scriptName |awk -F"=" '{print $2}'`
   sessions=`cat $dirName/testInfo.txt | grep sessions |awk -F"=" '{print $2}'`
   iterations=`cat $dirName/testInfo.txt | grep iterations |awk -F"=" '{print $2}'`
   grpTestType=`cat $dirName/testInfo.txt | grep grpTestType |awk -F"=" '{print $2}'`
   grpTestNum=`cat $dirName/testInfo.txt | grep grpTestNum |awk -F"=" '{print $2}'`
#   swVersion=`cat $dirName/1/getcalpontsoftwareinfo.log |grep Version |awk -F" " '{print $3}'`
#   swRelease=`cat $dirName/1/getcalpontsoftwareinfo.log |grep Release |awk -F" " '{print $3}'`
swVersion=""
swRelease=""
#
   fileName=`basename $scriptName`
   fileExt=${scriptName##*.}
   testRunDesc=""
#   stackName=`cat 1/Calpont.xml |grep "<SystemName>"|sed "s/</>/g"|awk -F">" '{print $3}'`
stackName=""
   stackConfig=""
#
   IOType=$grpTestType
#
   numDM=0
   numUM=0
   numPM=0

}
#=========================================================================================
# Create SQL test stats
#=========================================================================================
function getSQLTestStats {
#
   getSQLStats=0
#
   if [ $1 -eq 0 ]; then
      pathName="."
   elif [ $2 -eq 0 ]; then
         pathName="./$1"
   else
      pathName="./$1/$2"
      getSQLStats=1
   fi
   st=`cat $pathName/starttime.log`
   et=`cat $pathName/stoptime.log`
   echo $testRunID\|$1\|$2\|0\|0\|$st\|$et\| >>testResultTime.txt
#
   if [ $getSQLStats -eq 1 ] && [ $fileExt = "sql" ]; then
# get timing information
#
      if [ $grpTestType = "M" ]; then
         sfn=$2.sql
      else
         sfn=$fileName
      fi
      cat $sfn | grep CalpontStart >tmp1.txt
      cat $pathName/$sfn.log|grep Calpont |grep -v now |grep -v calgetstats >tmp2.txt

#
      cat tmp1.txt |
      while read c1 c2 c3 idx qNum restofLine; do
         st=`cat tmp2.txt |grep "CalpontStart ( ${idx} ${qNum} )" |awk -F" " '{print $6 " " $7}'`
         et=`cat tmp2.txt |grep "CalpontEnd ( ${idx} ${qNum} )" |awk -F" " '{print $6 " " $7}'`
         qstats=`cat tmp2.txt |grep "CalpontStats ( ${idx} ${qNum} )"`
         echo $testRunID\|$1\|$2\|$qNum\|$idx\|$st\|$et\| >>testResultTime.txt
      done
#
# get query stat information
#
      cat $pathName/$sfn.log|grep CalpontStats|grep -v calgetstats|sed 's/\;//g'|sed 's/-/ /g'|sed 's/MB//g'|sed 's/|//g' >tmp2.txt
      cat tmp1.txt |
      while read c1 c2 c3 idx qNum restofLine; do
         statLine=`cat tmp2.txt |grep "CalpontStats ( ${idx} ${qNum} )"`
#iteration 18, use the following line
         qstats=`echo $statLine |awk -F" " '{print $9"|"$11"|"$13"|"$15"|"$17"|"$19"|"$21"|"$23"|"$25"|"$28"."$29"|"}'`
#iteration 17 and back, use the following line
#         qstats=`echo $statLine |awk -F" " '{print $9"|"$11"|"$13"|"$15"|"$17"|"$19"|"$21"|"$23"|"$25"|"$26"."$27"|"}'`
         qstats=`echo $qstats|sed 's/|\.|/||/g'`
         echo $testRunID\|$1\|$2\|$qNum\|$idx\|$qstats>>testResultStats.txt
      done
   fi
   rm -f tmp*.txt
}
#=========================================================================================
# Create SQL test summary
#=========================================================================================
function getSQLTestSummary {
#
   numStmts=`cat testResultTime.txt| grep -v "|0|"|grep -v "#"|wc -l`
   touch tmp2.txt
   cat testResultTime.txt| grep -v "|0|"|grep -v "#"|
   while read statLine; do
      st=`echo $statLine|awk -F"|" '{print $6}'`
      et=`echo $statLine|awk -F"|" '{print $7}'`
      if [ ! -z "$st" ] && [ ! -z "$et" ]; then
         echo Y >>tmp2.txt
      fi
   done
   numStmtsProcessed=`grep Y tmp2.txt|wc -l`
   if [ $numStmts -eq $numStmtsProcessed ]; then
      runCompleted=Y
   else
      runCompleted=N
   fi   
   echo $testID\|$testRunID\|$testRunDesc\|$execServer\|$stackName\|$numDM\|$numUM\|$numPM\|$calpontDB\|$swVersion.$swRelease\|$grpTestNum\|$fileName\|$iterations\|$sessions\|$IOType\|$numStmts\|$numStmtsProcessed\|$runCompleted\| > testResultSummary.txt
   rm -f tmp*.txt

}
#=========================================================================================
# Create bulk test stats
#=========================================================================================
function getBulkTestStats {
#
   cat Job_9999.xml |grep "<Table" |
   while read c1 tableName c2 sourceName restOfLine; do
      tableName=`echo $tableName |awk -F"\"" '{print $2}'`
      sourceName=`echo $sourceName |awk -F"\"" '{print $2}'`
      loadTime=`cat Job_9999.log |grep "Finished reading"|grep $sourceName |awk -F" " '{print $13}'`
      rowsProcessed=`cat Job_9999.log |grep "For table $tableName:"|grep "rows processed"|awk -F" " '{print $9}'`
      rowsInserted=`cat Job_9999.log |grep "For table $tableName:"|grep "rows processed"|awk -F" " '{print $13}'`
      echo $testRunID\|$tableName\|$sourceName\|$loadTime\|$rowsProcessed\|$rowsInserted\|NULL\| >>testResultStats.txt
   done
}
#=========================================================================================
# Create bulk test summary
#=========================================================================================
function getBulkTestSummary {
#
#
   st=`cat starttime.log`
   et=`cat stoptime.log`
#
   numTables=`cat Job_9999.xml |grep "inserted"|wc -l`
#
   touch tmp1.txt
   touch tmp2.txt
#
   rowCntMatched=Y
   cat testResultStats.txt|
   while read statLine; do
      elapsedTime=`echo $statLine|awk -F"|" '{print $4}'`
      rowsProcessed=`echo $statLine|awk -F"|" '{print $5}'`
      rowsInserted=`echo $statLine|awk -F"|" '{print $6}'`
      if [ $rowsProcessed -ne $rowsInserted ]; then
         rowCntMatched=N
      fi
      if [ ! -z $elapsedTime ]; then
         echo Y  >>tmp1.txt
      fi
   done
   tablesLoaded=`grep Y tmp1.txt|wc -l`
#
   if [ $numTables -eq $tablesLoaded ]; then
      runCompleted=Y
   else
      runCompleted=N
   fi
   echo $testID\|$testRunID\|$testRunDesc\|$execServer\|$stackName\|$numDM\|$numUM\|$numPM\|$calpontDB\|$fileName\|$numTables\|$tablesLoaded\|$runCompleted\|$rowCntMatched\|$st\|$et\| > testResultSummary.txt
   rm -f tmp*.txt
}
#=========================================================================================
# get SQL test results
#=========================================================================================
function getSQLTestResult {
#
   getSQLTestStats 0 0
   for (( i=1; i<=$iterations; i++ ))
   do
      getSQLTestStats $i 0
      for (( s=1; s<=sessions; s++ ))
      do
         getSQLTestStats $i $s
      done
   done
   getSQLTestSummary
}
#=========================================================================================
# get Bulk test results
#=========================================================================================
function getBulkTestResult {
#
   getBulkTestStats
   getBulkTestSummary
}

#=========================================================================================
# Main
#=========================================================================================
#
   if [ $# -ne 1 ]; then
      echo Syntax: collExtcResult.sh testRunID
      echo Exiting.....
      exit 1
   fi
#
# Verified existance of testRunID
#
   curDir=`pwd`
   testRunID=$1
   host=`hostname -s`
   dirName=~/pf/testResult/$testRunID
#
   if [ ! -d $dirName ]; then
      echo TestRunID $testRunID does not exist on this server \($host\).
      echo Please make sure the test was executed on this server.
      echo Exit.....
      exit 1
   fi
   cd $dirName
#-----------------------------------------------------------------------------------------
# Initialize files
#-----------------------------------------------------------------------------------------
   rm -f testResult*.txt
   touch testResultTime.txt
   touch testResultStats.txt
#
   getTestInfo
   case "$testID" in
      1)
         getSQLTestResult
         ;;
      2)
         getBulkTestResult
         ;;
   esac
   rm -rf tmp*.txt



