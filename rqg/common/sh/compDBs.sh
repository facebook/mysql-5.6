#! /bin/bash
#
# setup directories
   if [ "$#" -ne 2 ]; then
      echo Usage: $0 testBuild testDB
      exit 
   fi
#
   testBuild=$1
   testDB=$2
   MYRBUILD=$testBuild
   source $MYRHOME/common/env/myrclient.sh
   rm -f /tmp/dump*
#
# wait for slave to finish replication for a maximum number of seconds
  $MYRHOME/common/sh/waitForReplicationDone.sh reptest
#
  $MYRCLIENTDIR/mysqldump -u root --port=$MYRMASTERPORT --socket=$MYRMASTERSOCKET --routines --skip-triggers $testDB > /tmp/dumpMaster.original.sql
  $MYRCLIENTDIR/mysqldump -u root --port=$MYRSLAVE1PORT --socket=$MYRSLAVE1SOCKET --routines --skip-triggers $testDB > /tmp/dumpSlave1.original.sql
#
# engine in the create table is different, as well as the auto increment number
# skip the last line of the create table command in diff
#  cat /tmp/dumpSlave1.original.sql |sed 's/InnoDB/ROCKSDB/' > /tmp/dumpSlave1.sql
   cat /tmp/dumpMaster.original.sql |grep -v "ROCKSDB" |grep -v "InnoDB" |grep -v "Dump completed" > /tmp/dumpMaster.sql
   cat /tmp/dumpSlave1.original.sql |grep -v "InnoDB"  |grep -v "Dump completed" > /tmp/dumpSlave1.sql
#   
  touch /tmp/dumpDiff.txt
  diff /tmp/dumpMaster.sql /tmp/dumpSlave1.sql > /tmp/dumpDiff.txt
#
