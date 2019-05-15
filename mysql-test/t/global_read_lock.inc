# Save the current settings for read_only and super_read_only
connection default;
SET @save_read_only=@@global.read_only;
SET @save_super_read_only=@@global.super_read_only;

# In one thread start a statement that takes 10 seconds
connection con1;
if ($with_transactions) {
  BEGIN WORK;
}
SELECT * FROM t1;
send INSERT INTO t1 SELECT sleep(10);

# Back on the original thread show the process list
connection default;

let $wait_condition= SELECT COUNT(*)= 1 FROM information_schema.processlist
    WHERE info='INSERT INTO t1 SELECT sleep(10)';
--source include/wait_condition.inc

SELECT command, info FROM information_schema.processlist ORDER BY command, info;

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

if (!$legacy_mode) {
  # Also try to create a table - this should fail when running in default mode
  # because READ ONLY is on
  --error ER_OPTION_PREVENTS_STATEMENT
  CREATE TABLE t2 (a INT) Engine=InnoDB;
}

# Wait for the SET GLOBAL super_read_only statement to finish
connection con2;
reap;

if (!$with_transactions) {
  # Wait for the long running statement to finish
  # (error expected when not in legacy mode)
  connection con1;
  if ($legacy_mode) {
    reap;
  }

  if (!$legacy_mode) {
    --error ER_OPTION_PREVENTS_STATEMENT
    reap;
  }
}

if ($with_transactions) {
  # Wait for the long running statement to finish (no error expected)
  connection con1;
  reap;

  # The changes should be visible to the current connection if using
  # transactions
  SELECT COUNT(*) FROM t1;

  # But should fail when the transaction is committed (read_only mode is on)
  --error ER_OPTION_PREVENTS_STATEMENT
  COMMIT;
}

# The changes should not exist
SELECT COUNT(*) FROM t1;

# Reset the super_read_only and read_only variables
connection default;
SET @@global.super_read_only = @save_super_read_only;
SET @@global.read_only = @save_read_only;
