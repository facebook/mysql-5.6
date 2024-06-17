#! /bin/bash
#
# startEnvRep.sh does not apply when MYRRUNMODE is set to 2, running on existing stack.
   if [ "$MYRRUNMODE" != "2" ]; then
      buildName=$1
      if [ "$buildName" = "" ]; then
         echo Missing build name
         echo Usage: $0 nightly
         exit 
      fi
#
# Stop existing msyqld servers if exist
      killall mysqld
#   
      testDir=$MYRRELHOME/$buildName
      echo Module: $0 TestDir=$testDir
#
# The following statement is needed to generate a core file in the data directory
# if mysqld crashes
#
      ulimit -c unlimited
#
      cd $testDir/mysql-5.6/sql
      ./mysqld --defaults-file=$testDir/$buildName.cnf &
#
      cat /proc/`pidof -s mysqld`/limits|egrep '(Limit|core)'
   fi
#
