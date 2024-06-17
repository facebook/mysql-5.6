#!/bin/bash
echo "starting tpch100 stream0 test.........."
echo
sid=1
for q in 14 2 9 20 6 17 18 8 21 13 3 22 16 4 11 15 1 10 19 5 7 12; do
	qq=`printf %02d $q`
       /home/qa/srv/common/script/sendPlanExec.sh $sid 9 /home/qa/srv/tpchtest/sqlplan/tpch100/s0/hex/i16/tpch100_s0_${qq}.hex
	((sid++))
done
echo "tpch100 Stream0 test completed."

