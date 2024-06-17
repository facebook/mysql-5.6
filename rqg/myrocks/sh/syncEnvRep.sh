#! /bin/bash
#
# setup directories
   buildName=$1
   if [ "$buildName" = "" ]; then
      echo Missing build name
      echo Usage: $0 reptest
      exit 
   fi
#
   MYRBUILD=$buildName
   source $MYRHOME/common/env/myrclient.sh
#   
# run SQL script resets replication master and checks for current binlog file and position
#
  $MYRCMASTER -vvv test < $MYRHOME/myrocks/sql/repResetMaster.sql > /tmp/repResetMaster.log
#
# extract current binlog file name and position and create a SQL script for syncing the Slave
#
  binLogPos=`cat /tmp/repResetMaster.log |grep "mysql-bin"`
#  binLogPos="| mysql-bin.000001 |       107 |"
#  echo $binLogPos
  binLogPos=`echo -n "${binLogPos//[[:space:]]/}"`
#  echo $binLogPos
  binlogFile=`echo $binLogPos |awk -F"|" '{ print $2 }'`
  binlogPos=`echo $binLogPos |awk -F"|" '{ print $3 }'`
  echo stop slave\; > /tmp/repResetSlave.sql
  echo CHANGE MASTER TO MASTER_HOST=\'localhost\',MASTER_USER=\'slave_user\', MASTER_PASSWORD=\'password\', MASTER_LOG_FILE=\'$binlogFile\', MASTER_LOG_POS= $binlogPos\; >>/tmp/repResetSlave.sql
  echo start slave\; >> /tmp/repResetSlave.sql
  echo show slave status \\G\; >> /tmp/repResetSlave.sql
#
# execute SQL script to sync the slave
#
  $MYRCSLAVE1 -vvv test < /tmp/repResetSlave.sql > /tmp/repResetSlave.log
#

      

