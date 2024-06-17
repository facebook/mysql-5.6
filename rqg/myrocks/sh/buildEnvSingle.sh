#! /bin/bash
#
# This script builds the RocksDB project from Github
# The commands are from the following link
# https://github.com/MySQLOnRocksDB/mysql-5.6/wiki/Build-steps
#
   buildName=$1
   if [ "$buildName" = "" ]; then
      echo Missing build name
      echo Usage: $0 nightly
      exit 
   fi
   testDir=$MYRRELHOME/$buildName
   echo Module: $0 TestDir=$testDir
#
# remove existing project directory and create one
   rm -rf $testDir
   mkdir -p $testDir
#
# get project from GitHub and compile it.
#
   cd $testDir
   mkdir log
   mkdir var
   echo Build log files are in $testDir/log
   echo `date` 1/5 git clone ...... 1.gitclone.log
   git clone https://github.com/facebook/mysql-5.6.git > ./log/1.gitclone.log 2>&1
   cd mysql-5.6
   echo `date` 2/5 git init ....... 2.gitinit.log
   git submodule init > ../log/2.gitinit.log 2>&1
   echo `date` 3/5 git update ..... 3.gitupdate.log
   git submodule update > ../log/3.gitupdate.log 2>&1
   echo `date` 4/5 cmake .......... 4.cmake.log
   cmake . -DCMAKE_BUILD_TYPE=Debug -DWITH_SSL:STRING=system -DWITH_ZLIB:STRING=system -DMYSQL_MAINTAINER_MODE=1 > ../log/4.cmake.log 2>&1
#   cmake . -DWITH_SSL:STRING=system -DWITH_ZLIB:STRING=system -DMYSQL_MAINTAINER_MODE=1 > ../log/4.cmake.log 2>&1
   echo `date` 5/5 make ........... 5.make.log
   make -j8 > ../log/5.make.log 2>&1
   echo `date` Done
#
   cd $testDir
   cp $MYRHOME/myrocks/cnf/$buildName.cnf $testDir/.
   cp -r $MYRHOME/myrocks/db/install.db .
   cd
   
# end of script

