#! /bin/bash
#
# This script runs a list of RQG tests against a database
# Additional RQG tests can be added to the list
#
   numIters=1
   numThreads=5
   numQueries=10000
   testName=examples
#   
#   
   dataFileName=example.zz
   grammerFileName=example.yy   
   $MYRHOME/rqg/common/sh/rqgTest.sh $numIters $testName $dataFileName $grammerFileName $numThreads $numQueries
   exit $?
# end of script
