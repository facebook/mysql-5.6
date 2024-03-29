# Load chunks produced by DUMP TABLE.
# Retains `t1load` table for verification.
# Expects:
# - $num_chunks, $chunk_prefix to be defined,
# - t1 (source table) to exist.

let $dumpdir=`SELECT CONCAT(@@datadir, database())`;
let $_select_cols = $select_list;
if (!$select_list)
{
    let $_select_cols = *;
}

# Create the load table with the same schema as t1 (or a subset)
eval CREATE TABLE t1load AS SELECT $_select_cols FROM t1 LIMIT 0;

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
