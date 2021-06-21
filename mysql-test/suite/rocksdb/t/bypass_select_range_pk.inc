# Bypass range query test for PRIMARY KEY $query_table
# Parameters
#  $query_order
#    ASC or DESC in SELECT query
#  $query_table
#    Which table

--echo # Prefix queries
let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 1 AND visibility = 1
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 1 AND visibility = 2
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 1 AND visibility = 3
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND visibility = 1
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND visibility = 2
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1
ORDER BY id1 $query_order, id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND time = 1
ORDER BY id1 $query_order, id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE time = 1
ORDER BY link_type $query_order, id1 $query_order,
id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

--echo # Range queries

--echo # > and >=
let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 >= 0
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 > 0
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 > 1
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 >= 1
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 >= 2
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 > 2
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 >= 3
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 > 3
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 > 5
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 >= 5
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 > 6
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 >= 6
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 2 AND id1 > 1
ORDER BY id1 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 2 AND id1 >= 2
ORDER BY id1 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

--echo # < and <=

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 < 0
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 <= 0
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 < 1
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 <= 1
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 < 2
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 <= 2
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 < 4
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 <= 4
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 <= 5
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 < 5
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 <= 6
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 < 6
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 0 AND id1 < 1
ORDER BY id1 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 0 AND id1 <= 1
ORDER BY id1 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

--echo # Both > and <

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 > 3 AND id2 < 3
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 > 4 AND id2 < 3
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 > 3 AND id2 < 4
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 >= 3 AND id2 <= 4
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 >= 3 AND id2 < 4
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 > 3 AND id2 <= 4
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 > 1 AND id2 < 5
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 >= 1 AND id2 <= 5
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 >= 1 AND id2 < 5
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 > 1 AND id2 <= 5
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 >= 0 AND id2 <= 6
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 > 0 AND id2 < 6
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 > 0 AND id2 <= 6
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 1 AND id1 = 2 AND id2 >= 0 AND id2 < 6
ORDER BY id2 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 2 AND id1 > 1 and id1 < 100
ORDER BY id1 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 2 AND id1 >= 2 and id1 < 100
ORDER BY id1 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 0 AND id1 <= 1 and id1 >= 0
ORDER BY id1 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc

let bypass_query=
SELECT /*+ bypass */ id1, id2, link_type, visibility, data, TIME, version
FROM $query_table FORCE INDEX (PRIMARY)
WHERE link_type = 0 AND id1 < 2 and id1 >= 0
ORDER BY id1 $query_order LIMIT 1000;
--source ../include/verify_bypass_query.inc
