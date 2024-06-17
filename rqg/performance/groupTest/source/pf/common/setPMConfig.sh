#! /bin/sh
#
#/*******************************************************************************
#*  Script Name:    setPMConfig.sh
#*  Date Created:   2009.03.05
#*  Author:      	  Stephen Cargile
#*  Purpose:        Set PM configuration (ie: 1-4 PMs)
#*
#*  Parameter:      numPMs - number of PMs to be enabled 
#*                  
#******************************************************************************/
#
echo Start - Set PM configuration
#
# Stopping the Calpont software
/usr/local/Calpont/bin/calpontConsole stopsystem y ACK_YES
#
# set maximum number of PMs possible for the stack
maxPMs=4
#-----------------------------------------------------------------------------
# Get number of PMs from user input (command line parameters)
#-----------------------------------------------------------------------------
numPMs=$1
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
# Starting the Calpont software
/usr/local/Calpont/bin/calpontConsole startsystem y ACK_YES
sleep 60
echo *-*-*-*-*-*-*-*-*-*-*-*-*-*-*
echo End - Set PM configuration
echo *-*-*-*-*-*-*-*-*-*-*-*-*-*-*
#
# End of script
