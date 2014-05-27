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
SELECT VARIABLE_VALUE INTO @innodb_rows_inserted_1 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'Innodb_rows_inserted';
SELECT VARIABLE_VALUE INTO @innodb_rows_deleted_1 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'Innodb_rows_deleted';
SELECT VARIABLE_VALUE INTO @innodb_rows_updated_1 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'Innodb_rows_updated';
eval SELECT ROWS_INSERTED, ROWS_UPDATED, ROWS_DELETED, IO_INDEX_INSERTS
        INTO @table_stat_rows_inserted_1, @table_stat_rows_updated_1, @table_stat_rows_deleted_1, @table_stat_io_index_inserts_1
        FROM INFORMATION_SCHEMA.TABLE_STATISTICS
        WHERE TABLE_NAME='$fake_changes_table' AND TABLE_SCHEMA='test';
SELECT ROWS_INSERTED, ROWS_UPDATED, ROWS_DELETED
        INTO @user_stat_rows_inserted_1, @user_stat_rows_updated_1, @user_stat_rows_deleted_1
        FROM INFORMATION_SCHEMA.USER_STATISTICS
        WHERE USER_NAME='root';

SET innodb_fake_changes=1;
