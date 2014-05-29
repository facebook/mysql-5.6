#
# Include this file in a session before fake changes workload.  It will enable 
# the fake changes for the current session and save the relevant server state 
# to variables that will be used by stop_fake_changes.inc that it didn't change.
#
# Since some of the state checked is global InnoDB counters, do not issue a 
# non-fake-changes workload in parallel.
#

let $t_checksum_1= `CHECKSUM TABLE $fake_changes_table EXTENDED`;
COMMIT;

# Reset I_S.INNODB_SYS_TABLESTATS.MODIFIED_COUNTER
ANALYZE TABLE t1;

--source include/wait_innodb_all_purged.inc
--source include/wait_until_clean_bp.inc

SELECT VARIABLE_VALUE INTO @innodb_rows_inserted_1 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'Innodb_rows_inserted';
SELECT VARIABLE_VALUE INTO @innodb_rows_deleted_1 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'Innodb_rows_deleted';
SELECT VARIABLE_VALUE INTO @innodb_rows_updated_1 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'Innodb_rows_updated';
SELECT VARIABLE_VALUE AS should_be_0 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'Innodb_buffer_pool_pages_dirty';
SELECT VARIABLE_VALUE INTO @innodb_bp_writes_1 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'Innodb_buffer_pool_write_requests';
SELECT VARIABLE_VALUE INTO @innodb_max_trx_id_1 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'Innodb_max_trx_id';

eval SELECT MODIFIED_COUNTER AS should_be_0 FROM INFORMATION_SCHEMA.INNODB_SYS_TABLESTATS WHERE NAME LIKE 'test/$fake_changes_table';

eval SELECT ROWS_INSERTED, ROWS_UPDATED, ROWS_DELETED, IO_WRITE_REQUESTS, INNODB_PAGES_WRITTEN, IO_INDEX_INSERTS
        INTO @table_stat_rows_inserted_1, @table_stat_rows_updated_1, @table_stat_rows_deleted_1,
             @table_stat_io_write_requests_1, @table_stat_innodb_pages_written_1, @table_stat_io_index_inserts_1
        FROM INFORMATION_SCHEMA.TABLE_STATISTICS
        WHERE TABLE_NAME='$fake_changes_table' AND TABLE_SCHEMA='test';
SELECT ROWS_INSERTED, ROWS_UPDATED, ROWS_DELETED
        INTO @user_stat_rows_inserted_1, @user_stat_rows_updated_1, @user_stat_rows_deleted_1
        FROM INFORMATION_SCHEMA.USER_STATISTICS
        WHERE USER_NAME='root';

SET innodb_fake_changes=1;
