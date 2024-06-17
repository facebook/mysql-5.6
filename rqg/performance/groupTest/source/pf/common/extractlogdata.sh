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
#*  	            streamNum - the type of stream (0 or 17)
#*                  
#******************************************************************************/
#
# Get user input
#
dbSize=$1
streamNum=$2
#
awk -F'[;-]' '/Query Stats/ {print $2,"\t", $4,"\t", $6,"\t" $8,"\t" $10,"\t" $12,"\t" $14,"\t" $16,"\t" $18,"\t" $20}' tpch${dbSize}_s${streamNum}.log | sed 's/MB//'  >> afile.txt

grep "time:" tpch${dbSize}_s${streamNum}.log | cut -d" " -f3 >> time.txt

# End of script
