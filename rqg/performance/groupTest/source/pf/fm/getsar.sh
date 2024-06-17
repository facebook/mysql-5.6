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
host1=$(hostname -s)
host2=srvqaperf3
host3=srvqaperf4
host4=srvqaperf5
host5=srvqaperf8
#
# clean up previous data files
if  [ -d /tmp/$host1/sar ] 
then
    rm -rf /tmp/$host1/sar
fi
#
mkdir /tmp/$host1/sar
#
#------------------------------------------------------------------------------
# Create sar statements and extract data to text files
#
echo "LC_ALL=C sar -P ALL -s $2:00 -e $3:00 -f /var/log/sa/sa$1 > /tmp/$host1/sar/cpu_$1_$host1.txt" >> /tmp/$host1/sar/sarcpu.sh
chmod 755 /tmp/$host1/sar/sarcpu.sh
/tmp/$host1/sar/sarcpu.sh
#
echo "LC_ALL=C sar -r -s $2:00 -e $3:00 -f /var/log/sa/sa$1 > /tmp/$host1/sar/mem_$1_$host1.txt" >> /tmp/$host1/sar/sarmem.sh
chmod 755 /tmp/$host1/sar/sarmem.sh
/tmp/$host1/sar/sarmem.sh
#
echo "LC_ALL=C sar -n DEV -s $2:00 -e $3:00 -f /var/log/sa/sa$1 > /tmp/$host1/sar/net_$1_$host1.txt" >> /tmp/$host1/sar/sarnet.sh
chmod 755 /tmp/$host1/sar/sarnet.sh
/tmp/$host1/sar/sarnet.sh
#
#------------------------------------------------------------------------------
# Copy files to file server
#
cd /tmp/$host1/sar/
smbclient //calweb/perf -Wcalpont -Uoamuser%Calpont1 -c "cd ${host1};prompt OFF;mput *_$1_$host1.txt"
#
#------------------------------------------------------------------------------
# Execute the script on the other servers in the stack
#
/usr/local/Calpont/bin/remote_command.sh $host2 qalpont! "/home/pf/auto/fm/sar.sh $1 $2 $3" 1
/usr/local/Calpont/bin/remote_command.sh $host3 qalpont! "/home/pf/auto/fm/sar.sh $1 $2 $3" 1
/usr/local/Calpont/bin/remote_command.sh $host4 qalpont! "/home/pf/auto/fm/sar.sh $1 $2 $3" 1
/usr/local/Calpont/bin/remote_command.sh $host5 qalpont! "/home/pf/auto/fm/sar.sh $1 $2 $3" 1
#
# End of Script
