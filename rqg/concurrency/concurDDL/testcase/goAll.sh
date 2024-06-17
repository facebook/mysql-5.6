#!/bin/bash
#
# Generates a SQL script to create numTables tables with numColumns columns each.
# Execute the script in numSessions sessions
#
# Usage: concurDDL.sh testBuild testDB numTables numColumns numSessions
#
   $MYRHOME/concurrency/concurDDL/test/concurDDL.sh mytest test 5 50 5
   exit $?
   
