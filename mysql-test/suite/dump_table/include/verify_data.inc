# Verify chunks produced by DUMP TABLE
# Expects:
# t1 (source table) to exist.

# Verify the row count.
SELECT COUNT(*) FROM t1load;

# Verify the checksums of both tables match.
let $expected_checksum = query_get_value(CHECKSUM TABLE t1, Checksum, 1);
let $loaded_checksum = query_get_value(CHECKSUM TABLE t1load, Checksum, 1);
assert($expected_checksum == $loaded_checksum);
