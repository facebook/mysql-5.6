#! /bin/sh
#
echo  
echo ------------------------------------------------------------------
echo BEGIN AUTOMATED TEST - 203.13 1UM 1PM 1Array 100GB S0 1 Pass
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
echo END OF AUTOMATED TEST - 203.13 1UM 1PM 1Array 100GB S0 1 Pass
echo ------------------------------------------------------------------
echo  
# End of script
