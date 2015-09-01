##############################################################################
# The testcase input used for mysqlbinlog_gtid_skip_empty_trans_rocksdb
##############################################################################

create database test2;
create database test3;

use test;
create table t1 (a int primary key, b char(8)) ENGINE=RocksDB;
insert into t1 values(1, 'a');
insert into t1 values(2, 'b');
# Create t2 for transaction
create table t2 (a int primary key, b char(8)) ENGINE=RocksDB;
# Transaction
start transaction;
insert into t2 values(1, 'a');
insert into t2 values(2, 'b');
insert into t2 values(3, 'c');
insert into t2 values(4, 'd');
commit;

use test2;
create table t1 (a int primary key, b char(8)) ENGINE=RocksDB;
insert into t1 values(1, 'a');
insert into t1 values(2, 'b');
# Create t2 for transaction
create table t2 (a int primary key, b char(8)) ENGINE=RocksDB;
# Transaction
start transaction;
insert into t2 values(1, 'a');
insert into t2 values(2, 'b');
insert into t2 values(3, 'c');
insert into t2 values(4, 'd');
commit;

use test3;
create table t1 (a int primary key, b char(8)) ENGINE=RocksDB;
insert into t1 values(1, 'a');
insert into t1 values(2, 'b');
# Create t2 for transaction
create table t2 (a int primary key, b char(8)) ENGINE=RocksDB;
# Transaction
start transaction;
insert into t2 values(1, 'a');
insert into t2 values(2, 'b');
insert into t2 values(3, 'c');
insert into t2 values(4, 'd');
commit;

let $master_binlog= query_get_value(SHOW MASTER STATUS, File, 1);
let $MYSQLD_DATADIR= `select @@datadir`;
FLUSH LOGS;

--echo ==== Output of mysqlbinlog with --short-form --skip-empty-trans, --database and --skip-gtids options ====
--exec $MYSQL_BINLOG --short-form --skip_empty_trans --database=test2 --skip-gtids $MYSQLD_DATADIR/$master_binlog

# Test the case when database gets changed in the middle of a transaction
# Selected database
use test2;
start transaction;
insert into t2 values(5, 'e');
insert into t2 values(6, 'f');
use test;
insert into t2 values(7, 'g');
insert into t2 values(8, 'h');
commit;

let $master_binlog= query_get_value(SHOW MASTER STATUS, File, 1);
let $MYSQLD_DATADIR= `select @@datadir`;
FLUSH LOGS;

--echo ==== Output of mysqlbinlog with --short-form --skip-empty-trans, --database and --skip-gtids options ====
--echo ==== DB changed in the middle of the transaction, which belongs to the selected database
--error 1
--exec $MYSQL_BINLOG --short-form --skip_empty_trans --database=test2 --skip-gtids $MYSQLD_DATADIR/$master_binlog

# Test the case when database gets changed in the middle of a transaction
# Non-selected database
use test;
start transaction;
insert into t2 values(9, 'i');
insert into t2 values(10, 'j');
use test2;
insert into t2 values(11, 'k');
insert into t2 values(12, 'l');
commit;

let $master_binlog= query_get_value(SHOW MASTER STATUS, File, 1);
let $MYSQLD_DATADIR= `select @@datadir`;
FLUSH LOGS;

--echo ==== Output of mysqlbinlog with --short-form --skip-empty-trans, --database and --skip-gtids options ====
--echo ==== DB changed in the middle of the transaction, which belongs to the non-selected database
--error 1
--exec $MYSQL_BINLOG --short-form --skip_empty_trans --database=test2 --skip-gtids $MYSQLD_DATADIR/$master_binlog

use test;
drop table t1;
drop table t2;

use test2;
drop table t1;
drop table t2;

use test3;
drop table t1;
drop table t2;

drop database test2;
drop database test3;
