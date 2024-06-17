#!/bin/bash
#
   dbName=$1
   dbSize=$2
   tableName=$3
#
   echo set rocksdb_bulk_load=ON\; > ldiScript.sql
   echo "load data infile '$MYRHOME/data/source/tpch/$dbSize/$tableName.tbl' into table $tableName fields terminated by '|';" >> ldiScript.sql
   $MYRCLIENT $dbName -vvv -f < ldiScript.sql
#