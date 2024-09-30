# Use in conjunction with include/show_status.inc

# This script creates a tmp table using the provided name $status_tab
# and fills it with STATUS counters that match the given $status_like string

--disable_query_log

eval CREATE TEMPORARY TABLE $status_tab AS
SELECT * FROM performance_schema.global_status
WHERE variable_name LIKE $status_like;

--enable_query_log
