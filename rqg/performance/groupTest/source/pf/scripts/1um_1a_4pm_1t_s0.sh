#! /bin/sh
#
echo  
echo ------------------------------------------------------------------
echo starting 1UM 4PM 1Array stream0 1TB i16 1x w/restart
echo ------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 1 4 1
/home/pf/auto/common/step2.sh 0 1t 16 1 Y
#
# End of script
