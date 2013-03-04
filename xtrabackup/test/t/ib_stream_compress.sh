############################################################################
# Test streaming + compression
############################################################################

if ! which qpress > /dev/null 2>&1 ; then
  echo "Requires qpress to be installed" > $SKIPPED_REASON
  exit $SKIPPED_EXIT_CODE
fi

stream_format=xbstream
stream_extract_cmd="xbstream -xv <"
stream_uncompress_cmd="for i in *.qp;  do qpress -d \$i ./; done; \
for i in sakila/*.qp; do qpress -d \$i sakila/; done"
innobackupex_options="--compress --compress-threads=4"

. inc/ib_stream_common.sh
