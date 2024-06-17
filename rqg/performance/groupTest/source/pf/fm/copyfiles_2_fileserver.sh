#! /bin/sh
#
#/*******************************************************************************
#*  Script Name:    copyfiles.sh
#*  Date Created:   2009.02.04
#*  Author:         Stephen Cargile
#*  Purpose:        copy the data files to \\calweb\perf for archiving and further processing
#*
#*  Input Parameters:
#* 	            host - fqdn of host
#* 	            date - day of month
#*		    starttime
#*		    endtime
#*                  
#******************************************************************************/
#
# Get user input (command line parameters passed from Step2)
#
date=$1
starttime=$2
endtime=$3
host=$(hostname)
#
cd /tmp/$host/sar
#
smbclient //calweb/perf -Wcalpont -Uoamuser%Calpont1 -c "mkdir ${host};cd ${host};mkdir sar;cd sar;prompt OFF;mput sar_data_*.txt"
#
cd /tmp/$host/ps
smbclient //calweb/perf -Wcalpont -Uoamuser%Calpont1 -c "mkdir ${host};cd ${host};mkdir ps;cd ps;prompt OFF;mput ps_*.txt"
#
# End of script
