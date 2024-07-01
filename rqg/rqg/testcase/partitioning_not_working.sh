#! /bin/bash
#
# This script runs a list of RQG tests against a database
# Additional RQG tests can be added to the list
#
   numIters=1
   numThreads=1
   numQueries=1000
   testCat=partitioning
#   
   dataFileName=partition_pruning.zz
   grammerFileName=partition_pruning.yy   
#   $MYRHOME/rqg/test/rqgTest.sh $numIters $testCat $dataFileName $grammerFileName $numThreads $numQueries
#
   grammerFileName=partitions-99-hash.yy
   $MYRHOME/rqg/test/rqgTest.sh $numIters $testCat $dataFileName $grammerFileName $numThreads $numQueries
exit
#
   grammerFileName=partitions-99-range-old.yy
   $MYRHOME/rqg/test/rqgTest.sh $numIters $testCat $dataFileName $grammerFileName $numThreads $numQueries
#
   grammerFileName=partitions-99-range.yy
   $MYRHOME/rqg/test/rqgTest.sh $numIters $testCat $dataFileName $grammerFileName $numThreads $numQueries
#
   grammerFileName=partitions-ddl.yy
   $MYRHOME/rqg/test/rqgTest.sh $numIters $testCat $dataFileName $grammerFileName $numThreads $numQueries
#
   grammerFileName=partitions_hash_key_less_rand.yy
   $MYRHOME/rqg/test/rqgTest.sh $numIters $testCat $dataFileName $grammerFileName $numThreads $numQueries
#
   grammerFileName=partitions_less_rand.yy
   $MYRHOME/rqg/test/rqgTest.sh $numIters $testCat $dataFileName $grammerFileName $numThreads $numQueries
#
   grammerFileName=partitions_list_less_rand.yy
   $MYRHOME/rqg/test/rqgTest.sh $numIters $testCat $dataFileName $grammerFileName $numThreads $numQueries
#
   grammerFileName=partitions_procedures_triggers.yy
   $MYRHOME/rqg/test/rqgTest.sh $numIters $testCat $dataFileName $grammerFileName $numThreads $numQueries
#
   grammerFileName=partitions_range_less_rand.yy
   $MYRHOME/rqg/test/rqgTest.sh $numIters $testCat $dataFileName $grammerFileName $numThreads $numQueries
#
   grammerFileName=partitions_range_ver_nb.yy
   $MYRHOME/rqg/test/rqgTest.sh $numIters $testCat $dataFileName $grammerFileName $numThreads $numQueries
#
   grammerFileName=partitions_redefine.yy
   $MYRHOME/rqg/test/rqgTest.sh $numIters $testCat $dataFileName $grammerFileName $numThreads $numQueries
#
   grammerFileName=partitions-wl4571.yy
   $MYRHOME/rqg/test/rqgTest.sh $numIters $testCat $dataFileName $grammerFileName $numThreads $numQueries
#
   grammerFileName=partitions.yy
   $MYRHOME/rqg/test/rqgTest.sh $numIters $testCat $dataFileName $grammerFileName $numThreads $numQueries
#
# end of script
