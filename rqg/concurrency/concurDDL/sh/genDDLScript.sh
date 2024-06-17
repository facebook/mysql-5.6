#!/bin/bash
#
   scriptCnt=$1
   tableCnt=$2
   columnCnt=$3
#
   rm -f ddlTest*.sql
#
   for ((i=1; $i<=$scriptCnt; i++)); do
      for ((j=1; $j<=$tableCnt; j++)); do
         echo create table ddltest$i\_$j \( >> ddlTest$i.sql
#
         for ((k=1; $k<=$columnCnt; k++)); do
#            if [ $k -eq $columnCnt ]; then
#               comma=""
#            else
#               comma=","
#            fi
            echo c$k int\, >> ddlTest$i.sql
         done
#
         echo primary key \(\`c1\`\) >> ddlTest$i.sql 
         echo \) engine=rocksdb\; >> ddlTest$i.sql
         echo drop table if exists ddltest$i\_$j\;>> ddlTestDropTables.sql
      done
   done
#
