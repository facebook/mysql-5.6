#!/bin/bash
#
function getGroupQueries {
   gid=$1
   tt=$2
   dt=$3
#
   groupNum=$gid
   idx=`cat idx.txt`
   cat $MYRHOME/performance/groupTest/data/perfGrpQueryList.txt |grep "#$gid#" |
   while read gid queryNum fileName; do
      ((idx++))
      if [ $tt = "D" ] || [ $tt = "C" ]; then
         echo select \'CalpontFlush \( $idx $queryNum \)\', calflushcache\(\)\; >> $scriptFileName
      fi 
#
      if  [ $dt = "M" ]; then
         echo select \'CalpontStart   \( $idx $queryNum \)\', now\(\)\; >> $scriptFileName
      else
         echo execute calpont.caltraceon\(9\)\; >> $scriptFileName
         echo select \'CalpontStart   \( $idx $queryNum \)\', sysdate from dual\; >> $scriptFileName
      fi
#
      cat $MYRHOME/performance/groupTest/sql/$groupNum/$fileName >>$scriptFileName
#
      if  [ $dt = "M" ]; then
         echo select \'CalpontEnd   \( $idx $queryNum \)\', now\(\)\; >> $scriptFileName
#         echo select \'CalpontStats \( $idx $queryNum \)\', calgetstats\(\)\; >> $scriptFileName
      else
         echo select \'CalpontEnd   \( $idx $queryNum \)\', sysdate from dual\; >> $scriptFileName
#         echo select \'CalpontStats \( $idx $queryNum \)\', calpont.getstats\(\) from dual\; >> $scriptFileName
      fi
#
      if [ $tt = "C" ]; then
         ((idx++))
         if  [ $dt = "M" ]; then
            echo select \'CalpontStart   \( $idx $queryNum \)\', now\(\)\; >> $scriptFileName
         else
            echo select \'CalpontStart   \( $idx $queryNum \)\', sysdate from dual\; >> $scriptFileName
         fi
         cat $MYRHOME/performance/groupTest/sql/$groupNum/$fileName >>$scriptFileName
         if  [ $dt = "M" ]; then
            echo select \'CalpontEnd   \( $idx $queryNum \)\', now\(\)\; >> $scriptFileName
#            echo select \'CalpontStats \( $idx $queryNum \)\', calgetstats\(\)\; >> $scriptFileName
         else
            echo select \'CalpontEnd   \( $idx $queryNum \)\', sysdate from dual\; >> $scriptFileName
#            echo select \'CalpontStats \( $idx $queryNum \)\', calpont.getstats\(\) from dual\; >> $scriptFileName
         fi
      fi
      if [ $tt = "M" ]; then
         echo ^ >> $scriptFileName
      fi
      echo $idx > idx.txt
   done
}
   if [ $# -lt 3 ]; then
      echo ***** pfGetGroupQueries.sh queryGroupNumber testType dbmsType
      echo testType=S  Stream run.  No primproc disk cache flush
      echo testType=D  Disk run. Flush cache before executing each query
      echo testType=C  Cache run.  Flush cache before 1st execution.  No flush before 2nd execution.
      echo testType=M  Stream run.  All queries from groups 1 to 5
      exit 1
   fi
#
   grpID=$1
   testType=$2
   dbmsType=$3
#
   if [ $testType = M ]; then
      scriptFileName=GroupQueryMixed.sql
   else
      scriptFileName=GroupQuery$grpID.sql 
   fi
   rm -rf $scriptFileName
#
   groupNum=$groupNum
   echo $idx >idx.txt
#
#
  case "$testType" in
      S|D|C)
         getGroupQueries $grpID $testType $dbmsType
         ;;   
      M)
         for (( g=1; g<=5; g++)); do
            getGroupQueries $g $testType $dbmsType
         done
         ;;
   esac
   rm -f idx.txt
   exit 0
