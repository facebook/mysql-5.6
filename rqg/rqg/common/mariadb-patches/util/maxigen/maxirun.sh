# Directory (user-adjustable) settings
if [ "$WORKDIR" == "" ]; then
  WORKDIR=/ssd
fi
if [ "$RQGDIR" == "" ]; then
  RQGDIR=$WORKDIR/randgen
fi

# Check if lib/GenTest/Random.pm is patched correctly
if [ $(grep "use strict" $RQGDIR/lib/GenTest/Random.pm | sed 's/\(.\).*/\1/') != "#" ]; then
  echo "lib/GenTest/Random.pm workaround patch not in place. Aborting."
  echo "--------------------------------------------------------------"
  echo "There is some limitation or shortcoming in RQG currently which makes it terminate many (if not all) maxigen trials with STATUS_ENVIRONMENT_FAILURE"
  echo -e "This can be easily avoided by making the following patch to $RQGDIR/lib/GenTest/Random.pm:\n"
  echo "=== modified file 'lib/GenTest/Random.pm'"
  echo "-use strict;"
  echo "+#use strict;"
  echo -e "\n If you like you can make the same patch to lib/GenTest/Validator/Transformer.pm, though this has not been established as necessary or helpful [yet]."
  echo -e "\n Please ensure this patch is in place (exactly as shown above; with '#' as the first character on the line) before re-starting maxigen."
  exit 1
fi

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
  echo "Directory already exists. Retry.";
else
  mkdir $WORKDIR/$WORKDIRSUB
  mkdir $WORKDIR/$WORKDIRSUB/tmp
  export TMP=$WORKDIR/$WORKDIRSUB/tmp

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

  # Make sure we keep a copy of the yy grammars
  mkdir $WORKDIR/$WORKDIRSUB/KEEP
  cp * $WORKDIR/$WORKDIRSUB/KEEP

  cd $RQGDIR
  MTR_BUILD_THREAD=$MTR_BT; perl ./combinations.pl \
  --clean \
  --force \
  --parallel=8 \
  --run-all-combinations-once \
  --workdir=$WORKDIR/$WORKDIRSUB \
  --config=COMBINATIONS
fi
