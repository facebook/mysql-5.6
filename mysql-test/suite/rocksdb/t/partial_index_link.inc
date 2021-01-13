--let $binary_id1=1
if (`select DATA_TYPE = 'binary' from information_schema.columns where TABLE_NAME = 'link_table' and TABLE_SCHEMA = 'test' and COLUMN_NAME = 'id1'`) {
  --let $binary_id1="1"
}
--let $text=`select DATA_TYPE = 'text' from information_schema.columns where TABLE_NAME = 'link_table' and TABLE_SCHEMA = 'test' and COLUMN_NAME = 'data'`

# This creates 10 distinct types, with up to 9 distinct id1s per type, to give up to 90 groups.
--disable_query_log
let $i=0;
while ($i < 1000)
{
  if ($text) {
    eval INSERT INTO link_table VALUES (FLOOR(RAND($i) * 9), 123, $i, 456, FLOOR($i / 100), FLOOR(RAND($i) * 2), REPEAT("1234567890", FLOOR(RAND($i) * 50)), FLOOR(RAND($i) * 100000), 789);
  }
  if (!$text) {
    eval INSERT INTO link_table VALUES (FLOOR(RAND($i) * 9), 123, $i, 456, FLOOR($i / 100), FLOOR(RAND($i) * 2), REPEAT("1234567890", FLOOR(RAND($i) * 20)), FLOOR(RAND($i) * 100000), 789);
  }
  inc $i;
}
--enable_query_log

let $i=0;
while ($i < 10) {
  # This gives a range plan
  --let $query1= SELECT id1, id2, link_type, visibility, data, time, version FROM link_table FORCE INDEX (id1_type) WHERE link_type = $i AND id1 = $binary_id1 AND visibility = 1 AND time >= 100 AND time <= 50000 ORDER BY time DESC, id2 DESC LIMIT 10000
  --let $query2= SELECT id1, id2, link_type, visibility, data, time, version FROM link_table FORCE INDEX (id1_type2) WHERE link_type = $i AND id1 = $binary_id1 AND visibility = 1 AND time >= 100 AND time <= 50000 ORDER BY time DESC, id2 DESC LIMIT 10000
  --source include/diff_queries.inc

  # This gives a ref plan
  --let $query1= SELECT id1, id2, link_type, visibility, data, time, version FROM link_table FORCE INDEX (id1_type) WHERE link_type = $i AND id1 = $binary_id1 AND visibility = 0 AND time >= 0 AND time <= 4294967295 ORDER BY time DESC, id2 DESC LIMIT 10000
  --let $query2= SELECT id1, id2, link_type, visibility, data, time, version FROM link_table FORCE INDEX (id1_type2) WHERE link_type = $i AND id1 = $binary_id1 AND visibility = 0 AND time >= 0 AND time <= 4294967295 ORDER BY time DESC, id2 DESC LIMIT 10000
  --source include/diff_queries.inc

  inc $i;
}

let $i=0;
while ($i < 10) {
  --let $query1= SELECT id1, id2, link_type, visibility, data, time, version FROM link_table FORCE INDEX (id1_type) WHERE link_type = 1
  --let $query2= SELECT id1, id2, link_type, visibility, data, time, version FROM link_table FORCE INDEX (id1_type2) WHERE link_type = 1
  --source include/diff_queries.inc

  --let $query1= SELECT id1, id2, link_type, visibility, data, time, version FROM link_table FORCE INDEX (id1_type) WHERE link_type <= 2
  --let $query2= SELECT id1, id2, link_type, visibility, data, time, version FROM link_table FORCE INDEX (id1_type2) WHERE link_type <= 2
  --source include/diff_queries.inc

  inc $i;
}

# Rebuild the table so that nothing is materialized anymore.
ALTER TABLE link_table ENGINE=ROCKSDB;

CREATE TEMPORARY TABLE t1 AS
SELECT * FROM performance_schema.global_status
WHERE variable_name LIKE 'rocksdb_partial_index%';

--let $query1= SELECT id1, id2, link_type, visibility, time, version FROM link_table FORCE INDEX (id1_type)
--let $query2= SELECT id1, id2, link_type, visibility, time, version FROM link_table FORCE INDEX (id1_type2)
--source include/diff_queries.inc

CREATE TEMPORARY TABLE t2 AS
SELECT * FROM performance_schema.global_status
WHERE variable_name LIKE 'rocksdb_partial_index%';

SELECT variable_name, t2.variable_value - t1.variable_value AS diff FROM t1 JOIN t2 USING (variable_name);
--let $assert_text = Check that materialized groups are non-zero.
--let $assert_cond = [SELECT t2.variable_value - t1.variable_value AS diff FROM t1 JOIN t2 USING (variable_name) WHERE variable_name = "rocksdb_partial_index_groups_materialized", diff, 1] > 0
--source include/assert.inc
--let $assert_text = Check that materialized rows are non-zero.
--let $assert_cond = [SELECT t2.variable_value - t1.variable_value AS diff FROM t1 JOIN t2 USING (variable_name) WHERE variable_name = "rocksdb_partial_index_rows_materialized", diff, 1] > 0
--source include/assert.inc
DROP TABLE t1, t2;

# Rerun full index scan a second time, and check that no materialization occurs
CREATE TEMPORARY TABLE t1 AS
SELECT * FROM performance_schema.global_status
WHERE variable_name LIKE 'rocksdb_partial_index%';

--disable_result_log
SELECT id1, id2, link_type, visibility, time, version FROM link_table FORCE INDEX (id1_type2);
--enable_result_log

CREATE TEMPORARY TABLE t2 AS
SELECT * FROM performance_schema.global_status
WHERE variable_name LIKE 'rocksdb_partial_index%';

SELECT variable_name, t2.variable_value - t1.variable_value AS diff FROM t1 JOIN t2 USING (variable_name);
--let $assert_text = Check that materialized groups are zero.
--let $assert_cond = [SELECT t2.variable_value - t1.variable_value AS diff FROM t1 JOIN t2 USING (variable_name) WHERE variable_name = "rocksdb_partial_index_groups_materialized", diff, 1] = 0
--source include/assert.inc
--let $assert_text = Check that materialized rows are zero.
--let $assert_cond = [SELECT t2.variable_value - t1.variable_value AS diff FROM t1 JOIN t2 USING (variable_name) WHERE variable_name = "rocksdb_partial_index_rows_materialized", diff, 1] = 0
--source include/assert.inc

DROP TABLE t1, t2;
