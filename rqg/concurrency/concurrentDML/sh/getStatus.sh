#!/bin/bash
#
   lastLine=`tail -1 testStatus.txt|awk -F" " '{print $1}'`
   if [ "$lastLine" = "0" ]; then
      echo Passed > status.txt
      exit 0
   else
      echo Failed > status.txt
      exit 1
   fi

