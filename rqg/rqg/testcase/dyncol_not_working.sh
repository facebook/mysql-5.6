#! /bin/bash
#
# This script runs a list of RQG tests against a database
# Additional RQG tests can be added to the list
#
   numIters=5
   numThreads=5
   numQueries=1000
   testCat=dyncol
#   
   dataFileName=dyncol_dml.zz
   grammerFileName=dyncol_dml.yy   
   $MYRHOME/rqg/test/rqgTest.sh $numIters $testCat $dataFileName $grammerFileName $numThreads $numQueries
#
