#
# Include this file in a session after fake changes workload.  It will disable the fake 
# changes and assert that the relevant server state has not changed from the one saved
# by a previous start_fake_changes.inc.
#
SET innodb_fake_changes=0;

SELECT VARIABLE_VALUE INTO @innodb_rows_inserted_2 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'Innodb_rows_inserted';
SELECT VARIABLE_VALUE INTO @innodb_rows_deleted_2 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'Innodb_rows_deleted';
SELECT VARIABLE_VALUE INTO @innodb_rows_updated_2 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'Innodb_rows_updated';
SELECT VARIABLE_VALUE INTO @innodb_bp_pages_dirty FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'Innodb_buffer_pool_pages_dirty';
SELECT VARIABLE_VALUE INTO @innodb_bp_writes_2 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'Innodb_buffer_pool_write_requests';
SELECT VARIABLE_VALUE INTO @innodb_max_trx_id_2 FROM INFORMATION_SCHEMA.GLOBAL_STATUS WHERE VARIABLE_NAME = 'Innodb_max_trx_id';
SET @innodb_max_trx_id_delta = @innodb_max_trx_id_2 - @innodb_max_trx_id_1;

eval SELECT MODIFIED_COUNTER AS should_be_0 FROM INFORMATION_SCHEMA.INNODB_SYS_TABLESTATS WHERE NAME LIKE 'test/$fake_changes_table';

eval SELECT ROWS_INSERTED, ROWS_UPDATED, ROWS_DELETED, IO_WRITE_REQUESTS, INNODB_PAGES_WRITTEN, IO_INDEX_INSERTS
        INTO @table_stat_rows_inserted_2, @table_stat_rows_updated_2, @table_stat_rows_deleted_2,
             @table_stat_io_write_requests_2, @table_stat_innodb_pages_written_2, @table_stat_io_index_inserts_2
        FROM INFORMATION_SCHEMA.TABLE_STATISTICS
        WHERE TABLE_NAME='$fake_changes_table' AND TABLE_SCHEMA='test';
SELECT ROWS_INSERTED, ROWS_UPDATED, ROWS_DELETED
        INTO @user_stat_rows_inserted_2, @user_stat_rows_updated_2, @user_stat_rows_deleted_2
        FROM INFORMATION_SCHEMA.USER_STATISTICS
        WHERE USER_NAME='root';

eval CHECK TABLE $fake_changes_table;

let $t_checksum_2= `CHECKSUM TABLE $fake_changes_table EXTENDED`;

--disable_query_log
eval SELECT "$t_checksum_1" LIKE "$t_checksum_2" AS should_be_1;
--enable_query_log
SELECT @innodb_rows_inserted_2 - @innodb_rows_inserted_1 AS should_be_0;
SELECT @innodb_rows_deleted_2 - @innodb_rows_deleted_1 AS should_be_0;
SELECT @innodb_rows_updated_2 - @innodb_rows_updated_1 AS should_be_0;
SELECT @table_stat_rows_inserted_2 - @table_stat_rows_inserted_1 AS should_be_0;
SELECT @table_stat_rows_updated_2 - @table_stat_rows_updated_1 AS should_be_0;
SELECT @table_stat_rows_deleted_2 - @table_stat_rows_deleted_1 AS should_be_0;
SELECT @table_stat_io_index_inserts_2 - @table_stat_io_index_inserts_1 AS should_be_0;
SELECT @user_stat_rows_inserted_2 - @user_stat_rows_inserted_1 AS should_be_0;
SELECT @user_stat_rows_updated_2 - @user_stat_rows_updated_1 AS should_be_0;
SELECT @user_stat_rows_deleted_2 - @user_stat_rows_deleted_1 AS should_be_0;

# The transaction header page is rewritten every TRX_SYS_TRX_ID_WRITE_MARGIN (256) transactions
SELECT @innodb_bp_pages_dirty <= IF(@innodb_max_trx_id_delta > 0, 1, 0) AS should_be_1;
SELECT @innodb_bp_writes_2 - @innodb_bp_writes_1
       <= IF (@innodb_max_trx_id_delta > 0, @innodb_max_trx_id_delta DIV 256 + 1, 0) AS should_be_1;
SELECT @table_stat_io_write_requests_2 - @table_stat_io_write_requests_1
       <= IF (@innodb_max_trx_id_delta > 0, @innodb_max_trx_id_delta DIV 256 + 1, 0) AS should_be_1;
SELECT @table_stat_innodb_pages_written_2 - @table_stat_innodb_pages_written_1
       <= IF (@innodb_max_trx_id_delta > 0, @innodb_max_trx_id_delta DIV 256 + 1, 0) AS should_be_1;
