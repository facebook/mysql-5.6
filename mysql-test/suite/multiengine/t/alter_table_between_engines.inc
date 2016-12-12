eval CREATE TABLE t1 (pk INT PRIMARY KEY, a CHAR(30), b INT, KEY(a, b))
  ENGINE=$src_engine COLLATE utf8_bin;
CREATE TABLE t2 LIKE t1;
INSERT INTO t2 VALUES(1, "1", 1);
# Check flush status
# https://github.com/mysql/mysql-server/commit/60b110ae651cb452c355b812b248fcc220e85eb4
FLUSH STATUS;
eval ALTER TABLE t2 ENGINE=$dst_engine;
SELECT * FROM t2;

DROP TABLE t1, t2;
