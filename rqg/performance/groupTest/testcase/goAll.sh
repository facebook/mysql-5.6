#!/bin/bash
#
#   grpNum=		group number 
#   testDB=		database to be used
#   numConcur		number of concurrent user
#   numRepeat		number of iterations to repeat the test
#   testType		D = disk run.   Flush disk catch before running a query
#			C = cache run.  Run each query twice, one disk and one cache
#			S = stream run. Do not flush cache and run all queries in the group test
#                    M = mixed run.  Use query groups 1 to 5 as one group.  all users will pick queries from the group.
#                                    Each query will be executed only once.
#   timeoutVal	Timeout value to abort the test.  Not yet implemented.
#   dbmsType		DBMS type, M for mySQL. 
#
# This test runs for one group only.

   testBuild=mytest
   testDB=test
   grpNum=201
   loadDBFlag=1
#
   $MYRHOME/performance/groupTest/test/groupTest.sh $testBuild $testDB $grpNum $loadDBFlag
   exit $?
#   