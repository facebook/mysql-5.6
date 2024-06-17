#! /bin/bash
#
# This script executes the alias test that is included in the source distribution
# The alias test will initialize the database and creates needs system tables
#
   buildName=$1
   if [ "$buildName" = "" ]; then
      echo Missing build name
      echo Ex: $0 nightly
      exit 
   fi
#
   testDir=$MYRRELHOME/$buildName
   echo Module: $0 TestDir=$testDir
#
   cd $testDir/mysql-5.6/mysql-test
   ./mysql-test-run t/alias.test 
   cd
#
# end of script

