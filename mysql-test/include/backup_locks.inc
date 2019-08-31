########################################################################
# Tests for LOCK TABLES FOR BACKUP
#
# Variables to control teste execution:
#   $engine - specifies additional transactional storage engine to test
#   $handler_support - specifies if the engine supports the HANDLER interface
#   $tmp_table_support - specifies if the engine supports temporary tables
#   $for_export_support - specifies if the engine supports FLUSH TABLES ... FOR
#     EXPORT
########################################################################

--source include/have_myisam.inc
--source include/have_archive.inc
--source include/have_blackhole.inc

SHOW VARIABLES LIKE 'have_backup_locks';

--echo #-----------------------------------------------------------------------
--echo # Setup
--echo #-----------------------------------------------------------------------

# Save the initial number of concurrent sessions.
--source include/count_sessions.inc

CREATE TABLE t_myisam(a INT NOT NULL, KEY (a)) ENGINE=MyISAM;
CREATE TABLE t_memory(a INT NOT NULL, KEY (a)) ENGINE=MEMORY;
CREATE TABLE t_csv(a INT NOT NULL) ENGINE=CSV;
CREATE TABLE t_blackhole(a INT NOT NULL, KEY (a)) ENGINE=BLACKHOLE;
CREATE TABLE t_archive(a INT NOT NULL) ENGINE=ARCHIVE;
eval CREATE TABLE t_$engine(a INT NOT NULL, KEY(a)) ENGINE=$engine;

CREATE VIEW v_myisam AS SELECT * FROM t_myisam;
CREATE VIEW v_memory AS SELECT * FROM t_memory;
CREATE VIEW v_csv AS SELECT * FROM t_csv;
CREATE VIEW v_blackhole AS SELECT * FROM t_blackhole;
CREATE VIEW v_archive AS SELECT * FROM t_archive;
eval CREATE VIEW v_$engine AS SELECT * FROM t_$engine;

--echo #-----------------------------------------------------------------------
--echo # Basic syntax checks
--echo #-----------------------------------------------------------------------

--error ER_PARSE_ERROR
eval LOCK TABLES t_$engine FOR BACKUP;
--error ER_PARSE_ERROR
eval LOCK TABLES t_$engine READ FOR BACKUP;
--error ER_PARSE_ERROR
eval LOCK TABLES t_$engine FOR BACKUP READ;

LOCK TABLES FOR BACKUP;
UNLOCK TABLES;

--echo #-----------------------------------------------------------------------
--echo # No backup locks are allowed in stored routines
--echo #-----------------------------------------------------------------------

delimiter |;
--error ER_SP_BADSTATEMENT
CREATE PROCEDURE p1()
BEGIN
  LOCK TABLES FOR BACKUP;
END|

--error ER_SP_BADSTATEMENT
CREATE FUNCTION f1() RETURNS INT DETERMINISTIC
BEGIN
  LOCK TABLES FOR BACKUP;
  RETURN 1;
END|

delimiter ;|

--echo #-----------------------------------------------------------------------
--echo # Single-threaded tests
--echo #-----------------------------------------------------------------------

SET SESSION lock_wait_timeout=1;

FLUSH STATUS;

#
# Multiple TFB locks in the same connection
#
LOCK TABLES FOR BACKUP;
LOCK TABLES FOR BACKUP;
SHOW STATUS LIKE 'Com_lock_tables_for_backup';

UNLOCK TABLES;
SHOW STATUS LIKE 'Com_unlock_tables';

#
# LTFB + DDL / DML in the same connection
#

LOCK TABLES FOR BACKUP;

--echo myisam
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
DELETE FROM t_myisam;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
INSERT INTO t_myisam VALUES(0);
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
UPDATE t_myisam SET a = 1;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
REPLACE INTO t_myisam VALUES(1);
SELECT * FROM t_myisam;
HANDLER t_myisam OPEN;
HANDLER t_myisam READ a FIRST;
HANDLER t_myisam CLOSE;

--echo memory
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
DELETE FROM t_memory;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
INSERT INTO t_memory VALUES(0);
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
UPDATE t_memory SET a = 1;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
REPLACE INTO t_memory VALUES(1);
SELECT * FROM t_memory;

--echo csv
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
DELETE FROM t_csv;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
INSERT INTO t_csv VALUES(0);
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
UPDATE t_csv SET a = 1;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
REPLACE INTO t_csv VALUES(1);
SELECT * FROM t_csv;

--echo blackhole
DELETE FROM t_blackhole;
INSERT INTO t_blackhole VALUES(0);
UPDATE t_blackhole SET a = 1;
REPLACE INTO t_blackhole VALUES(1);
SELECT * FROM t_blackhole;

--echo archive
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
DELETE FROM t_archive;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
INSERT INTO t_archive VALUES(0);
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
UPDATE t_archive SET a = 1;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
REPLACE INTO t_archive VALUES(1);
SELECT * FROM t_archive;

--echo $engine
eval DELETE FROM t_$engine;
eval INSERT INTO t_$engine VALUES(0);
eval UPDATE t_$engine SET a = 1;
eval REPLACE INTO t_$engine VALUES(1);
eval SELECT * FROM t_$engine;

if ($handler_support)
{
eval HANDLER t_$engine OPEN;
eval HANDLER t_$engine READ a FIRST;
eval HANDLER t_$engine CLOSE;
}

--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
CREATE TABLE tt_myisam(a INT NOT NULL) ENGINE=MyISAM;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
CREATE TABLE tt_memory(a INT NOT NULL) ENGINE=MEMORY;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
CREATE TABLE tt_csv(a INT NOT NULL) ENGINE=CSV;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
CREATE TABLE tt_blackhole(a INT NOT NULL) ENGINE=BLACKHOLE;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
CREATE TABLE tt_archive(a INT NOT NULL) ENGINE=ARCHIVE;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
eval CREATE TABLE tt_$engine(a INT NOT NULL) ENGINE=$engine;

--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
DROP TABLE t_myisam;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
DROP TABLE t_memory;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
DROP TABLE t_csv;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
DROP TABLE t_blackhole;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
DROP TABLE t_archive;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
eval DROP TABLE t_$engine;

--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
DROP TABLE non_existing;

--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
TRUNCATE TABLE t_myisam;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
TRUNCATE TABLE t_memory;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
TRUNCATE TABLE t_csv;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
TRUNCATE TABLE t_blackhole;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
TRUNCATE TABLE t_archive;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
eval TRUNCATE TABLE t_$engine;

--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
RENAME TABLE t_myisam TO tmp;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
RENAME TABLE t_memory TO tmp;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
RENAME TABLE t_csv TO tmp;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
RENAME TABLE t_blackhole TO tmp;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
RENAME TABLE t_archive TO tmp;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
eval RENAME TABLE t_$engine TO tmp;

--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
ALTER TABLE t_myisam ADD COLUMN b CHAR(10);
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
ALTER TABLE t_memory ADD COLUMN b CHAR(10);
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
ALTER TABLE t_csv ADD COLUMN b CHAR(10);
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
ALTER TABLE t_blackhole ADD COLUMN b CHAR(10);
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
ALTER TABLE t_archive ADD COLUMN b CHAR(10);
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
eval ALTER TABLE t_$engine ADD COLUMN b CHAR(10);

--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
ALTER TABLE t_myisam ADD KEY (a);
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
ALTER TABLE t_memory ADD KEY (a);
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
ALTER TABLE t_csv ADD KEY (a);
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
ALTER TABLE t_blackhole ADD KEY (a);
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
ALTER TABLE t_archive ADD KEY (a);
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
eval ALTER TABLE t_$engine ADD KEY (a);

--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
DROP DATABASE test;
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
CREATE DATABASE test1;

--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
eval CREATE VIEW v1 AS SELECT * FROM t_$engine;

--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
INSERT INTO v_myisam VALUES(1);
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
INSERT INTO v_memory VALUES(1);
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
INSERT INTO v_csv VALUES(1);
# no error for blackhole
INSERT INTO v_blackhole VALUES(1);
--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
INSERT INTO v_archive VALUES(1);
eval INSERT INTO v_$engine VALUES(1);

UNLOCK TABLES;

#
# Test incompatibility with delay_key_write=ALL
#
SELECT @@delay_key_write;

SET GLOBAL delay_key_write=ALL;
--error ER_OPTION_PREVENTS_STATEMENT
LOCK TABLES FOR BACKUP;

SET GLOBAL delay_key_write=default;
SELECT @@delay_key_write;

#
# read-only=1 + backup locks
#

SET GLOBAL read_only=1;

INSERT INTO t_myisam VALUES(0);
eval INSERT INTO t_$engine VALUES(0);

LOCK TABLES FOR BACKUP;

--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
INSERT INTO t_myisam VALUES(0);
eval INSERT INTO t_$engine VALUES(0);

UNLOCK TABLES;

INSERT INTO t_myisam VALUES(0);
eval INSERT INTO t_$engine VALUES(0);

SET GLOBAL read_only=0;

#
# Test that temporary tables are not affected by backup locks
#
LOCK TABLES FOR BACKUP;
CREATE TEMPORARY TABLE tt_myisam(a INT NOT NULL) ENGINE=MyISAM;
CREATE TEMPORARY TABLE tt_memory(a INT NOT NULL) ENGINE=MEMORY;
CREATE TEMPORARY TABLE tt_csv(a INT NOT NULL) ENGINE=CSV;
CREATE TEMPORARY TABLE tt_blackhole(a INT NOT NULL) ENGINE=BLACKHOLE;
CREATE TEMPORARY TABLE tt_archive(a INT NOT NULL) ENGINE=ARCHIVE;
if ($tmp_table_support)
{
eval CREATE TEMPORARY TABLE tt_$engine(a INT NOT NULL) ENGINE=$engine;
}

INSERT INTO tt_myisam VALUES(5);
INSERT INTO tt_memory VALUES(5);
INSERT INTO tt_csv VALUES(5);
INSERT INTO tt_blackhole VALUES(5);
INSERT INTO tt_archive VALUES(5);
if ($tmp_table_support)
{
eval INSERT INTO tt_$engine VALUES(5);
}

UNLOCK TABLES;

#
# Log tables
#

SET @old_general_log = @@general_log;
SET @old_slow_query_log = @@slow_query_log;
SET @old_log_output = @@log_output;
SET @old_long_query_time = @@SESSION.long_query_time;

SET SESSION long_query_time = 0;
SET GLOBAL log_output = 'TABLE';
SET GLOBAL general_log = ON;
SET GLOBAL slow_query_log = ON;

LOCK TABLES FOR BACKUP;

SELECT 1;

UNLOCK TABLES;

SET SESSION long_query_time = @old_long_query_time;
SET GLOBAL log_output = @old_log_output;
SET GLOBAL slow_query_log = @old_slow_query_log;
SET GLOBAL general_log = @old_general_log;

SET SESSION lock_wait_timeout=default;


--echo #-----------------------------------------------------------------------
--echo # Multi-threaded tests
--echo #-----------------------------------------------------------------------

--echo # connection default
SET GLOBAL lock_wait_timeout=1;

CREATE USER user@localhost;
GRANT ALL PRIVILEGES ON test.* TO user@localhost;

--connect(con1,localhost,root,,)

--connect(con2,localhost,user,,)

--connection default
LOCK TABLES FOR BACKUP;

--connection con1
--echo # connection con1
--error ER_LOCK_WAIT_TIMEOUT
SET GLOBAL delay_key_write=ALL;

--connection default
--echo # connection default
UNLOCK TABLES;

#
# read-only=1 + backup locks
#

--connection con2
--echo # connection con2

START TRANSACTION;
eval INSERT INTO t_$engine VALUES(0);

--connection default
--echo # connection default

SET GLOBAL read_only=1;

--connection con2
--echo # connection con2

--error ER_OPTION_PREVENTS_STATEMENT
COMMIT;

--error ER_OPTION_PREVENTS_STATEMENT
eval INSERT INTO t_$engine VALUES(0);
--error ER_OPTION_PREVENTS_STATEMENT
INSERT INTO t_myisam VALUES(0);

--connection con1
--echo # connection con1

LOCK TABLES FOR BACKUP;

--connection con2
--echo # connection con2

--error ER_OPTION_PREVENTS_STATEMENT
eval INSERT INTO t_$engine VALUES(0);
--error ER_OPTION_PREVENTS_STATEMENT
INSERT INTO t_myisam VALUES(0);

--connection default
--echo # connection default

SET GLOBAL read_only=0;

--connection con2
--echo # connection con2
eval INSERT INTO t_$engine VALUES(0);
--error ER_LOCK_WAIT_TIMEOUT
INSERT INTO t_myisam VALUES(0);

--connection default
--echo # connection default

SET GLOBAL read_only=1;

--connection con2
--echo # connection con2

--error ER_OPTION_PREVENTS_STATEMENT
eval INSERT INTO t_$engine VALUES(0);
--error ER_OPTION_PREVENTS_STATEMENT
INSERT INTO t_myisam VALUES(0);

--connection con1
--echo # connection con1

UNLOCK TABLES;

--connection con2
--echo # connection con2

--error ER_OPTION_PREVENTS_STATEMENT
eval INSERT INTO t_$engine VALUES(0);
--error ER_OPTION_PREVENTS_STATEMENT
INSERT INTO t_myisam VALUES(0);

--connection default
--echo # connection default

SET GLOBAL read_only=0;

#
# Test that temporary tables are not affected by backup locks
#
--connection default
--echo # connection default
LOCK TABLES FOR BACKUP;

--connection con1
--echo # connection con1

CREATE TEMPORARY TABLE tt_myisam(a INT NOT NULL) ENGINE=MyISAM;
CREATE TEMPORARY TABLE tt_memory(a INT NOT NULL) ENGINE=MEMORY;
CREATE TEMPORARY TABLE tt_csv(a INT NOT NULL) ENGINE=CSV;
CREATE TEMPORARY TABLE tt_blackhole(a INT NOT NULL) ENGINE=BLACKHOLE;
CREATE TEMPORARY TABLE tt_archive(a INT NOT NULL) ENGINE=ARCHIVE;
if ($tmp_table_support)
{
eval CREATE TEMPORARY TABLE tt_$engine(a INT NOT NULL) ENGINE=$engine;
}

INSERT INTO tt_myisam VALUES(5);
INSERT INTO tt_memory VALUES(5);
INSERT INTO tt_csv VALUES(5);
INSERT INTO tt_blackhole VALUES(5);
INSERT INTO tt_archive VALUES(5);
if ($tmp_table_support)
{
eval INSERT INTO tt_$engine VALUES(5);
}

--connection default
--echo # connection default

UNLOCK TABLES;

#
# LTFB + DDL / DML in another connection
#

--connection default
--echo # connection default

LOCK TABLES FOR BACKUP;

--connection con1
--echo # connection con1

--echo myisam
--error ER_LOCK_WAIT_TIMEOUT
INSERT INTO t_myisam VALUES(0);
SELECT * FROM t_myisam;
HANDLER t_myisam OPEN;
HANDLER t_myisam READ a FIRST;
HANDLER t_myisam CLOSE;

--echo memory
--error ER_LOCK_WAIT_TIMEOUT
INSERT INTO t_memory VALUES(0);
SELECT * FROM t_memory;

--echo csv
--error ER_LOCK_WAIT_TIMEOUT
INSERT INTO t_csv VALUES(0);
SELECT * FROM t_csv;

--echo blackhole
DELETE FROM t_blackhole;
INSERT INTO t_blackhole VALUES(0);
UPDATE t_blackhole SET a = 1;
REPLACE INTO t_blackhole VALUES(1);
SELECT * FROM t_blackhole;

--echo archive
--error ER_LOCK_WAIT_TIMEOUT
INSERT INTO t_archive VALUES(0);
SELECT * FROM t_archive;

--echo $engine
eval DELETE FROM t_$engine;
eval INSERT INTO t_$engine VALUES(0);
eval UPDATE t_$engine SET a = 1;
eval REPLACE INTO t_$engine VALUES(1);
eval SELECT * from t_$engine;
if ($handler_support)
{
eval HANDLER t_$engine OPEN;
eval HANDLER t_$engine READ a FIRST;
eval HANDLER t_$engine CLOSE;
}

--error ER_LOCK_WAIT_TIMEOUT
eval CREATE TABLE tt_$engine(a INT NOT NULL) ENGINE=$engine;

--error ER_LOCK_WAIT_TIMEOUT
eval DROP TABLE t_$engine;

--error ER_LOCK_WAIT_TIMEOUT
DROP TABLE non_existing;

--error ER_LOCK_WAIT_TIMEOUT
eval TRUNCATE TABLE t_$engine;

--error ER_LOCK_WAIT_TIMEOUT
eval RENAME TABLE t_$engine TO tmp;

--error ER_LOCK_WAIT_TIMEOUT
eval ALTER TABLE t_$engine ADD COLUMN b CHAR(10);

--error ER_LOCK_WAIT_TIMEOUT
eval ALTER TABLE t_$engine ADD KEY (a);

--error ER_LOCK_WAIT_TIMEOUT
DROP DATABASE test;
--error ER_LOCK_WAIT_TIMEOUT
CREATE DATABASE test1;


--send INSERT INTO t_myisam VALUES(0)

--connection default
--echo # connection default

let $wait_condition=
    SELECT COUNT(*) = 1 FROM INFORMATION_SCHEMA.PROCESSLIST
    WHERE STATE = "Waiting for table backup lock" AND
    INFO = "INSERT INTO t_myisam VALUES(0)";
--source include/wait_condition.inc

--connection con1
--echo # connection con1

--error ER_LOCK_WAIT_TIMEOUT
--reap

--error ER_LOCK_WAIT_TIMEOUT
eval CREATE VIEW v1 AS SELECT * FROM t_$engine;

eval INSERT INTO v_$engine VALUES(1);
INSERT INTO v_blackhole VALUES(1);
--error ER_LOCK_WAIT_TIMEOUT
INSERT INTO v_myisam VALUES(1);

--connection default
--echo # connection default

UNLOCK TABLES;

#
# Log tables
#

SET @old_general_log = @@general_log;
SET @old_slow_query_log = @@slow_query_log;
SET @old_log_output = @@log_output;

SET GLOBAL log_output = 'TABLE';
SET GLOBAL general_log = ON;
SET GLOBAL slow_query_log = ON;

LOCK TABLES FOR BACKUP;

--connection con1
--echo # connection con1

SET @old_long_query_time = @@SESSION.long_query_time;
SET SESSION long_query_time = 0;

SELECT 1;

--connection default
--echo # connection default

UNLOCK TABLES;

SET GLOBAL log_output = @old_log_output;
SET GLOBAL slow_query_log = @old_slow_query_log;
SET GLOBAL general_log = @old_general_log;

--echo ########################################################################
--echo # Bug #1360064: LOCK TABLES FOR BACKUP should be incompatible with LOCK
--echo #               TABLES, FLUSH TABLES ... WITH READ LOCK and FLUSH TABLES
--echo #               ... FOR EXPORT in the same connection
--echo ########################################################################

eval CREATE TABLE t1(a INT) ENGINE=$engine;

LOCK TABLES t1 READ;
--error ER_LOCK_OR_ACTIVE_TRANSACTION
LOCK TABLES FOR BACKUP;

UNLOCK TABLES;

LOCK TABLES t1 WRITE;
--error ER_LOCK_OR_ACTIVE_TRANSACTION
LOCK TABLES FOR BACKUP;

UNLOCK TABLES;

if ($for_export_support)
{
FLUSH TABLES t1 FOR EXPORT;
--error ER_LOCK_OR_ACTIVE_TRANSACTION
LOCK TABLES FOR BACKUP;

UNLOCK TABLES;
}

FLUSH TABLES t1 WITH READ LOCK;
--error ER_LOCK_OR_ACTIVE_TRANSACTION
LOCK TABLES FOR BACKUP;

UNLOCK TABLES;

LOCK TABLES FOR BACKUP;

--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
LOCK TABLE t1 READ;

--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
LOCK TABLE t1 WRITE;

--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
FLUSH TABLES t1 FOR EXPORT;

--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
FLUSH TABLES t1 WITH READ LOCK;

--error ER_CANT_EXECUTE_WITH_BACKUP_LOCK
FLUSH TABLES WITH READ LOCK;

UNLOCK TABLES;

DROP TABLE t1;

--echo #-----------------------------------------------------------------------
--echo # Cleanup
--echo #-----------------------------------------------------------------------

SET GLOBAL lock_wait_timeout=default;
DROP USER user@localhost;

--disconnect con1
--disconnect con2

--echo #
--echo # End of 5.7 tests
--echo #

--echo # 8.0: LOCK TABLES FOR BACKUP should require BACKUP_ADMIN instead of RELOAD
CREATE USER u1@localhost;
--connect (con3, localhost, u1)
--error ER_SPECIFIC_ACCESS_DENIED_ERROR
LOCK TABLES FOR BACKUP;

--connection default
GRANT RELOAD ON *.* TO u1@localhost;

--connection con3
--error ER_SPECIFIC_ACCESS_DENIED_ERROR
LOCK TABLES FOR BACKUP;

--connection default
REVOKE RELOAD ON *.* FROM u1@localhost;
GRANT BACKUP_ADMIN ON *.* TO u1@localhost;

--connection con3
LOCK TABLES FOR BACKUP;
UNLOCK TABLES;
--disconnect con3

--connection default
DROP USER u1@localhost;


--source include/wait_until_count_sessions.inc

eval DROP VIEW v_$engine, v_myisam, v_memory, v_csv, v_blackhole, v_archive;
eval DROP TABLE t_$engine, t_myisam, t_memory, t_csv, t_blackhole, t_archive;
