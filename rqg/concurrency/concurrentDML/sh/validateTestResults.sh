#!/bin/bash
#
   testDB=$1
   dmlCommand=$2
   testMode=$3
   iter=$4
#
# Get row counts for 1g TPCH tables
   cat $MYRHOME/concurrency/concurrentDML/data/tpchTableList.txt |grep -v "#"|awk '{ print $4}' > zeroRowCnts.txt
   cat $MYRHOME/concurrency/concurrentDML/data/tpchTableList.txt |grep -v "#"|awk '{ print $5}' > allRowCnts.txt
# Query for row counts
   $MYRCLIENT $testDB -f <$dmlCommand$testMode.result.validation.sql > tmp.txt 2>&1
   cat tmp.txt|grep -v count >$dmlCommand$testMode$iter.result.txt
# determin which row count file to use depending on DML command and test mode
   case "$dmlCommand" in
      update|ldi)
         case "$testMode" in
            commit|commiteach|default)
               cntFileName=allRowCnts.txt
               ;;
            rollback|rollbackeach)
               cntFileName=zeroRowCnts.txt
               ;;
         esac
         ;;
      delete)
         case "$testMode" in
            commit|commiteach|default)
               cntFileName=zeroRowCnts.txt
               ;;
            rollback|rollbackeach)
               cntFileName=allRowCnts.txt
               ;;
         esac
         ;;
      *)
         ;;
   esac
# Compare test results
   diff $dmlCommand$testMode$iter.result.txt $cntFileName > $dmlCommand$testMode$iter.status.txt

