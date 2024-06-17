#! /bin/sh
#
echo
echo ------------------------------------------------------------------
echo START OF 12-12 WEEKEND AUTOMATED TEST - 203.13 S0 1T & 100GB
echo ------------------------------------------------------------------
echo
#
echo 
echo ------------------------------------------------------------------
echo starting 1UM 1PM 1Array stream0 100G i16 1x w/restart
echo -------------------------------------------------------------------
echo 
/home/pf/auto/common/step1.sh 1 1 1
/home/pf/auto/common/step2.sh 0 100 16 1 Y
#
echo 
echo ------------------------------------------------------------------
echo starting 1UM 2PM 1Array stream0 100G i16 1x w/restart
echo -------------------------------------------------------------------
echo 
/home/pf/auto/common/step1.sh 1 2 1
/home/pf/auto/common/step2.sh 0 100 16 1 Y
#
echo 
echo ------------------------------------------------------------------
echo starting 1UM 4PM 1Array stream0 100G i16 1x w/restart
echo ------------------------------------------------------------------
echo 
/home/pf/auto/common/step1.sh 1 4 1
/home/pf/auto/common/step2.sh 0 100 16 1 Y
#
echo 
echo ------------------------------------------------------------------
echo starting 1UM 1PM 2Array stream0 100G i16 1x w/restart
echo ------------------------------------------------------------------
echo 
/home/pf/auto/common/step1.sh 1 1 2
/home/pf/auto/common/step2.sh 0 100 16 1 Y
#
echo 
echo ------------------------------------------------------------------
echo starting 1UM 2PM 2Array stream0 100G i16 1x w/restart
echo ------------------------------------------------------------------
echo 
/home/pf/auto/common/step1.sh 1 2 2
/home/pf/auto/common/step2.sh 0 100 16 1 Y
#
echo 
echo ------------------------------------------------------------------
echo starting 1UM 4PM 2Array stream0 100 i16 1x w/restart
echo ------------------------------------------------------------------
echo 
/home/pf/auto/common/step1.sh 1 4 2
/home/pf/auto/common/step2.sh 0 100 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 1PM 1Array stream0 1TB i16 1x w/restart
echo -------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 1 1 1
/home/pf/auto/common/step2.sh 0 1t 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 2PM 1Array stream0 1TB i16 1x w/restart
echo -------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 1 2 1
/home/pf/auto/common/step2.sh 0 1t 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 4PM 1Array stream0 1TB i16 1x w/restart
echo ------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 1 4 1
/home/pf/auto/common/step2.sh 0 1t 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 1PM 2Array stream0 1TB i16 1x w/restart
echo ------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 1 1 2
/home/pf/auto/common/step2.sh 0 1t 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 2PM 2Array stream0 1TB i16 1x w/restart
echo ------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 1 2 2
/home/pf/auto/common/step2.sh 0 1t 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 4PM 2Array stream0 1TB i16 1x w/restart
echo ------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 1 4 2
/home/pf/auto/common/step2.sh 0 1t 16 1 Y
#
echo  
echo ------------------------------------------------------------------
echo END OF WEEKEND AUTOMATED TEST - 203.13 S0 1T & 100GB
echo ------------------------------------------------------------------
echo  
# End of script
