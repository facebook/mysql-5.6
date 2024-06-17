#!/bin/bash
#
   if [ -f testStatus.txt ]; then
      cnt=`cat testStatus.txt |grep -i Failed|wc -l`
      if [ $cnt = 0 ]; then
         status=Passed
      else
         status=Failed
      fi
   else
      status=Failed
   fi
   echo $status > status.txt