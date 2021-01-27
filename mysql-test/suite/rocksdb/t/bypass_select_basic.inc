--source include/have_rocksdb.inc

--source ../include/bypass_create_table.inc

--echo # Default
SELECT @@rocksdb_select_bypass_policy;
SELECT @@rocksdb_select_bypass_policy INTO @save_rocksdb_select_bypass_policy;

--echo # Always off
SELECT @@rocksdb_select_bypass_policy;
SET GLOBAL rocksdb_select_bypass_policy=0;
SELECT @@rocksdb_select_bypass_policy;

SELECT variable_value INTO @a FROM performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";

SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*+ no_bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;

SELECT variable_value INTO @b FROM performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";
--echo # Should be 0
SELECT @b-@a;

--echo # Always on
SELECT @@rocksdb_select_bypass_policy;
SET GLOBAL rocksdb_select_bypass_policy=1;
SELECT @@rocksdb_select_bypass_policy;

SELECT variable_value INTO @a FROM performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";

SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*+ abc */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*+ no_bypass*/ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;

SELECT variable_value INTO @b FROM performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";
--echo # Should be 4
SELECT @b-@a;

--echo # Opt-in
SELECT @@rocksdb_select_bypass_policy;
SET GLOBAL rocksdb_select_bypass_policy=2;
SELECT @@rocksdb_select_bypass_policy;

SELECT variable_value into @a FROM performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";

SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*+ no_bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;

SELECT /*+ bypass abc*/ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*+ bypass  */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*+ bypass*/ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /* + bypass abc*/ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*aaaaaaaaabbbbbbbbb*/ id1,id2,id1_type,id2_type,data,version
FROM link_table WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*+*/ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*+b*/ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*+byp*/ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*+bypw*/ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*-b*/ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /**/ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;

SELECT variable_value INTO @b FROM performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";
--echo # Should be 1
SELECT @b-@a;

--echo # Opt-out
select @@rocksdb_select_bypass_policy;
set global rocksdb_select_bypass_policy=3;
select @@rocksdb_select_bypass_policy;

SELECT variable_value INTO @a FROM performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";

SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*+ bypass  */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*+*/ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;

SELECT variable_value INTO @b FROM performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";
--echo # Should be 4
SELECT @b-@a;

SELECT /*+ no_bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*+ no_bypass*/ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*+no_bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*+no_bypassabc */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;

SELECT variable_value INTO @c FROM performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";
--echo # Should be 3
SELECT @c-@b;

--echo # Point Query Stats

SELECT variable_value INTO @bypass_executed_old FROM
performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";
SELECT variable_value INTO @bypass_rows_read_old FROM
performance_schema.global_status WHERE
variable_name="rocksdb_rows_read";
SELECT variable_value INTO @bypass_queries_point_old FROM
performance_schema.global_status WHERE
variable_name="rocksdb_queries_point";
SELECT variable_value INTO @bypass_queries_range_old FROM
performance_schema.global_status WHERE
variable_name="rocksdb_queries_range";
SELECT variable_value INTO @bypass_rows_sent_old FROM
performance_schema.global_status WHERE
variable_name="rows_sent";

SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1 IN (1) AND id2 IN (2) AND link_type=3;
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1 IN (1, 2) AND id2 IN (2, 3, 4) AND link_type=3;

SELECT variable_value INTO @bypass_executed_new FROM
performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";
SELECT variable_value INTO @bypass_rows_read_new FROM
performance_schema.global_status WHERE
variable_name="rocksdb_rows_read";
SELECT variable_value INTO @bypass_queries_point_new FROM
performance_schema.global_status WHERE
variable_name="rocksdb_queries_point";
SELECT variable_value INTO @bypass_queries_range_new FROM
performance_schema.global_status WHERE
variable_name="rocksdb_queries_range";
SELECT variable_value INTO @bypass_rows_sent_new FROM
performance_schema.global_status WHERE
variable_name="rows_sent";

SELECT @bypass_executed_new - @bypass_executed_old;
SELECT @bypass_rows_read_new - @bypass_rows_read_old;
SELECT @bypass_queries_point_new - @bypass_queries_point_old;
SELECT @bypass_queries_range_new - @bypass_queries_range_old;
SELECT @bypass_rows_sent_new - @bypass_rows_sent_old;

--echo # Range Query Stats

SELECT variable_value INTO @bypass_executed_old FROM
performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";
SELECT variable_value INTO @bypass_rows_read_old FROM
performance_schema.global_status WHERE
variable_name="rocksdb_rows_read";
SELECT variable_value INTO @bypass_queries_point_old FROM
performance_schema.global_status WHERE
variable_name="rocksdb_queries_point";
SELECT variable_value INTO @bypass_queries_range_old FROM
performance_schema.global_status WHERE
variable_name="rocksdb_queries_range";
SELECT variable_value INTO @bypass_rows_sent_old FROM
performance_schema.global_status WHERE
variable_name="rows_sent";

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 10;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 5;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 1;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 0;

SELECT variable_value INTO @bypass_executed_new FROM
performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";
SELECT variable_value INTO @bypass_rows_read_new FROM
performance_schema.global_status WHERE
variable_name="rocksdb_rows_read";
SELECT variable_value INTO @bypass_queries_point_new FROM
performance_schema.global_status WHERE
variable_name="rocksdb_queries_point";
SELECT variable_value INTO @bypass_queries_range_new FROM
performance_schema.global_status WHERE
variable_name="rocksdb_queries_range";
SELECT variable_value INTO @bypass_rows_sent_new FROM
performance_schema.global_status WHERE
variable_name="rows_sent";

SELECT @bypass_executed_new - @bypass_executed_old;
SELECT @bypass_rows_read_new - @bypass_rows_read_old;
SELECT @bypass_queries_point_new - @bypass_queries_point_old;
SELECT @bypass_queries_range_new - @bypass_queries_range_old;
SELECT @bypass_rows_sent_new - @bypass_rows_sent_old;

--echo # Point query
let bypass_query=
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
--source ../include/verify_bypass_query.inc
let bypass_query=
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2 IN (2, 3, 4) AND link_type=3;
--source ../include/verify_bypass_query.inc
let bypass_query=
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2 IN (2) AND link_type=3;
--source ../include/verify_bypass_query.inc
let bypass_query=
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1 IN (1) AND id2 IN (2) AND link_type=3;
--source ../include/verify_bypass_query.inc
let bypass_query=
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1 IN (1, 2) AND id2 IN (2, 3, 4) AND link_type=3;
--source ../include/verify_bypass_query.inc

--echo # Prefix range query

--echo # Prefix range query with SK
let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND time = 10
ORDER BY TIME DESC LIMIT 10;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND time = 10
ORDER BY TIME ASC LIMIT 10;
--source ../include/verify_bypass_query.inc

--echo # Prefix range query with SK with limits
let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 10;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 5;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 1;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 0;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 0,10;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 0,5;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 0,1;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 1,0;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 1,10;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 1,5;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 1,1;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 1,0;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 5,10;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 5,5;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 5,1;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 5,0;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 10,10;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 10,5;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 10,1;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 10,0;
--source ../include/verify_bypass_query.inc

--echo # Prefix range query with PK
let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type FROM link_table FORCE INDEX (PRIMARY)
WHERE link_type=3 AND id1=1 ORDER BY id2 DESC;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type FROM link_table FORCE INDEX (PRIMARY)
WHERE link_type=3 AND id1=1 ORDER BY id2 ASC;
--source ../include/verify_bypass_query.inc

--echo # Prefix range query with PK + value
let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (PRIMARY)
WHERE link_type=3 AND id1=1 ORDER BY id2 DESC;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (PRIMARY)
WHERE link_type=3 AND id1=1 ORDER BY id2 ASC;
--source ../include/verify_bypass_query.inc

--echo # Transaction
BEGIN;
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
UPDATE link_table set data="bcd" WHERE id1=1 AND id2=2 AND link_type = 3;
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
COMMIT;

BEGIN;
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
UPDATE link_table set data="cde" WHERE id1=1 AND id2=2 AND link_type = 3;
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
ROLLBACK;

SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;

--echo # Data types
SELECT /*+ bypass */ id1 FROM link_table where link_type="3";
SELECT /*+ bypass */ id1 FROM link_table where link_type="3" AND id1="1";
SELECT /*+ bypass */ id1 FROM link_table where link_type="3" AND id1=True;
SELECT /*+ bypass */ id1 FROM link_table where link_type="3" AND id1=b'1';
SELECT /*+ bypass */ id1 FROM link_table where link_type="3" AND id1=x'01';
SELECT /*+ bypass */ id1 FROM link_table where link_type="3" AND id1=NULL;

--echo # Full table scan
let bypass_query=
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1_type=1;
--source ../include/verify_bypass_query.inc

# 15+ fields
CREATE TABLE t5 (pk int primary key,
                 a int, b int, c int, d int, e int, f int, g int, h int,
                 i int, j int, k int, l int, m int, n int, o int, p int);
let $1=5;
--disable_query_log
while ($1)
{
 eval insert into t5 values(
     $1, $1+1, $1+2, $1+3, $1+4, $1+5, $1+6, $1+7, $1+8, $1+9, $1+10,
     $1+11, $1+12, $1+13, $1+14, $1+15, $1+16);
 dec $1;
}
--enable_query_log

# 15+ select expressions
let bypass_query=
SELECT /*+ bypass */ a,a,a,a,a, a,a,a,a,a, a,a,a,a,a, a,a FROM t5 WHERE pk=1;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p FROM t5
WHERE pk<4;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p,
                     a, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p FROM t5
                     WHERE pk >= 2;
--source ../include/verify_bypass_query.inc

DROP TABLE t5;

--echo MultiGet

select @@rocksdb_select_bypass_multiget_min into
    @save_rocksdb_select_bypass_multiget_min;
set global rocksdb_select_bypass_multiget_min=0;

let bypass_query=
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version from link_table
WHERE id1=1 and id2 IN (1) and link_type=3;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version from link_table
WHERE id1=1 and id2 IN (1, 2, 3) and link_type=3 ORDER BY link_type, id1, id2;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version from link_table
WHERE id1=1 and id2 IN (1, 2, 3, 4, 5) and link_type=3
ORDER BY link_type, id1, id2;
--source ../include/verify_bypass_query.inc

BEGIN;
let bypass_query=
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version from link_table
WHERE id1=1 and id2 IN (1, 2, 3, 4, 5) and link_type=3
ORDER BY link_type, id1, id2;
--source ../include/verify_bypass_query.inc
ROLLBACK;

BEGIN;
UPDATE link_table set data="cde" WHERE id1=1 and id2=2 and link_type = 3;
let bypass_query=
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version from link_table
WHERE id1=1 and id2 IN (1, 2, 3, 4, 5) and link_type=3
ORDER BY link_type, id1, id2;
--source ../include/verify_bypass_query.inc
ROLLBACK;

let bypass_query=
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version from link_table
WHERE id1=1 and id2 IN (1, 2, 3, 4, 5) and link_type=3
ORDER BY link_type, id1, id2;
--source ../include/verify_bypass_query.inc

set global rocksdb_select_bypass_multiget_min=
    @save_rocksdb_select_bypass_multiget_min;

--echo # SHOW PROCESSLIST and KILL
connect (conn1, localhost, root,,);
connection conn1;
let $ID= `select connection_id()`;

select @@rocksdb_select_bypass_debug_row_delay;
set global rocksdb_select_bypass_debug_row_delay=1;
select @@rocksdb_select_bypass_debug_row_delay;

send select /*+ bypass */ id1 from link_table where link_type=3;

connection default;
let $wait_timeout= 10;
let $wait_condition=select count(*)=1 from information_schema.processlist
where info='select /*+ bypass */ id1 from link_table where link_type=3';
--source include/wait_condition.inc

disable_query_log;
eval kill $ID;
enable_query_log;
disconnect conn1;
let $wait_timeout= 10;
let $wait_condition=select count(*)=0 from information_schema.processlist
where info='select /*+ bypass */ id1 from link_table where link_type=3';
--source include/wait_condition.inc

select @@rocksdb_select_bypass_debug_row_delay;
set global rocksdb_select_bypass_debug_row_delay=0;
select @@rocksdb_select_bypass_debug_row_delay;

--source ../include/bypass_drop_table.inc

SELECT @@rocksdb_select_bypass_policy;
set global rocksdb_select_bypass_policy=@save_rocksdb_select_bypass_policy;
SELECT @@rocksdb_select_bypass_policy;

