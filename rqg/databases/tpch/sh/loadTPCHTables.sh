#!/bin/bash
#
   dbName=$1
   dbSize=$2
#
   $MYRHOME/databases/tpch/sh/loadTPCHOneTable.sh $dbName $dbSize nation
   $MYRHOME/databases/tpch/sh/loadTPCHOneTable.sh $dbName $dbSize region
   $MYRHOME/databases/tpch/sh/loadTPCHOneTable.sh $dbName $dbSize part
   $MYRHOME/databases/tpch/sh/loadTPCHOneTable.sh $dbName $dbSize supplier
   $MYRHOME/databases/tpch/sh/loadTPCHOneTable.sh $dbName $dbSize partsupp
   $MYRHOME/databases/tpch/sh/loadTPCHOneTable.sh $dbName $dbSize customer
   $MYRHOME/databases/tpch/sh/loadTPCHOneTable.sh $dbName $dbSize orders
   $MYRHOME/databases/tpch/sh/loadTPCHOneTable.sh $dbName $dbSize lineitem
#