#! /bin/bash
#
# This script builds the RocksDB project from Github and setup databases 
# for test and reference environments.
#
# testing   environment engine: MyRocks
# reference environment engine: Innodb
#
# The commands are from the following link
# https://github.com/MySQLOnRocksDB/mysql-5.6/wiki/Build-steps
#
   buildName1=myrtst
   buildName2=myrref
   testDir1=$MYRRELHOME/$buildName1
   testDir2=$MYRRELHOME/$buildName2
   echo Module: $0 TestDir=$testDir1 RefDir=$testDir2
#
# Build test environment
#
   $MYRHOME/myrocks/sh/buildEnvSingle.sh $buildName1
# Build reference environment
#
   $MYRHOME/myrocks/sh/buildEnvSingle.sh $buildName2
#
# end of script
