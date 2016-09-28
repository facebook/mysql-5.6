set -e

# Remove file for safety
if [ -f $LOAD_DONE_INDICATOR_FILE ]; then
  rm $LOAD_DONE_INDICATOR_FILE
fi

# Execute the 10000 inserts and then create a file to indicate we are done
suite/xtrabackup/include/xb_load_data.sh 2>&1
echo "done" >$LOAD_DONE_INDICATOR_FILE
