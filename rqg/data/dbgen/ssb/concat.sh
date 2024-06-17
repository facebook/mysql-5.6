#! /bin/bash
#
   cat $1.txt |
   while read fileName; do
      echo $fileName
      cat $fileName >> $1.tbl
   done
