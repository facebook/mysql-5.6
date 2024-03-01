# Load some dummy data into t1.
# Option:
# $rows - Number of rows to INSERT. Defaults to 1024.
# $repeat - use REPEAT on string columns to inflate the row sizes.
#   Default is 1 for a single repetition.

--disable_query_log

if (!$rows)
{
    let $rows = 1024;
}

if (!$repeat)
{
    let $repeat = 1;
}

# Default is 1000, which will only allow 1000 rows to be generated with the
# current query. Set to a bigger value.
set @@cte_max_recursion_depth = 10000;

# Use recursive CTE to generate a sequence of numbers from 1..$rows
# Then use that CTE to produce the real rows by SELECTing from it.
eval INSERT INTO t1
WITH RECURSIVE cte(i) AS
    (SELECT 1 union all select i+1 from cte where i < $rows)
SELECT NULL, 1, 2, REPEAT('aaa',$repeat), REPEAT('bbb',$repeat) from cte;

set @@cte_max_recursion_depth = default;

--enable_query_log
