############################################################################
# Test parallel streaming feature of the 'xbstream' format
############################################################################

stream_format=xbstream
stream_extract_cmd="xbstream -xv <"
innobackupex_options="--parallel=16"

. inc/ib_stream_common.sh
