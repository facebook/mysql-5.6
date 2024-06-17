#!/bin/bash

echo "Quick reducer status output (number of lines processed by each mysqld versus lines to be processed etc.)"
echo "For initial simplification during the verify stage ([V]), note that there are 6 verify stages (each time with less simplification),"
echo "and each time all queries are processed. So you may think about this as Verify stage 1 x (as reported here) out of y (as reported"
echo "by reducer.sh) queries, and this 6 times, if necessary."

# This can be improved by checking if verify [V] stage is past (if so, there is no need for this reporting nor message/info above
echo -e "\n=== Verify stages progress per mysqld, only relevant for initial simplification during the verify stage ([V])"
ps -ef | grep mysqld | grep subreducer | \
  sed 's/.*--datadir=/sort -r /;s/data .*/reducer.log | grep -m1 "Verify attempt #" | sed "s\/.*Verify\/Verify\/;s\/:.*\/\/"/' > /tmp/_qstat1.sh
chmod +x /tmp/_qstat1.sh
/tmp/_qstat1.sh
rm -f /tmp/_qstat1.sh

INPUT_LINES=$(ps -ef | grep mysqld | grep subreducer | \
  sed 's/.*--datadir=//;s/data .*/reducer.log/' | xargs grep "Number of lines in input file" | sed 's/.*: //' | head -n1)

echo -e "\n=== Queries processed per mysqld"
ps -ef | grep mysqld | grep subreducer | \
  sed "s/.*:[0-9][0-9] //;s/d .*--socket/ --socket/;s/\.sock .*/.sock -uroot -e\"show global status like 'Queries'\" | grep Queries/" > /tmp/_qstat1.sh
chmod +x /tmp/_qstat1.sh
if [ "$INPUT_LINES" == "" ]; then  # Error protection
  /tmp/_qstat1.sh
else
  /tmp/_qstat1.sh | sed "s|$|/$INPUT_LINES|"
fi
rm -f /tmp/_qstat1.sh

echo -e "\n=== Client version strings for easy access"
ps -ef | grep mysqld | grep subreducer | \
  sed "s/.*:[0-9][0-9] //;s/d .*--socket/ --socket/;s/\.sock .*/.sock -uroot/" 

echo -e "\n=== # Running mysqld sessions (can be used to quickly gauge if at least some subreducers terminated early - i.e. a crash/bug was seen)"
ps -ef | grep mysqld | grep subreducer | wc -l

echo -e "\n=== Sessions still active (can be used to compare against reducer.sh's 'Finished/Terminated verification subreducer threads')"
ps -ef | grep mysqld | grep subreducer | sed 's|.*subreducer/\([0-9]*\).*|\1|' | sort -n | sed 's|^|[|;s|$|]|' | tr '\n' ' ' | sed 's|$|\n|'

echo -e "\n=== Threads still active"
ps -ef | grep mysqld | grep subreducer | awk '{print $2}' | sed 's|^|[|;s|$|]|' | tr '\n' ' ' | sed 's|$|\n|'

echo ""
