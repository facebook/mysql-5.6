# Test to check if rows are properly overwritten during idempotent recovery
# The invariant we'd like to have is that after execution of every transasction
# the rows that the transaction touches have the same contents in all cols as if
# it was executed normally.

connection slave;
source include/stop_slave.inc;
change replication source to source_auto_position=1;
# this debug var will enable idempotent recovery for every transaction (no need to restart)
set @@global.debug= "+d,dbg_enable_idempotent_recovery";
set @@sql_log_bin= 0;
source include/start_slave.inc;

connection master;
echo "Create schema";
drop table if exists t1;
eval create table t1(a int primary key, b int unique, c int, d int, key(c)) engine = $engine;
source include/sync_slave_sql_with_master.inc;

connection slave;
flush logs;

connection master;
echo "Check if inserts overwrite correctly";
connection slave;
insert into t1 values(1, 2, 3, 4); # all cols except PK are different
connection master;
insert into t1 values(1, 1, 1, 1);
source include/sync_slave_sql_with_master.inc;
connection master;
select * from t1;
connection slave;
select * from t1 force index(primary);
select * from t1 force index(b);
select * from t1 force index(c);

echo "Check if updates overwrite correctly";
connection slave;
delete from t1;
insert into t1 values(1, 20, 30, 40); # all cols except PK are different
connection master;
/* update UK */ update t1 set b = 2 where a = 1;
source include/sync_slave_sql_with_master.inc;
connection master;
select * from t1;
connection slave;
select * from t1 force index(primary);
select * from t1 force index(b);
select * from t1 force index(c);
connection master;
/* update Key */ update t1 set c = 2 where a = 1;
source include/sync_slave_sql_with_master.inc;
connection master;
select * from t1;
connection slave;
select * from t1 force index(primary);
select * from t1 force index(b);
select * from t1 force index(c);
connection master;
/* update Non-Key */ update t1 set d = 2 where a = 1;
source include/sync_slave_sql_with_master.inc;
connection master;
select * from t1;
connection slave;
select * from t1 force index(primary);
select * from t1 force index(b);
select * from t1 force index(c);
connection master;
/* update PK */ update t1 set a = 2 where a = 1;
source include/sync_slave_sql_with_master.inc;
connection master;
select * from t1;
connection slave;
select * from t1 force index(primary);
select * from t1 force index(b);
select * from t1 force index(c);
connection master;
/* update all cols */ update t1 set a = 3, b = 3, c = 3, d = 3 where a = 2;
source include/sync_slave_sql_with_master.inc;
connection master;
select * from t1;
connection slave;
select * from t1 force index(primary);
select * from t1 force index(b);
select * from t1 force index(c);

echo "Check if deletes overwrite correctly";
connection slave;
update t1 set b = 30, c = 30, d = 30; # create a different version of the row
connection master;
/* delete an existing row */ delete from t1 where a = 3;
source include/sync_slave_sql_with_master.inc;
connection master;
select * from t1;
connection slave;
select * from t1 force index(primary);
select * from t1 force index(b);
select * from t1 force index(c);

# create a row on both master and slave
connection master;
insert into t1 values(1, 1, 1, 1);
source include/sync_slave_sql_with_master.inc;

connection slave;
delete from t1 where a = 1; # delete the row on the slave
connection master;
/* delete a non-existing row */ delete from t1 where a = 1;
source include/sync_slave_sql_with_master.inc;
connection master;
select * from t1;
connection slave;
select * from t1 force index(primary);
select * from t1 force index(b);
select * from t1 force index(c);

echo "Check if multi-stmt trxs with stmt that mutate multiple rows work correctly";
connection master;
delete from t1; # start from scratch
source include/sync_slave_sql_with_master.inc;
connection slave;
# create diff version of multiple rows on the slave
insert into t1 values(1, 10, 100, 1000);
insert into t1 values(2, 20, 200, 2000);
insert into t1 values(3, 30, 300, 3000);
insert into t1 values(5, 50, 500, 5000);
connection master;
begin;
insert into t1 values(1, 1, 1, 1), (2, 2, 2, 2), (3, 3, 3, 3), (4, 4, 4, 4);
update t1 set b = b + 10 where a in (2, 3, 4);
update t1 set a = 5 where a = 4;
delete from t1 where a > 2;
commit;
source include/sync_slave_sql_with_master.inc;
connection master;
select * from t1;
connection slave;
select * from t1 force index(primary);
select * from t1 force index(b);
select * from t1 force index(c);

echo "Double check if master and slave have the same data";
let $diff_tables= master:t1, slave:t1;
source include/diff_tables.inc;

echo "Check if slave binlogs are generated correctly";
connection slave;
set @@global.debug= "-d,dbg_enable_idempotent_recovery";
exec $MYSQL_BINLOG -v -v $MYSQLD_DATADIR/slave-bin.0* > $MYSQLTEST_VARDIR/tmp/fulldump.sql;
reset master;
exec $MYSQL --user=root --host=127.0.0.1 --port=$SLAVE_MYPORT < $MYSQLTEST_VARDIR/tmp/fulldump.sql;
remove_file $MYSQLTEST_VARDIR/tmp/fulldump.sql;

let $diff_tables= master:t1, slave:t1;
source include/diff_tables.inc;

connection master;
drop table t1;

source include/sync_slave_sql_with_master.inc;

