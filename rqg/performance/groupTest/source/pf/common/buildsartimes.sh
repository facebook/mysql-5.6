#! /bin/bash
#
#
# clean up any leftover work files
#
rm -rf stoptimes.txt
rm -rf starttimes.txt
rm -rf sarstep.sh
rm -rf sartest.sh
rm -rf testtimes.txt
rm -rf temptimes.txt
#
# pull start and stop times for each individual test and plug them into the sar statement
#
mkdir sar
k=1
cat startstoptimes.txt |
    while read a b c d e f g h; do
         starttime="$a $b $c $d"
         stoptime="$e $f $g $h"
	echo "LC_ALL=C sar -A -s $d -e $h -f /var/log/sa/sa$c > sar/q$k.txt" >> sarstep.sh	
	((k++))
    done
chmod 755 sarstep.sh
#
# Grab the start time from the first line and save it
#
head -1 startstoptimes.txt |
    while read a b c d e f g h; do
        teststart="$a $b $c $d"
echo $c $d >> testtimes.txt
	done
#
# Grab the end time from the last line and save it
#
tail -1 startstoptimes.txt |
    while read a b c d e f g h; do
        teststop="$e $f $g $h"
echo $h >> testtimes.txt
        done
#
# put start stop times on one line and then pull the times and plug them into the sar statement
#
cat testtimes.txt | sed '$!N;s/\n/ /' > temptimes.txt
t=1
cat temptimes.txt |
    while read i j k; do
       test="$i $j $k"
echo "LC_ALL=C sar -A -s $j -e $k -f /var/log/sa/sa$i > sar/t$t.txt" >> sartest.sh
	done
chmod 755 sartest.sh
#
# Clean up all temp files
#
rm -rf stoptimes.txt
rm -rf starttimes.txt
rm -rf testtimes.txt
rm -rf temptimes.txt
