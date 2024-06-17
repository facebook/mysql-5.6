#!/bin/bash
#
   testDB=$1
   dmlCommand=`echo $2 |tr A-Z a-z`
   testMode=$3
#
   dbSize=1m   
#
   rm -f $dmlCommand$testMode*.sql
#
   cat $MYRHOME/concurrency/concurrentDML/data/tpchTableList.txt |grep -v "#" |
   while read tableName columnName iter restOfLine; do
      case "$testMode" in
         commit|rollback|commiteach|rollbackeach)
            echo set autocommit\=0\; >>$dmlCommand$testMode$tableName.sql
            ;;
         *)
            ;;
      esac
#
# When the DML command is delete or ldi, we will run the command only once
#
      case "$dmlCommand" in
         delete|ldi)
            iter=1
            ;;
         *)
            ;;
      esac
#
      for ((i=1; $i<=$iter; i++)); do
# Generate DML command

         val=$(($i*-1))
         case "$dmlCommand" in
            ldi)
               echo load data infile \'$MYRHOME/data/source/tpch/$dbSize/$tableName.tbl\' into table $tableName fields terminated by \'\|\'\; >>$dmlCommand$testMode$tableName.sql
               ;;
            update)
               echo update $tableName set $columnName=$val\; >>$dmlCommand$testMode$tableName.sql
               ;;
            delete)
               echo delete from $tableName\; >>$dmlCommand$testMode$tableName.sql
               ;;
            *)
               echo \# SQL command $dmlCommand is not supported by this test. >>$dmlCommand$testMode$tableName.sql
               ;;
         esac
# Generate commit or rollback command, if applicable
         case "$testMode" in
            commiteach)
               echo commit\; >>$dmlCommand$testMode$tableName.sql
               ;;
            rollbackeach)
               echo rollback\; >>$dmlCommand$testMode$tableName.sql
               ;;
            *)
               ;;
         esac
     done
# Generate commit or rollback at the end of the script, if applicable
      case "$testMode" in
         commit)
            echo commit\; >>$dmlCommand$testMode$tableName.sql
            ;;
         rollback)
            echo rollback\; >>$dmlCommand$testMode$tableName.sql
             ;;
         *)
            ;;
      esac
# Generate validation script
      case "$dmlCommand" in
         ldi|delete)
            echo select count\(\*\) from $tableName\; >> $dmlCommand$testMode.result.validation.sql
            ;;
         update)
            echo select count\(\*\) from $tableName where $columnName=$val\; >> $dmlCommand$testMode.result.validation.sql
            ;;
         *)
            ;;
      esac 
   done
# Generate table preperation script
   echo $MYRHOME/databases/tpch/sh/createTPCHTablesMYR.sh $testDB > tablesPrep.sh
   case "$dmlCommand" in
      delete|update)
         echo $MYRHOME/databases/tpch/sh/loadTPCHTables.sh $testDB $dbSize >> tablesPrep.sh
            ;;
         *)
            ;;
   esac
   chmod 777 tablesPrep.sh
#




