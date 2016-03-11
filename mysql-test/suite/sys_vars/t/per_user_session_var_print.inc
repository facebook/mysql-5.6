SELECT @@GLOBAL.tx_isolation, @@SESSION.tx_isolation;
SELECT @@GLOBAL.gap_lock_raise_error, @@SESSION.gap_lock_raise_error;
SELECT @@GLOBAL.gap_lock_write_log, @@SESSION.gap_lock_write_log;
SELECT @@GLOBAL.big_tables, @@SESSION.big_tables;
SELECT @@GLOBAL.auto_increment_increment, @@SESSION.auto_increment_increment;
SELECT @@GLOBAL.long_query_time, @@SESSION.long_query_time;
SELECT @@GLOBAL.binlog_format, @@SESSION.binlog_format;
SELECT @@GLOBAL.bulk_insert_buffer_size, @@SESSION.bulk_insert_buffer_size;
