#!/bin/bash
#
   if [ "$#" -ne 6 ]; then
      echo "Usage: $0 numIterations testName dataFileName grammerFileName numThreads numQueries repBuildName"
      exit 1
   fi
#
   numIters=$1
   testName=$2
   dataFileName=$3
   grammerFileName=$4
   numThreads=$5
   numQueries=$6   
#
   MYRBUILD=setup
   source $MYRHOME/common/env/myrclient.sh
   testDB=test
# setup test directory and log files
   testDir=~/tests/rqg/$testName/$dataFileName.$grammerFileName
   logFileName=$testDir/exec.log
   mkdir -p $testDir
   echo `date` > $logFileName
#
# start and synchronize replication master and slave servers
   $MYRHOME/myrocks/sh/resetEnvRep.sh $MYRBUILD > $testDir/resetEnvRep.log 2>&1
#
# run requested tests
   cd $MYRHOME/rqg/common/mariadb-patches
   for i in $(seq 1 $numIters)
   do
      echo iteration=$i  `date` >> $logFileName
      perl gentest.pl --dsn=dbi:mysql:host=localhost:port=$MYRMASTERPORT:user=root:database=$testDB:mysql_socket=$MYRMASTERSOCKET --gendata=conf/$testName/$dataFileName --grammar=conf/$testName/$grammerFileName --threads=$numThreads --queries=$numQueries --sqltrace>> $logFileName 2>&1 
   done
#
# check rqg test status
   tmpStatus=`tail -n 1 $logFileName|grep "Test completed successfully"|wc -l`
   if [ $tmpStatus -eq 1 ]; then
       rqgStatus=Passed
# check replication status
       $MYRHOME/common/sh/compDBs.sh $MYRBUILD $testDB
       mv /tmp/repStatusSlave1.txt $testDir
       mv /tmp/dump* $testDir
       numDiff=`cat $testDir/dumpDiff.txt|wc -l`
       if [ "$numDiff" -eq 0 ]; then
          repStatus=Passed
       else
          repStatus=Failed
       fi
   else
       rqgStatus=Failed
       repStatus=NA
   fi
#
# set final status
   if [ "$rqgStatus" = "Passed" ] && [ "$repStatus" = "Passed" ]; then
      status=Passed
   else
      status=Failed
   fi
#
   echo $dataFileName.$grammerFileName=$status rqgStatus=$rqgStatus repStatus=$repStatus numThreads=$numThreads numQueries=$numQueries>> $testDir/../testStatus.txt
   echo $dataFileName.$grammerFileName=$status rqgStatus=$rqgStatus repStatus=$repStatus numThreads=$numThreads numQueries=$numQueries
   if [ $status = "Passed" ]; then
      exit 0
   else
      exit 1
   fi
#