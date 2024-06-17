#!/bin/bash
#
   numStmtsPerQuery=$1
#
  rm -f *.sql
#
   j=1
   cat $MYRHOME/concurrency/concurWriteI124/data/sqlStatements.txt |grep -v "#"|
   while read testName statement ; do
      for ((i=1; $i<=numStmtsPerQuery; i++)); do
         echo $statement >> $testName.sql
      done
      j=$[$j+1]
   done
