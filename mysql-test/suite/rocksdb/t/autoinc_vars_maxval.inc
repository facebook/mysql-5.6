
#
# AUTO_INCREMENT keyword in Create table
# 
# Usage:
# let $col_type = <column type>;
# let $col_value = <default value for a column>;
# let $error = <type of error expected>
# let $errorType = <to control errors that are only thrown on certain types>
# --source autoinc_vars_maxval.inc
#
# We will add NOT NULL to the column options;
#


--disable_warnings
DROP TABLE IF EXISTS t1;
--enable_warnings

SET auto_increment_increment = 1;
SET auto_increment_offset = 1;
eval CREATE TABLE t1 (a $col_type AUTO_INCREMENT PRIMARY KEY, b CHAR(8)) ENGINE=rocksdb;
eval INSERT INTO t1 VALUES ($col_value , 'a');
SHOW CREATE TABLE t1;
INSERT INTO t1 VALUES (NULL, 'b');
SHOW CREATE TABLE t1;
--error $error
INSERT INTO t1 VALUES (NULL, 'c');
SELECT * FROM t1;
DROP TABLE t1;

SET auto_increment_increment = 200;
eval CREATE TABLE t1 (a $col_type  AUTO_INCREMENT PRIMARY KEY, b CHAR(8)) ENGINE=rocksdb;
eval INSERT INTO t1 VALUES ($col_value , 'a');
SHOW CREATE TABLE t1;
if($errorType)
{
    --error $error
}
INSERT INTO t1 VALUES (NULL, 'b');
SHOW CREATE TABLE t1;
--error $error
INSERT INTO t1 VALUES (NULL, 'c');
SELECT * FROM t1;
DROP TABLE t1;

SET auto_increment_offset = 200;
eval CREATE TABLE t1 (a $col_type  AUTO_INCREMENT PRIMARY KEY, b CHAR(8)) ENGINE=rocksdb;
eval INSERT INTO t1 VALUES ($col_value , 'a');
SHOW CREATE TABLE t1;
if($errorType)
{
    --error $error
}
INSERT INTO t1 VALUES (NULL, 'b');
SHOW CREATE TABLE t1;
--error $error
INSERT INTO t1 VALUES (NULL, 'c');
SELECT * FROM t1;
DROP TABLE t1;

