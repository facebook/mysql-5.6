#! /bin/sh
#
#/*******************************************************************************
#*  Script Name:    getlogs.sh
#*  Date Created:   2009.02.09
#*  Author:         Stephen Cargile
#*  Purpose:        copy Calpont log files to temp
#*
#*  Parameters:     date - day of month in question (dd)
#*                  starttime - start of sar period (hh:mm)
#*                  endtime - end of sar period (hh:mm)
#*                  
#******************************************************************************/
#
#-----------------------------------------------------------------------------
# command line parameters
#-----------------------------------------------------------------------------
#date=$1
#starttime=$2
#endtime=$3
#
host=$(hostname -s)
#
# clean up previous data files
if  [ -d /tmp/$host/logs ] 
then
    rm -rf /tmp/$host/logs
fi
#
mkdir /tmp/$host/logs
cp -r /var/log/Calpont/* /tmp/$host/logs
#
# End of script
