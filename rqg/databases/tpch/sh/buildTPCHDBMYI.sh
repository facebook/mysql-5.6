#!/bin/bash
#
   dbName=$1
   dbSize=$2
#
   $MYRCLIENT -vvv -e "create database if not exists $dbName;" > createDB$dbName.log 2>&1
   $MYRCLIENT $dbName -vvv < $MYRHOME/databases/tpch/sql/createTPCHMyisam.sql > createTables$dbName.log 2>&1
   $MYRHOME/databases/tpch/sh/loadTPCHTables.sh $dbName $dbSize > loadTables$dbName.log 2>&1
#
