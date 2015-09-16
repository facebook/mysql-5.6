##############################################################################
# The testcase input used for mysqlbinlog_gtid_skip_empty_trans_myisam
##############################################################################

create database test2;
create database test3;

use test;
create table t1 (a int) ENGINE=MyISAM;
insert into t1 values(1);
insert into t1 values(2);
insert into t1 values(3);
insert into t1 values(4);

use test2;
create table t1 (a int) ENGINE=MyISAM;
insert into t1 values(1);
insert into t1 values(2);
insert into t1 values(3);
insert into t1 values(4);

use test3;
create table t1 (a int) ENGINE=MyISAM;
insert into t1 values(1);
insert into t1 values(2);
insert into t1 values(3);
insert into t1 values(4);

let $master_binlog= query_get_value(SHOW MASTER STATUS, File, 1);
let $MYSQLD_DATADIR= `select @@datadir`;
FLUSH LOGS;

--echo ==== Output of mysqlbinlog with --short-form --skip-empty-trans, --database and --skip-gtids options ====
--exec $MYSQL_BINLOG --short-form --skip_empty_trans --database=test2 --skip-gtids $MYSQLD_DATADIR/$master_binlog

use test;
drop table t1;

use test2;
drop table t1;

use test3;
drop table t1;

drop database test2;
drop database test3;
