#! /bin/bash
#
# This script builds the RocksDB project from Github 
# and run a simple test with RocksDB engine.
#
   buildName=nightly
   testDir=$MYRRELHOME\/$buildName
   echo Module: $0 TestDir=$testDir
#
   $MYRHOME/myrocks/sh/buildEnvSingle.sh $buildName
   $MYRHOME/myrocks/test/alias.sh $buildName
   cp -r $testDir/mysql-5.6/mysql-test/var/install.db $testDir/.
   $MYRHOME/myrocks/sh/startEnvSingle.sh $buildName
#
# end of script

