--disable_query_log
--eval create table temp_orig as $query
--eval create table temp_skip as $query
--enable_query_log

set optimizer_switch = 'skip_scan=off';
--replace_column 9 #
--eval explain $query
--disable_query_log
--let $before= query_get_value("select rows_read from information_schema.table_statistics where table_name = 't'", rows_read, 1)
--disable_result_log
--eval $query
--enable_result_log
--let $after= query_get_value("select rows_read from information_schema.table_statistics where table_name = 't'", rows_read, 1)
--eval select $after - $before as "rows_read"
--eval insert into temp_orig $query
--enable_query_log

set optimizer_switch = 'skip_scan=on,skip_scan_cost_based=off';
--replace_column 9 #
--eval explain $query
--disable_query_log
--let $before= query_get_value("select rows_read from information_schema.table_statistics where table_name = 't'", rows_read, 1)
--disable_result_log
--eval $query
--enable_result_log
--let $after= query_get_value("select rows_read from information_schema.table_statistics where table_name = 't'", rows_read, 1)
--eval select $after - $before as "rows_read"
--eval insert into temp_skip $query
--enable_query_log

let $diff_tables = temp_orig, temp_skip;
--source include/diff_tables.inc

set optimizer_switch = 'skip_scan=off,skip_scan_cost_based=on';

--disable_query_log
drop table temp_orig;
drop table temp_skip;
--enable_query_log
