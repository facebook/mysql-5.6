#! /bin/sh
#
echo  
echo ------------------------------------------------------------------
echo BEGIN AUTOMATED TEST - S0 100GB and 1TB 1UM 2 Array 3 Pass
echo ------------------------------------------------------------------
echo  
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 1PM 2Array stream0 100GB i16 3x w/restart 
echo ------------------------------------------------------------------
echo  
#
/home/pf/auto/common/step1.sh 1 1 2
/home/pf/auto/common/step2.sh 0 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 2PM 2Array stream0 100GB i16 3x w/restart 
echo ------------------------------------------------------------------
echo  
#
/home/pf/auto/common/step1.sh 1 2 2
/home/pf/auto/common/step2.sh 0 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 4PM 2Array stream0 100GB i16 3x w/restart
echo ------------------------------------------------------------------
echo  
#
/home/pf/auto/common/step1.sh 1 4 2
/home/pf/auto/common/step2.sh 0 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo END OF S0 100GB TESTS!
echo ------------------------------------------------------------------
echo  
echo ------------------------------------------------------------------
echo BEGIN AUTOMATED TEST - S0 1TB 1UM 2 Array 3 Pass
echo ------------------------------------------------------------------
echo 
#
echo 
echo ------------------------------------------------------------------
echo starting 1UM 1PM 2Array stream0 1TB i16 3x w/restart
echo ------------------------------------------------------------------
echo 
#
/home/pf/auto/common/step1.sh 1 1 2
/home/pf/auto/common/step2.sh 0 1t 16 1 Y
#
echo 
echo ------------------------------------------------------------------
echo starting 1UM 2PM 2Array stream0 1TB i16 3x w/restart
echo ------------------------------------------------------------------
echo 
#
/home/pf/auto/common/step1.sh 1 2 2
/home/pf/auto/common/step2.sh 0 1t 16 1 Y
#
echo 
echo ------------------------------------------------------------------
echo starting 1UM 4PM 2Array stream0 1TB i16 3x w/restart
echo ------------------------------------------------------------------
echo 
#
/home/pf/auto/common/step1.sh 1 4 2
/home/pf/auto/common/step2.sh 0 1t 16 1 Y
#
echo 
echo ------------------------------------------------------------------
echo END OF AUTOMATED TEST - S0 100GB and 1TB 1UM 2 Array 3 Pass
echo ------------------------------------------------------------------
echo
# End of script
