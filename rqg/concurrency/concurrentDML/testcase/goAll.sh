#!/bin/bash
#
#-------------------MYRHOME/concurrency------------------------------------------------------------------------------
#
# Usage: DMLTest.sh dbName dmlCommand testMode numIter
#        buildName  = build of MyRocks to be tested
#        dbName     = name of database to be tested in
#        dmlCommand = update, delete, or ldi
#        testMode   = commit     = commit at the of SQL script
#                     rollback   = rollback at the end of SQL script
#                     commiteach = commit after each DML statement
#                     rollback   = rollback after each statement
#                     default    = default behavior controlled by the autocommit variable.
#        numIter    = number of test iterations
# Generate a script to of dmlCommands for each of the TPCH tables
# Execute the scripts in 8 sessions, one for each table
# Repeat the test for numIter times
#
   rm -f *.txt
   numIter=1
   errCode=0
#   
   $MYRHOME/concurrency/concurrentDML/test/DMLTest.sh mytest test update commit $numIter
   errCode=`expr $errCode + $?`
   $MYRHOME/concurrency/concurrentDML/test/DMLTest.sh mytest test update commiteach $numIter
   errCode=`expr $errCode + $?`
   $MYRHOME/concurrency/concurrentDML/test/DMLTest.sh mytest test update rollback $numIter
   errCode=`expr $errCode + $?`
   $MYRHOME/concurrency/concurrentDML/test/DMLTest.sh mytest test update rollbackeach $numIter
   errCode=`expr $errCode + $?`
   $MYRHOME/concurrency/concurrentDML/test/DMLTest.sh mytest test update default $numIter
   errCode=`expr $errCode + $?`
#
   $MYRHOME/concurrency/concurrentDML/test/DMLTest.sh mytest test delete commit $numIter
   errCode=`expr $errCode + $?`
   $MYRHOME/concurrency/concurrentDML/test/DMLTest.sh mytest test delete commiteach $numIter
   errCode=`expr $errCode + $?`
   $MYRHOME/concurrency/concurrentDML/test/DMLTest.sh mytest test delete rollback $numIter
   errCode=`expr $errCode + $?`
   $MYRHOME/concurrency/concurrentDML/test/DMLTest.sh mytest test delete rollbackeach $numIter
   errCode=`expr $errCode + $?`
   $MYRHOME/concurrency/concurrentDML/test/DMLTest.sh mytest test delete default $numIter
   errCode=`expr $errCode + $?`
#
   $MYRHOME/concurrency/concurrentDML/test/DMLTest.sh mytest test ldi commit $numIter
   errCode=`expr $errCode + $?`
   $MYRHOME/concurrency/concurrentDML/test/DMLTest.sh mytest test ldi commiteach $numIter
   errCode=`expr $errCode + $?`
   $MYRHOME/concurrency/concurrentDML/test/DMLTest.sh mytest test ldi rollback $numIter
   errCode=`expr $errCode + $?`
   $MYRHOME/concurrency/concurrentDML/test/DMLTest.sh mytest test ldi rollbackeach $numIter
   errCode=`expr $errCode + $?`
   $MYRHOME/concurrency/concurrentDML/test/DMLTest.sh mytest test ldi default $numIter
   errCode=`expr $errCode + $?`
#
   exit $errCode
   