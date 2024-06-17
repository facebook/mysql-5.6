#!/bin/bash
#
   dbName=$1
   dbSize=$2
#
   $MYRHOME/databases/tpch/sh/createTPCHTablesMYR.sh $dbName > createTables$dbName.log 2>&1
   $MYRHOME/databases/tpch/sh/loadTPCHTables.sh $dbName $dbSize > loadTables$dbName.log 2>&1
#
