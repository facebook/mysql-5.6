eval
CREATE TABLE t (i varchar(64), j varchar(64), k varchar(64), l varchar(64),
  PRIMARY KEY (i, j),
  KEY ik1 (i, k) COMMENT 'cfname=$cfname;partial_group_keyparts=1;partial_group_threshold=5',
  KEY ik2 (i, k) COMMENT 'cfname=$cfname'
) ENGINE=ROCKSDB;

INSERT INTO t values ("1", "1", "2", "1");
INSERT INTO t values ("1", "2", "1", "1");

INSERT INTO t values ("11111111", "1", "9", "1");
INSERT INTO t values ("11111111", "2", "8", "1");
INSERT INTO t values ("11111111", "3", "7", "1");
INSERT INTO t values ("11111111", "4", "5", "1");
INSERT INTO t values ("11111111", "5", "4", "1");
INSERT INTO t values ("11111111", "6", "2", "1");

INSERT INTO t values ("111111111", "1", "9", "1");
INSERT INTO t values ("111111111", "2", "2", "1");

INSERT INTO t values ("11111112", "1", "1", "1");

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i = "1" ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i = "1" ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i = "11111111" ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i = "11111111" ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i = "111111110" ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i = "111111110" ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i = "11111112" ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i = "11111112" ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i = "11111111" AND k < "5" ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i = "11111111" AND k < "5" ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i = "11111111" AND k > "2" ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i = "11111111" AND k > "2" ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i = "11111111" AND k > "2" AND k < "5" ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i = "11111111" AND k > "2" AND k < "5" ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i = "11111111" AND k > "7" AND k < "9" ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i = "11111111" AND k > "7" AND k < "9" ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i < "111111110" ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i < "111111110" ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i > "111111110" ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i > "111111110" ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i >= "111111110" ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i >= "111111110" ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i <= "111111110" ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i <= "111111110" ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i LIKE "1%" ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i LIKE "1%" ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i LIKE "11111111%" ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i LIKE "11111111%" ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

DROP TABLE t;

eval
CREATE TABLE t (i int, j int, k int, l int,
  PRIMARY KEY (i, j),
  KEY ik1 (i, k) COMMENT 'cfname=$cfname;partial_group_keyparts=1;partial_group_threshold=5',
  KEY ik2 (i, k) COMMENT 'cfname=$cfname'
) ENGINE=ROCKSDB;

INSERT INTO t values (1, 1, 2, 1);
INSERT INTO t values (1, 2, 1, 1);

INSERT INTO t values (2, 1, 9, 1);
INSERT INTO t values (2, 2, 8, 1);
INSERT INTO t values (2, 3, 7, 1);
INSERT INTO t values (2, 4, 5, 1);
INSERT INTO t values (2, 5, 4, 1);
INSERT INTO t values (2, 6, 2, 1);

INSERT INTO t values (4, 1, 1, 1);

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i = 1 ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i = 1 ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i = 2 ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i = 2 ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i = 3 ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i = 3 ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i = 4 ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i = 4 ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i = 2 AND k < 5 ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i = 2 AND k < 5 ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i = 2 AND k > 2 ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i = 2 AND k > 2 ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i = 2 AND k > 2 AND k < 5 ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i = 2 AND k > 2 AND k < 5 ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i = 2 AND k > 7 AND k < 9 ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i = 2 AND k > 7 AND k < 9 ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i < 3 ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i < 3 ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i > 3 ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i > 3 ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i >= 3 ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i >= 3 ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

--let $query1= SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i <= 3 ORDER BY i $asc , k $asc;
--let $query2= SELECT i, j, k FROM t FORCE INDEX (ik2) WHERE i <= 3 ORDER BY i $asc , k $asc;
--source include/diff_queries.inc

set rocksdb_partial_index_sort_max_mem = 1;
--error ER_OUT_OF_RESOURCES
--eval SELECT i, j, k FROM t FORCE INDEX (ik1) WHERE i = 1 ORDER BY i $asc , k $asc
set rocksdb_partial_index_sort_max_mem = DEFAULT;

DROP TABLE t;

