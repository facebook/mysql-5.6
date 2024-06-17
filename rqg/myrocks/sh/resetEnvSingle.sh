#! /bin/bash
#
# resetEnvSingle.sh does not apply when MYRRUNMODE is set to 2, running on existing stack.
   if [ "$MYRRUNMODE" != "2" ]; then
#
# setup directories
      buildName=$1
      if [ "$buildName" = "" ]; then
         echo Missing build name
         echo Usage: $0 mytest
         exit 
      fi
#
      testDir=$MYRRELHOME/$buildName
      echo Module: $0 TestDir=$testDir
#
# stop mysqld servers if exist
      killall mysqld
      sleep 3
#
# Get a fresh copy of the databases
      echo 1. Copying a fresh copy of databases
      $MYRHOME/myrocks/sh/copyDBSingle.sh $buildName
#
# Start single stack, master and slave
      echo 2. Starting single stack
      $MYRHOME/myrocks/sh/startEnvSingle.sh $buildName
      sleep 3
#
      ps -ef |grep mysqld
   fi
#