#1 /bin/bash
#
   pidFile=$1
#
   pidIdx=0
   while read pid; do
      ((pidIdx++))
      pids[$pidIdx]=$pid
   done <$pidFile
   numSess=$pidIdx
# 
   stillGoing=1
   while [ $stillGoing -gt 0 ]; do
      stillGoing=0
      for (( sess=1; sess<=numSess; sess++ ))
      do
         if [ ${pids[sess]} -ne 0 ]
         then
            lines=`ps -p ${pids[sess]} |wc -l`
            if [ $lines -eq 1 ]
            then
               pids[$sess]=0
            else
               stillGoing=1
               break
            fi
         fi
      done
      if [ $stillGoing -eq 1 ]
      then
         sleep 1
      fi
   done
