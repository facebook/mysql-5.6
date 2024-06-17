#!/bin/bash
#
#1 = testDB
#2 = start hour
#3 = stop hour
#4 = grpNum
#5 = numConCur
#6 = secsToPause
#
   mkdir -p ~/tests/dwweek
   cd ~/tests/dwweek
#
   top -b >memUsageUM.log &
# set continue flag for test to run
   echo 1 > continue.txt
# test #1
#  run user queries from 7:00am to 6:00pm
# Usage: dwControlGroup.sh testDB startHour stopHour testGroup numSessions secsToPauseBetweenRuns

   $MYRHOME/stability/dwweek/test/dwControlGroup.sh dwweek 7 18 200 3 0 &
   sleep 5
   $MYRHOME/stability/dwweek/test/dwControlGroup.sh dwweek 7 18 201 3 15 &
   sleep 5
   $MYRHOME/stability/dwweek/test/dwControlGroup.sh dwweek 7 18 202 4 30 &
#
# run user query group #3 from 6:00pm to midnight
# Each run should take over one hour so we will stop initiating jobs after 10:00pm
# Effectively, jobs will finished sometime after 11:00pm
#
   $MYRHOME/stability/dwweek/test/dwControlGroup.sh dwweek 18 22 3 2 0 &
#
#  From 9:00pm to 11:00pm, instead of do cpimport in the PM, do "load data infile..." instead.
# Usage: dwLDI.sh testDB startHour stopHour intervalInMinute
#        intervalInMinutes=15 means every 15 minutes at 0, 15, 30, 45 of the hour
   $MYRHOME/stability/dwweek/test/dwLDI.sh dwweek 21 22 15 &
#
#  Nightly delete, update, and backup -midnight to 7:00am
# Usage: dwControlNightly.sh testDB startHour
#
   $MYRHOME/stability/dwweek/test/dwControlNightly.sh dwweek 0 &
#
