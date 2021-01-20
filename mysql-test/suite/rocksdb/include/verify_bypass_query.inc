# Run bypass query and print rows being read
# This is needed to make sure bypass query read exactly the right amount of
# rows and avoid performance regression
#
# Parameters
#   $bypass_query
#     Bypass query to run

--disable_query_log
SELECT @@rocksdb_select_bypass_policy INTO @__rocksdb_select_bypass_policy;
SET GLOBAL rocksdb_select_bypass_policy=2;

SELECT variable_value INTO @bypass_rows_read_0 FROM
performance_schema.global_status WHERE
variable_name="rocksdb_rows_read";
SELECT variable_value INTO @bypass_rows_sent_0 FROM
performance_schema.global_status WHERE
variable_name="rows_sent";
SELECT variable_value INTO @bypass_executed_0 FROM
performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";

--echo ========== Verifying Bypass Query ==========
--echo WITH BYPASS:

--enable_query_log
--output $MYSQL_TMP_DIR/bypass_query_rows
--eval $bypass_query
--disable_query_log
--cat_file $MYSQL_TMP_DIR/bypass_query_rows

SELECT variable_value INTO @bypass_rows_read_1 FROM
performance_schema.global_status WHERE
variable_name="rocksdb_rows_read";
SELECT variable_value INTO @bypass_rows_sent_1 FROM
performance_schema.global_status WHERE
variable_name="rows_sent";
SELECT variable_value INTO @bypass_executed_1 FROM
performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";

--let rows_read_bypass=`SELECT @bypass_rows_read_1 - @bypass_rows_read_0`
SELECT @bypass_rows_read_1 - @bypass_rows_read_0 AS ROWS_READ;

--let $assert_cond= @bypass_executed_1 - @bypass_executed_0 = 1
--let $assert_text = Verify executed in bypass
--source include/assert.inc

SELECT variable_value INTO @bypass_rows_read_2 FROM
performance_schema.global_status WHERE
variable_name="rocksdb_rows_read";
SELECT variable_value INTO @bypass_rows_sent_2 FROM
performance_schema.global_status WHERE
variable_name="rows_sent";
SELECT variable_value INTO @bypass_executed_2 FROM
performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";

--echo WITHOUT BYPASS:
SET GLOBAL rocksdb_select_bypass_policy=0;

--enable_query_log
--output $MYSQL_TMP_DIR/regular_query_rows
--eval $bypass_query
--disable_query_log
--cat_file $MYSQL_TMP_DIR/regular_query_rows

--diff_files $MYSQL_TMP_DIR/bypass_query_rows $MYSQL_TMP_DIR/regular_query_rows
--remove_file $MYSQL_TMP_DIR/bypass_query_rows
--remove_file $MYSQL_TMP_DIR/regular_query_rows

SELECT variable_value INTO @bypass_rows_read_3 FROM
performance_schema.global_status WHERE
variable_name="rocksdb_rows_read";
SELECT variable_value INTO @bypass_rows_sent_3 FROM
performance_schema.global_status WHERE
variable_name="rows_sent";
SELECT variable_value INTO @bypass_executed_3 FROM
performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";

--let $assert_cond= @bypass_executed_3 - @bypass_executed_2 = 0
--let $assert_text = Verify not executed in bypass
--source include/assert.inc

--let rows_read_no_bypass=`SELECT @bypass_rows_read_3 - @bypass_rows_read_2`
# This doesn't seem to be stable outside bypass
# SELECT @bypass_rows_read_3 - @bypass_rows_read_2 AS ROWS_READ;

SET GLOBAL rocksdb_select_bypass_policy=@__rocksdb_select_bypass_policy;

let $assert_cond= @bypass_rows_sent_3 - @bypass_rows_sent_2 =
                  @bypass_rows_sent_1 - @bypass_rows_sent_0;
--let $assert_text = Verify bypass and regular query return same number of rows
--source include/assert.inc

--let $assert_cond= $ROWS_READ_BYPASS <= $ROWS_READ_NO_BYPASS;
--let $assert_text = Verify bypass reads no more than regular query
--source include/assert.inc

--enable_query_log
