#! /bin/bash
#
   buildName1=myrtst
   buildName2=myrref
   testDir1=$MYRRELHOME/$buildName1
   testDir2=$MYRRELHOME/$buildName2
   echo Module: $0 TestDir=$testDir1 RefDir=$testDir2
#
# Stop existing msyqld servers if exist
   killall mysqld
#   
# start test server
   cd $testDir1/mysql-5.6/sql
   ./mysqld --defaults-file=$testDir1/testdbtst.cnf &
   sleep 5
#
# start reference server
   cd $testDir2/mysql-5.6/sql
   ./mysqld --defaults-file=$testDir2/testdbref.cnf &
#
# End of script
