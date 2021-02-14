# Verify SHOW PROCESS LIST AND SHOW ENGINE STATUS under show_query_digest
#
# Parameters:
#   $engine - INNODB or ROCKSDB
#

#
# SHOW PROCESSLIST with show_query_digest=1
#
SET @show_query_digest_save = @@show_query_digest;
SET show_query_digest=1;

# Need to specify engine explicitly otherwise in InnoDB default engine is MyIASM
--eval CREATE TABLE t1 (pk int primary key, a int, b int, c int) ENGINE=$engine;
SHOW CREATE TABLE t1;
INSERT INTO t1 values (1, 1, 1, 1), (2, 2, 2, 2), (3, 3, 3, 3);

START TRANSACTION;
SELECT * FROM t1 WHERE pk=3 FOR UPDATE;
--connect (con1,localhost,root,,)
--connection con1
START TRANSACTION;
SELECT * FROM t1 WHERE pk=1;
--send SELECT 1+2, "abc", a, b, c FROM t1 WHERE PK = 3 FOR UPDATE;
--connection default
let $wait_timeout= 10;
let $wait_condition= SELECT COUNT(*) FROM INFORMATION_SCHEMA.PROCESSLIST
                     WHERE STATE LIKE 'Waiting for row lock' OR
                           STATE LIKE 'statistics'
                     AND ID != CONNECTION_ID();
--source include/wait_condition.inc
--replace_column 1 ### 3 ### 6 ### 9 ### 10 ### 11 ### 12 ###
--replace_result 'Query attributes' 'Query'
--sorted_result
SHOW PROCESSLIST;
SELECT STATE, INFO FROM INFORMATION_SCHEMA.PROCESSLIST
WHERE ID != CONNECTION_ID();

--echo Searching for digest query in SHOW ENGINE $engine TRANSACTION STATUS;
#--replace_regex /[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2} [0-9a-f]+/TIMESTAMP TID/ /0x[a-f0-9]+/PTR/ /[0-9]+/#/
--output $MYSQL_TMP_DIR/show_transaction_status
--eval SHOW ENGINE $engine TRANSACTION STATUS;
--exec grep "SELECT ? + ?, ... , \`a\` , \`b\` , \`c\` FROM \`t1\` WHERE \`PK\` = ? FOR UPDATE" $MYSQL_TMP_DIR/show_transaction_status

if ($engine == INNODB) {
  --echo Search for skipped records
  --exec grep "record skipped" $MYSQL_TMP_DIR/show_transaction_status
}

COMMIT;
--connection con1
--reap
--disconnect con1
--source include/wait_until_disconnected.inc
--connection default

# Cleanup
--remove_file $MYSQL_TMP_DIR/show_transaction_status

DROP TABLE t1;
SET show_query_digest=@show_query_digest_save;
