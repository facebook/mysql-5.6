#! /bin/sh
#
#/*******************************************************************************
#*  Script Name:    setTestEnv.sh
#*  Date Created:   2008.11.12
#*  Author:         Daniel Lee
#*  Purpose:        Set test environment by setting stack configurations and dbroots.
#*		      The stack will be started.
#*
#*  Parameter:      numUMs - number of UMs to be enabled
#*		      numPMs - number of PMs to be enabled
#*                  setNum - 1 or 2, dbroot set number
#*                  
#******************************************************************************/
#
# Get number of UMs, PMs, and dbroot set number from user input (command line parameters)
#
numUMs=$1
numPMs=$2
setNum=$3
#
/home/pf/auto/common/setStackConfig.sh $numUMs $numPMs
/home/pf/auto/common/setDBRoots.sh $setNum
/usr/local/Calpont/bin/calpontConsole startsystem
sleep 90
#
# End of script
