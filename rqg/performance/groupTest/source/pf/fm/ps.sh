#! /bin/sh
#
#/*******************************************************************************
#*  Script Name:    ps.sh
#*  Date Created:   2009.01.27
#*  Author:         Stephen Cargile
#*  Purpose:        capture system activity every x seconds
#*
#******************************************************************************/
#
cd /usr/local/prat/kernel
    if [ ! -d /usr/local/prat/kernel/`date +%m%d%y` ]
    then 
	mkdir `date +%m%d%y`
    else
	cd `date +%m%d%y`
    	hostname > ps_`date +%R`.txt
	date >> ps_`date +%R`.txt
	/bin/ps -leaf >> ps_`date +%R`.txt   
    fi

    if [ -f /usr/bin/pstree ]
    then
	/usr/bin/pstree -G > pstree_`date +%R`.txt
    else
	echo "binary /usr/bin/pstree not installed." > pstree_`date +%R`.txt
    fi

# End of script
