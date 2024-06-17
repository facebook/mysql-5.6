#! /bin/bash
#
# This script run a simple test on both test and reference databases
#
cd ~/mariadb-patches
#
perl runall-new.pl \
   --basedir1=$HOME/builds/testdbtst/mysql-5.6 \
   --basedir2=$HOME/builds/testdbref/mysql-5.6 \
   --vardir1=$HOME/builds/testdbtst/var \
   --vardir2=$HOME/builds/testdbref/var \
   --mysqld1=--default-storage-engine=rocksdb \
   --mysqld2=--default-storage-engine=InnoDB \
   --gendata=conf/examples/example.zz \
   --grammar=conf/examples/example.yy
#
# end of script
