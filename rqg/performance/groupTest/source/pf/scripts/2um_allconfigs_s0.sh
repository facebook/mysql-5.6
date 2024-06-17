#! /bin/sh
#
echo
echo ------------------------------------------------------------------
echo START OF AUTOMATED TEST - 203.13 2UMs S0 100GB and 1TB
echo ------------------------------------------------------------------
echo
#
echo 
echo ------------------------------------------------------------------
echo starting 2UM 1PM 1Array stream0 100G i16 2x w/restart
echo -------------------------------------------------------------------
echo 
/home/pf/auto/common/step1.sh 2 1 1
/home/pf/auto/common/step2.sh 0 100 16 2 Y
#
echo 
echo ------------------------------------------------------------------
echo starting 2UM 2PM 1Array stream0 100G i16 2x w/restart
echo -------------------------------------------------------------------
echo 
/home/pf/auto/common/step1.sh 2 2 1
/home/pf/auto/common/step2.sh 0 100 16 2 Y
#
echo 
echo ------------------------------------------------------------------
echo starting 2UM 1PM 2Array stream0 100G i16 2x w/restart
echo ------------------------------------------------------------------
echo 
/home/pf/auto/common/step1.sh 2 1 2
/home/pf/auto/common/step2.sh 0 100 16 2 Y
#
echo 
echo ------------------------------------------------------------------
echo starting 2UM 2PM 2Array stream0 100G i16 2x w/restart
echo ------------------------------------------------------------------
echo 
/home/pf/auto/common/step1.sh 2 2 2
/home/pf/auto/common/step2.sh 0 100 16 2 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 2UM 1PM 1Array stream0 1TB i16 2x w/restart
echo -------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 2 1 1
/home/pf/auto/common/step2.sh 0 1t 16 2 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 2UM 2PM 1Array stream0 1TB i16 2x w/restart
echo -------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 2 2 1
/home/pf/auto/common/step2.sh 0 1t 16 2 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 2UM 1PM 2Array stream0 1TB i16 2x w/restart
echo ------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 2 1 2
/home/pf/auto/common/step2.sh 0 1t 16 2 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 2UM 2PM 2Array stream0 1TB i16 2x w/restart
echo ------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 2 2 2
/home/pf/auto/common/step2.sh 0 1t 16 2 Y
#
echo ------------------------------------------------------------------
echo END OF AUTOMATED TEST - 203.13 2UMs S0 1T and 100GB
echo ------------------------------------------------------------------
echo  
# End of script
