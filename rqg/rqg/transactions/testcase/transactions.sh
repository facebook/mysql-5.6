#! /bin/bash
#
# This script runs a list of RQG tests against a database
# Additional RQG tests can be added to the list
#
   numIters=1
   numThreads=3
   numQueries=10000
   testName=transactions
#   
   dataFileName=transactions.zz
   grammerFileName=transactions.yy   
   $MYRHOME/rqg/common/sh/rqgTest.sh $numIters $testName $dataFileName $grammerFileName $numThreads $numQueries
   errCode=`expr $errCode + $?`
#
   grammerFileName=repeatable_read.yy
   $MYRHOME/rqg/common/sh/rqgTest.sh $numIters $testName $dataFileName $grammerFileName $numThreads $numQueries
   errCode=`expr $errCode + $?`
#
   grammerFileName=transaction_durability.yy
   $MYRHOME/rqg/common/sh/rqgTest.sh $numIters $testName $dataFileName $grammerFileName $numThreads $numQueries
   errCode=`expr $errCode + $?`
#
   grammerFileName=transactions-flat.yy
   $MYRHOME/rqg/common/sh/rqgTest.sh $numIters $testName $dataFileName $grammerFileName $numThreads $numQueries
   errCode=`expr $errCode + $?`
#
   dataFileName=combinations.zz
   grammerFileName=combinations.yy   
   $MYRHOME/rqg/common/sh/rqgTest.sh $numIters $testName $dataFileName $grammerFileName $numThreads $numQueries
   errCode=`expr $errCode + $?`
#
   grammerFileName=repeatable_read.yy
   $MYRHOME/rqg/common/sh/rqgTest.sh $numIters $testName $dataFileName $grammerFileName $numThreads $numQueries
   errCode=`expr $errCode + $?`
#
   grammerFileName=transaction_durability.yy
   $MYRHOME/rqg/common/sh/rqgTest.sh $numIters $testName $dataFileName $grammerFileName $numThreads $numQueries
   errCode=`expr $errCode + $?`
#
   grammerFileName=transactions-flat.yy
   $MYRHOME/rqg/common/sh/rqgTest.sh $numIters $testName $dataFileName $grammerFileName $numThreads $numQueries
   errCode=`expr $errCode + $?`
#
   exit $errCode
# end of script
