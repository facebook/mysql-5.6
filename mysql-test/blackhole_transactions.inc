CREATE TABLE t(a INT NOT NULL) ENGINE=BLACKHOLE;

--echo # Test commit
START TRANSACTION;
INSERT INTO t VALUES (1), (2);
COMMIT;
SELECT * from t;

--echo # Test rollback
START TRANSACTION;
INSERT INTO t VALUES (1), (2);
ROLLBACK;
SELECT * from t;

--echo # Test transaction with consistent snapshot.
--echo # Should behave just like a normal transaction.
--echo # There just won't be snapshots used on the blackhole engine.
START TRANSACTION WITH CONSISTENT SNAPSHOT;
SELECT * from t;
INSERT INTO t VALUES (1), (2);
COMMIT;
SELECT * from t;

--echo # Test transaction with shared snapshot on blackhole engine specifically.
--echo # Should fail because we don't support it.
error ER_UNKNOWN_ERROR;
START TRANSACTION WITH SHARED BLACKHOLE SNAPSHOT;

--echo # Test transaction with existing shared snapshot on blackhole engine
--echo # specifically. Should fail because we don't support it.
error ER_UNKNOWN_ERROR;
START TRANSACTION WITH EXISTING BLACKHOLE SNAPSHOT 1;

--echo # Creating an explicit snapshot for blackhole should fail because we
--echo # don't support it.
error ER_UNKNOWN_ERROR;
CREATE EXPLICIT BLACKHOLE SNAPSHOT;

--source include/show_binlog_events.inc

DROP TABLE t;
