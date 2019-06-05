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

--echo # Point query
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2=2 AND link_type=3;
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2 IN (2, 3, 4) AND link_type=3;
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1=1 AND id2 IN (2) AND link_type=3;
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1 IN (1) AND id2 IN (2) AND link_type=3;
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1 IN (1, 2) AND id2 IN (2, 3, 4) AND link_type=3;

--echo # Prefix range query

--echo # Prefix range query with SK
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND time = 10
ORDER BY TIME DESC LIMIT 10;
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND time = 10
ORDER BY TIME ASC LIMIT 10;

--echo # Prefix range query with SK with limits
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

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 0,10;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 0,5;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 0,1;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 1,0;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 1,10;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 1,5;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 1,1;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 1,0;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 5,10;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 5,5;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 5,1;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 5,0;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 10,10;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 10,5;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 10,1;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND time >= 10
ORDER BY TIME DESC LIMIT 10,0;

--echo # Prefix range query with PK
SELECT /*+ bypass */ id1, id2, link_type FROM link_table FORCE INDEX (PRIMARY)
WHERE link_type=3 AND id1=1 ORDER BY id2 DESC;
SELECT /*+ bypass */ id1, id2, link_type FROM link_table FORCE INDEX (PRIMARY)
WHERE link_type=3 AND id1=1 ORDER BY id2 ASC;

--echo # Prefix range query with PK + value
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (PRIMARY)
WHERE link_type=3 AND id1=1 ORDER BY id2 DESC;
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (PRIMARY)
WHERE link_type=3 AND id1=1 ORDER BY id2 ASC;

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
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version FROM link_table
WHERE id1_type=1;

--echo MultiGet

select @@rocksdb_select_bypass_multiget_min into
    @save_rocksdb_select_bypass_multiget_min;
set global rocksdb_select_bypass_multiget_min=0;

SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version from link_table
WHERE id1=1 and id2 IN (1) and link_type=3;
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version from link_table
WHERE id1=1 and id2 IN (1, 2, 3) and link_type=3;
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version from link_table
WHERE id1=1 and id2 IN (1, 2, 3, 4, 5) and link_type=3;

BEGIN;
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version from link_table
WHERE id1=1 and id2 IN (1, 2, 3, 4, 5) and link_type=3;
ROLLBACK;

BEGIN;
UPDATE link_table set data="cde" WHERE id1=1 and id2=2 and link_type = 3;
SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version from link_table
WHERE id1=1 and id2 IN (1, 2, 3, 4, 5) and link_type=3;
ROLLBACK;

SELECT /*+ bypass */ id1,id2,id1_type,id2_type,data,version from link_table
WHERE id1=1 and id2 IN (1, 2, 3, 4, 5) and link_type=3;

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

DROP TABLE count_table;
DROP TABLE link_table;
DROP TABLE link_table3;
DROP TABLE link_table2;
DROP TABLE id_table;
DROP TABLE node_table;
DROP TABLE link_table5;
DROP TABLE link_table6;
DROP TABLE link_table4;

SELECT @@rocksdb_select_bypass_policy;
set global rocksdb_select_bypass_policy=@save_rocksdb_select_bypass_policy;
SELECT @@rocksdb_select_bypass_policy;

