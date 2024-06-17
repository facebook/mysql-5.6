#! /bin/bash
#
   buildName=$1
   if [ "$buildName" = "" ]; then
      echo Missing build name
      echo Usage: $0 reptest
      exit 
   fi
#
   testDir=$MYRRELHOME/$buildName
   echo Module: $0 TestDir=$testDir
#
# Remove existing database directories
    cd $testDir
    rm -rf install.db
#
# Remove existing database directories
    cp -r $MYRHOME/myrocks/db/install.db .
#


