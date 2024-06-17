#! /bin/bash
#
   curDir=`pwd`
# change to the directory where the existing RQG is installed
# if directory of the test frame changes, the next variable may need to be modified
# to reflect the correct directory
#
# the rest of the script works on relative directory
   rqgHomeDir=$MYRHOME/rqg/common
   cd $rqgHomeDir
#   
# backkup the conf directory, where data and grammer files are stored, in the existing RQG installation
   rm -rf ./backup/conf
   cp -r ./mariadb-patches/conf ./backup
#
# Move existing RQG installation to a backup, along with all data and grammer files
   backupDir=`date +%Y%m%d_%H%M%S`
   mkdir -p ./backup/$backupDir
   mv mariadb-patches ./backup/$backupDir
#
# get latest RQG from the internet   
   bzr branch lp:~elenst/randgen/mariadb-patches
#
#  backup the new conf directory and restore the conf directory from the one just backed up
   mv ./mariadb-patches/conf ./mariadb-patches/conf.original
   cp -r ./backup/conf ./mariadb-patches
#
   cd $curDir
#