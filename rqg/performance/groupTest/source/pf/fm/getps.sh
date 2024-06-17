#! /bin/sh
#
#/*******************************************************************************
#*  Script Name:    getps.sh
#*  Date Created:   2009.02.05
#*  Author:         Stephen Cargile
#*  Purpose:        retrieve ps files between start and stop times
#*
#*  Input Parameters:
#*                  date - day of month
#*                  starttime - beginning of time period (hh:mm)
#*                  endtime - end of time period (hh:mm)
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
# clean up previous data files
if  [ -d /tmp/$host/ps ] 
then
    rm -rf /tmp/$host/ps
fi
#
mkdir /tmp/$host/ps
#
cd /var/log/prat/ps/`date +%m$1%y`
#-----------------------------------------------------------------------------
# Loop thru the file names and copy them to tmp
#-----------------------------------------------------------------------------
st=`echo $starttime | awk -F":" '{ printf "%.4d\n", $1$2 }'`
sm=`echo $starttime | awk -F":" '{ print $2 }'`
et=`echo $endtime | awk -F":" '{ printf "%.4d\n", $1$2 }'`
k=$st
file=`echo $k | awk '{ printf "%.4d\n", $0 }'`
while [ $k -ge $st ] && [ $k -le $et ]; do
     if [ $sm -ge 60 ]; then
	k=`expr $k + 39`
	sm=`expr $sm - 61`
     elif [ $k -ge $st ] && [ $k -le $et ]; then
	cp ps_$file.txt /tmp/$host/ps
     fi
     k=`expr $k + 0`
     k=$((k + 1))
     file=`echo $k | awk '{ printf "%.4d\n", $0 }'`
     ((sm++))
done
#
# End of script
