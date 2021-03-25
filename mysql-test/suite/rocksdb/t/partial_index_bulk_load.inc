SET rocksdb_bulk_load_allow_sk=1;
SET rocksdb_bulk_load=1;

--disable_query_log
let $i = 1;
while ($i <= 10) {
  let $j = 1;
  while ($j <= $i) {
    if ($varchar) {
      eval INSERT INTO t VALUES (REPEAT("1", $i), REPEAT("2", $j), REPEAT("3", $j), "12345");
    }
    if (!$varchar) {
      eval INSERT INTO t VALUES ($i, $j, 10 - $j, 12345);
    }
    inc $j;
  }
  inc $i;
}
--enable_query_log

SET rocksdb_bulk_load=0;

CREATE TEMPORARY TABLE t1 AS
SELECT * FROM performance_schema.global_status
WHERE variable_name LIKE 'rocksdb_partial_index%';

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1);
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2);
--source include/diff_queries.inc

CREATE TEMPORARY TABLE t2 AS
SELECT * FROM performance_schema.global_status
WHERE variable_name LIKE 'rocksdb_partial_index%';

--let $assert_text = Check that materialized groups are zero.
--let $assert_cond = [SELECT t2.variable_value - t1.variable_value AS diff FROM t1 JOIN t2 USING (variable_name) WHERE variable_name = "rocksdb_partial_index_groups_materialized", diff, 1] = 0
--source include/assert.inc
--let $assert_text = Check that materialized rows are zero.
--let $assert_cond = [SELECT t2.variable_value - t1.variable_value AS diff FROM t1 JOIN t2 USING (variable_name) WHERE variable_name = "rocksdb_partial_index_rows_materialized", diff, 1] = 0
--source include/assert.inc
--let $assert_text = Check that sorted groups are non-zero.
--let $assert_cond = [SELECT t2.variable_value - t1.variable_value AS diff FROM t1 JOIN t2 USING (variable_name) WHERE variable_name = "rocksdb_partial_index_groups_sorted", diff, 1] > 0
--source include/assert.inc
--let $assert_text = Check that sorted rows are non-zero.
--let $assert_cond = [SELECT t2.variable_value - t1.variable_value AS diff FROM t1 JOIN t2 USING (variable_name) WHERE variable_name = "rocksdb_partial_index_rows_sorted", diff, 1] > 0
--source include/assert.inc

DROP TABLE t1, t2;

--let $comment=`select index_comment from information_schema.statistics where table_schema = 'test' and table_name = 't' limit 1`
ALTER TABLE t DROP INDEX ik1;
--eval ALTER TABLE t ADD INDEX ik1 (i, k) COMMENT '$comment'

CREATE TEMPORARY TABLE t1 AS
SELECT * FROM performance_schema.global_status
WHERE variable_name LIKE 'rocksdb_partial_index%';

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1);
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2);
--source include/diff_queries.inc

CREATE TEMPORARY TABLE t2 AS
SELECT * FROM performance_schema.global_status
WHERE variable_name LIKE 'rocksdb_partial_index%';

--let $assert_text = Check that materialized groups are zero.
--let $assert_cond = [SELECT t2.variable_value - t1.variable_value AS diff FROM t1 JOIN t2 USING (variable_name) WHERE variable_name = "rocksdb_partial_index_groups_materialized", diff, 1] = 0
--source include/assert.inc
--let $assert_text = Check that materialized rows are zero.
--let $assert_cond = [SELECT t2.variable_value - t1.variable_value AS diff FROM t1 JOIN t2 USING (variable_name) WHERE variable_name = "rocksdb_partial_index_rows_materialized", diff, 1] = 0
--source include/assert.inc
--let $assert_text = Check that sorted groups are non-zero.
--let $assert_cond = [SELECT t2.variable_value - t1.variable_value AS diff FROM t1 JOIN t2 USING (variable_name) WHERE variable_name = "rocksdb_partial_index_groups_sorted", diff, 1] > 0
--source include/assert.inc
--let $assert_text = Check that sorted rows are non-zero.
--let $assert_cond = [SELECT t2.variable_value - t1.variable_value AS diff FROM t1 JOIN t2 USING (variable_name) WHERE variable_name = "rocksdb_partial_index_rows_sorted", diff, 1] > 0
--source include/assert.inc

DROP TABLE t1, t2;
