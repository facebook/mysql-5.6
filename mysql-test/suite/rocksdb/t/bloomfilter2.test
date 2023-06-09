--source include/have_rocksdb.inc

DELIMITER //;
CREATE PROCEDURE bloom_start()
BEGIN
  select sum(variable_value) into @u from performance_schema.global_status
  where variable_name in (
    'rocksdb_bloom_filter_prefix_useful',
    'rocksdb_last_level_seek_filter_match',
    'rocksdb_last_level_seek_filtered',
    'rocksdb_last_level_seek_data_useful_filter_match',
    'rocksdb_non_last_level_seek_filter_match',
    'rocksdb_non_last_level_seek_filtered',
    'rocksdb_non_last_level_seek_data_useful_filter_match');
END//
CREATE PROCEDURE bloom_eq_check()
BEGIN
  select case when sum(variable_value)-@u = 0 then 'true' else 'false' end as useful from performance_schema.global_status
  where variable_name in (
    'rocksdb_bloom_filter_prefix_useful',
    'rocksdb_last_level_seek_filter_match',
    'rocksdb_last_level_seek_filtered',
    'rocksdb_last_level_seek_data_useful_filter_match',
    'rocksdb_non_last_level_seek_filter_match',
    'rocksdb_non_last_level_seek_filtered',
    'rocksdb_non_last_level_seek_data_useful_filter_match');
END//
CREATE PROCEDURE bloom_gt_check()
BEGIN
  select case when sum(variable_value)-@u > 0 then 'true' else 'false' end as useful from performance_schema.global_status
  where variable_name in (
    'rocksdb_bloom_filter_prefix_useful',
    'rocksdb_last_level_seek_filter_match',
    'rocksdb_last_level_seek_filtered',
    'rocksdb_last_level_seek_data_useful_filter_match',
    'rocksdb_non_last_level_seek_filter_match',
    'rocksdb_non_last_level_seek_filtered',
    'rocksdb_non_last_level_seek_data_useful_filter_match');
END//
DELIMITER ;//

## Test 0: Eq cond len includs VARCHAR, and real cond len < prefix bloom len < VARCHAR definition len
CREATE TABLE t0 (id1 VARCHAR(30), id2 INT, value INT, PRIMARY KEY (id1, id2)) ENGINE=rocksdb collate latin1_bin;
--disable_query_log
let $i = 1;
while ($i <= 10000) {
  let $insert = INSERT INTO t0 VALUES('X', $i, $i);
  inc $i;
  eval $insert;
}
--enable_query_log

# BF not used
call bloom_start();
SELECT COUNT(*) FROM t0 WHERE id1='X' AND id2>=1;
call bloom_eq_check();

DROP TABLE t0;


## Test 1: Eq cond len is shorter than prefix bloom len
CREATE TABLE t1 (id1 BIGINT, id2 INT, id3 BIGINT, value INT, PRIMARY KEY (id1, id2, id3)) ENGINE=rocksdb;

--disable_query_log
let $i = 1;
while ($i <= 10000) {
  let $insert = INSERT INTO t1 VALUES(1, 1, $i, $i);
  eval $insert;
  inc $i;
}
--enable_query_log

# BF not used (4+8+4=16)
call bloom_start();
SELECT COUNT(*) FROM t1 WHERE id1=1 AND id2=1 AND id3>=2;
call bloom_eq_check();

# BF not used (4+8=12)
call bloom_start();
SELECT COUNT(*) FROM t1 WHERE id1=1 AND id2>=1 AND id3>=2;
call bloom_eq_check();

DROP TABLE t1;


## Test 2: Long IN and short IN (varchar) -- can_use_bloom_filter changes within the same query
CREATE TABLE t2 (id1 INT, id2 VARCHAR(100), id3 BIGINT, value INT, PRIMARY KEY (id1, id2, id3)) ENGINE=rocksdb collate latin1_bin;
--disable_query_log
let $i = 1;
while ($i <= 10000) {
  let $insert = INSERT INTO t2 VALUES($i, $i, $i, $i);
  inc $i;
  eval $insert;
}
--enable_query_log

# BF used for large cond, not used for short cond
call bloom_start();
select count(*) from t2 WHERE id1=100 and id2 IN ('00000000000000000000', '100');
call bloom_gt_check();

call bloom_start();
select count(*) from t2 WHERE id1=200 and id2 IN ('00000000000000000000', '200');
call bloom_gt_check();

# BF not used because cond length is too small in all cases
call bloom_start();
select count(*) from t2 WHERE id1=200 and id2 IN ('3', '200');
call bloom_eq_check();

DROP TABLE t2;


## Test 3: Eq cond len is longer than prefix bloom len
CREATE TABLE t3 (id1 BIGINT, id2 BIGINT, id3 BIGINT, id4 BIGINT, PRIMARY KEY (id1, id2, id3, id4)) ENGINE=rocksdb collate latin1_bin;
--disable_query_log
let $i = 1;
while ($i <= 10000) {
  if ($i != 5000) {
    let $insert = INSERT INTO t3 VALUES(1, $i, $i, $i);
    eval $insert;
  }
  inc $i;
}
--enable_query_log

# Full BF works with Get(), Block based does not.
call bloom_start();
SELECT COUNT(*) FROM t3 WHERE id1=1 AND id2=5000 AND id3=1 AND id4=1;
call bloom_gt_check();

# BF used (4+8+8+8)
call bloom_start();
SELECT COUNT(*) FROM t3 WHERE id1=1 AND id2=1 AND id3=1;
call bloom_gt_check();

call bloom_start();
SELECT COUNT(*) FROM t3 WHERE id1=1 AND id2=1 AND id3=1 AND id4 <= 500;
call bloom_gt_check();

DROP TABLE t3;

DROP PROCEDURE bloom_start;
DROP PROCEDURE bloom_eq_check;
DROP PROCEDURE bloom_gt_check;
