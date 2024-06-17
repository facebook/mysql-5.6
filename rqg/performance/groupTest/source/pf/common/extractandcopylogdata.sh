#! /bin/sh
#
#/*******************************************************************************
#*  Script Name:    copyfiles.sh
#*  Date Created:   2008.11.24
#*  Author:         Stephen Cargile
#*  Purpose:        copy the test files to \\calweb\perf for archiving and furthter processing
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

awk -F'[;-]' '/Query Stats/ {print $2,"\t", $4,"\t", $6,"\t" $8,"\t" $10,"\t" $12,"\t" $14,"\t" $16,"\t" $18,"\t" $20}' tpch${dbSize}_s${streamNum}.log | sed 's/MB//'  >> afile.txt

grep "time:" tpch${dbSize}_s${streamNum}.log | cut -d" " -f3 >> time.txt

smbclient //calweb/perf -Wcalpont -Uoamuser%Calpont1 -c "mkdir 1${numUMs}${numPMs}_${setStg}array;cd 1${numUMs}${numPMs}_${setStg}array;mkdir rel${relNum}_${dbSize};cd rel${relNum}_${dbSize};mkdir test${testNo};cd test${testNo};mkdir s${streamNum};cd s${streamNum};mkdir run${dirNo};cd run${dirNo};mkdir exemgr;mkdir ep;cd ep;prompt OFF;mput *.png;cd ..;mput *.txt;mput *.log;mput *.xml"
#
# End of script
