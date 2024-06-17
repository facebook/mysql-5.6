#! /bin/bash
#
# startEnvRep.sh does not apply when MYRRUNMODE is set to 2, running on existing stack.
   if [ "$MYRRUNMODE" != "2" ]; then
#
# setup directories
      buildName=$1
      if [ "$buildName" = "" ]; then
         echo Missing test name
         echo Usage: $0 reptest
         exit 
      fi
#
# Stop existing msyqld servers if exist
      killall mysqld
      testDir=$MYRRELHOME/$buildName
      echo Module: $0 TestDir=$testDir
# The following statement is needed to generate a core file in the data directory
# if mysqld crashes
#
      ulimit -c unlimited
#
# start master server
      cd $testDir/mysql-5.6/sql
      ./mysqld --defaults-file=$testDir/"$buildName"Master.cnf &
      sleep 5
#
# start slave server
      ./mysqld --defaults-file=$testDir/"$buildName"Slave1.cnf &
#
      cat /proc/`pidof -s mysqld`/limits|egrep '(Limit|core)'
   fi
#
