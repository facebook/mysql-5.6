#! /bin/sh
#
#/*******************************************************************************
#*  Script Name:    setStackConfig.sh
#*  Date Created:   2008.11.12
#*  Author:         Daniel Lee
#*  Purpose:        Set stack configuration (ie: 2-4 for 2 UMs and 4 PMs)
#*
#*  Parameter:      numUMs - number of UMs to be enabled
#*		      numPMs - number of PMs to be enabled
#*                  
#******************************************************************************/
#
echo Start - Set stack configuration
#
# Stopping the Calpont software
/usr/local/Calpont/bin/calpontConsole stopsystem y ACK_YES
#
# set maximum number of UMs and PMs possible the stack
maxUMs=2
maxPMs=4
#-----------------------------------------------------------------------------
# Get number of UMs and PMs from user input (command line parameters)
#-----------------------------------------------------------------------------
numUMs=$1
numPMs=$2
#-----------------------------------------------------------------------------
#Enable all UMs
#-----------------------------------------------------------------------------
k=1
while [ $k -le $maxUMs ]; do
       /usr/local/Calpont/bin/calpontConsole enableModule um$k ACK_YES
       ((k++))
done
#-----------------------------------------------------------------------------
#Disable non-used UMs
#-----------------------------------------------------------------------------
k=$maxUMs
while [ $k -gt $numUMs ]; do
       /usr/local/Calpont/bin/calpontConsole disableModule um$k ACK_YES
       ((k--))
done
#-----------------------------------------------------------------------------
#Enable all PMs
#-----------------------------------------------------------------------------
k=1
while [ $k -le $maxPMs ]; do
       /usr/local/Calpont/bin/calpontConsole enableModule pm$k ACK_YES
       ((k++))
done
#-----------------------------------------------------------------------------
#Disable non-used PMs
#-----------------------------------------------------------------------------
k=$maxPMs
while [ $k -gt $numPMs ]; do
       /usr/local/Calpont/bin/calpontConsole disableModule pm$k ACK_YES
       ((k--))
done
#-----------------------------------------------------------------------------
echo *-*-*-*-*-*-*-*-*-*-*-*-*-*-*
echo End - Set stack configuration
echo *-*-*-*-*-*-*-*-*-*-*-*-*-*-*
#
# End of script
