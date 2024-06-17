#! /bin/sh
#
echo  
echo ------------------------------------------------------------------
echo BEGIN AUTOMATED TEST - S0 100GB 115 2 Array 1 Pass
echo ------------------------------------------------------------------
echo  
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 5PM 2Array stream0 100GB i16 1x w/restart
echo ------------------------------------------------------------------
echo  
#
/home/pf/auto/common/step1.sh 1 5 2
/home/pf/auto/common/step2.sh 0 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo END OF AUTOMATED TEST - S0 100GB 115 2 Array 1 Pass
echo ------------------------------------------------------------------
echo  
# End of script
