# Helper to execute the dump table statement with various options.
# Expects t1 to exist, and $chunk_prefix set to the chunk prefix and optionally
# $dump_opts set to extra options.

# Enable logging of affected rows.
--enable_info
eval DUMP TABLE t1 INTO '$chunk_prefix' $dump_opts;
--disable_info
