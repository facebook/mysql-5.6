# Executes sql command and validates the values of sum_filesort_disk_usage from various
# statements table.
# The following variables should be set before invoking this inc file
# $sql
# $expected_sum_filesort_disk_usage

--let $mesg = ''
if ($expected_sum_filesort_disk_usage > 0)
{
--let $mesg = non-zero
}

if ($expected_sum_filesort_disk_usage == 0)
{
--let $mesg = zero
}

#--let $rpl_debug = 1

# performance_schema_esms_by_all is OFF by default.
# enable it to populate performance_schema.events_statements_summary_by_all
SET @start_value = @@global.performance_schema_esms_by_all;
SET @@global.performance_schema_esms_by_all = ON;

# Start counters
--let $start_sum_filesort_disk_usage_user = query_get_value(select sum(sum_filesort_disk_usage) as Value from performance_schema.events_statements_summary_by_user_by_event_name, Value, 1)
--let $start_sum_filesort_disk_usage_host = query_get_value(select sum(sum_filesort_disk_usage) as Value from performance_schema.events_statements_summary_by_host_by_event_name, Value, 1)
--let $start_sum_filesort_disk_usage_program = query_get_value(select sum(sum_filesort_disk_usage) as Value from performance_schema.events_statements_summary_by_program, Value, 1)
--let $start_sum_filesort_disk_usage_event = query_get_value(select sum(sum_filesort_disk_usage) as Value from performance_schema.events_statements_summary_by_thread_by_event_name, Value, 1)
--let $start_sum_filesort_disk_usage_global = query_get_value(select sum(sum_filesort_disk_usage) as Value from performance_schema.events_statements_summary_global_by_event_name, Value, 1)
--let $start_sum_filesort_disk_usage_all = query_get_value(select sum(sum_filesort_disk_usage) as Value from performance_schema.events_statements_summary_by_all, Value, 1)
--let $start_sum_filesort_disk_usage_hist = query_get_value(select sum(filesort_disk_usage) as Value from performance_schema.events_statements_history_long, Value, 1)

--disable_result_log
--eval $sql
--enable_result_log

# Get counters
--let $end_sum_filesort_disk_usage_user = query_get_value(select sum(sum_filesort_disk_usage) as Value from performance_schema.events_statements_summary_by_user_by_event_name, Value, 1)
--let $end_sum_filesort_disk_usage_host = query_get_value(select sum(sum_filesort_disk_usage) as Value from performance_schema.events_statements_summary_by_host_by_event_name, Value, 1)
--let $end_sum_filesort_disk_usage_program = query_get_value(select sum(sum_filesort_disk_usage) as Value from performance_schema.events_statements_summary_by_program, Value, 1)
--let $end_sum_filesort_disk_usage_event = query_get_value(select sum(sum_filesort_disk_usage) as Value from performance_schema.events_statements_summary_by_thread_by_event_name, Value, 1)
--let $end_sum_filesort_disk_usage_global = query_get_value(select sum(sum_filesort_disk_usage) as Value from performance_schema.events_statements_summary_global_by_event_name, Value, 1)
--let $end_sum_filesort_disk_usage_all = query_get_value(select sum(sum_filesort_disk_usage) as Value from performance_schema.events_statements_summary_by_all, Value, 1)
--let $end_sum_filesort_disk_usage_hist = query_get_value(select sum(filesort_disk_usage) as Value from performance_schema.events_statements_history_long, Value, 1)

# Check sanity
--let $assert_text= sum_filesort_disk_usage on events_statements_summary_by_user_by_event_name should increase by $mesg.
if ($expected_sum_filesort_disk_usage > 0)
{
--let $assert_cond= $end_sum_filesort_disk_usage_user - $start_sum_filesort_disk_usage_user >= $expected_sum_filesort_disk_usage
}
if ($expected_sum_filesort_disk_usage == 0)
{
--let $assert_cond= $end_sum_filesort_disk_usage_user - $start_sum_filesort_disk_usage_user = $expected_sum_filesort_disk_usage
}
--source include/assert.inc

--let $assert_text= sum_filesort_disk_usage on events_statements_summary_by_host_by_event_name should increase by $mesg.
if ($expected_sum_filesort_disk_usage > 0)
{
--let $assert_cond= $end_sum_filesort_disk_usage_host - $start_sum_filesort_disk_usage_host >= $expected_sum_filesort_disk_usage
}
if ($expected_sum_filesort_disk_usage == 0)
{
--let $assert_cond= $end_sum_filesort_disk_usage_host - $start_sum_filesort_disk_usage_host = $expected_sum_filesort_disk_usage
}
--source include/assert.inc

--let $assert_text= sum_filesort_disk_usage on events_statements_summary_by_program should be increased by zero.
--let $assert_cond= $end_sum_filesort_disk_usage_program - $start_sum_filesort_disk_usage_program = 0
--source include/assert.inc

--let $assert_text= sum_filesort_disk_usage on events_statements_summary_by_thread_by_event_name should increase by $mesg.
if ($expected_sum_filesort_disk_usage > 0)
{
--let $assert_cond= $end_sum_filesort_disk_usage_event - $start_sum_filesort_disk_usage_event >= $expected_sum_filesort_disk_usage
}
if ($expected_sum_filesort_disk_usage == 0)
{
--let $assert_cond= $end_sum_filesort_disk_usage_event - $start_sum_filesort_disk_usage_event = $expected_sum_filesort_disk_usage
}
--source include/assert.inc

--let $assert_text= sum_filesort_disk_usage on events_statements_summary_global_by_event_name should increase by $mesg.
if ($expected_sum_filesort_disk_usage > 0)
{
--let $assert_cond= $end_sum_filesort_disk_usage_global - $start_sum_filesort_disk_usage_global >= $expected_sum_filesort_disk_usage
}
if ($expected_sum_filesort_disk_usage == 0)
{
--let $assert_cond= $end_sum_filesort_disk_usage_global - $start_sum_filesort_disk_usage_global = $expected_sum_filesort_disk_usage
}
--source include/assert.inc

--let $assert_text= sum_filesort_disk_usage on events_statements_summary_by_all should increase by $mesg.
if ($expected_sum_filesort_disk_usage > 0)
{
--let $assert_cond= $end_sum_filesort_disk_usage_all - $start_sum_filesort_disk_usage_all >= $expected_sum_filesort_disk_usage
}
if ($expected_sum_filesort_disk_usage == 0)
{
--let $assert_cond= $end_sum_filesort_disk_usage_all - $start_sum_filesort_disk_usage_all = $expected_sum_filesort_disk_usage
}
--source include/assert.inc

--let $assert_text= filesort_disk_usage on events_statements_history_long should increase by $mesg.
if ($expected_sum_filesort_disk_usage > 0)
{
--let $assert_cond= $end_sum_filesort_disk_usage_hist - $start_sum_filesort_disk_usage_hist >= $expected_sum_filesort_disk_usage
}
if ($expected_sum_filesort_disk_usage == 0)
{
--let $assert_cond= $end_sum_filesort_disk_usage_hist - $start_sum_filesort_disk_usage_hist = $expected_sum_filesort_disk_usage
}
--source include/assert.inc

# set performance_schema_esms_by_all to original value
SET @@global.performance_schema_esms_by_all = @start_value;
