#! /bin/bash
#
# clean up previous run data
#
rm -rf stoptimes.txt
rm -rf starttimes.txt
rm -rf sarstep.txt
rm -rf sartest.txt
rm -rf testtimes.txt
rm -rf temptimes.txt
#
# Search for the line with date and time, pull it out, delete every third line (the dot file copy time)
#  and then make the start and stop times to be side-by-side on one line and save the output
#
egrep -w '2008|2009|2010' tpch1*_s*.log | cut -d" " -f1,2,3,4 | sed 'n;n;d;' | sed '$!N;s/\n/ /' > startstoptimes.txt
#
# pull just the start and stop times out of each line and save them to individual files
#
#cat startstoptimes.txt | awk -F" " '{print $4}' > starttimes.txt
#cat startstoptimes.txt | awk -F" " '{print $8}' > stoptimes.txt
