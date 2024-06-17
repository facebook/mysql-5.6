   if [ "$MYRBUILD" = "" ]; then
      echo "MYRBUILD" environment variable is not set.  Please set it to your target test build.
      echo Exiting.....
      exit
   fi
#
# In the setup.txt file, repBuildName can be used to specify the name of the replication stack to use
# reptest: MyRocks to InnoDB replication
# idbreptest: InnoDB to InnoDB replication
# User can add other replication configurations and reference them by name
# Instead setting a specify build name, the main script can specify "setup" and
# the actual will be set build name automatically
# By default, reptest is used
#       
   if [ "$MYRBUILD" = "setup" ]; then
      MYRBuildName=reptest
      repBuildName=`cat $MYRHOME/common/env/setup.txt|grep repBuildName|awk -F"=" '{print $2}'`
      if [ "$repBuildName" != "" ]; then
         MYRBuildName=$repBuildName
      fi
      export MYRBUILD=$MYRBuildName
   fi
# Set some default values
#
   MYRRunMode=1
   MYRMasterPort=3306
   MYRSlave1Port=3307
   MYRMasterSocket=/tmp/mysql.socks
   MYRSlave1Socket=/tmp/repSlave1.socks
   MYRClientDir=$MYRRELHOME/$MYRBUILD/mysql-5.6/client
# default wait time for replication to finish to forever
   MYRRepWaitTime=""
#
# Get settings from setup.txt file
   runMode=`cat $MYRHOME/common/env/setup.txt|grep myrRunMode|awk -F"=" '{print $2}'`
   masterPort=`cat $MYRHOME/common/env/setup.txt|grep -v "#"|grep myrMasterPort|awk -F"=" '{print $2}'`
   slave1Port=`cat $MYRHOME/common/env/setup.txt|grep -v "#"|grep myrSlave1Port|awk -F"=" '{print $2}'`
   masterSocket=`cat $MYRHOME/common/env/setup.txt|grep -v "#"|grep myrMasterSocket|awk -F"=" '{print $2}'`
   slave1Socket=`cat $MYRHOME/common/env/setup.txt|grep -v "#"|grep myrSlave1Socket|awk -F"=" '{print $2}'`
   repWaitTime=`cat $MYRHOME/common/env/setup.txt|grep -v "#"|grep myrRepWaitTime|awk -F"=" '{print $2}'`
   mysqlClientDir=`cat $MYRHOME/common/env/setup.txt|grep -v "#"|grep mysqlClientDir|awk -F"=" '{print $2}'`
#
# Use settings in setup.txt file if defined
#
   if [ "$runMode" != "" ]; then
      MYRRunMode=$runMode
   fi
#
   if [ "$masterPort" != "" ]; then
      MYRMasterPort=$masterPort
   fi
#
   if [ "$slave1Port" != "" ]; then
      MYRSlave1Port=$slave1Port
   fi
#
   if [ "$masterSocket" != "" ]; then
      MYRMasterSocket=$masterSocket
   fi
#
   if [ "$slave1Socket" != "" ]; then
      MYRSlave1Socket=$slave1Socket
   fi
#
   if [ $MYRRunMode = 2 ] && [ "$mysqlClientDir" != "" ]; then
      MYRClientDir=$mysqlClientDir
   fi
#
   if [ "$repWaitTime" != "" ]; then
      MYRRepWaitTime=$repWaitTime
   fi
#
# Export variables
#
   export MYRRUNMODE=$MYRRunMode
   export MYRMASTERPORT=$MYRMasterPort
   export MYRSLAVE1PORT=$MYRSlave1Port
   export MYRMASTERSOCKET=$MYRMasterSocket
   export MYRSLAVE1SOCKET=$MYRSlave1Socket
   export MYRCLIENTDIR=$MYRClientDir
   export MYRREPWAITTIME=$MYRRepWaitTime
#
   export MYRCLIENT="$MYRCLIENTDIR/mysql -uroot --port=$MYRMASTERPORT --socket=/tmp/mysql.sock"
   export MYRCMASTER="$MYRCLIENTDIR/mysql -uroot --port=$MYRMASTERPORT --socket=/tmp/mysql.sock"
   export MYRCSLAVE1="$MYRCLIENTDIR/mysql -uroot --port=$MYRSLAVE1PORT --socket=/tmp/repSlave1.sock"
