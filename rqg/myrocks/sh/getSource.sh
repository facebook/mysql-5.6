#! /bin/bash
#
# This script downloads a copy of the MyRocks project from Github
# for archive purpose
#
   buildName=`date +%Y%m%d`
   testDir=$MYRRELHOME/.source/$buildName
   echo Module: $0 TestDir=$testDir
#
# remove existing project directory and create one
   rm -rf $testDir
   mkdir -p $testDir
#
# get project from GitHub and compile it.
#
   cd $testDir
   git clone https://github.com/facebook/mysql-5.6.git
   cd mysql-5.6
   git submodule init
   git submodule update
   cd
# end of script

