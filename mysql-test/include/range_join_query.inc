--eval EXPLAIN $query
FLUSH STATUS;
--disable_result_log
--eval $query
--enable_result_log
SHOW STATUS LIKE 'handler_read%';
--disable_query_log
--eval CREATE TABLE range_join $query
--enable_query_log

let $check_query = `select replace('$query', 'select', 'select $hint')`;

--eval EXPLAIN $check_query
FLUSH STATUS;
--disable_result_log
--eval $check_query
--enable_result_log
SHOW STATUS LIKE 'handler_read%';
--disable_query_log
--eval CREATE TABLE hint_range_join $check_query
--enable_query_log

--let $diff_tables= test.range_join, test.hint_range_join
--source include/diff_tables.inc

--disable_query_log
DROP TABLE range_join, hint_range_join;
--enable_query_log
