#!/bin/bash
#
#  waitTimeInMinutes
   waitTime=$1
#
   for ((iter=1;$iter<=$waitTime;iter++)); do
      sleep 60
      if [ -f continue.txt ]; then
         keepGoing=`cat continue.txt`
         if [ $keepGoing = 0 ]; then
            iter=$(($waitTime+1))
         fi
      fi
   done
