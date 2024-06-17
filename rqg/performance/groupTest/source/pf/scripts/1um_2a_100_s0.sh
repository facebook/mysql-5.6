#! /bin/sh
#
echo  
echo ------------------------------------------------------------------
echo BEGIN AUTOMATED TEST - 207 S0 100GB 1UM 2 Array 1 Pass
echo ------------------------------------------------------------------
echo  
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 1PM 2Array stream0 100GB i16 1x w/restart 
echo ------------------------------------------------------------------
echo  
#
/home/pf/auto/common/step1.sh 1 1 2
/home/pf/auto/common/step2.sh 0 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 2PM 2Array stream0 100GB i16 1x w/restart 
echo ------------------------------------------------------------------
echo  
#
/home/pf/auto/common/step1.sh 1 2 2
/home/pf/auto/common/step2.sh 0 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 4PM 2Array stream0 100GB i16 1x w/restart
echo ------------------------------------------------------------------
echo  
#
/home/pf/auto/common/step1.sh 1 4 2
/home/pf/auto/common/step2.sh 0 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo END OF AUTOMATED TEST - 207 S0 100GB 1UM 2 Array 1 Pass
echo ------------------------------------------------------------------
echo  
# End of script
