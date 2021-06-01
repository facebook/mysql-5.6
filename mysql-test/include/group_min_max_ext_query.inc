# This query uses loose index scan.
let $hint = ;
let $orig_query = $query $hint $cond;
let $check_query = `select replace('$orig_query', 'SELECT', 'SELECT /*+ GROUP_BY_LIS(t) */')`;

eval EXPLAIN $check_query;
FLUSH STATUS;
--sorted_result
eval $check_query;
--echo 
--skip_if_hypergraph  # Depends on the query plan.
SHOW STATUS LIKE 'handler_read%';
--disable_query_log
eval create table group_query $check_query;
--enable_query_log

# This query doesn't use loose index scan.
let $check_query = `select replace('$orig_query', 'SELECT', 'SELECT /*+ NO_GROUP_BY_LIS(t) */')`;
--disable_query_log
eval create table no_group_query $check_query;
--enable_query_log

# Check result diffence between tables.
--let $diff_tables= test.group_query, test.no_group_query
--source include/diff_tables.inc
drop tables test.group_query, test.no_group_query;
