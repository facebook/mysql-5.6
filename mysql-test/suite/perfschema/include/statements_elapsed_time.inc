# Executes sql command and validates the values of SUMS_ELAPSED_TIME from various
# statements table.
# The following variables should be set before invoking this inc file
# $sql

#--let $rpl_debug = 1

# performance_schema_esms_by_all is OFF by default.
# enable it to populate performance_schema.events_statements_summary_by_all
SET @start_value = @@global.performance_schema_esms_by_all;
SET @@global.performance_schema_esms_by_all = ON;

# Start counters
--let $start_sum_elapsed_time_user = query_get_value(select sum(SUM_ELAPSED_TIME) as Value from performance_schema.events_statements_summary_by_user_by_event_name, Value, 1)
--let $start_sum_elapsed_time_host = query_get_value(select sum(SUM_ELAPSED_TIME) as Value from performance_schema.events_statements_summary_by_host_by_event_name, Value, 1)
--let $start_sum_elapsed_time_program = query_get_value(select sum(SUM_ELAPSED_TIME) as Value from performance_schema.events_statements_summary_by_program, Value, 1)
--let $start_sum_elapsed_time_event = query_get_value(select sum(SUM_ELAPSED_TIME) as Value from performance_schema.events_statements_summary_by_thread_by_event_name, Value, 1)
--let $start_sum_elapsed_time_global = query_get_value(select sum(SUM_ELAPSED_TIME) as Value from performance_schema.events_statements_summary_global_by_event_name, Value, 1)
--let $start_sum_elapsed_time_all = query_get_value(select case when sum(SUM_ELAPSED_TIME) then sum(SUM_ELAPSED_TIME) else 0 end as Value from performance_schema.events_statements_summary_by_all, Value, 1)

--disable_result_log
--eval $sql
--enable_result_log

# Get counters
--let $end_sum_elapsed_time_user = query_get_value(select sum(SUM_ELAPSED_TIME) as Value from performance_schema.events_statements_summary_by_user_by_event_name, Value, 1)
--let $end_sum_elapsed_time_host = query_get_value(select sum(SUM_ELAPSED_TIME) as Value from performance_schema.events_statements_summary_by_host_by_event_name, Value, 1)
--let $end_sum_elapsed_time_program = query_get_value(select sum(SUM_ELAPSED_TIME) as Value from performance_schema.events_statements_summary_by_program, Value, 1)
--let $end_sum_elapsed_time_event = query_get_value(select sum(SUM_ELAPSED_TIME) as Value from performance_schema.events_statements_summary_by_thread_by_event_name, Value, 1)
--let $end_sum_elapsed_time_global = query_get_value(select sum(SUM_ELAPSED_TIME) as Value from performance_schema.events_statements_summary_global_by_event_name, Value, 1)
--let $end_sum_elapsed_time_all = query_get_value(select case when sum(SUM_ELAPSED_TIME) then sum(SUM_ELAPSED_TIME) else 0 end as Value from performance_schema.events_statements_summary_by_all, Value, 1)

# Check sanity
--let $assert_text= SUM_ELAPSED_TIME on events_statements_summary_by_user_by_event_name should increase by a non-zero value.
--let $assert_cond= $end_sum_elapsed_time_user - $start_sum_elapsed_time_user > 0
--source include/assert.inc

--let $assert_text= SUM_ELAPSED_TIME on events_statements_summary_by_host_by_event_name should increase by a non-zero value.
--let $assert_cond= $end_sum_elapsed_time_host - $start_sum_elapsed_time_host > 0
--source include/assert.inc

--let $assert_text= SUM_ELAPSED_TIME on events_statements_summary_by_program should be increased by 0.
--let $assert_cond= $end_sum_elapsed_time_program - $start_sum_elapsed_time_program = 0
--source include/assert.inc

--let $assert_text= SUM_ELAPSED_TIME on events_statements_summary_by_thread_by_event_name should increase by a non-zero value.
--let $assert_cond= $end_sum_elapsed_time_event - $start_sum_elapsed_time_event > 0
--source include/assert.inc

--let $assert_text= SUM_ELAPSED_TIME on events_statements_summary_global_by_event_name should increase by a non-zero value.
--let $assert_cond= $end_sum_elapsed_time_global - $start_sum_elapsed_time_global > 0
--source include/assert.inc

--let $assert_text= SUM_ELAPSED_TIME on events_statements_summary_by_all should increase by a non-zero value.
--let $assert_cond= $end_sum_elapsed_time_all - $start_sum_elapsed_time_all > 0
--source include/assert.inc

# set performance_schema_esms_by_all to original value
SET @@global.performance_schema_esms_by_all = @start_value;
