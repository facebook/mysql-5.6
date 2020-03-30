#
# Testcase for http://bugs.mysql.com/bug.php?id=71270
# (Failures to end bulk insert for partitioned tables handled incorrectly)
#
--source include/have_debug.inc
--source include/have_debug_sync.inc

SET SESSION DEBUG="+d,ha_partition_end_bulk_insert_fail";

CREATE TABLE t1 (id INT PRIMARY KEY);
INSERT INTO t1 VALUES (1),(2),(3);

--eval CREATE TABLE t2 (id INT PRIMARY KEY) ENGINE=$engine PARTITION BY HASH (id) PARTITIONS 2

--error ER_GET_ERRNO
INSERT INTO t2 SELECT * FROM t1;

DROP TABLE t1, t2;

SET SESSION DEBUG=default;
