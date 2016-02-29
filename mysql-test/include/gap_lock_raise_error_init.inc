--disable_warnings
drop table if exists t1,t2,t3;
--enable_warnings

CREATE DATABASE mysqlslap;
eval CREATE TABLE t1 (id1 INT, id2 INT, id3 INT, c1 INT, value INT,
                      PRIMARY KEY (id1, id2, id3),
                      INDEX i (c1)) ENGINE=$engine;
CREATE TABLE t2 like t1;
eval CREATE TABLE t3 (id INT, value INT,
                      PRIMARY KEY (id),
                      UNIQUE KEY ui(value)) ENGINE=$engine;
--disable_query_log
let $max = 1000;
let $i = 1;
while ($i <= $max) {
  eval INSERT INTO t1 (id1, id2, id3, c1, value)
       VALUES ($i div 2, $i div 10, $i, $i, $i);
  eval INSERT INTO t2 (id1, id2, id3, c1, value)
       VALUES ($i div 2, $i div 10, $i, $i, $i);
  inc $i;
}
--enable_query_log

insert into t3 values (1,1), (2,2),(3,3),(4,4),(5,5);

set session gap_lock_raise_error=1;
set session gap_lock_write_log=1;
