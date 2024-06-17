#! /bin/sh
#
#/*******************************************************************************
#*  Script Name:    gatherdata.sh
#*  Date Created:   2009.02.05
#*  Author:         Stephen Cargile
#*  Purpose:        gather up sar, sql calpont logs & ps files based on user input
#*
#*  Parameter:      date - day of month in question (dd)
#*                  starttime - start of period (hh:mm)
#*                  endtime - end of period (hh:mm)
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
if  [ -d /tmp/$host ] 
then
    rm -rf /tmp/$host
fi
#
mkdir /tmp/$host
#
# call sar script to get sar data
echo calling sar script to get sar data
/home/pf/auto/fm/getsar.sh $1 $2 $3
#
# call logs script to get Calpont logs
echo calling logs script to get Calpont logs
/home/pf/auto/fm/getlogs.sh
#
# call ps script to pull ps files
echo calling ps script to get ps files
/home/pf/auto/fm/getps.sh $1 $2 $3
#
# call sql script to pull sql data
echo calling mysql script to get mysql data
/home/pf/auto/fm/getsql.sh $1 $2 $3
#
# call copyfile script to copy files to calling server (DM)
echo copying files to server
#/home/pf/auto/fm/copyfiles.sh
#
# End of script
