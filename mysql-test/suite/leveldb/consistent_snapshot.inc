#
# TRANSACTION WITH CONSISTENT SNAPSHOT
#

--enable_connect_log

# Save the initial number of concurrent sessions
--source include/count_sessions.inc

--disable_warnings
DROP TABLE IF EXISTS t1;
--enable_warnings

connect (con1,localhost,root,,);
connect (con2,localhost,root,,);

connection con1;

CREATE TABLE t1 (a INT, pk INT AUTO_INCREMENT PRIMARY KEY) ENGINE=LevelDB;
eval SET SESSION TRANSACTION ISOLATION LEVEL $trx_isolation;

# While a consistent snapshot transaction is executed,
# no external inserts should be visible to the transaction.
# But it should only work this way for REPEATABLE-READ and SERIALIZABLE

START TRANSACTION WITH CONSISTENT SNAPSHOT;

connection con2;
INSERT INTO t1 (a) VALUES (1);

connection con1;
--echo # If consistent read works on this isolation level ($trx_isolation), the following SELECT should not return the value we inserted (1)
SELECT a FROM t1; 
COMMIT;

connection default;
disconnect con1;
disconnect con2;
DROP TABLE t1;

--source include/wait_until_count_sessions.inc

