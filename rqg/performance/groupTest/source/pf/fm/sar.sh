#! /bin/sh
#
#/*******************************************************************************
#*  Script Name:    getsar.sh
#*  Date Created:   2009.02.04
#*  Author:         Stephen Cargile
#*  Purpose:        Build a sar command based on user input and create the data file
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
date=$1
starttime=$2
endtime=$3
#
host=$(hostname -s)
#
# clean up previous data files
if  [ -d /tmp/$host/sar ] 
then
    rm -rf /tmp/$host/sar
fi
#
mkdir /tmp/$host/sar
#
#------------------------------------------------------------------------------
# Create sar statements and extract data to text files
#
echo "LC_ALL=C sar -P ALL -s $2:00 -e $3:00 -f /var/log/sa/sa$1 > /tmp/$host/sar/cpu_$1_$host.txt" >> /tmp/$host/sar/sarcpu.sh
chmod 755 /tmp/$host/sar/sarcpu.sh
/tmp/$host/sar/sarcpu.sh
#
echo "LC_ALL=C sar -r -s $2:00 -e $3:00 -f /var/log/sa/sa$1 > /tmp/$host/sar/mem_$1_$host.txt" >> /tmp/$host/sar/sarmem.sh
chmod 755 /tmp/$host/sar/sarmem.sh
/tmp/$host/sar/sarmem.sh
#
echo "LC_ALL=C sar -n DEV -s $2:00 -e $3:00 -f /var/log/sa/sa$1 > /tmp/$host/sar/net_$1_$host.txt" >> /tmp/$host/sar/sarnet.sh
chmod 755 /tmp/$host/sar/sarnet.sh
/tmp/$host/sar/sarnet.sh
#
#------------------------------------------------------------------------------
# Copy files to file server
#
cd /tmp/$host/sar/
smbclient //calweb/perf -Wcalpont -Uoamuser%Calpont1 -c "cd ${host};prompt OFF;mput *_$1_$host.txt"
#
# End of Script
