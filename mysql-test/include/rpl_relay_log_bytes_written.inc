connection master;
# Store the base values for master
let $master_binlog_bytes_base = query_get_value(show global status like "Binlog_bytes_written", Value, 1);
let $master_relay_log_bytes_base = query_get_value(show global status like "Relay_log_bytes_written", Value, 1);

sync_slave_with_master;
# store the base values for slave
let $slave_binlog_bytes_base = query_get_value(show global status like "Binlog_bytes_written", Value, 1);
let $slave_relay_log_bytes_base = query_get_value(show global status like "Relay_log_bytes_written", Value, 1);

connection master;
# Create Table
create table test_relay_log_bytes_written(first_column int);
--source extra/rpl_tests/check_relay_log_diff.inc

connection master;
# Insert into Table
insert into test_relay_log_bytes_written values(1);
--source extra/rpl_tests/check_relay_log_diff.inc

connection master;
# Insert into Table
insert into test_relay_log_bytes_written values(2);
--source extra/rpl_tests/check_relay_log_diff.inc

connection master;
# Delete from Table
delete from test_relay_log_bytes_written;
--source extra/rpl_tests/check_relay_log_diff.inc

connection master;
# Alter table
alter table test_relay_log_bytes_written add second_column int;
--source extra/rpl_tests/check_relay_log_diff.inc

connection master;
# Drop Table
drop table test_relay_log_bytes_written;
--source extra/rpl_tests/check_relay_log_diff.inc
