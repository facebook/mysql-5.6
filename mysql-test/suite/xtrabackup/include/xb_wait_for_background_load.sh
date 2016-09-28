set -e

# Wait for file indicating the background load is finished
while ! [ -f $LOAD_DONE_INDICATOR_FILE ];
do
  sleep 1
done
# Remove the file now that we are done with it
rm $LOAD_DONE_INDICATOR_FILE
