#!/bin/bash
# Certain parts created by Roel Van de Paar, Percona LLC

# User settings
WORKDIR=/sda
RQG_DIR=/sda/randgen
BASEDIR=/sda/Percona-Server-5.6.19-rel67.0-619.Linux.x86_64-debug

# Internal settings
MTR_BT=$[$RANDOM % 300 + 1]

# If an option was given to the script, use it as part of the workdir name
if [ -z $1 ]; then
  WORKDIRSUB=$(echo $RANDOM$RANDOM$RANDOM | sed 's/..\(......\).*/\1/')
else
  WORKDIRSUB=$1
fi

# Check if random directory already exists & start run if not
if [ -d $WORKDIR/$WORKDIRSUB ]; then
  echo "Directory $WORKDIR/$WORKDIRSUB already exists. Just retry running the script which will use a different random number.";
else
  echo "$(date +'%d/%m/%y %H:%M') Trial $WORKDIRSUB: Working directory: $WORKDIR/$WORKDIRSUB | Log: $WORKDIR/$WORKDIRSUB/$WORKDIRSUB.log" 

  mkdir $WORKDIR/$WORKDIRSUB
  mkdir $WORKDIR/$WORKDIRSUB/1
  mkdir $WORKDIR/$WORKDIRSUB/2
  mkdir $WORKDIR/$WORKDIRSUB/tmp
  export TMP=$WORKDIR/$WORKDIRSUB/tmp
  export TMPDIR=$WORKDIR/$WORKDIRSUB/tmp

  # Special preparation: _epoch temporary directory setup
  mkdir $WORKDIR/$WORKDIRSUB/_epoch
  export EPOCH_DIR=$WORKDIR/$WORKDIRSUB/_epoch

  # jemalloc provisioning (reqd for TokuDB)
  if [ -r /usr/lib64/libjemalloc.so.1 ]; then
    export LD_PRELOAD=/usr/lib64/libjemalloc.so.1
  else 
    echo "Error: jemalloc not found, please install it first"
    exit 1
  fi 

  cd $RQG_DIR
  MTR_BUILD_THREAD=$MTR_BT; perl ./runall-new.pl \
  --rpl_mode=row \
  --threads=1 \
  --sqltrace \
  --duration=300 \
  --queries=99999999999 \
  --basedir1=$BASEDIR \
  --basedir2=$BASEDIR \
  --mysqld1=--plugin-load=tokudb=ha_tokudb.so \
  --mysqld2=--plugin-load=tokudb=ha_tokudb.so \
  --mysqld1=--init-file=$RQG_DIR/conf/percona_qa/5.6/TokuDB.sql \
  --mysqld2=--init-file=$RQG_DIR/conf/percona_qa/5.6/TokuDB.sql \
  --vardir1=$WORKDIR/$WORKDIRSUB/1 \
  --vardir2=$WORKDIR/$WORKDIRSUB/2 \
  --grammar=./conf/percona_qa/5.6/5.6.yy >$WORKDIR/$WORKDIRSUB/$WORKDIRSUB.log 2>&1
fi
