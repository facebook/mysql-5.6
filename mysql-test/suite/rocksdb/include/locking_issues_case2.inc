#
# Check concurrent locking issues:
#   Locking rows that are scanned but not updated
#
# To call this, set $isolation_level and call this file
#
# let $isolation_level = REPEATABLE READ;
# --source suite/rocksdb/include/locking_issues_case2.inc
#

--echo
--echo -----------------------------------------------------------------------
--echo - Locking issues case 2:
--echo -   Locking rows that are scanned but not updated
--echo -   using $isolation_level transaction isolation level
--echo -----------------------------------------------------------------------

--disable_warnings
DROP TABLE IF EXISTS t0;
--enable_warnings

CREATE TABLE t0(id INT PRIMARY KEY, value INT);
INSERT INTO t0 VALUES (1,0), (2,0), (3,0), (4,0), (5,0);

connect (con1,localhost,root,,);
connect (con2,localhost,root,,);

connection con1;
eval SET SESSION TRANSACTION ISOLATION LEVEL $isolation_level;
BEGIN;
SELECT * FROM t0 WHERE value > 0 FOR UPDATE;

connection con2;
eval SET SESSION TRANSACTION ISOLATION LEVEL $isolation_level;
BEGIN;
--error ER_LOCK_WAIT_TIMEOUT
UPDATE t0 SET VALUE=10 WHERE id=1;

connection con1;
SELECT * FROM t0 WHERE value>0 FOR UPDATE;
UPDATE t0 SET value=100;

connection con2;
--error ER_LOCK_WAIT_TIMEOUT
SELECT * FROM t0 FOR UPDATE;
COMMIT;
SELECT * FROM t0;

connection con1;
COMMIT;
SELECT * FROM t0 FOR UPDATE;

connection default;
disconnect con1;
disconnect con2;

DROP TABLE t0;
