#! /bin/bash
#
# This script executes the stress test that is included in the source distribution
#
   buildName=$1
   if [ "$buildName" = "" ]; then
      echo Missing build name
      echo Ex: $0 nightly
      exit 
   fi
#
   testDir=$MYRRELHOME/$buildName
   echo Module: $0 TestDir=$testDir/mysql-5.6/mysql-test/qa/test
#
   cd $testDir/mysql-5.6/mysql-test
   rm -rf qa/test
   mkdir -p qa/test
   mkdir -p qa/test/var/log
   perl mysql-stress-test.pl \
      --cleanup \
      --stress-basedir=./qa/test \
      --stress-suite-basedir=./suite/innodb_stress \
      --server-logs-dir=./qa/test/var/log \
      --mysqltest=../client/mysqltest \
      --stress-tests-file=stress-tests.txt \
      --threads=10 \
      --test-count=10000 > ./qa/test/stress-test.log
   cd
#
# end of script
