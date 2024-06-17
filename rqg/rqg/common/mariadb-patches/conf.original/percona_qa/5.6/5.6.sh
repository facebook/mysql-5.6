# User settings
WORKDIR=/ssd/qa
RQG_DIR=/ssd/qa/randgen

# Internal settings
MTR_BT=$[$RANDOM % 300 + 1]

# If an option was given to the script, use it as part of the workdir name
if [ -z $1 ]; then
  WORKDIRSUB=$(echo $RANDOM$RANDOM$RANDOM | sed 's/..\(......\).*/\1/')
else
  WORKDIRSUB=$1
fi

# Second option will allow us to set timeout for RQG run. This will kill RQG run explicitly by kill command after x number of minutes.
if [ -n $2 ]; then
  TIME_OUT=$2
  rqg_time_out(){
    sleep $((TIME_OUT * 60));
    ps -ef | grep "${WORKDIRSUB}" | grep -v grep | awk '{print $2}' | xargs kill -9 2>/dev/null;
  }
fi

rqg_time_out &

# Check if random directory already exists & start run if not
if [ -d $WORKDIR/$WORKDIRSUB ]; then
  echo "Directory already exists. Retry.";
else
  mkdir $WORKDIR/$WORKDIRSUB
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
  MTR_BUILD_THREAD=$MTR_BT; perl ./combinations.pl \
  --clean \
  --force \
  --parallel=8 \
  --run-all-combinations-once \
  --workdir=$WORKDIR/$WORKDIRSUB \
  --config=$RQG_DIR/conf/percona_qa/5.6/5.6.cc
fi
