# Helper to execute the dump table statement with various options.
# Expects t1 to exist, and $chunk_prefix set to the chunk prefix and optionally
# $dump_opts set to extra options.

# Enable logging of affected rows.
--enable_info
let $dump_tbl_query = DUMP TABLE t1 INTO '$chunk_prefix' $dump_opts;
# Capture $num_chunks so that load_chunks.inc knows how many chunks to
# read/expect.
# Need quotes around the query so that embedded commas don't get misinterpreted
# as extra args to query_get_value.
let $num_chunks = query_get_value("$dump_tbl_query", num_chunks, 1);
echo $dump_tbl_query;
echo Num chunks dumped: $num_chunks;
--disable_info
