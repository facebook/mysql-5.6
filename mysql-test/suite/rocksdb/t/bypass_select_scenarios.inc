--source ../include/bypass_create_table.inc

select @@rocksdb_select_bypass_policy;
select @@rocksdb_select_bypass_policy into @save_rocksdb_select_bypass_policy;
set global rocksdb_select_bypass_policy=2;
select @@rocksdb_select_bypass_policy;

SELECT variable_value into @executed_start
FROM performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";

# Range query, flag = 0
--echo # Reverse Column family + ascending

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND TIME = 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND TIME = 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 12
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 12
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 11
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 11
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 9
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 9
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 16
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 16
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 16
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 16
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 7 AND TIME <= 8
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 7 AND TIME <= 8
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 5 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 5 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1
ORDER BY visibility ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1
ORDER BY visibility ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE id1 = 1
ORDER BY link_type ASC, visibility ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE id1 = 1
ORDER BY link_type ASC, visibility ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

--echo # Reverse Column family + descending
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND TIME = 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND TIME = 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 12
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 12
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 11
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 11
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 9
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 9
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 16
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 16
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 16
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 16
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 7 AND TIME <= 8
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 7 AND TIME <= 8
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 5 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 5 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1
ORDER BY visibility DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1
ORDER BY visibility DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE id1 = 1
ORDER BY link_type DESC, visibility DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE id1 = 1
ORDER BY link_type DESC, visibility DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

--echo # column family + ascending
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND TIME = 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND TIME = 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 12
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 12
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 11
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 11
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 9
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 9
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 16
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 16
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 16
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 16
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 7 AND TIME <= 8
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 7 AND TIME <= 8
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 5 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 5 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1
ORDER BY visibility ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1
ORDER BY visibility ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE id1 = 1
ORDER BY link_type ASC, visibility ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE id1 = 1
ORDER BY link_type ASC, visibility ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

--echo # column family + descending
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND TIME = 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND TIME = 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 12
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 12
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 11
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 11
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 9
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 9
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 10
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 16
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 16
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 16
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 16
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 7 AND TIME <= 8
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 7 AND TIME <= 8
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 5 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 5 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1
ORDER BY visibility DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1
ORDER BY visibility DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE id1 = 1
ORDER BY link_type DESC, visibility DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE id1 = 1
ORDER BY link_type DESC, visibility DESC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME DESC, id2 DESC LIMIT 0, 1000;

--echo # column family + ascending
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND TIME = 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND TIME = 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 12
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 12
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 11
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 11
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 9
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 9
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 10
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 16
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 16
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 16
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 16
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 7 AND TIME <= 8
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 7 AND TIME <= 8
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 5 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 5 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME ASC, id2 ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1
ORDER BY visibility ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1
ORDER BY visibility ASC LIMIT 0, 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE id1 = 1
ORDER BY link_type ASC, visibility ASC LIMIT 0, 1000;

SELECT id1, id2, link_type, visibility, data, TIME, version
FROM link_table2 FORCE INDEX (id1_type)
WHERE id1 = 1
ORDER BY link_type ASC, visibility ASC LIMIT 0, 1000;

--echo # Range query, flag = 1
--echo # Ascending
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table5 FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 1 AND visibility = 1
ORDER BY id2 ASC LIMIT 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table5 FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 1 AND visibility = 2
ORDER BY id2 ASC LIMIT 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table5 FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 1 AND visibility = 3
ORDER BY id2 ASC LIMIT 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table5 FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND visibility = 1
ORDER BY id2 ASC LIMIT 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table5 FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND visibility = 2
ORDER BY id2 ASC LIMIT 1000;

--echo # Dscending
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table5 FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 1 AND visibility = 1
ORDER BY id2 DESC LIMIT 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table5 FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 1 AND visibility = 2
ORDER BY id2 DESC LIMIT 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table5 FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 1 AND visibility = 3
ORDER BY id2 DESC LIMIT 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table5 FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND visibility = 1
ORDER BY id2 DESC LIMIT 1000;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table5 FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND visibility = 2
ORDER BY id2 DESC LIMIT 1000;

--echo # Range query, binary key
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table4 FORCE INDEX (id1_type)
WHERE link_type = 1 AND id1 = "a1\0\0\0\0\0\0\0\0\0\0\0\0\0\0" AND
      visibility = 1  AND time >= 0 AND time <= 4294967295
ORDER BY time DESC, id2 DESC LIMIT 0, 1000;
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table4 FORCE INDEX (id1_type)
WHERE link_type = 1 AND id1 = "a1\0\0\0\0\0\0\0\0\0\0\0\0\0\0" AND
      visibility = 1  AND time >= 0 AND time <= 4294967295
ORDER BY time ASC, id2 ASC LIMIT 0, 1000;

--echo # Point query, flag = 0
SELECT /*+ bypass */ id, type, version, update_time, data
FROM node_table FORCE INDEX (PRIMARY)
WHERE id IN (1, 2, 3, 4, 5) AND type = 1;

SELECT /*+ bypass */ id, type, version, update_time, data
FROM node_table FORCE INDEX (PRIMARY)
WHERE id IN (1) AND type = 1;

SELECT /*+ bypass */ id, type, version, update_time, data
FROM node_table FORCE INDEX (PRIMARY)
WHERE id = 2 AND type = 1;

--echo # Point query, flag = 1
SELECT /*+ bypass */ id, type, row_created_time, is_deleted
FROM id_table FORCE INDEX (PRIMARY)
WHERE id IN (1, 2, 3, 4, 5);

SELECT /*+ bypass */ id, type, row_created_time, is_deleted
FROM id_table FORCE INDEX (PRIMARY)
WHERE id IN (1);

SELECT /*+ bypass */ id, type, row_created_time, is_deleted
FROM id_table FORCE INDEX (PRIMARY)
WHERE id = 1;

--echo # Point query, flag = 2
SELECT /*+ bypass */ id, type, link_type, count, TIME, version
FROM count_table
WHERE id = 2 AND link_type = 1;

--echo # Bugs found when debugging real production scenarios
# pk_unpack_buf unassigned when unpack_pk=false and unpack_value=true
# PinnableSlice destroyed before send_row
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table3 FORCE INDEX (id1_type)
WHERE link_type = 1 AND id1 = 1 AND visibility = 1
      AND time >= 0 AND time <= 4294967295
ORDER BY time DESC, id2 DESC LIMIT 0, 10000;

# Partial keys should not be unpacked
SELECT /*+ bypass */ data from link_table6 FORCE INDEX (id1_type) where id1=1;

# time >= ONLY crashes
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 1 AND id1 = 1 AND visibility = 0 AND time >= 1551074583 AND
      time >= 0 AND time <= 4294967295
ORDER BY time DESC, id2 DESC LIMIT 0, 500;

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table FORCE INDEX (id1_type)
WHERE link_type = 1 AND id1 = 1 AND visibility = 0 AND time >= 1551074583
ORDER BY time DESC, id2 DESC LIMIT 0, 500;

# IN expression multiplexing
SELECT /*+ bypass */ id1, id1_type, id2, id2_type FROM link_table
WHERE link_type=3 and id1 in (1, 2, 3);

# IN expression multiplexing in middle
SELECT /*+ bypass */ id1, id1_type, id2, id2_type from link_table
WHERE link_type=3 and id1 IN (1, 2, 3) and id2=1;

# IN expression with multiple IN
SELECT /*+ bypass */ id1, id1_type, id2, id2_type from link_table
WHERE link_type=3 and id1 IN (1, 2, 3) and id2 IN (3, 4, 5);

# Decoding varchar with extra padding spaces from index
SELECT /*+ bypass */ id1, id2, data, version, time
FROM link_table FORCE INDEX (id1_type)
WHERE id1=9 and link_type=5;

# Point query with duplicate items
SELECT /*+ bypass */ id, type, version, update_time, data
FROM node_table FORCE INDEX (PRIMARY)
WHERE id IN (5, 5, 5, 5, 5) AND type = 1;

# Point query with reverse order
SELECT /*+ bypass */ id, type, version, update_time, data
FROM node_table FORCE INDEX (PRIMARY)
WHERE id IN (5, 4, 3, 2, 1) AND type = 1;

# Point query with correct order
SELECT /*+ bypass */ id, type, version, update_time, data
FROM node_table FORCE INDEX (PRIMARY)
WHERE id IN (5, 4, 3, 2, 1) AND type = 1 ORDER BY id DESC;

# Range query with bloom filter
SELECT /*+ bypass */ id1, id2, data, time, version FROM link_table5
WHERE link_type=1 and id1=2 and id2 > 1;
SELECT /*+ bypass */ id1, id2, data, time, version FROM link_table5
WHERE link_type=1 and id1=2 and id2 >= 1;

# Prefix query returning large rows causing reallocation m_row_buf
# which lead to stale pointers in m_send_mapping.first
CREATE TABLE `link_table7` (
  `id1` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  `id1_type` int(10) unsigned NOT NULL DEFAULT '0' ,
  `id2` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  `id2_type` int(10) unsigned NOT NULL DEFAULT '0' ,
  `link_type` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  `visibility` tinyint(3) NOT NULL DEFAULT '0' ,
  `data` varchar(255) COLLATE latin1_bin NOT NULL DEFAULT '' ,
  `time` int(10) unsigned NOT NULL DEFAULT '0' ,
  `version` bigint(20) unsigned NOT NULL DEFAULT '0' ,
  PRIMARY KEY (`link_type` , `id1` , `id2`) COMMENT 'cf_link' ,
  KEY `id1_type` (`id1`,`link_type`,`visibility`,`time`,`id2`,`version`,`data`)
  COMMENT 'rev:cf_assoc_id1_type'
) ENGINE=ROCKSDB DEFAULT CHARSET=latin1 COLLATE=latin1_bin
ROW_FORMAT=COMPRESSED KEY_BLOCK_SIZE=8;
insert into link_table7 values (9223372036854775807, 100, 9223372036854775801,
  100, 9223372036854775807, 0,
  'data1', 1234561, 9223372036854775801);
insert into link_table7 values (9223372036854775807, 100, 1223372036854775802,
  100, 9223372036854775807, 0,
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890', 1234562, 9223372036854775802);
insert into link_table7 values (9223372036854775807, 100, 1223372036854775803,
  100, 9223372036854775807, 0,
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890123456789012345678901234567890123456789'
  '0123456789012345678901234567890', 1234563, 9223372036854775803);

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, time, version
FROM link_table7 FORCE INDEX (id1_type)
WHERE link_type = 9223372036854775807  and id1 = 9223372036854775807
and visibility = 0 ORDER BY time ASC, id2 ASC limit 0, 1000;

DROP TABLE link_table7;

# KILL
--echo # Establish connection con1 (user=root)
connect (con1,localhost,root,,);

--echo # Switch to connection con1
connection con1;

SHOW STATUS LIKE 'rows_%';

SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM link_table5 FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 1 AND visibility = 1
ORDER BY id2 ASC LIMIT 1;

SHOW STATUS LIKE 'rows_%';

--echo # Switch back to default connection
connection default;

select @@rocksdb_select_bypass_policy;
set global rocksdb_select_bypass_policy=@save_rocksdb_select_bypass_policy;
select @@rocksdb_select_bypass_policy;

SELECT variable_value into @executed_end
FROM performance_schema.global_status WHERE
variable_name="rocksdb_select_bypass_executed";

SELECT @executed_end-@executed_start AS ROCKSDB_SELECT_BYPASS_EXECUTED;

DROP TABLE count_table;
DROP TABLE link_table;
DROP TABLE link_table2;
DROP TABLE id_table;
DROP TABLE node_table;
DROP TABLE link_table5;
DROP TABLE link_table3;
DROP TABLE link_table6;
DROP TABLE link_table4;
