# This script shows the output of STATUS counters/variables from the 
# performance_schema.global_status table. It takes the difference from 
# the last time these counters were accessed (zeroes them out) using a tmp
# table. 

# Usage:

# Set $status_like to a counter name or wildcard that can be used with LIKE clause
# Set $status_tab to a temporary table name

# Call show_status_init.inc

# Every time the counters need to be shown, call show_status.inc

# At the end of the file, call show_status_cleanup.inc

--echo
--echo Status counters
--echo ---------------
--disable_query_log

eval SELECT a.variable_name as COUNTER, (b.variable_value - a.variable_value) as VALUE 
FROM $status_tab a JOIN performance_schema.global_status b USING (variable_name) 
WHERE variable_name LIKE $status_like;

eval UPDATE $status_tab JOIN performance_schema.global_status b USING (variable_name)
SET $status_tab.variable_value = b.variable_value
WHERE variable_name LIKE $status_like;

--enable_query_log
--echo

