# Load an verify chunks produced by DUMP TABLE
# Expects:
# $num_chunks, $chunk_prefix to be defined,
# and t1 (source table) to exist.

let $dumpdir=`SELECT CONCAT(@@datadir, database())`;

# Create the load table with the same schema as t1.
CREATE TABLE t1load AS SELECT * FROM t1 LIMIT 0;

--disable_query_log
let $i = 0;
while ($i < $num_chunks)
{
    eval LOAD DATA INFILE '$dumpdir/$chunk_prefix.$i' INTO TABLE t1load;
    inc $i;
}

# Remove the files created by DUMP TABLE.
remove_files_wildcard $dumpdir $chunk_prefix.*;
--enable_query_log

# Verify the row count.
SELECT COUNT(*) FROM t1load;

# Verify the checksums of both tables match.
let $expected_checksum = query_get_value(CHECKSUM TABLE t1, Checksum, 1);
let $loaded_checksum = query_get_value(CHECKSUM TABLE t1load, Checksum, 1);
assert($expected_checksum == $loaded_checksum);

DROP TABLE t1load;
