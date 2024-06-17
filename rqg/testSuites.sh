#! /bin/bash
#
   mkdir -p ~/tests
   cd ~/tests
   rm -f allStatus.txt
#
#  filter for tests that are enabled
   cat $MYRHOME/testSuites.txt |grep -v "#"|grep "=Y=" |
   while read active testCat testName desc; do
      testDir=~/tests/$testCat/$testName
      rm -rf $testDir
      mkdir -p $testDir
      curDir=`pwd`
      cd $testDir
      echo ================================
      echo $testCat.$testName -- $desc
      echo ================================
      date >startEndTime.txt
      $MYRHOME/$testCat/$testName/testcase/goAll.sh
      cd $testDir
      $MYRHOME/$testCat/$testName/sh/getStatus.sh
      status=`cat status.txt`
      date >>startEndTime.txt
      cd $curDir
      echo Status = $status
      echo $testCat.$testName = $status >> allStatus.txt
   done
   echo All Tests
   echo =========
   cat allStatus.txt
#
