#! /bin/sh
#
echo  
echo -----------------------------------------------------------------------------------------------
echo BEGIN AUTOMATED TEST - 1 2 4 PMs S0 1TB 2 Arrays 1 Pass
echo -----------------------------------------------------------------------------------------------
echo  
#
echo  
echo ------------------------------------------------------------------
echo BEGINNING 1 2 and 4 PM  2 Array  S0  1TB TESTS!
echo ------------------------------------------------------------------
echo 
echo ------------------------------------------------------------------
echo starting 1PM 2Array stream0 1TB i16 1x w/restart
echo ------------------------------------------------------------------
echo 
#
/home/pf/auto/common/setTestEnv.sh 1 1 2
/home/pf/auto/common/exeStreamTest.sh 0 1t 17 1 Y
/home/pf/auto/common/extractlogdata.sh 1t 0
#
echo 
echo ------------------------------------------------------------------
echo starting 2PM 2Array stream0 1TB i16 1x w/restart
echo ------------------------------------------------------------------
echo 
#
/home/pf/auto/common/setTestEnv.sh 1 2 2
/home/pf/auto/common/exeStreamTest.sh 0 1t 17 1 Y
/home/pf/auto/common/extractlogdata.sh 1t 0
#
echo 
echo ------------------------------------------------------------------
echo starting 4PM 2Array stream0 1TB i16 1x w/restart
echo ------------------------------------------------------------------
echo 
#
/home/pf/auto/common/setTestEnv.sh 1 4 2
/home/pf/auto/common/exeStreamTest.sh 0 1t 17 1 Y
/home/pf/auto/common/extractlogdata.sh 1t 0
#
echo 
echo ------------------------------------------------------------------
echo END OF 1 2 and 4 PM  2 Array  S0  1TB TESTS!
echo ------------------------------------------------------------------
echo 
echo  
echo ------------------------------------------------------------------------------------------------
echo END OF AUTOMATED TEST - 1 2 and 4 PMs S0 1TB 2 Arrays 1 Pass
echo ------------------------------------------------------------------------------------------------
echo  
# End of script
