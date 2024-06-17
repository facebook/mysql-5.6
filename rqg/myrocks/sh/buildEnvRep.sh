#! /bin/bash
#
# This script builds the RocksDB project from Github and setup databases 
# for a two-node master and slave replication
#
# The commands are from the following link
# https://github.com/MySQLOnRocksDB/mysql-5.6/wiki/Build-steps
#
# setup directories
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
# Build test environment
   $MYRHOME/myrocks/sh/buildEnvSingle.sh $buildName
   cd $testDir
   mkdir data1
   mkdir data2
#
# Copy cnf files for both master and slave nodes
   cp $MYRHOME/myrocks/cnf/$buildName*.cnf .
#
# Copy default database directory, start servers, and sync replication 
   $MYRHOME/myrocks/sh/resetEnvRep.sh $buildName
   rm -rf install.db
#
# end of script
