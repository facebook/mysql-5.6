# ==== Usage ====
#
# [--let $warning_or_error= WARNING | ERROR ]
# --source include/assert_no_warnings.inc
#
# Parameters:
#
#   $warning_or_error
#     Whether we are expected to raise warning or error.
#

--disable_warnings
drop table if exists gap1,gap2,gap3;
--enable_warnings

CREATE DATABASE mysqlslap;
eval CREATE TABLE gap1 (id1 INT, id2 INT, id3 INT, c1 INT, value INT,
                      PRIMARY KEY (id1, id2, id3),
                      INDEX i (c1)) ENGINE=$engine;
CREATE TABLE gap2 like gap1;
eval CREATE TABLE gap3 (id INT, value INT,
                      PRIMARY KEY (id),
                      UNIQUE KEY ui(value)) ENGINE=$engine;
--disable_query_log
let $max = 1000;
let $i = 1;
while ($i <= $max) {
  eval INSERT INTO gap1 (id1, id2, id3, c1, value)
       VALUES ($i div 2, $i div 10, $i, $i, $i);
  eval INSERT INTO gap2 (id1, id2, id3, c1, value)
       VALUES ($i div 2, $i div 10, $i, $i, $i);
  inc $i;
}
--enable_query_log

insert into gap3 values (1,1), (2,2),(3,3),(4,4),(5,5);

eval create table gap4 (
  pk int primary key,
  a int,
  b int,
  key(a)
) ENGINE=$engine;
insert into gap4 values (1,1,1), (2,2,2), (3,3,3), (4,4,4);
create table gap5 like gap4;
insert into gap5 values (1,1,1), (2,2,2), (3,3,3), (4,4,4);

eval set session gap_lock_raise_error= $warning_or_error;
set session gap_lock_write_log=1;
