# Bypass range query test for SECONDARY KEY id1_type
# Parameters
#  $query_order
#    ASC or DESC in SELECT query
#  $query_table
#    Which table

--echo # prefix
let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND TIME = 10
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 12
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 11
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 0
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 0
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 8
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 9
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 9
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 10
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 10
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 15
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 15
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 16
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 16
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 7 AND TIME <= 8
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 5 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1
ORDER BY visibility $query_order LIMIT 0, 1000;

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE id1 = 1
ORDER BY link_type $query_order, visibility $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data
FROM $query_table FORCE INDEX (id1_type)
WHERE id1 = 1 AND TIME=12
ORDER BY link_type $query_order, visibility $query_order,
time $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data
FROM $query_table FORCE INDEX (id1_type)
WHERE TIME=12
ORDER BY id1 $query_order, link_type $query_order,
visibility $query_order, time $query_order,
id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

--echo # Range query
let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND TIME = 10
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 3 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 10
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 12
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 11
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 9
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 10
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 10
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 15
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 15
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME >= 16
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND TIME > 16
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1 AND visibility = 4 AND
      TIME >= 7 AND TIME <= 8
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 5 AND id1 = 1 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 1
ORDER BY visibility $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE id1 = 1
ORDER BY link_type $query_order, visibility $query_order
LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 15
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME > 10 AND TIME < 15
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME >= 10 AND TIME < 15
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME > 10 AND TIME <= 15
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME >= 10 AND TIME <= 16
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME > 10 AND TIME < 16
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME >= 10 AND TIME < 16
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME > 10 AND TIME <= 16
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME >= 9 AND TIME <= 16
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME > 9 AND TIME < 16
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME >= 9 AND TIME < 16
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME > 9 AND TIME <= 16
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

--echo # Invalid range
let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME > 10 AND TIME <= 9
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (id1_type)
WHERE link_type = 3 AND id1 = 3 AND visibility = 4 AND
      TIME > 9 AND TIME <= 9
ORDER BY TIME $query_order, id2 $query_order LIMIT 0, 1000;
--source ../include/verify_bypass_query.inc
