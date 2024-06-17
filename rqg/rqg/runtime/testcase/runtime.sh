#! /bin/bash
#
# This script runs a list of RQG tests against a database
# Additional RQG tests can be added to the list
#
   numIters=1
   numThreads=3
   numQueries=10000
   testName=runtime
   errCode=0
#   
   dataFileName=alter_online.zz
   grammerFileName=alter_online.yy   
   $MYRHOME/rqg/common/sh/rqgTest.sh $numIters $testName $dataFileName $grammerFileName $numThreads $numQueries
   errCode=`expr $errCode + $?`
#
   dataFileName=concurrency_1.zz
   grammerFileName=concurrency_1.yy
   $MYRHOME/rqg/common/sh/rqgTest.sh $numIters $testName $dataFileName $grammerFileName $numThreads $numQueries
   errCode=`expr $errCode + $?`
#
   dataFileName=connect_kill_data.zz
   grammerFileName=connect_kill_sql.yy
   $MYRHOME/rqg/common/sh/rqgTest.sh $numIters $testName $dataFileName $grammerFileName $numThreads $numQueries
   errCode=`expr $errCode + $?`
#
   dataFileName=metadata_stability.zz
   grammerFileName=metadata_stability.yy
   $MYRHOME/rqg/common/sh/rqgTest.sh $numIters $testName $dataFileName $grammerFileName $numThreads $numQueries
   errCode=`expr $errCode + $?`
#
   exit $errCode
# end of script
