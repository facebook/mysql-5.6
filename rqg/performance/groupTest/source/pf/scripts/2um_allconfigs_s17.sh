#! /bin/sh
#
echo  
echo ------------------------------------------------------------------
echo  
echo START OF TEST - 203.13 S17 100GB 2UMs
echo  
echo ------------------------------------------------------------------
echo  
echo  
echo ------------------------------------------------------------------
echo executing setLowMem100 script to prep for following stream1_7 tests
echo ------------------------------------------------------------------
echo  
/usr/local/Calpont/bin/setLowMem100.sh
#
echo  
echo ------------------------------------------------------------------
echo starting 2UM 1PM 1Array stream1_7 100GB i16 2x w/restart 
echo ------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 2 1 1
/home/pf/auto/common/step2.sh 1_7 100 16 2 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 2UM 2PM 1Array stream1_7 100GB i16 2x w/restart 
echo ------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 2 2 1
/home/pf/auto/common/step2.sh 1_7 100 16 2 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 2UM 1PM 2Array stream1_7 100GB i16 2x w/restart 
echo ------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 2 1 2
/home/pf/auto/common/step2.sh 1_7 100 16 2 Y
#
echo  
echo ------------------------------------------------------------------
echo starting 2UM 2PM 2Array stream1_7 100GB i16 2x w/restart 
echo ------------------------------------------------------------------
echo  
/home/pf/auto/common/step1.sh 2 2 2
/home/pf/auto/common/step2.sh 1_7 100 16 2 Y
#
echo  
echo ------------------------------------------------------------------
echo END OF TEST - 203.13 S17 100GB 2UMs
echo ------------------------------------------------------------------
echo  
# End of script
