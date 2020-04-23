eval CREATE TABLE t1 (id INT,
    fake_id INT,
    bigfield $FIELD_TYPE,
    PRIMARY KEY (id),
    KEY bf (bigfield(32)),
    KEY fid (fake_id, bigfield(32))
  ) ENGINE=rocksdb CHARACTER SET $CHAR_SET COLLATE $COLLATION ;

INSERT INTO t1 VALUES (0, 1000, ''),
                      (1, 1001, REPEAT('a', 1)),
                      (8, 1008, REPEAT('b', 8)),
                      (24, 1024, REPEAT('c', 24)),
                      (30, 1030, REPEAT(' ', 30)),
                      (31, 1031, REPEAT('d', 31)),
                      (32, 1032, REPEAT('x', 32)),
                      (33, 1033, REPEAT('y', 33)),
                      (34, 1034, REPEAT(' ', 34)),
                      (128, 1128, REPEAT('z', 128));
SELECT id, bigfield FROM t1 IGNORE INDEX(fid) ORDER BY fake_id;

--let $prefix_index_check_title= Eligible for covering index optimization
--let $prefix_index_check_read_avoided_delta= 7
--let $prefix_index_check_query= SELECT id, bigfield FROM t1 FORCE INDEX(fid) WHERE fake_id <= 1032 ORDER BY fake_id
--source suite/rocksdb/include/prefix_index_only_query_check.inc

--let $prefix_index_check_title= Not eligible for covering index optimization
--let $prefix_index_check_read_avoided_delta= 0
--let $prefix_index_check_query= SELECT id, bigfield FROM t1 FORCE INDEX(fid) WHERE fake_id > 1032 ORDER BY fake_id
--source suite/rocksdb/include/prefix_index_only_query_check.inc
DROP TABLE t1;
