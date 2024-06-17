#! /bin/sh
#
echo  
echo ------------------------------------------------------------------
echo BEGIN AUTOMATED TEST - 203.7 1UM 1PM 2Arrays 1TB S0 2 Pass
echo ------------------------------------------------------------------
echo	
echo ------------------------------------------------------------------
echo starting 1UM 1PM 2Arrays stream0 1TB i16 2x w/restart 
echo ------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 1 1 2
/home/pf/auto/common/step2.sh 0 1t 16 2 Y
#
echo 
echo ------------------------------------------------------------------
echo END OF AUTOMATED TEST - 203.7 1UM 1PM 2Arrays 1TB S0 2 Pass
echo ------------------------------------------------------------------
echo  
# End of script
