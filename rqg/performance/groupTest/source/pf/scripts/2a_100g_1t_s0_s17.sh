#! /bin/sh
#
echo  
echo -----------------------------------------------------------------------------------------------
echo BEGIN AUTOMATED TEST - 1 2 and 4 PMs S0 100GB and 1TB and S1_7 100GB with 2 Arrays 1 Pass
echo -----------------------------------------------------------------------------------------------
echo  
#
echo  
echo ------------------------------------------------------------------
echo BEGINNING OF 1 2 and 4 PM  2 Array  S0  100GB TESTS!
echo ------------------------------------------------------------------
echo  
echo ------------------------------------------------------------------
echo starting 1PM 2Array stream0 100GB i16 1x w/restart 
echo ------------------------------------------------------------------
echo  
#
/home/pf/auto/common/setTestEnv.sh 1 1 2
/home/pf/auto/common/exeStreamTest.sh 0 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 2PM 2Array stream0 100GB i16 1x w/restart 
echo ------------------------------------------------------------------
echo  
#
/home/pf/auto/common/setTestEnv.sh 1 2 2
/home/pf/auto/common/exeStreamTest.sh 0 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 4PM 2Array stream0 100GB i16 1x w/restart 
echo ------------------------------------------------------------------
echo  
#
/home/pf/auto/common/setTestEnv.sh 1 4 2
/home/pf/auto/common/exeStreamTest.sh 0 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo END OF 1 2 and 4 PM  2 Array  S0  100GB TESTS!
echo ------------------------------------------------------------------
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
/home/pf/auto/common/exeStreamTest.sh 0 1t 16 1 Y
#
echo 
echo ------------------------------------------------------------------
echo starting 2PM 2Array stream0 1TB i16 1x w/restart
echo ------------------------------------------------------------------
echo 
#
/home/pf/auto/common/setTestEnv.sh 1 2 2
/home/pf/auto/common/exeStreamTest.sh 0 1t 16 1 Y
#
echo 
echo ------------------------------------------------------------------
echo starting 4PM 2Array stream0 1TB i16 1x w/restart
echo ------------------------------------------------------------------
echo 
#
/home/pf/auto/common/setTestEnv.sh 1 4 2
/home/pf/auto/common/exeStreamTest.sh 0 1t 16 1 Y
#
echo 
echo ------------------------------------------------------------------
echo END OF 1 2 and 4 PM  2 Array  S0  1TB TESTS!
echo ------------------------------------------------------------------
echo 
echo ------------------------------------------------------------------
echo executing setLowMem100 script to prep for following stream1_7 tests
echo ------------------------------------------------------------------
echo  
#
/usr/local/Calpont/bin/setLowMem100.sh
#
echo  
echo ------------------------------------------------------------------
echo BEGINNING OF 1 2 and 4 PM  2 Array  S1_7  100GB TESTS!
echo ------------------------------------------------------------------
echo  
echo ------------------------------------------------------------------
echo starting 1PM 2Array stream1_7 100GB i16 1x w/restart 
echo ------------------------------------------------------------------
echo  
#
/home/pf/auto/common/setTestEnv.sh 1 1 2
/home/pf/auto/common/exeStreamTest.sh 1_7 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 2PM 2Array stream1_7 100GB i16 1x w/restart 
echo ------------------------------------------------------------------
echo  
#
/home/pf/auto/common/setTestEnv.sh 1 2 2
/home/pf/auto/common/exeStreamTest.sh 1_7 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 4PM 2Array stream1_7 100GB i16 1x w/restart 
echo ------------------------------------------------------------------
echo  
#
/home/pf/auto/common/setTestEnv.sh 1 4 2
/home/pf/auto/common/exeStreamTest.sh 1_7 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo END OF 1 2 and 4 PM  2 Array  S1_7  100GB TESTS!
echo ------------------------------------------------------------------
echo  
echo ------------------------------------------------------------------------------------------------
echo END OF AUTOMATED TEST - 1 2 and 4 PMs S0 100GB and 1TB and S1_7 100GB with 2 Arrays 1 Pass
echo ------------------------------------------------------------------------------------------------
echo  
# End of script
