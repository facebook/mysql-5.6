#! /bin/sh
#
#/*******************************************************************************
#*  Script Name:    move2win.sh
#*  Date Created:   2008.12.08
#*  Author:         Stephen Cargile
#*  Purpose:        copy the sar files to \\ws_scargile_tx\perf for furthter processing
#*
#*  Input Parameters:
#* 	            numUMs - number of UMs used in the test
#*                  numPMs - number of PMs used in the test
#*                  setStg - 1 or 2: number of arrays used
#*  		    relNum - Release number
#*  	            dbSize - Size of database
#*  	            testNo - the number that this config has been tested
#*  	            streamNum - the type of stream (0 or 17)
#*  	            dirNo - the number of the current directory
#*                  
#******************************************************************************/
#
# Get user input (command line parameters passed from Step2)
#
numUMs=$1
numPMs=$2
setStg=$3
relNum=$4
dbSize=$5
testNo=$6
streamNum=$7
dirNo=$8

smbclient //ws_scargile_tx/perf -Wcalpont -Uoamuser%Calpont1 -c "mkdir 1${numUMs}${numPMs}_${setStg}array"
smbclient //ws_scargile_tx/perf -Wcalpont -Uoamuser%Calpont1 -c "cd 1${numUMs}${numPMs}_${setStg}array"
smbclient //ws_scargile_tx/perf -Wcalpont -Uoamuser%Calpont1 -c "cd 1${numUMs}${numPMs}_${setStg}array;mkdir rel${relNum}_${dbSize}"
smbclient //ws_scargile_tx/perf -Wcalpont -Uoamuser%Calpont1 -c "cd 1${numUMs}${numPMs}_${setStg}array\rel${relNum}_${dbSize};mkdir test${testNo}"
smbclient //ws_scargile_tx/perf -Wcalpont -Uoamuser%Calpont1 -c "cd 1${numUMs}${numPMs}_${setStg}array\rel${relNum}_${dbSize}\test${testNo};mkdir s${streamNum}"
smbclient //ws_scargile_tx/perf -Wcalpont -Uoamuser%Calpont1 -c "cd 1${numUMs}${numPMs}_${setStg}array\rel${relNum}_${dbSize}\test${testNo}\s${streamNum};mkdir run${dirNo}"
smbclient //ws_scargile_tx/perf -Wcalpont -Uoamuser%Calpont1 -c "cd 1${numUMs}${numPMs}_${setStg}array\rel${relNum}_${dbSize}\test${testNo}\s${streamNum}\run${dirNo};mkdir sar"
smbclient //ws_scargile_tx/perf -Wcalpont -Uoamuser%Calpont1 -c "cd 1${numUMs}${numPMs}_${setStg}array\rel${relNum}_${dbSize}\test${testNo}\s${streamNum}\run${dirNo}\sar;prompt OFF;mput *.txt"
#
# End of script
