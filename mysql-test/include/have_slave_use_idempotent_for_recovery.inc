-- require r/have_slave_use_idempotent_for_recovery.require
disable_query_log;
show variables like 'slave_use_idempotent_for_recovery';
enable_query_log;
