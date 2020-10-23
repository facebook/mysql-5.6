# Executes sql command and validates the values of SUMS_INDEX_DIVE_COUNT from various
# statements table.
# The following variables should be set before invoking this inc file
# $sql
# $expected_sum_index_dive_count

#--let $rpl_debug = 1

# performance_schema_esms_by_all is OFF by default.
# enable it to populate performance_schema.events_statements_summary_by_all
SET @start_value = @@global.performance_schema_esms_by_all;
SET @@global.performance_schema_esms_by_all = ON;

# Start counters for index dive counts
--let $start_sum_index_dive_count_user = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_by_user_by_event_name, Value, 1)
--let $start_sum_index_dive_count_host = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_by_host_by_event_name, Value, 1)
--let $start_sum_index_dive_count_program = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_by_program, Value, 1)
--let $start_sum_index_dive_count_event = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_by_thread_by_event_name, Value, 1)
--let $start_sum_index_dive_count_global = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_global_by_event_name, Value, 1)
--let $start_sum_index_dive_count_all = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_by_all, Value, 1)

# Start counters for index dive cpu
--let $start_sum_index_dive_cpu_user = query_get_value(select sum(SUM_INDEX_DIVE_CPU) as Value from performance_schema.events_statements_summary_by_user_by_event_name, Value, 1)
--let $start_sum_index_dive_cpu_host = query_get_value(select sum(SUM_INDEX_DIVE_CPU) as Value from performance_schema.events_statements_summary_by_host_by_event_name, Value, 1)
--let $start_sum_index_dive_cpu_program = query_get_value(select sum(SUM_INDEX_DIVE_CPU) as Value from performance_schema.events_statements_summary_by_program, Value, 1)
--let $start_sum_index_dive_cpu_event = query_get_value(select sum(SUM_INDEX_DIVE_CPU) as Value from performance_schema.events_statements_summary_by_thread_by_event_name, Value, 1)
--let $start_sum_index_dive_cpu_global = query_get_value(select sum(SUM_INDEX_DIVE_CPU) as Value from performance_schema.events_statements_summary_global_by_event_name, Value, 1)
--let $start_sum_index_dive_cpu_all = query_get_value(select sum(SUM_INDEX_DIVE_CPU) as Value from performance_schema.events_statements_summary_by_all, Value, 1)

SET optimizer_trace="enabled=on,one_line=off";

--eval $sql

select round(length(trace)-
             length(replace(trace, "\"index_dives_for_eq_ranges\": true", "")))
               /length("\"index_dives_for_eq_ranges\": true") Num_Indexes_Used_For_Dives
  from information_schema.optimizer_trace;

SET optimizer_trace="enabled=off";

# Get counters for index dive counts
--let $end_sum_index_dive_count_user = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_by_user_by_event_name, Value, 1)
--let $end_sum_index_dive_count_host = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_by_host_by_event_name, Value, 1)
--let $end_sum_index_dive_count_program = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_by_program, Value, 1)
--let $end_sum_index_dive_count_event = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_by_thread_by_event_name, Value, 1)
--let $end_sum_index_dive_count_global = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_global_by_event_name, Value, 1)
--let $end_sum_index_dive_count_all = query_get_value(select sum(SUM_INDEX_DIVE_COUNT) as Value from performance_schema.events_statements_summary_by_all, Value, 1)

# Get counters for index dive cpu
--let $end_sum_index_dive_cpu_user = query_get_value(select sum(SUM_INDEX_DIVE_CPU) as Value from performance_schema.events_statements_summary_by_user_by_event_name, Value, 1)
--let $end_sum_index_dive_cpu_host = query_get_value(select sum(SUM_INDEX_DIVE_CPU) as Value from performance_schema.events_statements_summary_by_host_by_event_name, Value, 1)
--let $end_sum_index_dive_cpu_program = query_get_value(select sum(SUM_INDEX_DIVE_CPU) as Value from performance_schema.events_statements_summary_by_program, Value, 1)
--let $end_sum_index_dive_cpu_event = query_get_value(select sum(SUM_INDEX_DIVE_CPU) as Value from performance_schema.events_statements_summary_by_thread_by_event_name, Value, 1)
--let $end_sum_index_dive_cpu_global = query_get_value(select sum(SUM_INDEX_DIVE_CPU) as Value from performance_schema.events_statements_summary_global_by_event_name, Value, 1)
--let $end_sum_index_dive_cpu_all = query_get_value(select sum(SUM_INDEX_DIVE_CPU) as Value from performance_schema.events_statements_summary_by_all, Value, 1)

# Check sanity for index dive count
--let $assert_text= SUM_INDEX_DIVE_COUNT on events_statements_summary_by_user_by_event_name should increase by $expected_sum_index_dive_count.
--let $assert_cond= $end_sum_index_dive_count_user - $start_sum_index_dive_count_user = $expected_sum_index_dive_count
--source include/assert.inc

--let $assert_text= SUM_INDEX_DIVE_COUNT on events_statements_summary_by_host_by_event_name should increase by $expected_sum_index_dive_count.
--let $assert_cond= $end_sum_index_dive_count_host - $start_sum_index_dive_count_host = $expected_sum_index_dive_count
--source include/assert.inc

--let $assert_text= SUM_INDEX_DIVE_COUNT on events_statements_summary_by_program should be increased by 0.
--let $assert_cond= $end_sum_index_dive_count_program - $start_sum_index_dive_count_program = 0
--source include/assert.inc

--let $assert_text= SUM_INDEX_DIVE_COUNT on events_statements_summary_by_thread_by_event_name should increase by $expected_sum_index_dive_count.
--let $assert_cond= $end_sum_index_dive_count_event - $start_sum_index_dive_count_event = $expected_sum_index_dive_count
--source include/assert.inc

--let $assert_text= SUM_INDEX_DIVE_COUNT on events_statements_summary_global_by_event_name should increase by $expected_sum_index_dive_count.
--let $assert_cond= $end_sum_index_dive_count_global - $start_sum_index_dive_count_global = $expected_sum_index_dive_count
--source include/assert.inc

--let $assert_text= SUM_INDEX_DIVE_COUNT on events_statements_summary_by_all should increase by $expected_sum_index_dive_count.
--let $assert_cond= $end_sum_index_dive_count_all - $start_sum_index_dive_count_all = $expected_sum_index_dive_count
--source include/assert.inc

# Check sanity for index dive cpu
--let $assert_text= SUM_INDEX_DIVE_CPU on events_statements_summary_by_user_by_event_name should increase by a non-zero value.
--let $assert_cond= $end_sum_index_dive_cpu_user - $start_sum_index_dive_cpu_user > 0
--source include/assert.inc

--let $assert_text= SUM_INDEX_DIVE_CPU on events_statements_summary_by_host_by_event_name should increase by a non-zero value.
--let $assert_cond= $end_sum_index_dive_cpu_host - $start_sum_index_dive_cpu_host > 0
--source include/assert.inc

--let $assert_text= SUM_INDEX_DIVE_CPU on events_statements_summary_by_program should be increased by 0.
--let $assert_cond= $end_sum_index_dive_cpu_program - $start_sum_index_dive_cpu_program = 0
--source include/assert.inc

--let $assert_text= SUM_INDEX_DIVE_CPU on events_statements_summary_by_thread_by_event_name should increase by a non-zero value.
--let $assert_cond= $end_sum_index_dive_cpu_event - $start_sum_index_dive_cpu_event > 0
--source include/assert.inc

--let $assert_text= SUM_INDEX_DIVE_CPU on events_statements_summary_global_by_event_name should increase by a non-zero value.
--let $assert_cond= $end_sum_index_dive_cpu_global - $start_sum_index_dive_cpu_global > 0
--source include/assert.inc

--let $assert_text= SUM_INDEX_DIVE_CPU on events_statements_summary_by_all should increase by a non-zero value.
--let $assert_cond= $end_sum_index_dive_cpu_all - $start_sum_index_dive_cpu_all > 0
--source include/assert.inc

# set performance_schema_esms_by_all to original value
SET @@global.performance_schema_esms_by_all = @start_value;
