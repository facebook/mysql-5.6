#!/bin/bash
#
#$1 = testDB
#$2 = start hour
#$3 = stop hour
#$4 = grpNum
#$5 = numConCur
#$6 = secsToPause
#
   if [ $# -ne 6 ]; then
      echo Syntax: dwControlGroup.sh testDB startHour stopHour groupNum NumConcurrentUsers secsToPause
      echo Exiting.....
      exit 1
   fi
#
   testDB=$1
   startHour=$2
   stopHour=$(($3 - 1))
   grpNum=$4
   numConCur=$5
   secsToPause=$6
#
   jobNum=0
#
   keepGoing=1
   while [ $keepGoing -eq 1 ]; do
	vTime=$(date "+%H:%M:%S %x")
	vHour=${vTime:0:2}
   	if [ $vHour -ge $startHour ] && [ $vHour -le $stopHour ]; then
       dirName=Group$grpNum
       mkdir -p $dirName
       cd $dirName
	   $MYRHOME/performance/groupTest/test/pfSubmitGroupTest.sh $grpNum $testDB $numConCur 1 S 0 M
       sleep $secsToPause
	   cd ..
       else
          sleep 60
       fi
       if [ -f continue.txt ]; then
          keepGoing=`cat continue.txt`
       fi
   done
# stop the batch mode 'top' command if exists   
   killall top


 
