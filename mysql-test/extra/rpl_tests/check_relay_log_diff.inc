let $master_binlog_bytes = query_get_value(show global status like "Binlog_bytes_written", Value, 1);
let $master_relay_log_bytes = query_get_value(show global status like "Relay_log_bytes_written", Value, 1);
let $master_binlog_bytes_written = `select $master_binlog_bytes - $master_binlog_bytes_base`;
let $master_relay_log_bytes_written = `select $master_relay_log_bytes - $master_relay_log_bytes_base`;
--disable_query_log
--eval select $master_relay_log_bytes_written as master_relay_log_bytes_written;
--enable_query_log

sync_slave_with_master;
let $slave_binlog_bytes = query_get_value(show global status like "Binlog_bytes_written", Value, 1);
let $slave_relay_log_bytes = query_get_value(show global status like "Relay_log_bytes_written", Value, 1);
let $slave_binlog_bytes_written = `select $slave_binlog_bytes - $slave_binlog_bytes_base`;
let $slave_relay_log_bytes_written = `select $slave_relay_log_bytes - $slave_relay_log_bytes_base`;
--disable_query_log
--eval select $master_binlog_bytes_written - $slave_relay_log_bytes_written as master_binlog_slave_relaylog_diff;
--eval select $slave_relay_log_bytes_written - $slave_binlog_bytes_written as slave_relaylog_binlog_diff;
--eval select $master_binlog_bytes_written - $slave_binlog_bytes_written as master_slave_binlog_diff;
--enable_query_log
