#! /bin/sh
#
#/*******************************************************************************
#*  Script Name:    exeStreamTest.sh
#*  Date Created:   2008.11.13
#*  Author:         Daniel Lee
#*  Purpose:        Execute a TPCH stream test.
#*
#*  Parameter:      streamNum - Stream number (0, 1, etc)
#*		      dbSize    - Database size (1, 10, 100, 1t etc)
#*		      iteration - Software release iteration (15, 16, 17 etc)
#*		      repeatNum - Number of times to repeat the test
#*                  restart   - before each test (Y, N)
#*                  
#* Modified:	2008.12.09
#* Author:	Stephen Cargile
#* Purpose:	point output to new 'results' directory                  
#*                  
#* Modified:	2009.01.21
#* Author:	Stephen Cargile
#* Purpose:	add functionality to collect start and stop times                 
#*                  
#******************************************************************************/
#
#-----------------------------------------------------------------------------
# command line parameters
#-----------------------------------------------------------------------------
streamNum=$1
dbSize=$2
iteration=$3
repeatNum=$4
restart=$5
#-----------------------------------------------------------------------------
# set variables
#-----------------------------------------------------------------------------
testID=/home/pf/auto/results/tpch${dbSize}_s${streamNum}_i${iteration}_`date +%s`
exeCommand=/home/pf/auto/tpchtest/sqlplan/tpch$dbSize/s$streamNum/script/i$iteration/tpch${dbSize}_s${streamNum}.sh
#-----------------------------------------------------------------------------
# Make test directory and change to it
#-----------------------------------------------------------------------------
cd /home/pf/auto/results/
mkdir $testID
cd $testID
logFileName=tpch${dbSize}_s${streamNum}.log
#-----------------------------------------------------------------------------
# Loop N times to repeat the test
#-----------------------------------------------------------------------------
k=1
while [ $k -le $repeatNum ]
do
     if [ $restart == Y ] || [ $restart == y ]
     then
         /usr/local/Calpont/bin/calpontConsole restartsystem y
         sleep 90
     fi
     mkdir $k
     cd $k
     cp /usr/local/Calpont/etc/Calpont.xml .
     /usr/local/Calpont/bin/calpontConsole getCalpontSoftware >CalpontSoftware.txt
     ls -al /mnt/pm*/usr/local/Calpont/data* > dbRoots.txt
     /usr/local/Calpont/bin/calpontConsole getProcessStatus >stackConfigBefore.txt
     $exeCommand > $logFileName 2>&1
#
     if [ $streamNum == 1_7 ]
     then
         completed=0
         while [ $completed -lt 7 ]
         do
            sleep 5
            completed=`cat *scc.log |grep completed. | wc -l` 
         done
     fi
#
     /usr/local/Calpont/bin/calpontConsole getProcessStatus >stackConfigAfter.txt
     /home/pf/auto/common/extractstartstoptimes.sh
     cd ..
     ((k++))
done
cd ..
#
# End of script
