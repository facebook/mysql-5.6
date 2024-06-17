#! /bin/bash
#
   dbName=$1
   portNum=$2
   if [ "$dbName" = "" ]; then
      echo Missing dbName [portNum]
      echo Usage: $0 dbName 
      exit 
   fi
#
   $MYRHOME/client/mysql -uroot --socket=/tmp/mysql.sock -f -vvv test -e "show tables;"
#  
