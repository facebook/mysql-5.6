#! /bin/sh
#
#/*******************************************************************************
#*  Script Name:    extractlogdata.sh
#*  Date Created:   2008.12.02
#*  Author:         Stephen Cargile
#*  Purpose:        extract stats data plus time info
#*
#*  Input Parameters:
#*  	            dbSize - Size of database
#*                  
#******************************************************************************/
#
# Get user input
#
dbSize=$1
#
k=1
while [ $k -le 7 ]; do
awk -F'[;-]' '/Query Stats/ {print $2,"\t", $4,"\t", $6,"\t" $8,"\t" $10,"\t" $12,"\t" $14,"\t" $16,"\t" $18,"\t" $20}' tpch${dbSize}_s${k}_scc.log | sed 's/MB//'  >> afile_s${k}.txt
((k++))
done

t=1
while [ $t -le 7 ]; do
grep "time:" tpch${dbSize}_s${t}_scc.log | cut -d" " -f3 >> time_s${t}.txt
((t++))
done

# End of script
