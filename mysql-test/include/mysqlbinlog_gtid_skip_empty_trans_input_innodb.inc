##############################################################################
# The testcase input used for mysqlbinlog_gtid_skip_empty_trans_innodb
##############################################################################

create database test2;
create database test3;

use test;
create table t1 (a int) ENGINE=InnoDB;
insert into t1 values(1);
insert into t1 values(2);
# Create t2 for transaction
create table t2 (a int) ENGINE=InnoDB;
# Transaction
start transaction;
insert into t2 values(1);
insert into t2 values(2);
insert into t2 values(3);
insert into t2 values(4);
commit;

use test2;
create table t1 (a int) ENGINE=InnoDB;
insert into t1 values(1);
insert into t1 values(2);
# Create t2 for transaction
create table t2 (a int) ENGINE=InnoDB;
# Transaction
start transaction;
insert into t2 values(1);
insert into t2 values(2);
insert into t2 values(3);
insert into t2 values(4);
commit;

use test3;
create table t1 (a int) ENGINE=InnoDB;
insert into t1 values(1);
insert into t1 values(2);
# Create t2 for transaction
create table t2 (a int) ENGINE=InnoDB;
# Transaction
start transaction;
insert into t2 values(1);
insert into t2 values(2);
insert into t2 values(3);
insert into t2 values(4);
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
insert into t2 values(1);
insert into t2 values(2);
use test;
insert into t2 values(3);
insert into t2 values(4);
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
insert into t2 values(1);
insert into t2 values(2);
use test2;
insert into t2 values(3);
insert into t2 values(4);
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
