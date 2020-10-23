# Executes sql command and validates the values of SUMS_TMP_TABLE_BYTES_WRITTEN from various
# statements table.
# The following variables should be set before invoking this inc file
# $sql
# $expected_sum_tmp_table_bytes_written

--let $mesg = ''
if ($expected_sum_tmp_table_bytes_written > 0)
{
--let $mesg = non-zero
}

if ($expected_sum_tmp_table_bytes_written == 0)
{
--let $mesg = zero
}

#--let $rpl_debug = 1

# performance_schema_esms_by_all is OFF by default.
# enable it to populate performance_schema.events_statements_summary_by_all
SET @start_value = @@global.performance_schema_esms_by_all;
SET @@global.performance_schema_esms_by_all = ON;

# Start counters
--let $start_sum_tmp_table_bytes_written_user = query_get_value(select sum(SUM_TMP_TABLE_BYTES_WRITTEN) as Value from performance_schema.events_statements_summary_by_user_by_event_name, Value, 1)
--let $start_sum_tmp_table_bytes_written_host = query_get_value(select sum(SUM_TMP_TABLE_BYTES_WRITTEN) as Value from performance_schema.events_statements_summary_by_host_by_event_name, Value, 1)
--let $start_sum_tmp_table_bytes_written_program = query_get_value(select sum(SUM_TMP_TABLE_BYTES_WRITTEN) as Value from performance_schema.events_statements_summary_by_program, Value, 1)
--let $start_sum_tmp_table_bytes_written_event = query_get_value(select sum(SUM_TMP_TABLE_BYTES_WRITTEN) as Value from performance_schema.events_statements_summary_by_thread_by_event_name, Value, 1)
--let $start_sum_tmp_table_bytes_written_global = query_get_value(select sum(SUM_TMP_TABLE_BYTES_WRITTEN) as Value from performance_schema.events_statements_summary_global_by_event_name, Value, 1)
--let $start_sum_tmp_table_bytes_written_all = query_get_value(select sum(SUM_TMP_TABLE_BYTES_WRITTEN) as Value from performance_schema.events_statements_summary_by_all, Value, 1)

--disable_result_log
--eval $sql
--enable_result_log

# Get counters
--let $end_sum_tmp_table_bytes_written_user = query_get_value(select sum(SUM_TMP_TABLE_BYTES_WRITTEN) as Value from performance_schema.events_statements_summary_by_user_by_event_name, Value, 1)
--let $end_sum_tmp_table_bytes_written_host = query_get_value(select sum(SUM_TMP_TABLE_BYTES_WRITTEN) as Value from performance_schema.events_statements_summary_by_host_by_event_name, Value, 1)
--let $end_sum_tmp_table_bytes_written_program = query_get_value(select sum(SUM_TMP_TABLE_BYTES_WRITTEN) as Value from performance_schema.events_statements_summary_by_program, Value, 1)
--let $end_sum_tmp_table_bytes_written_event = query_get_value(select sum(SUM_TMP_TABLE_BYTES_WRITTEN) as Value from performance_schema.events_statements_summary_by_thread_by_event_name, Value, 1)
--let $end_sum_tmp_table_bytes_written_global = query_get_value(select sum(SUM_TMP_TABLE_BYTES_WRITTEN) as Value from performance_schema.events_statements_summary_global_by_event_name, Value, 1)
--let $end_sum_tmp_table_bytes_written_all = query_get_value(select sum(SUM_TMP_TABLE_BYTES_WRITTEN) as Value from performance_schema.events_statements_summary_by_all, Value, 1)

# Check sanity
--let $assert_text= SUM_TMP_TABLE_BYTES_WRITTEN on events_statements_summary_by_user_by_event_name should increase by $mesg.
if ($expected_sum_tmp_table_bytes_written > 0)
{
--let $assert_cond= $end_sum_tmp_table_bytes_written_user - $start_sum_tmp_table_bytes_written_user >= $expected_sum_tmp_table_bytes_written
}
if ($expected_sum_tmp_table_bytes_written == 0)
{
--let $assert_cond= $end_sum_tmp_table_bytes_written_user - $start_sum_tmp_table_bytes_written_user = $expected_sum_tmp_table_bytes_written
}
--source include/assert.inc

--let $assert_text= SUM_TMP_TABLE_BYTES_WRITTEN on events_statements_summary_by_host_by_event_name should increase by $mesg.
if ($expected_sum_tmp_table_bytes_written > 0)
{
--let $assert_cond= $end_sum_tmp_table_bytes_written_host - $start_sum_tmp_table_bytes_written_host >= $expected_sum_tmp_table_bytes_written
}
if ($expected_sum_tmp_table_bytes_written == 0)
{
--let $assert_cond= $end_sum_tmp_table_bytes_written_host - $start_sum_tmp_table_bytes_written_host = $expected_sum_tmp_table_bytes_written
}
--source include/assert.inc

--let $assert_text= SUM_TMP_TABLE_BYTES_WRITTEN on events_statements_summary_by_program should be increased by 0.
--let $assert_cond= $end_sum_tmp_table_bytes_written_program - $start_sum_tmp_table_bytes_written_program = 0
--source include/assert.inc

--let $assert_text= SUM_TMP_TABLE_BYTES_WRITTEN on events_statements_summary_by_thread_by_event_name should increase by $mesg.
if ($expected_sum_tmp_table_bytes_written > 0)
{
--let $assert_cond= $end_sum_tmp_table_bytes_written_event - $start_sum_tmp_table_bytes_written_event >= $expected_sum_tmp_table_bytes_written
}
if ($expected_sum_tmp_table_bytes_written == 0)
{
--let $assert_cond= $end_sum_tmp_table_bytes_written_event - $start_sum_tmp_table_bytes_written_event = $expected_sum_tmp_table_bytes_written
}
--source include/assert.inc

--let $assert_text= SUM_TMP_TABLE_BYTES_WRITTEN on events_statements_summary_global_by_event_name should increase by $mesg.
if ($expected_sum_tmp_table_bytes_written > 0)
{
--let $assert_cond= $end_sum_tmp_table_bytes_written_global - $start_sum_tmp_table_bytes_written_global >= $expected_sum_tmp_table_bytes_written
}
if ($expected_sum_tmp_table_bytes_written == 0)
{
--let $assert_cond= $end_sum_tmp_table_bytes_written_global - $start_sum_tmp_table_bytes_written_global = $expected_sum_tmp_table_bytes_written
}
--source include/assert.inc

--let $assert_text= SUM_TMP_TABLE_BYTES_WRITTEN on events_statements_summary_by_all should increase by $mesg.
if ($expected_sum_tmp_table_bytes_written > 0)
{
--let $assert_cond= $end_sum_tmp_table_bytes_written_all - $start_sum_tmp_table_bytes_written_all >= $expected_sum_tmp_table_bytes_written
}
if ($expected_sum_tmp_table_bytes_written == 0)
{
--let $assert_cond= $end_sum_tmp_table_bytes_written_all - $start_sum_tmp_table_bytes_written_all = $expected_sum_tmp_table_bytes_written
}
--source include/assert.inc

# set performance_schema_esms_by_all to original value
SET @@global.performance_schema_esms_by_all = @start_value;
