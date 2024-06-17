#!/bin/bash
#
#$1 = dwweek
#$2 = start hour
#$3 = stop hour
#$4 = interval in minute
#
   if [ $# -ne 4 ]; then
      echo Syntax: $0 testDB startHour stopHour intervalInMinutes
      echo Exiting.....
      exit 1
   fi
#
   testDB=$1
   startHour=$2
   stopHour=$(($3 - 1))
   interval=$4
#
   source $MYRHOME/common/env/myrclient.sh
   jobNum=0
#
   keepGoing=1
   while [ $keepGoing -eq 1 ]; do
      vTime=$(date "+%H:%M:%S %x")
      vHour=${vTime:0:2}
   	  if [ $vHour -ge $startHour ] && [ $vHour -le $stopHour ]; then
	     vMin=${vTime:3:2}
         vHour=`expr $vHour + 0`
         vMin=`expr $vMin + 0`
	     minutes=$((($vHour + 1) * 60 + $vMin - ($startHour + 1) * 60))
         remainder=`expr $minutes % $interval`         echo remainder=$remainder
         if [ $remainder -ge 0 ]; then
            dirName=$(date +%F)
            mkdir -p $dirName
            cd $dirName
            head -n 10000 $MYRHOME/data/tpch/1g/lineitem.tbl > /tmp/ldisource.txt
            $MYRCLIENT $testDB -vvv <$MYRHOME/stability/dwweek/sql/ldi.sql >> ldi.log 2>&1
# The stability test was designed for data warehousing, which involves large amount of data
# This frequently causes MyRocks to crashed due to exhaustion of memory
# The following sleep statement is to pause 10 seconds between LDI statements
# control the amount of data to be loaded
            sleep 10
            cd ..
         fi
         timeToSleep=1
      else
         timeToSleep=5
      fi
   	  sleep $timeToSleep
      if [ -f continue.txt ]; then
         keepGoing=`cat continue.txt`
      fi
   done


