# Load and verify chunks produced by DUMP TABLE
# Expects:
# $num_chunks, $chunk_prefix to be defined,
# and t1 (source table) to exist.

source suite/dump_table/include/load_chunks.inc;

source suite/dump_table/include/verify_data.inc;

DROP TABLE t1load;
