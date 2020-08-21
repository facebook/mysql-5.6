# Include file to decrease test code duplication

--eval $create_statement
--eval $insert_statement
--echo # State before failure
SHOW CREATE TABLE t1;
--sorted_result
SELECT * FROM t1;
SELECT PARTITION_NAME FROM information_schema.rocksdb_ddl
  WHERE TABLE_NAME="t1" ORDER BY PARTITION_NAME;
--sorted_result
--replace_column 13 # 14 # 15 # 16 # 17 # 18 # 19 # 20 # 21 #
SELECT * from information_schema.partitions WHERE TABLE_NAME="t1";

--disable_abort_on_error
--eval $fail_statement
--enable_abort_on_error
--echo # State after failure
SELECT PARTITION_NAME FROM information_schema.rocksdb_ddl
  WHERE TABLE_NAME="t1" ORDER BY PARTITION_NAME;
--sorted_result
--replace_column 13 # 14 # 15 # 16 # 17 # 18 # 19 # 20 # 21 #
SELECT * from information_schema.partitions WHERE TABLE_NAME="t1";

DROP TABLE t1;

--echo # Same test under LOCK TABLE
--eval $create_statement
--eval $insert_statement
--echo # State before failure
SHOW CREATE TABLE t1;
--sorted_result
SELECT * FROM t1;
SELECT PARTITION_NAME FROM information_schema.rocksdb_ddl
  WHERE TABLE_NAME="t1" ORDER BY PARTITION_NAME;
--sorted_result
--replace_column 13 # 14 # 15 # 16 # 17 # 18 # 19 # 20 # 21 #
SELECT * from information_schema.partitions WHERE TABLE_NAME="t1";

LOCK TABLE t1 WRITE;
--disable_abort_on_error
--eval $fail_statement
--enable_abort_on_error
--echo # State after failure
SHOW CREATE TABLE t1;
--sorted_result
SELECT * FROM t1;
SELECT PARTITION_NAME FROM information_schema.rocksdb_ddl
  WHERE TABLE_NAME="t1" ORDER BY PARTITION_NAME;
--sorted_result
--replace_column 13 # 14 # 15 # 16 # 17 # 18 # 19 # 20 # 21 #
SELECT * from information_schema.partitions WHERE TABLE_NAME="t1";
UNLOCK TABLES;
DROP TABLE t1;
