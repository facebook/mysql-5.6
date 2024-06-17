#!/bin/bash
#
   dbName=$1
#
   $MYRCLIENT -vvv -e "create database if not exists $dbName;"
   $MYRCLIENT $dbName -vvv < $MYRHOME/databases/tpch/sql/createTPCHMyrocks.sql
#
