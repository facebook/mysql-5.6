--echo # Test setting read only with LOCK TABLES transactions engine=$engine,
--echo # legacy_mode=$legacy_mode should_fail_on_commit=$should_fail_on_commit

connection default;
# Save the current settings for read_only and super_read_only
SET @save_read_only=@@global.read_only;
SET @save_super_read_only=@@global.super_read_only;

SET @@global.super_read_only = 0;
eval CREATE TABLE t1(a INT NOT NULL) ENGINE=$engine;

connection con1;
SET autocommit=0;
LOCK TABLES t1 WRITE;
INSERT INTO t1 VALUES (1), (2);

# In the second thread set super_read_only on.  Setting this used to block on
# the long running statement above.  Now, in the default mode, it should
# succeed and the statement should fail when it tries to commit.  In legacy
# mode this should block.
connection con2;
send SET GLOBAL super_read_only=1;

# Back on the original thread show the process list again and show that
# SET GLOBAL super_read_only is not waiting (unless in legacy mode)
connection default;

let $wait_condition= SELECT COUNT(*)=$legacy_mode FROM information_schema.processlist
    WHERE info='SET GLOBAL super_read_only=1';
--source include/wait_condition.inc

SELECT command, info FROM information_schema.processlist ORDER BY command, info;

if ($legacy_mode) {
  connection con1;
  COMMIT;
  UNLOCK TABLES;

  --echo # setting read only should succeed after unlocking the table
  connection con2;
  reap;

  --echo # The changes should exist
  SELECT COUNT(*) FROM t1;
}

if (!$legacy_mode) {
  --echo # setting read only should succeed
  connection con2;
  reap;

  --echo # Also try to create a table - this should fail when running in
  --echo # default mode because READ ONLY is on
  --error ER_OPTION_PREVENTS_STATEMENT
  CREATE TABLE t2 (a INT) Engine=InnoDB;

  connection con1;
  --echo # Changes should be visible
  SELECT COUNT(*) FROM t1;

  if ($should_fail_on_commit) {
    --echo # Commit should fail because read-only is on
    --error ER_OPTION_PREVENTS_STATEMENT
  }
  COMMIT;

  --echo # The changes should not exist
  SELECT COUNT(*) FROM t1;
}

SET @@global.super_read_only = 0;
DROP TABLE t1;

# Reset autocommit
connection con1;
SET autocommit=default;

# Reset the super_read_only and read_only variables
connection default;
SET @@global.super_read_only = @save_super_read_only;
SET @@global.read_only = @save_read_only;
