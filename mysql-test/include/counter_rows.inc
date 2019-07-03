# Executes sql command and validates the values of Rows_examined and Rows_sent
# values from status table.
# The following variables should be set before invoking this inc file
# $sql
# $expected_rows_examined
# $expected_rows_sent

# Start counters
--let $start_rows_examined = query_get_value(show status like "Rows_examined", Value, 1)
--let $start_rows_sent = query_get_value(show status like "Rows_sent", Value, 1)

--eval $sql

# Get counters
--let $end_rows_examined = query_get_value(show status like "Rows_examined", Value, 1)
--let $end_rows_sent = query_get_value(show status like "Rows_sent", Value, 1)

# Check sanity
--let $assert_text= Rows_examined should be increased by $expected_rows_examined.
--let $assert_cond= $end_rows_examined - $start_rows_examined = $expected_rows_examined
--source include/assert.inc

--let $assert_text= Rows_sent should be increased by $expected_rows_sent.
--let $assert_cond= $end_rows_sent - $start_rows_sent = $expected_rows_sent
--source include/assert.inc
