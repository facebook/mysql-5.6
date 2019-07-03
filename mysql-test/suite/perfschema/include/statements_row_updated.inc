# Executes sql command and validates the values of SUMS_ROWS_UPDATED from various
# statements table.
# The following variables should be set before invoking this inc file
# $sql
# $expected_sum_rows_updated

#--let $rpl_debug = 1

# Start counters
--let $start_sum_rows_updated_user = query_get_value(select sum(SUM_ROWS_UPDATED) as Value from performance_schema.events_statements_summary_by_user_by_event_name, Value, 1)
--let $start_sum_rows_updated_host = query_get_value(select sum(SUM_ROWS_UPDATED) as Value from performance_schema.events_statements_summary_by_host_by_event_name, Value, 1)
--let $start_sum_rows_updated_program = query_get_value(select sum(SUM_ROWS_UPDATED) as Value from performance_schema.events_statements_summary_by_program, Value, 1)
--let $start_sum_rows_updated_event = query_get_value(select sum(SUM_ROWS_UPDATED) as Value from performance_schema.events_statements_summary_by_thread_by_event_name, Value, 1)
--let $start_sum_rows_updated_global = query_get_value(select sum(SUM_ROWS_UPDATED) as Value from performance_schema.events_statements_summary_global_by_event_name, Value, 1)

--eval $sql

# Get counters
--let $end_sum_rows_updated_user = query_get_value(select sum(SUM_ROWS_UPDATED) as Value from performance_schema.events_statements_summary_by_user_by_event_name, Value, 1)
--let $end_sum_rows_updated_host = query_get_value(select sum(SUM_ROWS_UPDATED) as Value from performance_schema.events_statements_summary_by_host_by_event_name, Value, 1)
--let $end_sum_rows_updated_program = query_get_value(select sum(SUM_ROWS_UPDATED) as Value from performance_schema.events_statements_summary_by_program, Value, 1)
--let $end_sum_rows_updated_event = query_get_value(select sum(SUM_ROWS_UPDATED) as Value from performance_schema.events_statements_summary_by_thread_by_event_name, Value, 1)
--let $end_sum_rows_updated_global = query_get_value(select sum(SUM_ROWS_UPDATED) as Value from performance_schema.events_statements_summary_global_by_event_name, Value, 1)

# Check sanity
--let $assert_text= SUM_ROWS_UPDATED on events_statements_summary_by_user_by_event_name should be increased by $expected_sum_rows_updated.
--let $assert_cond= $end_sum_rows_updated_user - $start_sum_rows_updated_user = $expected_sum_rows_updated
--source include/assert.inc

--let $assert_text= SUM_ROWS_UPDATED on events_statements_summary_by_host_by_event_name should be increased by $expected_sum_rows_updated.
--let $assert_cond= $end_sum_rows_updated_host - $start_sum_rows_updated_host = $expected_sum_rows_updated
--source include/assert.inc

--let $assert_text= SUM_ROWS_UPDATED on events_statements_summary_by_program should be increased by 0.
--let $assert_cond= $end_sum_rows_updated_program - $start_sum_rows_updated_program = 0
--source include/assert.inc

--let $assert_text= SUM_ROWS_UPDATED on events_statements_summary_by_thread_by_event_name should be increased by $expected_sum_rows_updated.
--let $assert_cond= $end_sum_rows_updated_event - $start_sum_rows_updated_event = $expected_sum_rows_updated
--source include/assert.inc

--let $assert_text= SUM_ROWS_UPDATED on events_statements_summary_global_by_event_name should be increased by $expected_sum_rows_updated.
--let $assert_cond= $end_sum_rows_updated_global - $start_sum_rows_updated_global = $expected_sum_rows_updated
--source include/assert.inc
