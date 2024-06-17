#!/bin/bash
#
#/*******************************************************************************
#*  Script Name:    getsql.sh
#*  Date Created:   2009.02.17
#*  Author:         Joseph Williams 
#*  Purpose:        extract lines from log file within time block
#*
#*  Parameter:      date      - A day of month in question (dd)
#*                  starttime - A start time in (HH:mm)
#*                  endtime   - An end  time in (HH:mm)
#*                  
#******************************************************************************/
#
#-----------------------------------------------------------------------------
# command line parameters
#-----------------------------------------------------------------------------
date=$1
starttime=$2
endtime=$3
#
host=$(hostname -s)
#
# change date format to match sql log date format
newdate=`date +%y%m$1`
#
# clean up previous data files
if  [ -d /tmp/$host/sql ] 
then
    rm -rf /tmp/$host/sql
fi
mkdir -p /tmp/$host/sql
#
# create the beginning and ending time search variables
st=`echo $starttime | awk -F":" '{ printf "%.4d\n", $1$2 }'`
sh=`echo $starttime | awk -F":" '{ print $1 }'`
sm=`echo $starttime | awk -F":" '{ print $2 }'`
et=`echo $endtime | awk -F":" '{ printf "%.4d\n", $1$2 }'`
eh=`echo $endtime | awk -F":" '{ print $1 }'`
em=`echo $endtime | awk -F":" '{ print $2 }'`
start="$newdate $sh:$sm"
end="$newdate $eh:$em"
foundstart="no"
foundend="no"
# 
#-----------------------------------------------------------------------------
# Search through the file looking for start and end time matches 
#-----------------------------------------------------------------------------
k=$st
while [ $k -ge $st ] && [ $k -le $et ] && [ $foundstart == "no" ]; do
     if [ $sm -ge 60 ]; then
        k=`expr $k + 39`
        sm=`expr $sm - 61`
     elif [ $k -ge $st ] && [ $k -le $et ]; then
        grep -q "$newdate $sh:$sm" /usr/local/Calpont/mysql/db/$host.log 
	if [ "$?" -eq "0" ] && [ $foundstart == "no" ]; then
	   start="$newdate $sh:$sm"
	   foundstart="yes"
	fi 
     fi
     if [ $foundstart == "no" ]; then
        k=`expr $k + 0`
        k=$((k + 1))
        ((sm++))
     fi
done
while [ $k -ge $st ] && [ $k -le $et ] && [ $foundend == "no" ]; do     
     if [ $em -ge 60 ]; then        
	k=`expr $k + 39`
        em=`expr $em - 61`
     elif [ $k -ge $st ] && [ $k -le $et ]; then
        grep -q "$newdate $eh:$em" /usr/local/Calpont/mysql/db/$host.log
        if [ "$?" -eq "0" ] && [ $foundend == "no" ]; then
           end="$newdate $eh:$em"
           foundend="yes"
        fi
     fi
     if [ $foundend == "no" ]; then
        k=`expr $k + 0`
        k=$((k + 1))
        ((em++))
     fi
done
#
#  create the awk command and write it to a temporary  run file 
cmd="/$start/,/$end/ {print \$0} "
echo $cmd >> /tmp/$host/sql/cmd.$$
# 
# execute the command 
awk -f /tmp/$host/sql/cmd.$$ /usr/local/Calpont/mysql/db/$host.log > /tmp/$host/sql/temp.log
#
exit
#
# End of Script
