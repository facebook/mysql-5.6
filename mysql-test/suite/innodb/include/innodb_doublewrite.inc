# Embedded server does not support crashing
--source include/not_embedded.inc
--disable_warnings
DROP TABLE IF EXISTS t1;
DROP TABLE IF EXISTS t2;
--enable_warnings

if (`SELECT VARIABLE_VALUE NOT LIKE 'ON'
     FROM INFORMATION_SCHEMA.GLOBAL_VARIABLES
     WHERE VARIABLE_NAME = 'INNODB_CHECKSUMS'`)
{
--die Test expects InnoDB checksums to be enabled.
}

if (`SELECT VARIABLE_VALUE NOT LIKE 'INNODB'
     FROM INFORMATION_SCHEMA.GLOBAL_VARIABLES
     WHERE VARIABLE_NAME = 'INNODB_CHECKSUM_ALGORITHM'`)
{
--die Test expects InnoDB checksum algorithm to be innodb.
}

let $data_dir = `SELECT @@datadir`;
let $page_size = `SELECT @@innodb_page_size`;
let $corrupt_exec = python2 suite/innodb/t/innodb_corrupt_doublewrite_pages.py $data_dir $page_size;

# Compute the page number of the first doublewrite page.
# The doublewrite buffer starts at the second extent in the file.
# See definition of FSP_EXTENT_SIZE in fsp0types.h.
let $first_dblwr_page=`select 1024 * 1024 DIV $page_size`;
if ($page_size > 16384) {
 let $first_dblwr_page=`select $first_dblwr_page * 2`;
}
if ($page_size > 32768) {
 let $first_dblwr_page=`select $first_dblwr_page * 2`;
}
let $second_dblwr_page=`select $first_dblwr_page + 1`;

SET GLOBAL innodb_doublewrite=2;
CREATE TABLE t1(a INT PRIMARY KEY AUTO_INCREMENT, b char(255) default '') ENGINE=innodb;
INSERT INTO t1(a) VALUES(NULL);
INSERT INTO t1(a) SELECT NULL FROM t1;
INSERT INTO t1(a) SELECT NULL FROM t1;
INSERT INTO t1(a) SELECT NULL FROM t1;
INSERT INTO t1(a) SELECT NULL FROM t1;
INSERT INTO t1(a) SELECT NULL FROM t1;
INSERT INTO t1(a) SELECT NULL FROM t1;
INSERT INTO t1(a) SELECT NULL FROM t1;
INSERT INTO t1(a) SELECT NULL FROM t1;
INSERT INTO t1(a) SELECT NULL FROM t1;
INSERT INTO t1(a) SELECT NULL FROM t1;
INSERT INTO t1(a) SELECT NULL FROM t1;
INSERT INTO t1(a) SELECT NULL FROM t1;
INSERT INTO t1(a) SELECT NULL FROM t1;

let $space_id = `SELECT SPACE FROM INFORMATION_SCHEMA.INNODB_SYS_TABLES
  WHERE NAME = 'test/t1'`;

--echo # Ensure that dirty pages of table t1 are flushed.
let $wait_condition= SELECT VARIABLE_VALUE = 0
  FROM INFORMATION_SCHEMA.GLOBAL_STATUS
  WHERE VARIABLE_NAME = 'INNODB_BUFFER_POOL_PAGES_DIRTY';
--source include/wait_condition.inc

--exec echo "wait" > $MYSQLTEST_VARDIR/tmp/mysqld.1.expect
--shutdown_server
--source include/wait_until_disconnected.inc

# Verify innochecksum can read the modified doublewrite format.
--replace_result $page_size page_size $first_dblwr_page NNN
--exec $INNOCHECKSUM $data_dir/ibdata1 --per-page-detail --start-page=$first_dblwr_page --end-page=$second_dblwr_page

exec $corrupt_exec $space_id 2;

--enable_reconnect
# Write file to make mysql-test-run.pl start up the server again
--exec echo "restart" > $MYSQLTEST_VARDIR/tmp/mysqld.1.expect
--source include/wait_until_connected_again.inc

SELECT COUNT(*) FROM t1;

SET GLOBAL innodb_doublewrite=1;
CREATE TABLE t2(a INT PRIMARY KEY AUTO_INCREMENT, b char(255) default '') ENGINE=innodb;
INSERT INTO t2(a) VALUES(NULL);
INSERT INTO t2(a) SELECT NULL FROM t2;
INSERT INTO t2(a) SELECT NULL FROM t2;
INSERT INTO t2(a) SELECT NULL FROM t2;
INSERT INTO t2(a) SELECT NULL FROM t2;
INSERT INTO t2(a) SELECT NULL FROM t2;
INSERT INTO t2(a) SELECT NULL FROM t2;
INSERT INTO t2(a) SELECT NULL FROM t2;
INSERT INTO t2(a) SELECT NULL FROM t2;
INSERT INTO t2(a) SELECT NULL FROM t2;
INSERT INTO t2(a) SELECT NULL FROM t2;
INSERT INTO t2(a) SELECT NULL FROM t2;
INSERT INTO t2(a) SELECT NULL FROM t2;
INSERT INTO t2(a) SELECT NULL FROM t2;

let $space_id = `SELECT SPACE FROM INFORMATION_SCHEMA.INNODB_SYS_TABLES
  WHERE NAME = 'test/t2'`;

--echo # Ensure that dirty pages of table t1 are flushed.
let $wait_condition= SELECT VARIABLE_VALUE = 0
  FROM INFORMATION_SCHEMA.GLOBAL_STATUS
  WHERE VARIABLE_NAME = 'INNODB_BUFFER_POOL_PAGES_DIRTY';
--source include/wait_condition.inc

--exec echo "wait" > $MYSQLTEST_VARDIR/tmp/mysqld.1.expect
--shutdown_server
--source include/wait_until_disconnected.inc

exec $corrupt_exec $space_id 1;

--enable_reconnect
# Write file to make mysql-test-run.pl start up the server again
--exec echo "restart" > $MYSQLTEST_VARDIR/tmp/mysqld.1.expect
--source include/wait_until_connected_again.inc

SELECT COUNT(*) FROM t2;

DROP TABLE t1;
DROP TABLE t2;
