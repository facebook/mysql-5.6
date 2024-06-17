#! /bin/sh
#
echo  
echo ------------------------------------------------------------------
echo BEGIN AUTOMATED TEST - 203.7 S0 100GB 1UM 1 Array 1 Pass
echo ------------------------------------------------------------------
echo	
echo ------------------------------------------------------------------
echo starting 1UM 1PM 1Array stream0 100GB i16 1x w/restart 
echo ------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 1 1 1
/home/pf/auto/common/step2.sh 0 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 2PM 1Array stream0 100GB i16 1x w/restart 
echo ------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 1 2 1
/home/pf/auto/common/step2.sh 0 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 4PM 1Array stream0 100GB i16 1x w/restart 
echo ------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 1 4 1
/home/pf/auto/common/step2.sh 0 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo END OF AUTOMATED TEST - 203.11 S0 100GB 1UM 1 Array 1 Pass
echo ------------------------------------------------------------------
echo  
# End of script
