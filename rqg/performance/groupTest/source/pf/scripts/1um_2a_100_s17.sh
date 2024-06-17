#! /bin/sh
#
echo  
echo ------------------------------------------------------------------
echo BEGIN AUTOMATED TEST - 203.9 S17 100GB 1UM 2 Array 1 Pass
echo ------------------------------------------------------------------
echo executing setLowMem100 script to prep for following stream1_7 tests
echo ------------------------------------------------------------------
echo  
#
/usr/local/Calpont/bin/setLowMem100.sh
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 1PM 2Array stream1_7 100GB i16 1x w/restart 
echo ------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 1 1 2
/home/pf/auto/common/step2.sh 1_7 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 2PM 2Array stream1_7 100GB i16 1x w/restart 
echo ------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 1 2 2
/home/pf/auto/common/step2.sh 1_7 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 4PM 2Array stream1_7 100GB i16 1x w/restart 
echo ------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 1 4 2
/home/pf/auto/common/step2.sh 1_7 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo END OF AUTOMATED TEST - 203.9 S17 100GB 1UM 2 Array 1 Pass
echo ------------------------------------------------------------------
echo  
# End of script
