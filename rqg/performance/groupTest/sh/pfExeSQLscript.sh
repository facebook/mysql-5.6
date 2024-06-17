#!/bin/bash
#
# This script does the following:
#   1) Executes supplied SQL script on test database and captures output to file
#
#$1 = Test database name
#$2 = SQL script to execute
   if [ $# -lt 2 ]
   then
       echo***** Syntax: pfExeSQLScript.sh testDBName scriptFileName
       exit 1
   fi
#
   logFileName=`basename $2`
#
#  Execute script on reference database
#
#   mysql $3 -h$2 -u$4 -p$5 <$6 > $logFileName.test.log
#
#  Execute script on test database
#
   /usr/local/Calpont/mysql/bin/mysql --defaults-file=/usr/local/Calpont/mysql/my.cnf -u root $1 <$2 > $logFileName.test.log
   exit 0
