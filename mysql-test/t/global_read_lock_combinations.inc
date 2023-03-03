SET @@global.legacy_global_read_lock_mode = 0;
--let $legacy_mode=0
--let $with_transactions=1
--source global_read_lock.inc

# Now do it without the BEGIN statement - make sure the statement fails
# (instead of the COMMIT following the statement)
--let $legacy_mode=0
--let $with_transactions=0
--source global_read_lock.inc

# Now run both tests again with legacy_global_read_lock_mode on to make
# sure we get the old behavior
--let $legacy_mode=1
SET @@global.legacy_global_read_lock_mode = 1;
--let $with_transactions=1
--source global_read_lock.inc

--let $legacy_mode=1
--let $with_transactions=0
--source global_read_lock.inc

# Now test LOCK TABLES transactions
SET @@global.legacy_global_read_lock_mode = 0;
--let $legacy_mode=0
--source global_read_lock_table_lock_test.inc

# Now test LOCK TABLES transactions with legacy mode on
SET @@global.legacy_global_read_lock_mode = 1;
--let $legacy_mode=1
--source global_read_lock_table_lock_test.inc

# Also test to make sure we don't leak a lock during the following commands:
# con1 - BEGIN
# con1 - LOCK TABLE ...
# con1 - INSERT INTO t1 ...
# con2 - SET GLOBAL super_read_only=1
# con1 - UNLOCK TABLES
connection default;
SET @@global.legacy_global_read_lock_mode = 0;
--source global_read_lock_leak_test.inc
