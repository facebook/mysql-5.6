--echo # Test that we don't leak locks on $engine

connection default;
# Save the current settings for read_only and super_read_only
SET @save_read_only = @@global.read_only;
SET @save_super_read_only = @@global.super_read_only;

SET @@global.super_read_only = 0;
SET @@global.read_only = 0;
eval CREATE TABLE t(a INT NOT NULL) ENGINE=$engine;

connection con1;
BEGIN;
LOCK TABLES t WRITE;
INSERT INTO t VALUES(100);

--echo # The In_use column should be 1 to indicate the lock;
connection default;
SHOW OPEN TABLES like 't';

connection con2;
SET @@global.super_read_only = 1;

connection con1;
UNLOCK TABLES;

--echo #The In_use column should be 0 to indicate the lock was removed;
connection default;
SHOW OPEN TABLES like 't';

SET @@global.super_read_only = 0;
DROP TABLE t;

SET @@global.super_read_only = @save_super_read_only;
SET @@global.read_only = @save_read_only;
